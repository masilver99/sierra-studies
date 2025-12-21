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
				scaled = std::llround(scaled);
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
	};

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
		AppendMenuW(menu, MF_STRING, CMD_SELL_MARKET, L"Sell Market");
		AppendMenuW(menu, MF_STRING, CMD_SELL_LIMIT, L"Sell Limit @ Click Price");
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
		std::wstring q[4];
		int qCount = 0;
	};

	static bool RunChecklistCheckboxDialog(HWND parent, const ChecklistDialogParams& params);

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

	static bool RunChecklist(SCStudyInterfaceRef sc, HWND hwnd)
	{
		if (!sc.Input[INPUT_CHECKLIST_ENABLED].GetYesNo())
			return true;

		const SCString q1 = sc.Input[INPUT_CHECK_1_TEXT].GetString();
		const SCString q2 = sc.Input[INPUT_CHECK_2_TEXT].GetString();
		const SCString q3 = sc.Input[INPUT_CHECK_3_TEXT].GetString();
		const SCString q4 = sc.Input[INPUT_CHECK_4_TEXT].GetString();

		ChecklistDialogParams params;
		params.finalConfirm = sc.Input[INPUT_FINAL_CONFIRM].GetYesNo();

		auto addQuestion = [&](const SCString& q)
		{
			if (q.IsEmpty())
				return;
			std::wstring w = ToWideBestEffort(q);
			if (w.empty())
				return;
			if (params.qCount < 4)
				params.q[params.qCount++] = std::move(w);
		};

		addQuestion(q1);
		addQuestion(q2);
		addQuestion(q3);
		addQuestion(q4);

		if (params.qCount == 0)
		{
			if (!params.finalConfirm)
				return true;
			return AskYesNoDetailed(hwnd, L"Trade Gate", L"Submit this order now?", L"", L"Submit", L"Cancel", IDNO);
		}

		return RunChecklistCheckboxDialog(hwnd, params);
	}

	namespace
	{
		constexpr UINT IDC_CHECKLIST_MAIN_TEXT = 3001;
		constexpr UINT IDC_CHECKLIST_Q_BASE = 3100;
		constexpr UINT IDC_CHECKLIST_OK = IDOK;
		constexpr UINT IDC_CHECKLIST_CANCEL = IDCANCEL;

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

		static void UpdateChecklistOkEnabled(HWND hDlg, int qCount)
		{
			bool allChecked = true;
			for (int i = 0; i < qCount; ++i)
			{
				HWND hCheck = GetDlgItem(hDlg, (int)IDC_CHECKLIST_Q_BASE + i);
				if (hCheck == nullptr)
					continue;
				if (SendMessageW(hCheck, BM_GETCHECK, 0, 0) != BST_CHECKED)
				{
					allChecked = false;
					break;
				}
			}
			HWND hOk = GetDlgItem(hDlg, IDC_CHECKLIST_OK);
			if (hOk != nullptr)
				EnableWindow(hOk, allChecked ? TRUE : FALSE);
		}

		static INT_PTR CALLBACK ChecklistDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			ChecklistDialogParams* params = reinterpret_cast<ChecklistDialogParams*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
			switch (msg)
			{
				case WM_INITDIALOG:
				{
					params = reinterpret_cast<ChecklistDialogParams*>(lParam);
					SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)params);
					if (params != nullptr && params->title != nullptr)
						SetWindowTextW(hDlg, params->title);

					// Auto-size dialog based on wrapped text.
					const int margin = 12;
					const int buttonWidth = 110;
					const int buttonHeight = 28;
					const int buttonGap = 10;
					const int minClientWidth = 460;

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
					const int checkboxTextW = textW - 26; // approx checkbox box + padding

					const wchar_t* mainText = (params != nullptr && params->finalConfirm)
						? L"Check each item, then submit the order."
						: L"Check each item to proceed.";
					int mainH = MeasureWrappedTextHeight(hdc, dlgFont, mainText, textW);
					if (mainH < 18) mainH = 18;

					int qCount = params != nullptr ? params->qCount : 0;
					int contentH = 0;
					for (int i = 0; i < qCount; ++i)
					{
						const std::wstring& q = params->q[i];
						int qh = MeasureWrappedTextHeight(hdc, dlgFont, q.c_str(), checkboxTextW);
						// ensure room for checkbox and focus rect
						qh += 8;
						if (qh < 22) qh = 22;
						contentH += qh;
						contentH += 6;
					}
					if (contentH > 0)
						contentH -= 6;

					ReleaseDC(hDlg, hdc);

					const int desiredClientH = margin + mainH + 10 + contentH + 14 + buttonHeight + margin;
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

					hdc = GetDC(hDlg);
					for (int i = 0; i < qCount; ++i)
					{
						const std::wstring& q = params->q[i];
						int qh = MeasureWrappedTextHeight(hdc, dlgFont, q.c_str(), width - 26);
						qh += 8;
						if (qh < 22) qh = 22;

						HWND hCheck = CreateWindowExW(
							0,
							L"BUTTON",
							q.c_str(),
							WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE,
							margin,
							y,
							width,
							qh,
							hDlg,
							(HMENU)(uintptr_t)((int)IDC_CHECKLIST_Q_BASE + i),
							GetModuleHandleW(nullptr),
							nullptr);
						if (hCheck != nullptr)
							SendMessageW(hCheck, WM_SETFONT, (WPARAM)dlgFont, TRUE);
						y += qh + 6;
					}
					ReleaseDC(hDlg, hdc);

					y += 14;
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

					UpdateChecklistOkEnabled(hDlg, qCount);
					return TRUE;
				}

				case WM_COMMAND:
				{
					const UINT id = LOWORD(wParam);
					const UINT code = HIWORD(wParam);
					if (id == IDC_CHECKLIST_CANCEL)
					{
						EndDialog(hDlg, 0);
						return TRUE;
					}
					if (id == IDC_CHECKLIST_OK)
					{
						int qCount = params != nullptr ? params->qCount : 0;
						UpdateChecklistOkEnabled(hDlg, qCount);
						HWND hOk = GetDlgItem(hDlg, IDC_CHECKLIST_OK);
						if (hOk != nullptr && IsWindowEnabled(hOk))
						{
							EndDialog(hDlg, 1);
							return TRUE;
						}
						return TRUE;
					}

					if (id >= IDC_CHECKLIST_Q_BASE && id < IDC_CHECKLIST_Q_BASE + 4 && code == BN_CLICKED)
					{
						int qCount = params != nullptr ? params->qCount : 0;
						UpdateChecklistOkEnabled(hDlg, qCount);
						return TRUE;
					}

					break;
				}
			}
			return FALSE;
		}
	}

	static bool RunChecklistCheckboxDialog(HWND parent, const ChecklistDialogParams& params)
	{
		// Build a minimal dialog template with no controls; we add all controls in WM_INITDIALOG.
		// Using DialogBoxIndirectParam keeps it modal and provides default keyboard navigation.
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

		INT_PTR r = DialogBoxIndirectParamW(
			GetModuleHandleW(nullptr),
			reinterpret_cast<LPCDLGTEMPLATEW>(&dt),
			parent,
			ChecklistDlgProc,
			reinterpret_cast<LPARAM>(&const_cast<ChecklistDialogParams&>(params)));

		return r == 1;
	}

	static int GetOrderQuantity(SCStudyInterfaceRef sc)
	{
		if (sc.Input[INPUT_USE_TRADE_WINDOW_QTY].GetYesNo())
			return (int)sc.TradeWindowOrderQuantity;
		const int q = sc.Input[INPUT_FIXED_QTY].GetInt();
		return q > 0 ? q : 1;
	}

	static double GetClickedPrice(SCStudyInterfaceRef sc, HWND hwnd)
	{
		// Prefer deriving from the current cursor pixel location.
		double price = 0.0;
		if (hwnd != nullptr)
		{
			POINT pt{};
			if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt))
			{
				price = sc.YPixelCoordinateToGraphValue(pt.y);
			}
		}

		// Fallback: many ACSIL builds provide this as the pointer's chart value.
		if (price == 0.0)
			price = sc.ActiveToolYValue;
		if (price == 0.0 && sc.ArraySize > 0)
			price = sc.Close[sc.ArraySize - 1];
		return price;
	}
}

SCSFExport scsf_TradeGate(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName = "Trade Gate (SHIFT + Left Click)";
		sc.StudyDescription = "SHIFT+LeftClick opens an order menu, runs a checklist gate, and submits the selected order programmatically.";
		sc.AutoLoop = 0;
		sc.ReceivePointerEvents = ACS_RECEIVE_POINTER_EVENTS_ALWAYS;
		sc.UpdateAlways = 1;
		sc.SupportAttachedOrdersForTrading = true;
		sc.AllowEntryWithWorkingOrders = true;
		sc.AllowOppositeEntryWithOpposingPositionOrOrders = true;

		sc.Input[INPUT_ENABLE].Name = "Enable";
		sc.Input[INPUT_ENABLE].SetYesNo(true);

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

		return;
	}

	if (!sc.Input[INPUT_ENABLE].GetYesNo())		return;

	// Prevent re-entrant menu/dialog loops.
	int& InProgress = sc.GetPersistentInt(1);
	if (InProgress)		return;

	// Reliable click detection: poll for a left-button down edge while SHIFT is held,
	// and only when the cursor is actually over this chart window.
	int& PrevLButtonDown = sc.GetPersistentInt(2);
	const bool lDown = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
	const bool justPressed = lDown && (PrevLButtonDown == 0);
	PrevLButtonDown = lDown ? 1 : 0;

	if (justPressed)
	{
		if (sc.Input[INPUT_REQUIRE_SHIFT].GetYesNo() && !GetShiftDown())
			return;

		HWND hwnd = GetChartHwnd(sc);
		if (!IsCursorOverWindow(hwnd))
			return;

		InProgress = 1;

		UINT cmd = ShowOrderMenu(hwnd);
		if (cmd == CMD_NONE || cmd == CMD_CANCEL)
		{
			InProgress = 0;
			return;
		}

		if (!RunChecklist(sc, hwnd))
		{
			sc.AddMessageToLog("Trade Gate: Checklist failed/cancelled; order not submitted.", 1);
			InProgress = 0;
			return;
		}

		s_SCNewOrder order{};
		order.OrderQuantity = GetOrderQuantity(sc);

		const double clickedPrice = GetClickedPrice(sc, hwnd);

		bool isBuy = false;
		switch (cmd)
		{
			case CMD_BUY_MARKET:
				isBuy = true;
				order.OrderType = SCT_ORDERTYPE_MARKET;
				break;
			case CMD_BUY_LIMIT:
				isBuy = true;
				order.OrderType = SCT_ORDERTYPE_LIMIT;
				order.Price1 = RoundToTick(clickedPrice, sc.TickSize, TickRounding::Down);
				break;
			case CMD_BUY_STOP:
				isBuy = true;
				order.OrderType = SCT_ORDERTYPE_STOP;
				order.Price1 = RoundToTick(clickedPrice, sc.TickSize, TickRounding::Up);
				break;
			case CMD_SELL_MARKET:
				isBuy = false;
				order.OrderType = SCT_ORDERTYPE_MARKET;
				break;
			case CMD_SELL_LIMIT:
				isBuy = false;
				order.OrderType = SCT_ORDERTYPE_LIMIT;
				order.Price1 = RoundToTick(clickedPrice, sc.TickSize, TickRounding::Up);
				break;
			case CMD_SELL_STOP:
				isBuy = false;
				order.OrderType = SCT_ORDERTYPE_STOP;
				order.Price1 = RoundToTick(clickedPrice, sc.TickSize, TickRounding::Down);
				break;
			default:
				InProgress = 0;
				return;
		}

		double result = isBuy ? sc.BuyEntry(order) : sc.SellEntry(order);
		if (result <= 0.0)
		{
			SCString msg;
			msg.Format("Trade Gate: Order rejected (Result=%.0f, Type=%d, Price1=%.8f)", result, (int)order.OrderType, order.Price1);
			sc.AddMessageToLog(msg, 1);
		}
		else
		{
			sc.AddMessageToLog("Trade Gate: Order submitted.", 0);
		}

		InProgress = 0;
		return;
	}
}
