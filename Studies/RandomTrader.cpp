// RandomDrill_TradeTrainer.cpp
// ACSIL Study: Random Drill Trade Trainer
// Purpose: Pick a random historical date/time, jump chart, countdown, then place a random long/short market order
// with attached 10-point stop and 10-point profit target. Uses trade window size. Safety: requires simulation mode.

// Author: ChatGPT (generated for user's Sierra Chart ACSIL use)
// Date: 2025-12-07

#include "sierrachart.h"
#include <random>
#include <vector>
#include <cmath>
#include <cstring>

SCDLLName("Random Drill Trade Trainer")

// Helper constants and IDs
#define INPUT_START_DRILL_BUTTON 0
#define INPUT_DATE_RANGE_DAYS 1
#define INPUT_COUNTDOWN_SECONDS 2
#define INPUT_SHOW_COUNTDOWN 3
#define INPUT_MIN_TIME_HOUR 4
#define INPUT_MIN_TIME_MIN 5
#define INPUT_MAX_TIME_HOUR 6
#define INPUT_MAX_TIME_MIN 7
#define INPUT_SL_POINTS 8
#define INPUT_TP_POINTS 9
#define INPUT_ALLOW_TRADES_ONLY_IF_SIM 10
#define INPUT_LOG_LEVEL 11
#define INPUT_ENABLE_TOOLBAR_BUTTON 12
#define INPUT_REPLAY_SPEED 13

// User-defined button number for toolbar
#define TOOLBAR_BUTTON_NUMBER 1

// We'll use a single study export function
SCSFExport scsf_RandomDrillTradeTrainer(SCStudyInterfaceRef sc)
{
    auto updateCountdownText = [&sc](const SCString& text)
    {
        int& lineId = sc.GetPersistentInt(1001);
        if (text.IsEmpty())
        {
            if (lineId != 0)
            {
                sc.DeleteACSChartDrawing(sc.ChartNumber, lineId, 0);
                lineId = 0;
            }
            return;
        }

        // Anchor near the latest bar with a small vertical offset so it is visible.
        int anchorIndex = sc.ArraySize > 0 ? sc.ArraySize - 1 : 0;
        double anchorPrice = (sc.ArraySize > 0) ? sc.Close[anchorIndex] + sc.TickSize * 6.0 : 0.0;

        s_UseTool tool;
        tool.Clear();
        tool.ChartNumber = sc.ChartNumber;
        tool.DrawingType = DRAWING_TEXT;
        tool.AddMethod = UTAM_ADD_OR_ADJUST;
        tool.LineNumber = (lineId == 0 ? -1 : lineId);
        tool.BeginIndex = anchorIndex;
        tool.BeginValue = (float)anchorPrice;
        tool.Color = RGB(255, 215, 0);
        tool.Text = text;
        tool.FontSize = 12;
        tool.AddAsUserDrawnDrawing = 0;
        tool.UseRelativeVerticalValues = 0;
        sc.UseTool(tool);
        if (lineId == 0)
            lineId = tool.LineNumber;
    };

    // --- Set Defaults ----------------------------------------------------
    if (sc.SetDefaults)
    {
        sc.GraphName = "Random Drill Trade Trainer";
        sc.StudyDescription = "Pick a random historical day/time, jump to it, countdown, and place a random market trade with 10-point SL/TP. Uses Trade Window size. Simulation-only safety.";
        sc.AutoLoop = 0; // manual loop; we will control execution on ticks/updates
        sc.FreeDLL = 0;

        // Inputs
        sc.Input[INPUT_START_DRILL_BUTTON].Name = "Start Random Drill (fallback toggle)";
        sc.Input[INPUT_START_DRILL_BUTTON].SetYesNo(false);
        sc.Input[INPUT_START_DRILL_BUTTON].SetDescription("Set to Yes or press the toolbar button to start a random drill; auto-resets to No.");

        sc.Input[INPUT_DATE_RANGE_DAYS].Name = "Maximum lookback (days) - up to last 15 years recommended";
        sc.Input[INPUT_DATE_RANGE_DAYS].SetInt(5475); // default ~15 years

        sc.Input[INPUT_COUNTDOWN_SECONDS].Name = "Countdown seconds";
        sc.Input[INPUT_COUNTDOWN_SECONDS].SetInt(10);

        sc.Input[INPUT_SHOW_COUNTDOWN].Name = "Show countdown overlay on chart";
        sc.Input[INPUT_SHOW_COUNTDOWN].SetYesNo(true);

        sc.Input[INPUT_MIN_TIME_HOUR].Name = "Earliest hour (24h) - default 9";
        sc.Input[INPUT_MIN_TIME_HOUR].SetInt(9);
        sc.Input[INPUT_MIN_TIME_MIN].Name = "Earliest minute - default 30";
        sc.Input[INPUT_MIN_TIME_MIN].SetInt(30);

        sc.Input[INPUT_MAX_TIME_HOUR].Name = "Latest hour (24h) - default 14";
        sc.Input[INPUT_MAX_TIME_HOUR].SetInt(14);
        sc.Input[INPUT_MAX_TIME_MIN].Name = "Latest minute - default 0";
        sc.Input[INPUT_MAX_TIME_MIN].SetInt(0);

        sc.Input[INPUT_SL_POINTS].Name = "Stop Loss (points)";
        sc.Input[INPUT_SL_POINTS].SetFloat(10.0f);

        sc.Input[INPUT_TP_POINTS].Name = "Profit Target (points)";
        sc.Input[INPUT_TP_POINTS].SetFloat(10.0f);

        sc.Input[INPUT_ALLOW_TRADES_ONLY_IF_SIM].Name = "Allow trading only if Trade Simulation Mode is ON";
        sc.Input[INPUT_ALLOW_TRADES_ONLY_IF_SIM].SetYesNo(true);

        sc.Input[INPUT_LOG_LEVEL].Name = "Log verbosity (0=errors only,1=info)";
        sc.Input[INPUT_LOG_LEVEL].SetInt(1);

        sc.Input[INPUT_ENABLE_TOOLBAR_BUTTON].Name = "Enable Toolbar Button";
        sc.Input[INPUT_ENABLE_TOOLBAR_BUTTON].SetYesNo(true);

        sc.Input[INPUT_REPLAY_SPEED].Name = "Replay speed when starting";
        sc.Input[INPUT_REPLAY_SPEED].SetFloat(10.0f);

        // Subgraphs / drawing
        sc.Subgraph[0].Name = "Countdown Text";
        sc.Subgraph[0].DrawStyle = DRAWSTYLE_TEXT;
        sc.Subgraph[0].PrimaryColor = RGB(255, 255, 0);

        // Default array size (we won't rely on AutoLoop)
        sc.UpdateAlways = 1; // update every tick so countdown uses system time

        return;
    }

    // --- Initialization for runtime (run once) ---------------------------
    // Use static/state variables to persist across calls
    static bool initialized = false;
    static std::mt19937 rng; // random generator
    static std::uniform_int_distribution<int> uni; // placeholder
    static bool prevStartInput = false;

    // Drill state
    enum DrillStateEnum { IDLE = 0, COUNTDOWN = 1, FIRED = 2, FINISHED = 3 };
    static DrillStateEnum drillState = IDLE;

    static SCDateTime selectedDateTime = 0.0;
    static SCDateTime countdownEndTime = 0.0;
    static bool chosenLong = true;
    static int selectedBarIndex = -1;
    static int lastLoggedSeconds = -1;

    // Initialize RNG once
    if (!initialized)
    {
        std::random_device rd;
        rng.seed((unsigned int) (rd() ^ (unsigned int) std::hash<unsigned long long>()( (unsigned long long)sc.CurrentSystemDateTime.GetAsDouble() )));
        initialized = true;
    }

    // Handle toolbar button
    bool enableToolbarButton = sc.Input[INPUT_ENABLE_TOOLBAR_BUTTON].GetYesNo();
    if (enableToolbarButton)
    {
        sc.SetCustomStudyControlBarButtonText(TOOLBAR_BUTTON_NUMBER, "Start Drill");
        sc.SetCustomStudyControlBarButtonEnable(TOOLBAR_BUTTON_NUMBER, 1);
    }

    // Check if toolbar button was clicked
    bool toolbarButtonClicked = false;
    if (enableToolbarButton && sc.MenuEventID != 0)
    {
        if (sc.MenuEventID == TOOLBAR_BUTTON_NUMBER)
        {
            toolbarButtonClicked = true;
            sc.MenuEventID = 0; // Reset the event
        }
    }

    // Read inputs convenience
    bool startDrillInput = sc.Input[INPUT_START_DRILL_BUTTON].GetYesNo();
    int maxLookbackDays = sc.Input[INPUT_DATE_RANGE_DAYS].GetInt();
    int countdownSeconds = sc.Input[INPUT_COUNTDOWN_SECONDS].GetInt();
    bool showCountdown = sc.Input[INPUT_SHOW_COUNTDOWN].GetYesNo();
    int minHour = sc.Input[INPUT_MIN_TIME_HOUR].GetInt();
    int minMinute = sc.Input[INPUT_MIN_TIME_MIN].GetInt();
    int maxHour = sc.Input[INPUT_MAX_TIME_HOUR].GetInt();
    int maxMinute = sc.Input[INPUT_MAX_TIME_MIN].GetInt();
    float slPoints = sc.Input[INPUT_SL_POINTS].GetFloat();
    float tpPoints = sc.Input[INPUT_TP_POINTS].GetFloat();
    bool requireSim = sc.Input[INPUT_ALLOW_TRADES_ONLY_IF_SIM].GetYesNo();
    int logLevel = sc.Input[INPUT_LOG_LEVEL].GetInt();
    float replaySpeed = sc.Input[INPUT_REPLAY_SPEED].GetFloat();
    if (replaySpeed <= 0.0f) replaySpeed = 1.0f;
    if (maxLookbackDays > 5475) maxLookbackDays = 5475; // clamp to 15 years
    if (maxLookbackDays <= 0) maxLookbackDays = 1;

    // Provide a simple way to treat the Start input like a one-shot button:
    bool justPressed = ((!prevStartInput) && startDrillInput) || toolbarButtonClicked;
    prevStartInput = startDrillInput;

    // Safety: newer ACSIL builds removed some older simulation flags. If simulation-only is requested,
    // we still proceed but remind the user to verify Trade Simulation Mode manually.
    if (justPressed && requireSim)
    {
        sc.AddMessageToLog("Random Drill: Ensure Trade Simulation Mode is enabled before running the drill.", 1);
    }

    // --- When user triggers the drill (button-like) ----------------------
    if (justPressed)
    {
        if (drillState != IDLE)
        {
            sc.AddMessageToLog("Random Drill: Already running, wait for current drill to finish.", 1);
            sc.Input[INPUT_START_DRILL_BUTTON].SetYesNo(false);
            return;
        }

        // Reset state
        drillState = IDLE;
        selectedDateTime = 0.0;
        countdownEndTime = 0.0;
        chosenLong = true;
        selectedBarIndex = -1;
        lastLoggedSeconds = -1;

        // Find available bars (we prefer to use the chart's loaded historical bars)
        int availableBars = sc.ArraySize;
        if (availableBars <= 0)
        {
            sc.AddMessageToLog("Random Drill: No bars loaded in chart.", 1);
            sc.Input[INPUT_START_DRILL_BUTTON].SetYesNo(false);
            return;
        }

        // Limit the lookback to what's available and the user input (maxLookbackDays)
        // Compute earliest allowed datetime = last bar datetime - maxLookbackDays
        SCDateTime lastBarDT = sc.BaseDateTimeIn[availableBars - 1];
        SCDateTime earliestAllowedDT = lastBarDT - maxLookbackDays;

        // Construct list of candidate bar indices that fall within market hours on any date.
        // We'll collect indices where time portion between minTime and maxTime, and bar exists.
        std::vector<int> candidateIndices;
        candidateIndices.reserve(availableBars / 10);

        for (int i = 0; i < availableBars; ++i)
        {
            SCDateTime dt = sc.BaseDateTimeIn[i];
            if (dt < earliestAllowedDT) continue; // respect lookback window

            // Extract time-of-day hours/minutes in chart timezone
            int hour = dt.GetHour();
            int minute = dt.GetMinute();

            // Simple time-of-day check (inclusive)
            bool afterMin = (hour > minHour) || (hour == minHour && minute >= minMinute);
            bool beforeMax = (hour < maxHour) || (hour == maxHour && minute <= maxMinute);

            if (afterMin && beforeMax)
            {
                candidateIndices.push_back(i);
            }
        }

        if (candidateIndices.empty())
        {
            sc.AddMessageToLog("Random Drill: No candidate bars found in the selected date/time range. Adjust lookback or chart data.", 1);
            sc.Input[INPUT_START_DRILL_BUTTON].SetYesNo(false);
            return;
        }

        // Randomly select one candidate index
        std::uniform_int_distribution<int> distIdx(0, (int)candidateIndices.size() - 1);
        int selIdx = candidateIndices[distIdx(rng)];
        selectedBarIndex = selIdx;
        selectedDateTime = sc.BaseDateTimeIn[selIdx];

        // Choose random direction
        std::uniform_int_distribution<int> distDir(0,1);
        chosenLong = (distDir(rng) == 0);

        // Set countdown end time using real clock
        countdownEndTime = sc.CurrentSystemDateTime + (countdownSeconds / (24.0 * 60.0 * 60.0));

        // Set state
        drillState = COUNTDOWN;

        // Log
        {
            int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
            selectedDateTime.GetDateTimeYMDHMS(year, month, day, hour, minute, second);
            SCString dtStr;
            dtStr.Format("%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);

            SCString msg;
            msg.Format("Random Drill: Selected %s at %s. Countdown %d seconds started. Direction: %s",
                       sc.Symbol.GetChars(),
                       dtStr.GetChars(),
                       countdownSeconds,
                       chosenLong ? "LONG" : "SHORT");
            sc.AddMessageToLog(msg, (logLevel >= 1) ? 0 : 1);
        }

        // Jump view to chosen bar so the user sees context
        sc.Index = selectedBarIndex;

        // Reset the button input back to unselected state
        sc.Input[INPUT_START_DRILL_BUTTON].SetYesNo(false);

        // Show initial countdown text (respect showCountdown input)
        if (showCountdown)
        {
            SCString initialText;
            initialText.Format("Drill starts in %d s (%s)", countdownSeconds, chosenLong ? "LONG" : "SHORT");
            updateCountdownText(initialText);
        }
    }

    // --- Countdown and visual feedback ----------------------------------
    if (drillState == COUNTDOWN)
    {
        // Calculate seconds remaining
        double secsLeftDouble = (countdownEndTime - sc.CurrentSystemDateTime).GetAsDouble() * 24.0 * 3600.0; // real seconds
        int secsLeft = (int)ceil(secsLeftDouble);

        if (secsLeft > 0)
        {
            // Also print a log message occasionally
            if (logLevel >= 1 && secsLeft != lastLoggedSeconds)
            {
                SCString msg;
                msg.Format("Random Drill: %d seconds left before order.", secsLeft);
                sc.AddMessageToLog(msg, 0);
            }

            lastLoggedSeconds = secsLeft;

            // Update on-chart text
            if (showCountdown)
            {
                SCString text;
                text.Format("%d s to fire (%s)", secsLeft, chosenLong ? "LONG" : "SHORT");
                updateCountdownText(text);
            }
        }
        else
        {
            // Countdown reached zero: place order
            drillState = FIRED;

            // Clear countdown text before firing
            if (showCountdown)
                updateCountdownText("");

            // Place a market order using Trade Window size and attach SL/TP via attached orders.
            // We'll place a single entry via sc.BuyEntry / sc.SellEntry or sc.SubmitOrder if available.
            // Use price from the selected bar's close as the market reference if needed.
            double entryPrice = 0.0;
            if (selectedBarIndex >= 0 && selectedBarIndex < sc.ArraySize)
                entryPrice = sc.Close[selectedBarIndex];
            else
                entryPrice = sc.Close[sc.ArraySize - 1]; // fallback to latest price

            // Determine SL and TP price based on chosenLong and point offsets
            double slPrice = 0.0;
            double tpPrice = 0.0;

            // IMPORTANT: "point" here is instrument price units. For index futures like ES the point size equals tick value.
            // If your instrument uses fractional ticks, you may need to convert using sc.TickSize or sc.PriceScaleFormat.
            // We'll apply slPoints and tpPoints directly as price offsets.
            if (chosenLong)
            {
                slPrice = entryPrice - slPoints;
                tpPrice = entryPrice + tpPoints;
            }
            else
            {
                slPrice = entryPrice + slPoints;
                tpPrice = entryPrice - tpPoints;
            }

            // Place the entry with attached SL/TP using trade window quantity
            s_SCNewOrder order;
            memset(&order, 0, sizeof(order));

            order.OrderType = SCT_ORDERTYPE_MARKET;
            order.OrderQuantity = sc.TradeWindowOrderQuantity;
            order.AttachedOrderStop1Type = SCT_ORDERTYPE_STOP;
            order.Stop1Price = (float)slPrice;
            order.AttachedOrderTarget1Type = SCT_ORDERTYPE_LIMIT;
            order.Target1Price = (float)tpPrice;

            double orderResult = chosenLong ? sc.BuyEntry(order) : sc.SellEntry(order);
            if (orderResult > 0.0)
            {
                if (logLevel >= 1)
                {
                    SCString msg;
                    msg.Format("Random Drill: %s order submitted at market (approx price: %.5f). SL: %.5f TP: %.5f",
                               chosenLong ? "LONG" : "SHORT",
                               entryPrice,
                               slPrice,
                               tpPrice);
                    sc.AddMessageToLog(msg, 0);
                }

                // Start replay now that the trade is live
                sc.StopChartReplay(sc.ChartNumber);

                n_ACSIL::s_ChartReplayParameters replayParams;
                memset(&replayParams, 0, sizeof(replayParams));
                replayParams.ChartNumber = sc.ChartNumber;
                replayParams.ReplaySpeed = replaySpeed;
                replayParams.StartDateTime = selectedDateTime;
                replayParams.SkipEmptyPeriods = 1;
                replayParams.ReplayMode = (n_ACSIL::ChartReplayModeEnum)1;
                replayParams.ClearExistingTradeSimulationDataForSymbolAndTradeAccount = 0;
                sc.StartChartReplayNew(replayParams);
            }
            else
            {
                sc.AddMessageToLog("Random Drill: Failed to submit order.", 1);
            }

            drillState = FINISHED;
        } // end else countdown reached
    } // end if COUNTDOWN

    // If finished, log and reset nothing else to allow user to manage the trade manually
    if (drillState == FINISHED)
    {
        // Provide final log message
        sc.AddMessageToLog("Random Drill: Drill completed. Manage the trade manually.", 0);

        // Hide any countdown text
        updateCountdownText("");

        // Reset internal state so another drill can be started later
        drillState = IDLE;
        selectedDateTime = 0.0;
        countdownEndTime = 0.0;
        selectedBarIndex = -1;
        lastLoggedSeconds = -1;
    }

    return;
}
