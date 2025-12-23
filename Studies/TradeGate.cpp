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
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include <cstdint>

#include "TradeGateJson.h"

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
		std::string checklistJson;
		HWND orderDialogHwnd = nullptr;
		bool orderDialogDone = false;
		bool orderDialogResult = false;
		UINT orderDialogCmd = 0;
		double clickedPrice = 0.0;
		int orderQuantity = 0;
		double previewPrice = 0.0;
		bool hasPreviewPrice = false;
		POINT lastClickScreenPt{};
		bool hasClickPoint = false;
		DWORD lastPopupTick = 0;
	};

	static TradeGateSession gSession;
	constexpr int STATE_IDLE = 0;
	constexpr int STATE_WAITING_DIALOG = 1;
	constexpr int STATE_WAITING_ORDER = 2;

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

	static void ClearPreviewLine(SCStudyInterfaceRef sc)
	{
		int& lineId = sc.GetPersistentInt(3);
		if (lineId != 0)
		{
			sc.DeleteACSChartDrawing(sc.ChartNumber, lineId, 0);
			lineId = 0;
		}
	}

	static void UpdatePreviewLine(SCStudyInterfaceRef sc, double price)
	{
		int& lineId = sc.GetPersistentInt(3);
		if (!std::isfinite(price) || price <= 0.0)
		{
			ClearPreviewLine(sc);
			return;
		}

		s_UseTool tool;
		tool.Clear();
		tool.ChartNumber = sc.ChartNumber;
		tool.DrawingType = DRAWING_LINE;
		tool.AddMethod = UTAM_ADD_OR_ADJUST;
		tool.LineNumber = (lineId == 0) ? -1 : lineId;
		tool.BeginIndex = 0;
		const int forward = 200;
		const int endIndex = (sc.ArraySize > 0 ? sc.ArraySize - 1 : 0) + forward;
		tool.EndIndex = endIndex;
		tool.BeginValue = (float)price;
		tool.EndValue = (float)price;
		tool.Color = RGB(255, 128, 0);
		tool.LineWidth = 2;
		tool.AddAsUserDrawnDrawing = 0;
		sc.UseTool(tool);
		if (lineId == 0)
			lineId = tool.LineNumber;
	}

	static void ResetOrderDialogState(TradeGateSession& s)
	{
		s.orderDialogCmd = CMD_NONE;
		s.orderDialogDone = false;
		s.orderDialogResult = false;
		s.orderDialogHwnd = nullptr;
		s.clickedPrice = 0.0;
		s.orderQuantity = 0;
		s.previewPrice = 0.0;
		s.hasPreviewPrice = false;
		s.lastClickScreenPt = POINT{};
		s.hasClickPoint = false;
	}

	static bool AskYesNoDetailed(
		HWND hwnd,
		const wchar_t* title,
		const wchar_t* mainInstruction,
		const wchar_t* content,
		const wchar_t* yesText,
		const wchar_t* noText,
		int defaultButtonId);

	static std::wstring ToWideBestEffort(const SCString& s);
	static std::wstring ToWideBestEffort(const std::string& s);

	struct OrderDialogRuntime
	{
		TradeGateSession* session = nullptr;
		double clickedPrice = 0.0;
		double tickSize = 0.0;
		int quantity = 1;
	};

	static std::wstring FormatPriceW(double price)
	{
		SCString s;
		s.Format("%.8f", price);
		return ToWideBestEffort(s);
	}

	static double PriceForCommand(const OrderDialogRuntime& rt, UINT cmd)
	{
		switch (cmd)
		{
			case CMD_BUY_LIMIT: return RoundToTick(rt.clickedPrice, rt.tickSize, TickRounding::Nearest);
			case CMD_BUY_STOP: return RoundToTick(rt.clickedPrice, rt.tickSize, TickRounding::Up);
			case CMD_SELL_LIMIT: return RoundToTick(rt.clickedPrice, rt.tickSize, TickRounding::Up);
			case CMD_SELL_STOP: return RoundToTick(rt.clickedPrice, rt.tickSize, TickRounding::Down);
			default: return rt.clickedPrice;
		}
	}

	static std::wstring ButtonLabel(const wchar_t* base, double price, bool showPrice)
	{
		if (!showPrice)
			return base;
		std::wstring label = base;
		label += L" @ ";
		label += FormatPriceW(price);
		return label;
	}

	constexpr UINT IDC_ORDER_HEADER = 2201;

	static void FinishOrderDialog(HWND hDlg, OrderDialogRuntime* rt, UINT cmd)
	{
		if (rt != nullptr)
		{
			TradeGateSession* session = rt->session;
			if (session != nullptr)
			{
				session->orderDialogCmd = cmd;
				session->orderDialogResult = (cmd != CMD_NONE);
				session->orderDialogDone = true;
				session->orderDialogHwnd = nullptr;
				double preview = PriceForCommand(*rt, cmd);
				session->previewPrice = preview;
				session->hasPreviewPrice = std::isfinite(preview) && preview > 0.0;
			}
		}
		DestroyWindow(hDlg);
	}

	static INT_PTR CALLBACK OrderDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		auto* rt = reinterpret_cast<OrderDialogRuntime*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
		switch (msg)
		{
			case WM_INITDIALOG:
			{
				rt = reinterpret_cast<OrderDialogRuntime*>(lParam);
				if (rt == nullptr)
					return FALSE;
				SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)rt);

				const int margin = 12;
				const int buttonWidth = 320;
				const int buttonHeight = 30;
				const int buttonGap = 8;

				HFONT dlgFont = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
				if (dlgFont == nullptr)
					dlgFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

				std::wstring header = L"Order price at click: ";
				header += FormatPriceW(rt->clickedPrice);
				header += L"    Qty: ";
				header += std::to_wstring(rt->quantity);

				RECT textRc{0, 0, buttonWidth, 0};
				HDC hdc = GetDC(hDlg);
				if (hdc != nullptr)
				{
					DrawTextW(hdc, header.c_str(), (int)header.size(), &textRc, DT_CALCRECT | DT_WORDBREAK);
					ReleaseDC(hDlg, hdc);
				}
				int headerH = textRc.bottom - textRc.top;
				if (headerH < 24)
					headerH = 24;

				int y = margin;
				HWND hHeader = CreateWindowExW(
					0,
					L"STATIC",
					header.c_str(),
					WS_CHILD | WS_VISIBLE,
					margin,
					y,
					buttonWidth,
					headerH,
					hDlg,
					(HMENU)(uintptr_t)IDC_ORDER_HEADER,
					GetModuleHandleW(nullptr),
					nullptr);
				if (hHeader != nullptr)
					SendMessageW(hHeader, WM_SETFONT, (WPARAM)dlgFont, TRUE);

				y += headerH + 10;

				struct BtnDef { UINT id; const wchar_t* text; bool showPrice; };
				BtnDef btns[] = {
					{ CMD_BUY_MARKET, L"Buy Market", false },
					{ CMD_BUY_LIMIT,  L"Buy Limit",  true },
					{ CMD_BUY_STOP,   L"Buy Stop",   true },
					{ CMD_SELL_MARKET, L"Sell Market", false },
					{ CMD_SELL_LIMIT, L"Sell Limit", true },
					{ CMD_SELL_STOP,  L"Sell Stop",  true },
				};

				for (const auto& b : btns)
				{
					double p = PriceForCommand(*rt, b.id);
					std::wstring label = ButtonLabel(b.text, p, b.showPrice);
					HWND hBtn = CreateWindowExW(
						0,
						L"BUTTON",
						label.c_str(),
						WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
						margin,
						y,
						buttonWidth,
						buttonHeight,
						hDlg,
						(HMENU)(uintptr_t)b.id,
						GetModuleHandleW(nullptr),
						nullptr);
					if (hBtn != nullptr)
						SendMessageW(hBtn, WM_SETFONT, (WPARAM)dlgFont, TRUE);
					y += buttonHeight + buttonGap;
				}

				const int cancelWidth = 120;
				HWND hCancel = CreateWindowExW(
					0,
					L"BUTTON",
					L"Cancel",
					WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
					margin,
					y,
					cancelWidth,
					buttonHeight,
					hDlg,
					(HMENU)CMD_CANCEL,
					GetModuleHandleW(nullptr),
					nullptr);
				if (hCancel != nullptr)
					SendMessageW(hCancel, WM_SETFONT, (WPARAM)dlgFont, TRUE);

				y += buttonHeight + margin;
				const int width = buttonWidth + margin * 2;
				SetWindowPos(hDlg, HWND_TOPMOST, 0, 0, width, y, SWP_NOMOVE);
				return TRUE;
			}
			case WM_COMMAND:
			{
				const UINT id = LOWORD(wParam);
				if (id == CMD_CANCEL)
				{
					FinishOrderDialog(hDlg, rt, CMD_NONE);
					return TRUE;
				}
				if (id == CMD_BUY_MARKET || id == CMD_BUY_LIMIT || id == CMD_BUY_STOP
					|| id == CMD_SELL_MARKET || id == CMD_SELL_LIMIT || id == CMD_SELL_STOP)
				{
					FinishOrderDialog(hDlg, rt, id);
					return TRUE;
				}
				break;
			}
			case WM_CLOSE:
			{
				FinishOrderDialog(hDlg, rt, CMD_NONE);
				return TRUE;
			}
			case WM_NCDESTROY:
			{
				if (rt != nullptr)
				{
					TradeGateSession* session = rt->session;
					if (session != nullptr && session->orderDialogHwnd == hDlg)
						session->orderDialogHwnd = nullptr;
					delete rt;
				}
				SetWindowLongPtrW(hDlg, GWLP_USERDATA, 0);
				return FALSE;
			}
		}
		return FALSE;
	}

	static void PositionWindowNearPoint(HWND hwnd, POINT screenPt)
	{
		RECT rc{};
		if (!GetWindowRect(hwnd, &rc))
			return;

		const int w = rc.right - rc.left;
		const int h = rc.bottom - rc.top;
		HMONITOR mon = MonitorFromPoint(screenPt, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi{};
		mi.cbSize = sizeof(mi);
		if (!GetMonitorInfoW(mon, &mi))
			return;

		int x = screenPt.x - w / 2;
		int y = screenPt.y - h / 2;
		const int minX = mi.rcWork.left;
		const int maxX = mi.rcWork.right - w;
		const int minY = mi.rcWork.top;
		const int maxY = mi.rcWork.bottom - h;
		x = std::clamp(x, minX, maxX);
		y = std::clamp(y, minY, maxY);

		SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	static HWND StartOrderSelectionDialog(HWND parent, TradeGateSession& session, double clickedPrice, int quantity, double tickSize, const POINT* clickScreenPt)
	{
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
		dt.dlg.cy = 200;
		dt.menu = 0;
		dt.windowClass = 0;
		dt.title[0] = L'\0';

		OrderDialogRuntime* rt = new OrderDialogRuntime();
		rt->session = &session;
		rt->clickedPrice = clickedPrice;
		rt->tickSize = tickSize;
		rt->quantity = quantity > 0 ? quantity : 1;

		HWND hDlg = CreateDialogIndirectParamW(
			GetModuleHandleW(nullptr),
			reinterpret_cast<LPCDLGTEMPLATEW>(&dt),
			parent,
			OrderDlgProc,
			reinterpret_cast<LPARAM>(rt));

		if (hDlg == nullptr)
		{
			delete rt;
			return nullptr;
		}

		ShowWindow(hDlg, SW_SHOWNORMAL);
		if (clickScreenPt != nullptr)
			PositionWindowNearPoint(hDlg, *clickScreenPt);
		return hDlg;
	}

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

	static std::string JsonEscape(const std::string& s)
	{
		std::string out;
		out.reserve(s.size() + 8);
		for (unsigned char c : s)
		{
			switch (c)
			{
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\b': out += "\\b"; break;
				case '\f': out += "\\f"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (c < 0x20)
					{
						char buf[7];
						snprintf(buf, sizeof(buf), "\\u%04x", c);
						out += buf;
					}
					else
					{
						out.push_back((char)c);
					}
					break;
			}
		}
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

	static bool ParseChecklistJson(const std::string& jsonText, ChecklistDialogParams& outParams, std::wstring& outError)
	{
		outError.clear();
		using namespace TradeGateJson;
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
			ChecklistDialogParams params{};
			bool hasParams = false;
			TradeGateSession* session = nullptr;
			std::wstring mainText;
			struct ControlRef
			{
				ChecklistDialogParams::Item::Type type;
				bool required = false;
				UINT idFirst = 0;
				int optionCount = 0;
				size_t itemIndex = 0;
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

		static std::wstring ReadWindowText(HWND h)
		{
			if (h == nullptr)
				return {};
			int len = GetWindowTextLengthW(h);
			if (len <= 0)
				return {};
			std::wstring out;
			out.resize((size_t)len);
			GetWindowTextW(h, out.data(), len + 1);
			return out;
		}

		static std::string CollectChecklistResponses(HWND hDlg, const ChecklistDialogRuntime& runtime)
		{
			if (!runtime.hasParams)
				return {};

			const auto& items = runtime.params.items;
			std::string json;
			json.reserve(256);
			json += "{\"items\":";
			json += "{";
			bool first = true;
			for (const auto& c : runtime.controls)
			{
				if (c.itemIndex >= items.size())
					continue;
				const auto& item = items[c.itemIndex];
				std::string key = JsonEscape(ToUtf8(item.id));
				std::string valueJson;
				switch (item.type)
				{
					case ChecklistDialogParams::Item::Type::Checkbox:
					{
						HWND h = GetDlgItem(hDlg, (int)c.idFirst);
						const bool checked = (h != nullptr && SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED);
						valueJson = checked ? "true" : "false";
						break;
					}
					case ChecklistDialogParams::Item::Type::Text:
					case ChecklistDialogParams::Item::Type::TextArea:
					{
						HWND h = GetDlgItem(hDlg, (int)c.idFirst);
						std::wstring w = ReadWindowText(h);
						std::string utf8 = ToUtf8(w);
						valueJson = "\"" + JsonEscape(utf8) + "\"";
						break;
					}
					case ChecklistDialogParams::Item::Type::Dropdown:
					{
						HWND h = GetDlgItem(hDlg, (int)c.idFirst);
						LRESULT sel = (h != nullptr) ? SendMessageW(h, CB_GETCURSEL, 0, 0) : (LRESULT)CB_ERR;
						if (sel != CB_ERR && sel >= 0 && sel < (LRESULT)item.options.size())
						{
							std::string utf8 = ToUtf8(item.options[(size_t)sel]);
							valueJson = "\"" + JsonEscape(utf8) + "\"";
						}
						else
						{
							valueJson = "null";
						}
						break;
					}
					case ChecklistDialogParams::Item::Type::Radio:
					{
						std::wstring choice;
						for (int i = 0; i < c.optionCount; ++i)
						{
							HWND h = GetDlgItem(hDlg, (int)c.idFirst + i);
							if (h != nullptr && SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED)
							{
								if ((size_t)i < item.options.size())
									choice = item.options[(size_t)i];
								break;
							}
						}
						if (!choice.empty())
						{
							std::string utf8 = ToUtf8(choice);
							valueJson = "\"" + JsonEscape(utf8) + "\"";
						}
						else
						{
							valueJson = "null";
						}
						break;
					}
					case ChecklistDialogParams::Item::Type::MultiCheckbox:
					{
						std::vector<std::string> selected;
						for (int i = 0; i < c.optionCount; ++i)
						{
							HWND h = GetDlgItem(hDlg, (int)c.idFirst + i);
							if (h != nullptr && SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED)
							{
								if ((size_t)i < item.options.size())
									selected.push_back(ToUtf8(item.options[(size_t)i]));
							}
						}
						valueJson = "[";
						for (size_t i = 0; i < selected.size(); ++i)
						{
							if (i > 0)
								valueJson += ",";
							valueJson += "\"" + JsonEscape(selected[i]) + "\"";
						}
						valueJson += "]";
						break;
					}
					default:
						valueJson = "null";
						break;
				}

				if (!first)
					json += ",";
				json += "\"" + key + "\":" + valueJson;
				first = false;
			}
			json += "}";
			json += ",\"finalConfirm\":";
			json += runtime.params.finalConfirm ? "true" : "false";
			json += "}";
			return json;
		}

		static INT_PTR CALLBACK ChecklistDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			auto* runtime = reinterpret_cast<ChecklistDialogRuntime*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
			ChecklistDialogParams* params = (runtime != nullptr && runtime->hasParams) ? &runtime->params : nullptr;
			switch (msg)
			{
				case WM_INITDIALOG:
				{
					runtime = reinterpret_cast<ChecklistDialogRuntime*>(lParam);
					if (runtime == nullptr)
						return FALSE;
					SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)runtime);
					params = (runtime != nullptr && runtime->hasParams) ? &runtime->params : nullptr;
					if (params != nullptr && params->title != nullptr)
						SetWindowTextW(hDlg, params->title);

					// Auto-size dialog based on wrapped text.
					const int margin = 12;
					const int buttonWidth = 110;
					const int buttonHeight = 28;
					const int buttonGap = 10;
					const int minClientWidth = 520;
					const int optionIndent = 18;
					const int optionBulletWidth = 26;

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
										int oh = MeasureWrappedTextHeight(hdc, dlgFont, opt.c_str(), textW - optionIndent - optionBulletWidth);
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
						for (size_t itemIndex = 0; itemIndex < params->items.size(); ++itemIndex)
						{
							const auto& item = params->items[itemIndex];
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
								runtime->controls.push_back({item.type, item.required, id, 1, itemIndex});
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
									runtime->controls.push_back({item.type, item.required, id, 1, itemIndex});
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
									runtime->controls.push_back({item.type, item.required, id, 1, itemIndex});
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
										int oh = MeasureWrappedTextHeight(hdc, dlgFont, opt.c_str(), width - optionIndent - optionBulletWidth);
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
											margin + optionIndent,
											y,
											width - optionIndent,
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
									runtime->controls.push_back({item.type, item.required, first, optCount, itemIndex});
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
							session->checklistJson.clear();
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
								if (runtime != nullptr)
									session->checklistJson = CollectChecklistResponses(hDlg, *runtime);
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
						session->checklistJson.clear();
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
			runtime->params = params;
			runtime->hasParams = true;
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

	static double GetClickedPrice(SCStudyInterfaceRef sc, HWND hwnd, const POINT* screenPoint)
	{
		const bool debug = sc.Input[INPUT_DEBUG_LOG].GetYesNo();

		// Prefer deriving from the current cursor pixel location.
		double pixelPrice = 0.0;
		bool pixelOk = false;
		if (hwnd != nullptr)
		{
			POINT pt{};
			if (screenPoint != nullptr)
				pt = *screenPoint;
			else
				GetCursorPos(&pt);
			if (ScreenToClient(hwnd, &pt))
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

	static void HandleOrderFlow(
		SCStudyInterfaceRef sc,
		HWND hwnd,
		s_SCNewOrder order,
		bool isBuy,
		const std::wstring& orderSummary,
		int& state)
	{
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
			ClearPreviewLine(sc);
			ResetOrderDialogState(gSession);
			return;
		}

		const bool checklistEnabled = sc.Input[INPUT_CHECKLIST_ENABLED].GetYesNo();
		const bool needDialog = checklistEnabled && (!params.items.empty() || params.finalConfirm);
		if (needDialog)
		{
			gSession.checklistDone = false;
			gSession.checklistResult = false;
			gSession.checklistJson.clear();
			gSession.pendingOrder = order;
			gSession.pendingIsBuy = isBuy;
			gSession.orderSummary = orderSummary;
			HWND dlg = StartChecklistCheckboxDialog(hwnd, params, gSession);
			if (dlg == nullptr)
			{
				sc.AddMessageToLog("Trade Gate: Could not create checklist dialog.", 1);
				gSession.checklistDone = false;
				gSession.pendingIsBuy = false;
				ClearPreviewLine(sc);
				ResetOrderDialogState(gSession);
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

		ClearPreviewLine(sc);
		ResetOrderDialogState(gSession);
		state = STATE_IDLE;
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
	{
		ClearPreviewLine(sc);
		return;
	}

	if (!sc.Input[INPUT_ENABLE].GetYesNo())	
	{
		ClearPreviewLine(sc);
		return;
	}

	int& state = sc.GetPersistentInt(1);
	int& PrevLButtonDown = sc.GetPersistentInt(2);

	if (state == STATE_IDLE)
	{
		// Safety: ensure any stray preview line is cleared when idle.
		ClearPreviewLine(sc);
	}

	if (state == STATE_WAITING_DIALOG)
	{
		if (gSession.hasPreviewPrice)
			UpdatePreviewLine(sc, gSession.previewPrice);
		else
			ClearPreviewLine(sc);

		if (gSession.checklistDone)
		{
			if (gSession.checklistResult)
			{
				if (!gSession.checklistJson.empty())
					gSession.pendingOrder.TextTag = gSession.checklistJson.c_str();
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
			gSession.checklistJson.clear();
			ClearPreviewLine(sc);
			ResetOrderDialogState(gSession);
			state = STATE_IDLE;
			return;
		}

		if (gSession.checklistHwnd != nullptr && !IsWindow(gSession.checklistHwnd))
		{
			gSession.checklistHwnd = nullptr;
			gSession.checklistDone = false;
			gSession.checklistResult = false;
			gSession.checklistJson.clear();
			ClearPreviewLine(sc);
			ResetOrderDialogState(gSession);
			state = STATE_IDLE;
		}
		return;
	}

	if (state == STATE_WAITING_ORDER)
	{
		HWND hwnd = GetChartHwnd(sc);
		if (gSession.hasPreviewPrice)
			UpdatePreviewLine(sc, gSession.previewPrice);
		else
			ClearPreviewLine(sc);

		if (gSession.orderDialogDone)
		{
			if (gSession.orderDialogResult && gSession.orderDialogCmd != CMD_NONE)
			{
				s_SCNewOrder order{};
				order.Price1 = 0.0;
				order.OrderQuantity = gSession.orderQuantity > 0 ? gSession.orderQuantity : GetOrderQuantity(sc);

				bool isBuy = false;
				std::wstring orderSummary;
				auto formatPrice = [&](double price) -> std::wstring
				{
					SCString s;
					s.Format("%.8f", price);
					return ToWideBestEffort(s);
				};

				switch (gSession.orderDialogCmd)
				{
					case CMD_BUY_MARKET:
						isBuy = true;
						order.OrderType = SCT_ORDERTYPE_MARKET;
						orderSummary = L"Order: Buy Market";
						break;
					case CMD_BUY_LIMIT:
						isBuy = true;
						order.OrderType = SCT_ORDERTYPE_LIMIT;
						order.Price1 = RoundToTick(gSession.clickedPrice, sc.TickSize, TickRounding::Nearest);
						orderSummary = L"Order: Buy Limit @ " + formatPrice(order.Price1);
						break;
					case CMD_BUY_STOP:
						isBuy = true;
						order.OrderType = SCT_ORDERTYPE_STOP;
						order.Price1 = RoundToTick(gSession.clickedPrice, sc.TickSize, TickRounding::Up);
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
						order.Price1 = RoundToTick(gSession.clickedPrice, sc.TickSize, TickRounding::Up);
						orderSummary = L"Order: Sell Limit @ " + formatPrice(order.Price1);
						break;
					case CMD_SELL_STOP:
						isBuy = false;
						order.OrderType = SCT_ORDERTYPE_STOP;
						order.Price1 = RoundToTick(gSession.clickedPrice, sc.TickSize, TickRounding::Down);
						orderSummary = L"Order: Sell Stop @ " + formatPrice(order.Price1);
						break;
					default:
						ClearPreviewLine(sc);
						ResetOrderDialogState(gSession);
						state = STATE_IDLE;
						return;
				}

				if (order.Price1 > 0.0)
				{
					gSession.previewPrice = order.Price1;
					gSession.hasPreviewPrice = true;
					UpdatePreviewLine(sc, gSession.previewPrice);
				}

				HandleOrderFlow(sc, hwnd, order, isBuy, orderSummary, state);
			}
			else
			{
				ClearPreviewLine(sc);
				ResetOrderDialogState(gSession);
				state = STATE_IDLE;
			}
			gSession.orderDialogDone = false;
			return;
		}

		if (gSession.orderDialogHwnd != nullptr && !IsWindow(gSession.orderDialogHwnd))
		{
			ClearPreviewLine(sc);
			ResetOrderDialogState(gSession);
			state = STATE_IDLE;
			return;
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
	POINT clickPt{};
	const bool hasClickPt = GetCursorPos(&clickPt) != FALSE;
	const double clickedPrice = SanitizePrice(sc, GetClickedPrice(sc, hwnd, hasClickPt ? &clickPt : nullptr));

	ResetOrderDialogState(gSession);
	gSession.clickedPrice = clickedPrice;
	gSession.orderQuantity = GetOrderQuantity(sc);
	gSession.orderDialogDone = false;
	gSession.orderDialogResult = false;
	gSession.previewPrice = clickedPrice;
	gSession.hasPreviewPrice = clickedPrice > 0.0 && std::isfinite(clickedPrice);
	if (hasClickPt)
	{
		gSession.lastClickScreenPt = clickPt;
		gSession.hasClickPoint = true;
	}
	if (gSession.hasPreviewPrice)
		UpdatePreviewLine(sc, clickedPrice);

	HWND orderDlg = StartOrderSelectionDialog(
		hwnd,
		gSession,
		clickedPrice,
		gSession.orderQuantity,
		sc.TickSize,
		gSession.hasClickPoint ? &gSession.lastClickScreenPt : nullptr);
	if (orderDlg != nullptr)
	{
		gSession.orderDialogHwnd = orderDlg;
		state = STATE_WAITING_ORDER;
		return;
	}

	// Fallback to legacy blocking menu if the modeless dialog could not be created.
	ClearPreviewLine(sc);
	ResetOrderDialogState(gSession);

	UINT cmd = ShowOrderMenu(hwnd);
	if (cmd == CMD_NONE || cmd == CMD_CANCEL)
	{
		ClearPreviewLine(sc);
		ResetOrderDialogState(gSession);
		state = STATE_IDLE;
		return;
	}

	s_SCNewOrder order{};
	order.Price1 = 0.0;
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

	if (order.Price1 > 0.0)
	{
		gSession.previewPrice = order.Price1;
		gSession.hasPreviewPrice = true;
		UpdatePreviewLine(sc, gSession.previewPrice);
	}

	HandleOrderFlow(sc, hwnd, order, isBuy, orderSummary, state);
}
