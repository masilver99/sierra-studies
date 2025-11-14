// Switch the current chart's symbol via a toolbar button or a chart shortcut menu item.
//
// How it works:
// - Set "Target Symbol" in Inputs (example: ESZ4, MESM5, AAPL, etc.).
// - Set "ACS Control Bar Button Number" to the ACS button you add to your Control Bar.
// - The study also adds a right-click chart shortcut menu item "Switch to <symbol>".
// - On click, it uses OpenChartOrGetChartReference with PriorChartNumber set to the current
//   chart and UpdatePriorChartNumberParametersToMatch=1, so the existing chart's symbol
//   is changed to the target and its bar/period/session settings are preserved.

#include "sierrachart.h"

SCDLLName("Switch Symbol via Button")

SCSFExport scsf_SwitchSymbolViaButton(SCStudyInterfaceRef sc)
{
    SCInputRef TargetSymbol      = sc.Input[0];
    SCInputRef ControlBarButton  = sc.Input[1];
    SCInputRef ButtonAbbreviation = sc.Input[2];

    // Persistent values
    int& r_MenuID   = sc.GetPersistentIntFast(0);
    int& r_Initialized = sc.GetPersistentIntFast(1);

    if (sc.SetDefaults)
    {
        sc.GraphName = "Switch Symbol via Button";
        sc.AutoLoop = 0;  // event-driven
        sc.GraphRegion = 0;

        TargetSymbol.Name = "Target Symbol";
        TargetSymbol.SetString("ESZ4"); // Change default as desired

        ControlBarButton.Name = "ACS Control Bar Button Number (1-150)";
        ControlBarButton.SetInt(1);

        ButtonAbbreviation.Name = "Control Bar Button Abbreviation";
        ButtonAbbreviation.SetString("ES");

        return;
    } 

    const int buttonNumber = ControlBarButton.GetInt();

    // Clean-up when study is removed 
    if (sc.LastCallToFunction)
    {
        if (r_MenuID > 0)
            sc.RemoveACSChartShortcutMenuItem(sc.ChartNumber, r_MenuID);

        return;
    }

    // One-time init after load/recalc
    if (sc.UpdateStartIndex == 0 && r_Initialized == 0)
    {
        r_Initialized = 1;

        // Create a chart shortcut menu item
        {
            SCString label;
            label.Format("Switch to %s", TargetSymbol.GetString());
            r_MenuID = sc.AddACSChartShortcutMenuItem(sc.ChartNumber, label);
        }

        // Initialize the ACS Control Bar Button text (user must add this button to a Control Bar)
        if (buttonNumber >= 1)
        {
            SCString caption = ButtonAbbreviation.GetString();
            if (caption.IsEmpty())
                caption = TargetSymbol.GetString();
            //sc.SetCustomStudyControlBarButtonEnable(buttonNumber, 1);
            sc.SetCustomStudyControlBarButtonText(buttonNumber, caption);
        }
    }

    // Re-label the menu item if input string changed (lightweight)
    if (r_MenuID > 0 && sc.Index == 0)
    {
        // Remove and re-add to update the text
        sc.RemoveACSChartShortcutMenuItem(sc.ChartNumber, r_MenuID);
        SCString label;
        label.Format("Switch to %s", TargetSymbol.GetString());
        r_MenuID = sc.AddACSChartShortcutMenuItem(sc.ChartNumber, label);
    }

    // Keep the button text indicating whether the current chart already shows the target symbol.
    if (buttonNumber >= 1)
    {
        const SCString target = TargetSymbol.GetString();
        SCString caption = ButtonAbbreviation.GetString();
        if (caption.IsEmpty())
            caption = target;

        sc.SetCustomStudyControlBarButtonText(buttonNumber, caption);
    }

    // Determine if our action was requested
    bool triggered = false;

    // 1) Right-click chart shortcut menu item
    if (sc.MenuEventID == r_MenuID)
        triggered = true;

    // 2) ACS Control Bar button (add ACS Button #N to your control bar and set the same N in the input)
    // Note: Sierra Chart reports ACS button presses through sc.MenuEventID.
    // The integer value here corresponds to the ACS button number you added.
    // Set the button number in the Input and we’ll match it directly.
    // 3) Force button back to up position if it was pressed
    if (sc.MenuEventID == buttonNumber && buttonNumber >= 1)
    {
        triggered = true;
        //sc.PriorSelectedCustomStudyControlBarButtonNumber = 0;
        sc.SetCustomStudyControlBarButtonEnable(buttonNumber, 0);
    }

    if (!triggered)
        return;

    // Safety checks
    SCString newSymbol = TargetSymbol.GetString();
    if (newSymbol.IsEmpty())
    {
        sc.AddMessageToLog("Target Symbol input is empty. Nothing to do.", 1);
        return;
    }

    if (sc.Symbol.CompareNoCase(newSymbol) == 0)
        return;

    // Get current bar period parameters to preserve them after switching
    n_ACSIL::s_BarPeriod barPeriod;
    sc.GetBarPeriodParameters(barPeriod);

    // Preserve current session times
    n_ACSIL::s_ChartSessionTimes sessionTimes;
    sc.GetSessionTimesFromChart(sc.ChartNumber, sessionTimes);

    // Build parameters to modify the existing chart (rather than opening a new one)
    s_ACSOpenChartParameters p;
    p.PriorChartNumber = sc.ChartNumber;                    // modify current chart
    p.UpdatePriorChartNumberParametersToMatch = 1;          // apply our settings to it
    p.AlwaysOpenNewChart = 0;                               // do not open a new chart
    p.HideNewChart = 0;
    p.Symbol = newSymbol;

    if (barPeriod.ChartDataType == INTRADAY_DATA)
    {
        p.ChartDataType = INTRADAY_DATA;
        p.IntradayBarPeriodType        = barPeriod.IntradayChartBarPeriodType;
        p.IntradayBarPeriodLength      = barPeriod.IntradayChartBarPeriodParameter1;
        p.IntradayBarPeriodParm2       = barPeriod.IntradayChartBarPeriodParameter2;
        p.IntradayBarPeriodParm3       = barPeriod.IntradayChartBarPeriodParameter3;
        p.IntradayBarPeriodParm4       = barPeriod.IntradayChartBarPeriodParameter4;
    }
    else
    {
        p.ChartDataType = DAILY_DATA;
        p.HistoricalChartBarPeriod             = barPeriod.HistoricalChartBarPeriodType;
        p.HistoricalChartBarPeriodLengthInDays = barPeriod.HistoricalChartDaysPerBar;
    }

    // Preserve session settings
    p.SessionStartTime         = sessionTimes.StartTime;
    p.SessionEndTime           = sessionTimes.EndTime;
    p.EveningSessionStartTime  = sessionTimes.EveningStartTime;
    p.EveningSessionEndTime    = sessionTimes.EveningEndTime;
    p.UseEveningSession        = sessionTimes.UseEveningSessionTimes;

    // Perform the switch (will take effect after this function returns)
    int resultChartNumber = sc.OpenChartOrGetChartReference(p);
    if (resultChartNumber == 0)
    {
        sc.AddMessageToLog("Failed to switch symbol (OpenChartOrGetChartReference returned 0).", 1);
    }
    else
    {
        // Ensure the chart reloads data for the newly assigned symbol
        sc.RecalculateChart(resultChartNumber);
    }
}
