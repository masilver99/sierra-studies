#include "sierrachart.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#elif _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <cmath>
#include <string>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include <cstdint>

SCDLLName("Trade Gate")

#ifndef ACS_RECEIVE_POINTER_EVENTS_ALWAYS
#define ACS_RECEIVE_POINTER_EVENTS_ALWAYS ACS_RECEIVE_POINTER_EVENTS_WHEN_ACS_BUTTON_ENABLED
#endif

namespace
{
	enum class TickRounding
	{
		Nearest = 0,
		Up = 1,
		Down = 2,
	};

	static double RoundToTick(double price, double tickSize, TickRounding rounding)
	{
		if (tickSize <= 0.0)
			return price;

		double scaled = price / tickSize;
		switch (rounding)
		{
			case TickRounding::Up:
				scaled = std::ceil(scaled - 1e-12);
				break;
			case TickRounding::Down:
				scaled = std::floor(scaled + 1e-12);
				break;
			case TickRounding::Nearest:
			default:
					scaled = std::round(scaled);
				break;
		}
		return scaled * tickSize;
	}

	enum InputIndex
	{
		INPUT_ENABLE = 0,
		INPUT_USE_TRADE_WINDOW_QTY = 1,
		INPUT_FIXED_QTY = 2,
		INPUT_REQUIRE_SHIFT = 3,
		INPUT_CHECKLIST_ENABLED = 4,
		INPUT_CHECK_1_TEXT = 5,
		INPUT_CHECK_2_TEXT = 6,
		INPUT_CHECK_3_TEXT = 7,
		INPUT_CHECK_4_TEXT = 8,
		INPUT_FINAL_CONFIRM = 9,
		INPUT_CONFIG_JSON_PATH = 10,
		INPUT_DEBUG_LOG = 11,
	};

	struct ChecklistDialogParams;

	// Helper forward declarations are placed near their first use below.

	struct TradeGateSession
	{
		HWND checklistHwnd = nullptr;
		bool checklistDone = false;
		bool checklistResult = false;
		s_SCNewOrder pendingOrder{};
		bool pendingIsBuy = false;
		std::wstring orderSummary;
	};

	static TradeGateSession gSession;
	constexpr int STATE_IDLE = 0;
	constexpr int STATE_WAITING_DIALOG = 1;

	enum MenuCommand : UINT
	{
		CMD_NONE = 0,
		CMD_BUY_MARKET = 1001,
		CMD_BUY_LIMIT = 1002,
		CMD_BUY_STOP = 1003,
		CMD_SELL_MARKET = 1004,
		CMD_SELL_LIMIT = 1005,
		CMD_SELL_STOP = 1006,
		CMD_CANCEL = 1099,
	};

	static HWND GetChartHwnd(SCStudyInterfaceRef sc)
	{
		// Many ACSIL versions expose ChartWindowHandle; keep this isolated for easy fixes.
		return (HWND)sc.ChartWindowHandle;
	}

	static bool IsCursorOverWindow(HWND hwnd)
	{
		if (hwnd == nullptr)
			return false;
		POINT pt{};
		if (!GetCursorPos(&pt))
			return false;
		HWND w = WindowFromPoint(pt);
		return (w == hwnd) || (w != nullptr && IsChild(hwnd, w));
	}

	static bool GetShiftDown()
	{
		return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	}

	static UINT ShowOrderMenu(HWND hwnd)
	{
		HMENU menu = CreatePopupMenu();
		if (menu == nullptr)
			return CMD_NONE;

		AppendMenuW(menu, MF_STRING, CMD_BUY_MARKET, L"Buy Market");
		AppendMenuW(menu, MF_STRING, CMD_BUY_LIMIT, L"Buy Limit @ Click Price");
		AppendMenuW(menu, MF_STRING, CMD_BUY_STOP, L"Buy Stop @ Click Price");
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(menu, MF_STRING, CMD_SELL_LIMIT, L"Sell Limit @ Click Price");
		AppendMenuW(menu, MF_STRING, CMD_SELL_MARKET, L"Sell Market");
		AppendMenuW(menu, MF_STRING, CMD_SELL_STOP, L"Sell Stop @ Click Price");
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(menu, MF_STRING, CMD_CANCEL, L"Cancel");

		POINT pt{};
		GetCursorPos(&pt);

		UINT cmd = TrackPopupMenu(
			menu,
			TPM_RETURNCMD | TPM_RIGHTBUTTON,
			pt.x,
			pt.y,
			0,
			hwnd,
			nullptr);

		DestroyMenu(menu);
		return cmd;
	}

	static bool AskYesNoDetailed(
		HWND hwnd,
		const wchar_t* title,
		const wchar_t* mainInstruction,
		const wchar_t* content,
		const wchar_t* yesText,
		const wchar_t* noText,
		int defaultButtonId);

	struct ChecklistDialogParams
	{
		const wchar_t* title = L"Trade Gate Checklist";
		bool finalConfirm = false;
		std::wstring headerText;

		struct Item
		{
			enum class Type
			{
				Checkbox,
				Radio,
				Dropdown,
				Text,
				TextArea,
				MultiCheckbox,
			};

			std::wstring id;
			Type type = Type::Checkbox;
			std::wstring label;
			bool required = false;
			std::wstring placeholder;
			std::vector<std::wstring> options;
			std::wstring defaultValue;
			std::vector<std::wstring> defaultValues;
		};

		std::vector<Item> items;
	};

	static HWND StartChecklistCheckboxDialog(HWND parent, ChecklistDialogParams& params, TradeGateSession& session);

	static bool AskYesNo(HWND hwnd, const wchar_t* title, const wchar_t* text)
	{
		// Kept for backward compatibility; uses the detailed version with an empty content.
		return AskYesNoDetailed(hwnd, title, text, L"", L"Yes", L"No", IDNO);
	}

	static std::wstring ToWideBestEffort(const SCString& s)
	{
		if (s.IsEmpty())
			return {};

		const char* chars = s.GetChars();
		if (chars == nullptr || chars[0] == '\0')
			return {};

		int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, chars, -1, nullptr, 0);
		UINT codePage = CP_UTF8;
		DWORD flags = MB_ERR_INVALID_CHARS;
		if (needed <= 0)
		{
			codePage = CP_ACP;
			flags = 0;
			needed = MultiByteToWideChar(codePage, flags, chars, -1, nullptr, 0);
		}
		if (needed <= 0)
			return {};

		std::wstring out;
		out.resize((size_t)needed - 1);
		MultiByteToWideChar(codePage, flags, chars, -1, out.data(), needed);
		return out;
	}

	static std::wstring ToWideBestEffort(const std::string& s)
	{
		if (s.empty())
			return {};
		int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1, nullptr, 0);
		UINT codePage = CP_UTF8;
		DWORD flags = MB_ERR_INVALID_CHARS;
		if (needed <= 0)
		{
			codePage = CP_ACP;
			flags = 0;
			needed = MultiByteToWideChar(codePage, flags, s.c_str(), -1, nullptr, 0);
		}
		if (needed <= 0)
			return {};

		std::wstring out;
		out.resize((size_t)needed - 1);
		MultiByteToWideChar(codePage, flags, s.c_str(), -1, out.data(), needed);
		return out;
	}

	static std::string ToUtf8(const std::wstring& w)
	{
		if (w.empty())
			return {};
		int needed = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (needed <= 0)
			return {};
		std::string out;
		out.resize((size_t)needed - 1);
		WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), needed, nullptr, nullptr);
		return out;
	}

	// BuildChecklistParams moved below once helper implementations are available.

	static std::wstring GetThisModuleDirectory()
	{
		HMODULE hMod = nullptr;
		if (!GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&GetThisModuleDirectory),
			&hMod))
		{
			return {};
		}

		wchar_t path[MAX_PATH]{};
		DWORD len = GetModuleFileNameW(hMod, path, (DWORD)_countof(path));
		if (len == 0 || len >= _countof(path))
			return {};
		std::wstring p(path, path + len);
		size_t pos = p.find_last_of(L"\\/");
		if (pos == std::wstring::npos)
			return {};
		p.resize(pos);
		return p;
	}

	static bool IsAbsolutePath(const std::wstring& p)
	{
		if (p.size() >= 2 && std::iswalpha(p[0]) && p[1] == L':')
			return true;
		if (p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\')
			return true;
		return false;
	}

	static std::wstring ResolveConfigPathRelativeToModule(const std::wstring& input)
	{
		std::wstring trimmed = input;
		while (!trimmed.empty() && (trimmed.front() == L' ' || trimmed.front() == L'\t' || trimmed.front() == L'\r' || trimmed.front() == L'\n'))
			trimmed.erase(trimmed.begin());
		while (!trimmed.empty() && (trimmed.back() == L' ' || trimmed.back() == L'\t' || trimmed.back() == L'\r' || trimmed.back() == L'\n'))
			trimmed.pop_back();
		if (trimmed.empty())
			return {};
		if (IsAbsolutePath(trimmed))
			return trimmed;
		std::wstring base = GetThisModuleDirectory();
		if (base.empty())
			return trimmed;
		if (!base.empty() && base.back() != L'\\')
			base += L'\\';
		return base + trimmed;
	}

	static bool ReadFileUtf8(const std::wstring& path, std::string& out)
	{
		out.clear();
		std::ifstream f(path, std::ios::binary);
		if (!f)
			return false;
		std::ostringstream ss;
		ss << f.rdbuf();
		out = ss.str();
		// strip UTF-8 BOM if present
		if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
			out.erase(0, 3);
		return true;
	}

	static std::string StripJsonCommentsAndTrailingCommas(const std::string& input)
	{
		std::string noComments;
		noComments.reserve(input.size());
		bool inString = false;
		bool escape = false;
		for (size_t i = 0; i < input.size(); ++i)
		{
			const char c = input[i];
			const char next = (i + 1 < input.size()) ? input[i + 1] : '\0';

			if (inString)
			{
				noComments.push_back(c);
				if (escape)
				{
					escape = false;
					continue;
				}
				if (c == '\\')
				{
					escape = true;
					continue;
				}
				if (c == '"')
					inString = false;
				continue;
			}

			if (c == '"')
			{
				inString = true;
				noComments.push_back(c);
				continue;
			}

			// line comment
			if (c == '/' && next == '/')
			{
				i += 1;
				while (i + 1 < input.size() && input[i + 1] != '\n')
					i += 1;
				continue;
			}
			// block comment
			if (c == '/' && next == '*')
			{
				i += 1;
				while (i + 1 < input.size())
				{
					if (input[i] == '*' && input[i + 1] == '/')
					{
						i += 1;
						break;
					}
					i += 1;
				}
				continue;
			}

			noComments.push_back(c);
		}

		// remove trailing commas before } or ] (while respecting strings)
		std::string out;
		out.reserve(noComments.size());
		inString = false;
		escape = false;
		for (size_t i = 0; i < noComments.size(); ++i)
		{
			const char c = noComments[i];
			if (inString)
			{
				out.push_back(c);
				if (escape)
				{
					escape = false;
					continue;
				}
				if (c == '\\')
				{
					escape = true;
					continue;
				}
				if (c == '"')
					inString = false;
				continue;
			}
			if (c == '"')
			{
				inString = true;
				out.push_back(c);
				continue;
			}

			if (c == ',')
			{
				// look ahead for next non-whitespace
				size_t j = i + 1;
				while (j < noComments.size() && (noComments[j] == ' ' || noComments[j] == '\t' || noComments[j] == '\r' || noComments[j] == '\n'))
					++j;
				if (j < noComments.size() && (noComments[j] == ']' || noComments[j] == '}'))
					continue;
			}

			out.push_back(c);
		}
		return out;
	}

	struct JsonValue
	{
		enum class Type
		{
			Null,
			Bool,
			String,
			Array,
			Object,
		};
		Type type = Type::Null;
		bool b = false;
		std::string s;
		std::vector<JsonValue> a;
		std::vector<std::pair<std::string, JsonValue>> o;

		const JsonValue* Find(const char* key) const
		{
			if (type != Type::Object)
				return nullptr;
			for (const auto& kv : o)
			{
				if (_stricmp(kv.first.c_str(), key) == 0)
					return &kv.second;
			}
			return nullptr;
		}
	};

	struct JsonParser
	{
		const char* p = nullptr;
		const char* end = nullptr;
		std::string error;

		explicit JsonParser(const std::string& text)
			: p(text.c_str())
			, end(text.c_str() + text.size())
		{
		}

		void SkipWs()
		{
			while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
				++p;
		}

		bool Match(char c)
		{
			SkipWs();
			if (p < end && *p == c)
			{
				++p;
				return true;
			}
			return false;
		}

		bool Expect(char c, const char* what)
		{
			if (Match(c))
				return true;
			error = what;
			return false;
		}

		bool ParseString(std::string& out)
		{
			SkipWs();
			if (p >= end || *p != '"')
			{
				error = "Expected string";
				return false;
			}
			++p;
			std::string s;
			while (p < end)
			{
				char c = *p++;
				if (c == '"')
				{
					out = std::move(s);
					return true;
				}
				if (c == '\\')
				{
					if (p >= end)
					{
						error = "Unterminated escape";
						return false;
					}
					char e = *p++;
					switch (e)
					{
						case '"': s.push_back('"'); break;
						case '\\': s.push_back('\\'); break;
						case '/': s.push_back('/'); break;
						case 'b': s.push_back('\b'); break;
						case 'f': s.push_back('\f'); break;
						case 'n': s.push_back('\n'); break;
						case 'r': s.push_back('\r'); break;
						case 't': s.push_back('\t'); break;
						case 'u':
						{
							unsigned v = 0;
							for (int i = 0; i < 4; ++i)
							{
								if (p >= end)
								{
									error = "Bad unicode escape";
									return false;
								}
								char h = *p++;
								v <<= 4;
								if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
								else if (h >= 'a' && h <= 'f') v |= (unsigned)(10 + h - 'a');
								else if (h >= 'A' && h <= 'F') v |= (unsigned)(10 + h - 'A');
								else
								{
									error = "Bad unicode hex";
									return false;
								}
							}
							if (v <= 0x7F)
								s.push_back((char)v);
							else
								s.push_back('?');
							break;
						}
						default:
							error = "Unknown escape";
							return false;
					}
					continue;
				}
				s.push_back(c);
			}
			error = "Unterminated string";
			return false;
		}

		bool ParseValue(JsonValue& out)
		{
			SkipWs();
			if (p >= end)
			{
				error = "Unexpected end";
				return false;
			}
			if (*p == '"')
			{
				out.type = JsonValue::Type::String;
				return ParseString(out.s);
			}
			if (*p == '{')
				return ParseObject(out);
			if (*p == '[')
				return ParseArray(out);
			if (end - p >= 4 && std::strncmp(p, "true", 4) == 0)
			{
				p += 4;
				out.type = JsonValue::Type::Bool;
				out.b = true;
				return true;
			}
			if (end - p >= 5 && std::strncmp(p, "false", 5) == 0)
			{
				p += 5;
				out.type = JsonValue::Type::Bool;
				out.b = false;
				return true;
			}
			if (end - p >= 4 && std::strncmp(p, "null", 4) == 0)
			{
				p += 4;
				out.type = JsonValue::Type::Null;
				return true;
			}
			error = "Unexpected token";
			return false;
		}

		bool ParseArray(JsonValue& out)
		{
			out = JsonValue{};
			out.type = JsonValue::Type::Array;
			if (!Expect('[', "Expected '['"))
				return false;
			SkipWs();
			if (Match(']'))
				return true;
			while (p < end)
			{
				JsonValue v;
				if (!ParseValue(v))
					return false;
				out.a.push_back(std::move(v));
				SkipWs();
				if (Match(']'))
					return true;
				if (!Expect(',', "Expected ','"))
					return false;
			}
			error = "Unterminated array";
			return false;
		}

		bool ParseObject(JsonValue& out)
		{
			out = JsonValue{};
			out.type = JsonValue::Type::Object;
			if (!Expect('{', "Expected '{'"))
				return false;
			SkipWs();
			if (Match('}'))
				return true;
			while (p < end)
			{
				std::string key;
				if (!ParseString(key))
					return false;
				if (!Expect(':', "Expected ':'"))
					return false;
				JsonValue v;
				if (!ParseValue(v))
					return false;
				out.o.emplace_back(std::move(key), std::move(v));
				SkipWs();
				if (Match('}'))
					return true;
				if (!Expect(',', "Expected ','"))
					return false;
			}
			error = "Unterminated object";
			return false;
		}
	};

	static bool ParseChecklistJson(const std::string& jsonText, ChecklistDialogParams& outParams, std::wstring& outError)
	{
		outError.clear();
		std::string cleaned = StripJsonCommentsAndTrailingCommas(jsonText);
		JsonParser parser(cleaned);
		JsonValue root;
		if (!parser.ParseValue(root))
		{
			outError = ToWideBestEffort(parser.error);
			return false;
		}
		parser.SkipWs();
		if (parser.p != parser.end)
		{
			outError = L"Extra data after JSON";
			return false;
		}
		if (root.type != JsonValue::Type::Array)
		{
			outError = L"Root must be an array";
			return false;
		}

		outParams.items.clear();
		std::vector<std::wstring> ids;

		auto toLower = [](std::string s)
		{
			for (char& c : s)
				c = (char)std::tolower((unsigned char)c);
			return s;
		};

		for (const JsonValue& elem : root.a)
		{
			if (elem.type != JsonValue::Type::Object)
				continue;
			const JsonValue* idv = elem.Find("id");
			const JsonValue* typev = elem.Find("type");
			const JsonValue* labelv = elem.Find("label");
			if (idv == nullptr || idv->type != JsonValue::Type::String)
				continue;
			if (typev == nullptr || typev->type != JsonValue::Type::String)
				continue;
			if (labelv == nullptr || labelv->type != JsonValue::Type::String)
				continue;

			ChecklistDialogParams::Item item;
			item.id = ToWideBestEffort(idv->s);
			item.label = ToWideBestEffort(labelv->s);
			if (item.id.empty() || item.label.empty())
				continue;

			for (const auto& existing : ids)
			{
				if (_wcsicmp(existing.c_str(), item.id.c_str()) == 0)
				{
					outError = L"Duplicate id in checklist JSON: ";
					outError += item.id;
					return false;
				}
			}
			ids.push_back(item.id);

			std::string t = toLower(typev->s);
			if (t == "checkbox") item.type = ChecklistDialogParams::Item::Type::Checkbox;
			else if (t == "radio") item.type = ChecklistDialogParams::Item::Type::Radio;
			else if (t == "dropdown") item.type = ChecklistDialogParams::Item::Type::Dropdown;
			else if (t == "text") item.type = ChecklistDialogParams::Item::Type::Text;
			else if (t == "textarea") item.type = ChecklistDialogParams::Item::Type::TextArea;
			else if (t == "multicheckbox") item.type = ChecklistDialogParams::Item::Type::MultiCheckbox;
			else
				continue;

			const JsonValue* reqv = elem.Find("required");
			if (reqv != nullptr && reqv->type == JsonValue::Type::Bool)
				item.required = reqv->b;

			const JsonValue* phv = elem.Find("placeholder");
			if (phv != nullptr && phv->type == JsonValue::Type::String)
				item.placeholder = ToWideBestEffort(phv->s);

			const JsonValue* optv = elem.Find("options");
			if (optv != nullptr && optv->type == JsonValue::Type::Array)
			{
				for (const JsonValue& ov : optv->a)
				{
					if (ov.type == JsonValue::Type::String)
					{
						std::wstring w = ToWideBestEffort(ov.s);
						if (!w.empty())
							item.options.push_back(std::move(w));
					}
				}
			}

			const JsonValue* defv = elem.Find("default");
			if (defv != nullptr)
			{
				if (defv->type == JsonValue::Type::String)
					item.defaultValue = ToWideBestEffort(defv->s);
				else if (defv->type == JsonValue::Type::Array)
				{
					for (const JsonValue& dv : defv->a)
					{
						if (dv.type == JsonValue::Type::String)
						{
							std::wstring w = ToWideBestEffort(dv.s);
							if (!w.empty())
								item.defaultValues.push_back(std::move(w));
						}
					}
				}
			}

			// Basic schema validation for option-based types.
			if ((item.type == ChecklistDialogParams::Item::Type::Dropdown
				|| item.type == ChecklistDialogParams::Item::Type::Radio
				|| item.type == ChecklistDialogParams::Item::Type::MultiCheckbox)
				&& item.options.empty())
			{
				outError = L"Item requires non-empty options array: ";
				outError += item.id;
				return false;
			}

			outParams.items.push_back(std::move(item));
		}

		if (outParams.items.empty())
		{
			outError = L"No valid checklist items found";
			return false;
		}
		return true;
	}

	static bool BuildChecklistParams(
		SCStudyInterfaceRef sc,
		const std::wstring& headerText,
		ChecklistDialogParams& outParams,
		std::wstring& outError)
	{
		outError.clear();
		outParams.items.clear();
		outParams.headerText = headerText;
		outParams.finalConfirm = sc.Input[INPUT_FINAL_CONFIRM].GetYesNo();

		if (!sc.Input[INPUT_CHECKLIST_ENABLED].GetYesNo())
			return true;

		const SCString jsonPathIn = sc.Input[INPUT_CONFIG_JSON_PATH].GetString();
		if (!jsonPathIn.IsEmpty())
		{
			std::wstring jsonPath = ResolveConfigPathRelativeToModule(ToWideBestEffort(jsonPathIn));
			std::string fileText;
			if (!ReadFileUtf8(jsonPath, fileText))
			{
				outError = L"Could not read checklist JSON file: ";
				outError += jsonPath;
				return false;
			}

			if (!ParseChecklistJson(fileText, outParams, outError))
			{
				if (outError.empty())
					outError = L"Checklist JSON parse/validation failed.";
				return false;
			}
			return true;
		}

		const SCString q1 = sc.Input[INPUT_CHECK_1_TEXT].GetString();
		const SCString q2 = sc.Input[INPUT_CHECK_2_TEXT].GetString();
		const SCString q3 = sc.Input[INPUT_CHECK_3_TEXT].GetString();
		const SCString q4 = sc.Input[INPUT_CHECK_4_TEXT].GetString();

		auto addLegacyCheckbox = [&](const wchar_t* id, const SCString& q)
		{
			if (q.IsEmpty())
				return;
			std::wstring w = ToWideBestEffort(q);
			if (w.empty())
				return;
			ChecklistDialogParams::Item item;
			item.id = id;
			item.type = ChecklistDialogParams::Item::Type::Checkbox;
			item.label = std::move(w);
			item.required = true;
			outParams.items.push_back(std::move(item));
		};

		addLegacyCheckbox(L"q1", q1);
		addLegacyCheckbox(L"q2", q2);
		addLegacyCheckbox(L"q3", q3);
		addLegacyCheckbox(L"q4", q4);
		return true;
	}

	static bool AskYesNoDetailed(
		HWND hwnd,
		const wchar_t* title,
		const wchar_t* mainInstruction,
		const wchar_t* content,
		const wchar_t* yesText,
		const wchar_t* noText,
		int defaultButtonId)
	{
		// Prefer TaskDialogIndirect for a more modern native dialog (Vista+). Load it dynamically
		// so we do not depend on additional link libraries in the study build.
		using TaskDialogIndirectFn = HRESULT(WINAPI*)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
		HMODULE comctl = LoadLibraryW(L"comctl32.dll");
		if (comctl != nullptr)
		{
			auto pTaskDialogIndirect = reinterpret_cast<TaskDialogIndirectFn>(
				GetProcAddress(comctl, "TaskDialogIndirect"));
			if (pTaskDialogIndirect != nullptr)
			{
				TASKDIALOG_BUTTON buttons[2] = {
					{IDYES, yesText != nullptr ? yesText : L"Yes"},
					{IDNO,  noText != nullptr ? noText : L"No"},
				};

				TASKDIALOGCONFIG cfg{};
				cfg.cbSize = sizeof(cfg);
				cfg.hwndParent = hwnd;
				cfg.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
				cfg.dwCommonButtons = 0;
				cfg.pszWindowTitle = title;
				cfg.pszMainInstruction = mainInstruction;
				cfg.pszContent = content;
				cfg.pszMainIcon = TD_INFORMATION_ICON;
				cfg.cButtons = (UINT)_countof(buttons);
				cfg.pButtons = buttons;
				cfg.nDefaultButton = defaultButtonId;

				int pressed = 0;
				const HRESULT hr = pTaskDialogIndirect(&cfg, &pressed, nullptr, nullptr);
				FreeLibrary(comctl);
				if (SUCCEEDED(hr))
					return pressed == IDYES;
				return false;
			}
			FreeLibrary(comctl);
		}

		std::wstring combined;
		if (mainInstruction != nullptr)
			combined += mainInstruction;
		if (content != nullptr && content[0] != L'\0')
		{
			if (!combined.empty())
				combined += L"\r\n\r\n";
			combined += content;
		}
		const int r = MessageBoxW(hwnd, combined.c_str(), title, MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND);
		return r == IDYES;
	}

	// Legacy RunChecklist removed; the modeless flow now builds params in BuildChecklistParams
	// and launches StartChecklistCheckboxDialog via the main state machine.

	namespace
	{
		constexpr UINT IDC_CHECKLIST_MAIN_TEXT = 3001;
		constexpr UINT IDC_CHECKLIST_DYNAMIC_BASE = 3100;
		constexpr UINT IDC_CHECKLIST_OK = IDOK;
		constexpr UINT IDC_CHECKLIST_CANCEL = IDCANCEL;

		struct ChecklistDialogRuntime
		{
			ChecklistDialogParams* params = nullptr;
			TradeGateSession* session = nullptr;
			std::wstring mainText;
			struct ControlRef
			{
				ChecklistDialogParams::Item::Type type;
				bool required = false;
				UINT idFirst = 0;
				int optionCount = 0;
			};
			std::vector<ControlRef> controls;
		};

		static void ResizeDialogToClient(HWND hDlg, int clientWidth, int clientHeight)
		{
			RECT rc{0, 0, clientWidth, clientHeight};
			DWORD style = (DWORD)GetWindowLongW(hDlg, GWL_STYLE);
			DWORD exStyle = (DWORD)GetWindowLongW(hDlg, GWL_EXSTYLE);
			AdjustWindowRectEx(&rc, style, FALSE, exStyle);
			SetWindowPos(
				hDlg,
				nullptr,
				0,
				0,
				rc.right - rc.left,
				rc.bottom - rc.top,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		}

		static void CenterDialogOverParent(HWND hDlg, HWND parent)
		{
			RECT rcDlg{};
			GetWindowRect(hDlg, &rcDlg);
			RECT rcParent{};
			if (parent != nullptr && GetWindowRect(parent, &rcParent))
			{
				const int dlgW = rcDlg.right - rcDlg.left;
				const int dlgH = rcDlg.bottom - rcDlg.top;
				const int parentW = rcParent.right - rcParent.left;
				const int parentH = rcParent.bottom - rcParent.top;
				const int x = rcParent.left + (parentW - dlgW) / 2;
				const int y = rcParent.top + (parentH - dlgH) / 2;
				SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			}
		}

		static int MeasureWrappedTextHeight(HDC hdc, HFONT font, const wchar_t* text, int width)
		{
			if (text == nullptr || text[0] == L'\0')
				return 0;

			HGDIOBJ prev = nullptr;
			if (font != nullptr)
				prev = SelectObject(hdc, font);

			RECT r{0, 0, width, 0};
			DrawTextW(hdc, text, -1, &r, DT_WORDBREAK | DT_CALCRECT);
			const int h = r.bottom - r.top;

			if (prev != nullptr)
				SelectObject(hdc, prev);
			return h;
		}

		static void UpdateChecklistOkEnabled(HWND hDlg)
		{
			auto* runtime = reinterpret_cast<ChecklistDialogRuntime*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
			bool ok = true;
			if (runtime != nullptr)
			{
				for (const auto& c : runtime->controls)
				{
					if (!c.required)
						continue;
					bool satisfied = true;
					switch (c.type)
					{
						case ChecklistDialogParams::Item::Type::Checkbox:
						{
							HWND h = GetDlgItem(hDlg, (int)c.idFirst);
							satisfied = (h != nullptr && SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED);
							break;
						}
						case ChecklistDialogParams::Item::Type::Text:
						case ChecklistDialogParams::Item::Type::TextArea:
						{
							HWND h = GetDlgItem(hDlg, (int)c.idFirst);
							int len = 0;
							if (h != nullptr)
								len = GetWindowTextLengthW(h);
							satisfied = (len > 0);
							break;
						}
						case ChecklistDialogParams::Item::Type::Dropdown:
						{
							HWND h = GetDlgItem(hDlg, (int)c.idFirst);
							LRESULT sel = (h != nullptr) ? SendMessageW(h, CB_GETCURSEL, 0, 0) : (LRESULT)CB_ERR;
							satisfied = (sel != CB_ERR);
							break;
						}
						case ChecklistDialogParams::Item::Type::Radio:
						{
							satisfied = false;
							for (int i = 0; i < c.optionCount; ++i)
							{
								HWND h = GetDlgItem(hDlg, (int)c.idFirst + i);
								if (h != nullptr && SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED)
								{
									satisfied = true;
									break;
								}
							}
							break;
						}
						case ChecklistDialogParams::Item::Type::MultiCheckbox:
						{
							satisfied = false;
							for (int i = 0; i < c.optionCount; ++i)
							{
								HWND h = GetDlgItem(hDlg, (int)c.idFirst + i);
								if (h != nullptr && SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED)
								{
									satisfied = true;
									break;
								}
							}
							break;
						}
						default:
							break;
					}
					if (!satisfied)
					{
						ok = false;
						break;
					}
				}
			}
			HWND hOk = GetDlgItem(hDlg, IDC_CHECKLIST_OK);
			if (hOk != nullptr)
				EnableWindow(hOk, ok ? TRUE : FALSE);
		}

		static INT_PTR CALLBACK ChecklistDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			auto* runtime = reinterpret_cast<ChecklistDialogRuntime*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
			ChecklistDialogParams* params = runtime != nullptr ? runtime->params : nullptr;
			switch (msg)
			{
				case WM_INITDIALOG:
				{
					runtime = reinterpret_cast<ChecklistDialogRuntime*>(lParam);
					if (runtime == nullptr)
						return FALSE;
					SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)runtime);
					params = runtime != nullptr ? runtime->params : nullptr;
					if (params != nullptr && params->title != nullptr)
						SetWindowTextW(hDlg, params->title);

					// Auto-size dialog based on wrapped text.
					const int margin = 12;
					const int buttonWidth = 110;
					const int buttonHeight = 28;
					const int buttonGap = 10;
					const int minClientWidth = 520;

					RECT rcClient{};
					GetClientRect(hDlg, &rcClient);
					int clientW = rcClient.right - rcClient.left;
					if (clientW < minClientWidth)
						clientW = minClientWidth;

					HFONT dlgFont = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
					if (dlgFont == nullptr)
						dlgFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

					HDC hdc = GetDC(hDlg);
					const int textW = clientW - margin * 2;

					std::wstring mainTextCombined;
					const wchar_t* baseMainText = (params != nullptr && params->finalConfirm)
						? L"Check each item, then submit the order."
						: L"Check each item to proceed.";
					if (params != nullptr && !params->headerText.empty())
					{
						mainTextCombined = params->headerText;
						mainTextCombined += L"\r\n";
						mainTextCombined += baseMainText;
					}
					else
					{
						mainTextCombined = baseMainText;
					}

					if (runtime != nullptr)
						runtime->mainText = mainTextCombined;

					const wchar_t* mainText = (runtime != nullptr) ? runtime->mainText.c_str() : mainTextCombined.c_str();
					int mainH = MeasureWrappedTextHeight(hdc, dlgFont, mainText, textW);
					if (mainH < 18) mainH = 18;

					int contentH = 0;
					if (params != nullptr)
					{
						for (const auto& item : params->items)
						{
							int labelH = MeasureWrappedTextHeight(hdc, dlgFont, item.label.c_str(), textW);
							if (labelH < 18) labelH = 18;

							switch (item.type)
							{
								case ChecklistDialogParams::Item::Type::Checkbox:
								{
									int h = labelH + 8;
									if (h < 22) h = 22;
									contentH += h + 8;
									break;
								}
								case ChecklistDialogParams::Item::Type::Text:
								{
									contentH += labelH + 6;
									contentH += 26 + 10;
									break;
								}
								case ChecklistDialogParams::Item::Type::TextArea:
								{
									contentH += labelH + 6;
									contentH += 88 + 10;
									break;
								}
								case ChecklistDialogParams::Item::Type::Dropdown:
								{
									contentH += labelH + 6;
									contentH += 28 + 10;
									break;
								}
								case ChecklistDialogParams::Item::Type::Radio:
								case ChecklistDialogParams::Item::Type::MultiCheckbox:
								{
									contentH += labelH + 6;
									for (const auto& opt : item.options)
									{
										int oh = MeasureWrappedTextHeight(hdc, dlgFont, opt.c_str(), textW - 26);
										oh += 8;
										if (oh < 22) oh = 22;
										contentH += oh + 4;
									}
									contentH += 6;
									break;
								}
								default:
									break;
							}
						}
					}

					ReleaseDC(hDlg, hdc);

					const int desiredClientH = margin + mainH + 10 + contentH + 8 + buttonHeight + margin;
					ResizeDialogToClient(hDlg, clientW, desiredClientH);
					CenterDialogOverParent(hDlg, GetParent(hDlg));

					GetClientRect(hDlg, &rcClient);
					const int width = (rcClient.right - rcClient.left) - margin * 2;
					int y = margin;

					HWND hMain = CreateWindowExW(
						0,
						L"STATIC",
						mainText,
						WS_CHILD | WS_VISIBLE,
						margin,
						y,
						width,
						mainH,
						hDlg,
						(HMENU)(uintptr_t)IDC_CHECKLIST_MAIN_TEXT,
						GetModuleHandleW(nullptr),
						nullptr);
					if (hMain != nullptr)
						SendMessageW(hMain, WM_SETFONT, (WPARAM)dlgFont, TRUE);

					y += mainH + 10;

					UINT nextId = IDC_CHECKLIST_DYNAMIC_BASE;
					runtime->controls.clear();
					hdc = GetDC(hDlg);
					if (params != nullptr)
					{
						for (const auto& item : params->items)
						{
							int labelH = MeasureWrappedTextHeight(hdc, dlgFont, item.label.c_str(), width);
							if (labelH < 18) labelH = 18;

							if (item.type == ChecklistDialogParams::Item::Type::Checkbox)
							{
								int qh = labelH + 8;
								if (qh < 22) qh = 22;
								UINT id = nextId++;
								HWND hCheck = CreateWindowExW(
									0,
									L"BUTTON",
									item.label.c_str(),
									WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE,
									margin,
									y,
									width,
									qh,
									hDlg,
									(HMENU)(uintptr_t)id,
									GetModuleHandleW(nullptr),
									nullptr);
								if (hCheck != nullptr)
								{
									SendMessageW(hCheck, WM_SETFONT, (WPARAM)dlgFont, TRUE);
									if (!item.defaultValue.empty())
										SendMessageW(hCheck, BM_SETCHECK, (WPARAM)BST_CHECKED, 0);
								}
								runtime->controls.push_back({item.type, item.required, id, 1});
								y += qh + 8;
								continue;
							}

							// label static
							HWND hLabel = CreateWindowExW(
								0,
								L"STATIC",
								item.label.c_str(),
								WS_CHILD | WS_VISIBLE,
								margin,
								y,
								width,
								labelH,
								hDlg,
								(HMENU)(uintptr_t)(nextId++),
								GetModuleHandleW(nullptr),
								nullptr);
							if (hLabel != nullptr)
								SendMessageW(hLabel, WM_SETFONT, (WPARAM)dlgFont, TRUE);
							y += labelH + 6;

							switch (item.type)
							{
								case ChecklistDialogParams::Item::Type::Text:
								case ChecklistDialogParams::Item::Type::TextArea:
								{
									UINT id = nextId++;
									DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER;
									int h = 26;
									if (item.type == ChecklistDialogParams::Item::Type::Text)
										style |= ES_AUTOHSCROLL;
									else
									{
										style |= ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL;
										h = 88;
									}
									HWND hEdit = CreateWindowExW(
										0,
										L"EDIT",
										item.defaultValue.c_str(),
										style,
										margin,
										y,
										width,
										h,
										hDlg,
										(HMENU)(uintptr_t)id,
										GetModuleHandleW(nullptr),
										nullptr);
									if (hEdit != nullptr)
									{
										SendMessageW(hEdit, WM_SETFONT, (WPARAM)dlgFont, TRUE);
										if (!item.placeholder.empty())
											SendMessageW(hEdit, EM_SETCUEBANNER, (WPARAM)TRUE, (LPARAM)item.placeholder.c_str());
									}
									runtime->controls.push_back({item.type, item.required, id, 1});
									y += h + 10;
									break;
								}
								case ChecklistDialogParams::Item::Type::Dropdown:
								{
									UINT id = nextId++;
									HWND hCombo = CreateWindowExW(
										0,
										L"COMBOBOX",
										L"",
										WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
										margin,
										y,
										width,
										200,
										hDlg,
										(HMENU)(uintptr_t)id,
										GetModuleHandleW(nullptr),
										nullptr);
									if (hCombo != nullptr)
									{
										SendMessageW(hCombo, WM_SETFONT, (WPARAM)dlgFont, TRUE);
										for (const auto& opt : item.options)
											SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)opt.c_str());
										if (!item.defaultValue.empty())
										{
											for (int i = 0; i < (int)item.options.size(); ++i)
											{
												if (_wcsicmp(item.options[i].c_str(), item.defaultValue.c_str()) == 0)
												{
													SendMessageW(hCombo, CB_SETCURSEL, (WPARAM)i, 0);
													break;
												}
											}
										}
									}
									runtime->controls.push_back({item.type, item.required, id, 1});
									y += 28 + 10;
									break;
								}
								case ChecklistDialogParams::Item::Type::Radio:
								case ChecklistDialogParams::Item::Type::MultiCheckbox:
								{
									UINT first = nextId;
									int optCount = (int)item.options.size();
									for (int i = 0; i < optCount; ++i)
									{
										const auto& opt = item.options[i];
										int oh = MeasureWrappedTextHeight(hdc, dlgFont, opt.c_str(), width - 26);
										oh += 8;
										if (oh < 22) oh = 22;
										DWORD st = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_MULTILINE;
										if (i == 0)
											st |= WS_GROUP;
										if (item.type == ChecklistDialogParams::Item::Type::Radio)
											st |= BS_AUTORADIOBUTTON;
										else
											st |= BS_AUTOCHECKBOX;
										UINT id = nextId++;
										HWND hBtn = CreateWindowExW(
											0,
											L"BUTTON",
											opt.c_str(),
											st,
											margin,
											y,
											width,
											oh,
											hDlg,
											(HMENU)(uintptr_t)id,
											GetModuleHandleW(nullptr),
											nullptr);
										if (hBtn != nullptr)
											SendMessageW(hBtn, WM_SETFONT, (WPARAM)dlgFont, TRUE);

										bool doCheck = false;
										if (item.type == ChecklistDialogParams::Item::Type::Radio)
										{
											if (!item.defaultValue.empty() && _wcsicmp(opt.c_str(), item.defaultValue.c_str()) == 0)
												doCheck = true;
											else if (item.defaultValue.empty() && i == 0)
												doCheck = false;
										}
										else
										{
											for (const auto& dv : item.defaultValues)
											{
												if (_wcsicmp(opt.c_str(), dv.c_str()) == 0)
												{
													doCheck = true;
													break;
												}
											}
										}
										if (doCheck)
											SendMessageW(hBtn, BM_SETCHECK, (WPARAM)BST_CHECKED, 0);

										y += oh + 4;
									}
									runtime->controls.push_back({item.type, item.required, first, optCount});
									y += 6;
									break;
								}
								default:
									break;
							}
						}
					}
					ReleaseDC(hDlg, hdc);

					y += 6;
					const int buttonsY = (rcClient.bottom - rcClient.top) - margin - buttonHeight;
					const int buttonsRight = (rcClient.right - rcClient.left) - margin;
					const wchar_t* okText = (params != nullptr && params->finalConfirm) ? L"Submit" : L"Proceed";

					HWND hOk = CreateWindowExW(
						0,
						L"BUTTON",
						okText,
						WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
						buttonsRight - (buttonWidth * 2 + buttonGap),
						buttonsY,
						buttonWidth,
						buttonHeight,
						hDlg,
						(HMENU)(uintptr_t)IDC_CHECKLIST_OK,
						GetModuleHandleW(nullptr),
						nullptr);
					if (hOk != nullptr)
						SendMessageW(hOk, WM_SETFONT, (WPARAM)dlgFont, TRUE);

					HWND hCancel = CreateWindowExW(
						0,
						L"BUTTON",
						L"Cancel",
						WS_CHILD | WS_VISIBLE | WS_TABSTOP,
						buttonsRight - buttonWidth,
						buttonsY,
						buttonWidth,
						buttonHeight,
						hDlg,
						(HMENU)(uintptr_t)IDC_CHECKLIST_CANCEL,
						GetModuleHandleW(nullptr),
						nullptr);
					if (hCancel != nullptr)
						SendMessageW(hCancel, WM_SETFONT, (WPARAM)dlgFont, TRUE);

					UpdateChecklistOkEnabled(hDlg);
					SetWindowPos(hDlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
					return TRUE;
				}

				case WM_COMMAND:
				{
					const UINT id = LOWORD(wParam);
					const UINT code = HIWORD(wParam);
					TradeGateSession* session = runtime != nullptr ? runtime->session : nullptr;
					if (id == IDC_CHECKLIST_CANCEL)
					{
						if (session != nullptr)
						{
							session->checklistResult = false;
							session->checklistDone = true;
							session->checklistHwnd = nullptr;
						}
						DestroyWindow(hDlg);
						return TRUE;
					}
					if (id == IDC_CHECKLIST_OK)
					{
						UpdateChecklistOkEnabled(hDlg);
						HWND hOk = GetDlgItem(hDlg, IDC_CHECKLIST_OK);
						if (hOk != nullptr && IsWindowEnabled(hOk))
						{
							if (session != nullptr)
							{
								session->checklistResult = true;
								session->checklistDone = true;
								session->checklistHwnd = nullptr;
							}
							DestroyWindow(hDlg);
							return TRUE;
						}
						return TRUE;
					}
					if (code == BN_CLICKED || code == EN_CHANGE || code == CBN_SELCHANGE)
					{
						UpdateChecklistOkEnabled(hDlg);
						return TRUE;
					}

					break;
				}
				case WM_CLOSE:
				{
					TradeGateSession* session = runtime != nullptr ? runtime->session : nullptr;
					if (session != nullptr)
					{
						session->checklistResult = false;
						session->checklistDone = true;
						session->checklistHwnd = nullptr;
					}
					DestroyWindow(hDlg);
					return TRUE;
				}
				case WM_NCDESTROY:
				{
					TradeGateSession* session = runtime != nullptr ? runtime->session : nullptr;
					if (session != nullptr && session->checklistHwnd == hDlg)
						session->checklistHwnd = nullptr;
					if (runtime != nullptr)
					{
						delete runtime;
						SetWindowLongPtrW(hDlg, GWLP_USERDATA, 0);
					}
					return FALSE;
				}
			}
			return FALSE;
		}
	}

	static HWND StartChecklistCheckboxDialog(HWND parent, ChecklistDialogParams& params, TradeGateSession& session)
	{
		// Build a minimal dialog template with no controls; we add all controls in WM_INITDIALOG.
		// The modeless dialog keeps Sierra Chart's UI thread free so price updates continue.
		struct DialogTemplate
		{
			DLGTEMPLATE dlg;
			WORD menu;
			WORD windowClass;
			WCHAR title[1];
		};

		DialogTemplate dt{};
		dt.dlg.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
		dt.dlg.dwExtendedStyle = WS_EX_DLGMODALFRAME;
		dt.dlg.cdit = 0;
		dt.dlg.x = 10;
		dt.dlg.y = 10;
		dt.dlg.cx = 260;
		dt.dlg.cy = 180;
		dt.menu = 0;
		dt.windowClass = 0;
		dt.title[0] = L'\0';

		ChecklistDialogRuntime* runtime = new ChecklistDialogRuntime();
		runtime->params = &params;
		runtime->session = &session;

		HWND hDlg = CreateDialogIndirectParamW(
			GetModuleHandleW(nullptr),
			reinterpret_cast<LPCDLGTEMPLATEW>(&dt),
			parent,
			ChecklistDlgProc,
			reinterpret_cast<LPARAM>(runtime));

		if (hDlg == nullptr)
		{
			delete runtime;
			return nullptr;
		}

		ShowWindow(hDlg, SW_SHOWNORMAL);
		return hDlg;
	}

	static int GetOrderQuantity(SCStudyInterfaceRef sc)
	{
		const int tradeWindowQty = (int)sc.TradeWindowOrderQuantity;
		if (sc.Input[INPUT_USE_TRADE_WINDOW_QTY].GetYesNo() && tradeWindowQty > 0)
			return tradeWindowQty;

		const int fixedQty = sc.Input[INPUT_FIXED_QTY].GetInt();
		return fixedQty > 0 ? fixedQty : 1;
	}

	static double GetClickedPrice(SCStudyInterfaceRef sc, HWND hwnd)
	{
		const bool debug = sc.Input[INPUT_DEBUG_LOG].GetYesNo();

		// Prefer deriving from the current cursor pixel location.
		double pixelPrice = 0.0;
		bool pixelOk = false;
		if (hwnd != nullptr)
		{
			POINT pt{};
			if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt))
			{
				pixelPrice = sc.YPixelCoordinateToGraphValue(pt.y);
				pixelOk = true;
			}
		}

		double price = pixelPrice;
		double activeVal = sc.ActiveToolYValue;
		double closeVal = (sc.ArraySize > 0) ? sc.Close[sc.ArraySize - 1] : 0.0;

		// Fallback: many ACSIL builds provide this as the pointer's chart value.
		if (price == 0.0)
			price = activeVal;
		if (price == 0.0)
			price = closeVal;

		if (debug)
		{
			SCString msg;
			msg.Format(
				"Trade Gate debug: GetClickedPrice pixelOk=%d pixel=%.8f active=%.8f close=%.8f final=%.8f",\
				pixelOk ? 1 : 0,
				pixelPrice,
				activeVal,
				closeVal,
				price);
			sc.AddMessageToLog(msg, 0);
		}

		return price;
	}

	static double SanitizePrice(SCStudyInterfaceRef sc, double price)
	{
		// Protect against invalid coordinates returning extremely large sentinels.
		if (!std::isfinite(price) || std::fabs(price) > 1e9)
		{
			if (sc.ArraySize > 0)
				return sc.Close[sc.ArraySize - 1];
			return 0.0;
		}
		return price;
	}
}

SCSFExport scsf_TradeGate(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName = "Trade Gate (SHIFT + Left Click)";
		sc.StudyDescription = "SHIFT+LeftClick opens an order menu, runs a checklist gate (legacy inputs or JSON config), and submits the selected order programmatically.";
		sc.GraphRegion = 1;
		sc.AutoLoop = 0;
		sc.ReceivePointerEvents = ACS_RECEIVE_POINTER_EVENTS_ALWAYS;
		sc.UpdateAlways = 1;
		sc.SupportAttachedOrdersForTrading = true;
		sc.AllowEntryWithWorkingOrders = true;
		sc.AllowOppositeEntryWithOpposingPositionOrOrders = true;
		sc.AllowOnlyOneTradePerBar = false;

		sc.Input[INPUT_ENABLE].Name = "Enable";
		sc.Input[INPUT_ENABLE].SetYesNo(true);

		sc.Input[INPUT_CONFIG_JSON_PATH].Name = "Checklist JSON config file (optional; relative to DLL folder)";
		sc.Input[INPUT_CONFIG_JSON_PATH].SetString("");

		sc.Input[INPUT_USE_TRADE_WINDOW_QTY].Name = "Use Trade Window Quantity";
		sc.Input[INPUT_USE_TRADE_WINDOW_QTY].SetYesNo(true);

		sc.Input[INPUT_FIXED_QTY].Name = "Fixed Quantity (if not using Trade Window)";
		sc.Input[INPUT_FIXED_QTY].SetInt(1);
		sc.Input[INPUT_FIXED_QTY].SetIntLimits(1, 1000);

		sc.Input[INPUT_REQUIRE_SHIFT].Name = "Require SHIFT modifier";
		sc.Input[INPUT_REQUIRE_SHIFT].SetYesNo(true);

		sc.Input[INPUT_CHECKLIST_ENABLED].Name = "Enable Checklist Gate";
		sc.Input[INPUT_CHECKLIST_ENABLED].SetYesNo(true);

		sc.Input[INPUT_CHECK_1_TEXT].Name = "Checklist Q1 (empty disables)";
		sc.Input[INPUT_CHECK_1_TEXT].SetString("Setup valid?");

		sc.Input[INPUT_CHECK_2_TEXT].Name = "Checklist Q2 (empty disables)";
		sc.Input[INPUT_CHECK_2_TEXT].SetString("HTF alignment? (Yes required)");

		sc.Input[INPUT_CHECK_3_TEXT].Name = "Checklist Q3 (empty disables)";
		sc.Input[INPUT_CHECK_3_TEXT].SetString("Risk acceptable? (Yes required)");

		sc.Input[INPUT_CHECK_4_TEXT].Name = "Checklist Q4 (empty disables)";
		sc.Input[INPUT_CHECK_4_TEXT].SetString("Time-of-day OK? (Yes required)");

		sc.Input[INPUT_FINAL_CONFIRM].Name = "Final confirm dialog";
		sc.Input[INPUT_FINAL_CONFIRM].SetYesNo(true);

		sc.Input[INPUT_DEBUG_LOG].Name = "Debug: log GetClickedPrice paths";
		sc.Input[INPUT_DEBUG_LOG].SetYesNo(false);

		return;
	}

	// Do not run trading logic during full chart recalculation; submissions would be skipped.
	if (sc.IsFullRecalculation)
		return;

	if (!sc.Input[INPUT_ENABLE].GetYesNo())	return;

	int& state = sc.GetPersistentInt(1);
	int& PrevLButtonDown = sc.GetPersistentInt(2);

	if (state == STATE_WAITING_DIALOG)
	{
		if (gSession.checklistDone)
		{
			if (gSession.checklistResult)
			{
				double result = gSession.pendingIsBuy ? sc.BuyEntry(gSession.pendingOrder) : sc.SellEntry(gSession.pendingOrder);
				if (result <= 0.0)
				{
					const int resultCode = (int)result;
					const char* resultText = sc.GetTradingErrorTextMessage(resultCode);
					if (resultText == nullptr)
						resultText = "";

					SCString msg;
					msg.Format(
						"Trade Gate: Order rejected (Result=%d '%s', Type=%d, Qty=%d, Price1=%.8f, Replay=%d, ReplayStatus=%d, Sim=%d, SendOrders=%d, AutoTrading=%d, AutoTradingChart=%d, TradingLocked=%u)",
						resultCode,
						resultText,
						(int)gSession.pendingOrder.OrderType,
						(int)gSession.pendingOrder.OrderQuantity,
						gSession.pendingOrder.Price1,
						sc.IsReplayRunning(),
						sc.ReplayStatus,
						sc.GlobalTradeSimulationIsOn,
						sc.SendOrdersToTradeService,
						sc.IsAutoTradingEnabled,
						sc.IsAutoTradingOptionEnabledForChart,
						(unsigned)sc.TradingIsLocked);
					sc.AddMessageToLog(msg, 1);
				}
				else
				{
					sc.AddMessageToLog("Trade Gate: Order submitted.", 0);
				}
			}
			else
			{
				sc.AddMessageToLog("Trade Gate: Checklist failed/cancelled; order not submitted.", 1);
			}

			gSession.checklistDone = false;
			gSession.checklistResult = false;
			gSession.pendingOrder = s_SCNewOrder{};
			gSession.pendingIsBuy = false;
			state = STATE_IDLE;
			return;
		}

		if (gSession.checklistHwnd != nullptr && !IsWindow(gSession.checklistHwnd))
		{
			gSession.checklistHwnd = nullptr;
			gSession.checklistDone = false;
			gSession.checklistResult = false;
			state = STATE_IDLE;
		}
		return;
	}

	// Reliable click detection: poll for a left-button down edge while SHIFT is held,
	// and only when the cursor is actually over this chart window.
	const bool lDown = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
	const bool justPressed = lDown && (PrevLButtonDown == 0);
	PrevLButtonDown = lDown ? 1 : 0;

	if (!justPressed)
		return;

	if (sc.Input[INPUT_REQUIRE_SHIFT].GetYesNo() && !GetShiftDown())
		return;

	HWND hwnd = GetChartHwnd(sc);
	if (!IsCursorOverWindow(hwnd))
		return;

	// Capture the chart price exactly at the click point before the menu opens
	// so mouse movement while choosing a menu item does not shift the price.
	const double clickedPrice = SanitizePrice(sc, GetClickedPrice(sc, hwnd));

	UINT cmd = ShowOrderMenu(hwnd);
	if (cmd == CMD_NONE || cmd == CMD_CANCEL)
		return;

	s_SCNewOrder order{};
	order.Price1 = 0.0; // avoid sentinel values if the backend inspects Price1 on market orders
	order.OrderQuantity = GetOrderQuantity(sc);

	bool isBuy = false;
	std::wstring orderSummary;
	auto formatPrice = [&](double price) -> std::wstring
	{
		SCString s;
		s.Format("%.8f", price);
		return ToWideBestEffort(s);
	};

	switch (cmd)
	{
		case CMD_BUY_MARKET:
			isBuy = true;
			order.OrderType = SCT_ORDERTYPE_MARKET;
			orderSummary = L"Order: Buy Market";
			break;
		case CMD_BUY_LIMIT:
			isBuy = true;
			order.OrderType = SCT_ORDERTYPE_LIMIT;
			order.Price1 = RoundToTick(clickedPrice, sc.TickSize, TickRounding::Nearest);
			orderSummary = L"Order: Buy Limit @ " + formatPrice(order.Price1);
			break;
		case CMD_BUY_STOP:
			isBuy = true;
			order.OrderType = SCT_ORDERTYPE_STOP;
			order.Price1 = RoundToTick(clickedPrice, sc.TickSize, TickRounding::Up);
			orderSummary = L"Order: Buy Stop @ " + formatPrice(order.Price1);
			break;
		case CMD_SELL_MARKET:
			isBuy = false;
			order.OrderType = SCT_ORDERTYPE_MARKET;
			orderSummary = L"Order: Sell Market";
			break;
		case CMD_SELL_LIMIT:
			isBuy = false;
			order.OrderType = SCT_ORDERTYPE_LIMIT;
			order.Price1 = RoundToTick(clickedPrice, sc.TickSize, TickRounding::Up);
			orderSummary = L"Order: Sell Limit @ " + formatPrice(order.Price1);
			break;
		case CMD_SELL_STOP:
			isBuy = false;
			order.OrderType = SCT_ORDERTYPE_STOP;
			order.Price1 = RoundToTick(clickedPrice, sc.TickSize, TickRounding::Down);
			orderSummary = L"Order: Sell Stop @ " + formatPrice(order.Price1);
			break;
		default:
			return;
	}

	ChecklistDialogParams params;
	std::wstring checklistError;
	if (!BuildChecklistParams(sc, orderSummary, params, checklistError))
	{
		SCString msg = "Trade Gate: Checklist init failed.";
		if (!checklistError.empty())
		{
			std::string utf8 = ToUtf8(checklistError);
			if (!utf8.empty())
			{
				msg += " Detail: ";
				msg += utf8.c_str();
			}
		}
		sc.AddMessageToLog(msg, 1);
		return;
	}

	const bool checklistEnabled = sc.Input[INPUT_CHECKLIST_ENABLED].GetYesNo();
	const bool needDialog = checklistEnabled && (!params.items.empty() || params.finalConfirm);
	if (needDialog)
	{
		gSession.checklistDone = false;
		gSession.checklistResult = false;
		gSession.pendingOrder = order;
		gSession.pendingIsBuy = isBuy;
		gSession.orderSummary = orderSummary;
		HWND dlg = StartChecklistCheckboxDialog(hwnd, params, gSession);
		if (dlg == nullptr)
		{
			sc.AddMessageToLog("Trade Gate: Could not create checklist dialog.", 1);
			gSession.checklistDone = false;
			gSession.pendingIsBuy = false;
			return;
		}
		gSession.checklistHwnd = dlg;
		state = STATE_WAITING_DIALOG;
		return;
	}

	double result = isBuy ? sc.BuyEntry(order) : sc.SellEntry(order);
	if (result <= 0.0)
	{
		const int resultCode = (int)result;
		const char* resultText = sc.GetTradingErrorTextMessage(resultCode);
		if (resultText == nullptr)
			resultText = "";

		SCString msg;
		msg.Format(
			"Trade Gate: Order rejected (Result=%d '%s', Type=%d, Qty=%d, Price1=%.8f, Replay=%d, ReplayStatus=%d, Sim=%d, SendOrders=%d, AutoTrading=%d, AutoTradingChart=%d, TradingLocked=%u)",
			resultCode,
			resultText,
			(int)order.OrderType,
			(int)order.OrderQuantity,
			order.Price1,
			sc.IsReplayRunning(),
			sc.ReplayStatus,
			sc.GlobalTradeSimulationIsOn,
			sc.SendOrdersToTradeService,
			sc.IsAutoTradingEnabled,
			sc.IsAutoTradingOptionEnabledForChart,
			(unsigned)sc.TradingIsLocked);
		sc.AddMessageToLog(msg, 1);
	}
	else
	{
		sc.AddMessageToLog("Trade Gate: Order submitted.", 0);
	}
}
