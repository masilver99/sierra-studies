#include "sierrachart.h"

SCDLLName("Decision Tree Trading Assistant V1.5")

enum MarketState {
    STATE_UNKNOWN = 0,
    TREND,
    TRADING_RANGE
};

enum TradeSignal {
    SIGNAL_NONE = 0,
    SIGNAL_LONG,
    SIGNAL_SHORT
};

SCSFExport scsf_DecisionTreeAssistant(SCStudyInterfaceRef sc)
{
    SCInputRef EnableSignals = sc.Input[0];
    SCInputRef ShowBarColoring = sc.Input[1];
    SCInputRef MinConfidence = sc.Input[2];
    SCInputRef MinutesAfterOpen = sc.Input[3];

    SCSubgraphRef BarColor = sc.Subgraph[0];
    SCSubgraphRef BuySignal = sc.Subgraph[1];
    SCSubgraphRef SellSignal = sc.Subgraph[2];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Decision Tree Assistant V1.5";
        sc.AutoLoop = 1;

        EnableSignals.Name = "Enable Signals";
        EnableSignals.SetYesNo(1);

        ShowBarColoring.Name = "Show Bar Coloring";
        ShowBarColoring.SetYesNo(1);

        MinConfidence.Name = "Minimum Confidence (0-1)";
        MinConfidence.SetFloat(0.6f);

        MinutesAfterOpen.Name = "Minutes After Open";
        MinutesAfterOpen.SetInt(15);

        BarColor.Name = "Bar Color";
        BarColor.DrawStyle = DRAWSTYLE_COLOR_BAR;

        BuySignal.Name = "Buy Signal";
        BuySignal.DrawStyle = DRAWSTYLE_ARROW_UP;
        BuySignal.PrimaryColor = RGB(0,255,0);
        BuySignal.LineWidth = 3;

        SellSignal.Name = "Sell Signal";
        SellSignal.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        SellSignal.PrimaryColor = RGB(255,0,0);
        SellSignal.LineWidth = 3;

        return;
    }

    int i = sc.Index;
    if (i < 5) return;

    // ===== TIME FILTER =====
    SCDateTime barTime = sc.BaseDateTimeIn[i];
    int secondsSinceOpen = sc.SecondsSinceStartTime(barTime);
    int minutesSinceOpen = secondsSinceOpen / 60;

    if (minutesSinceOpen < MinutesAfterOpen.GetInt())
        return;

    // ===== PRICE DATA =====
    float open = sc.Open[i];
    float close = sc.Close[i];
    float high = sc.High[i];
    float low = sc.Low[i];

    float range = high - low;
    if (range == 0) return;

    // ===== SIGNAL BAR =====
    float body = fabs(close - open);
    float upperTail = high - max(open, close);
    float lowerTail = min(open, close) - low;

    bool strongBullBar = (close > open) && (upperTail < body * 0.3f);
    bool strongBearBar = (close < open) && (lowerTail < body * 0.3f);

    // ===== TREND vs TR =====
    sc.SimpleMovAvg(sc.Close, sc.Subgraph[10], i, 10);
    float smaFast = sc.Subgraph[10][i];
    sc.SimpleMovAvg(sc.Close, sc.Subgraph[11], i, 30);
    float smaSlow = sc.Subgraph[11][i];

    MarketState state = STATE_UNKNOWN;

    if (fabs(smaFast - smaSlow) > sc.TickSize * 10)
        state = TREND;
    else
        state = TRADING_RANGE;

    // ===== TRUE SECOND ENTRY LOGIC =====

    // LONG (M2)
    bool firstPushDown =
        sc.Low[i - 3] < sc.Low[i - 4];

    bool secondPushDown =
        sc.Low[i - 1] <= sc.Low[i - 3];

    bool failedBreakdown =
        close > open;

    bool secondEntryLong =
        firstPushDown &&
        secondPushDown &&
        failedBreakdown &&
        strongBullBar;

    // SHORT (H2)
    bool firstPushUp =
        sc.High[i - 3] > sc.High[i - 4];

    bool secondPushUp =
        sc.High[i - 1] >= sc.High[i - 3];

    bool failedBreakout =
        close < open;

    bool secondEntryShort =
        firstPushUp &&
        secondPushUp &&
        failedBreakout &&
        strongBearBar;

    // ===== TRIGGERS =====
    bool longTrigger = close > sc.High[i - 1];
    bool shortTrigger = close < sc.Low[i - 1];

    // ===== CONFIDENCE =====
    float confidence = 0.0f;

    if (state == TREND) confidence += 0.3f;
    if (strongBullBar || strongBearBar) confidence += 0.3f;
    if (secondEntryLong || secondEntryShort) confidence += 0.4f;

    TradeSignal signal = SIGNAL_NONE;

    if (confidence >= MinConfidence.GetFloat())
    {
        if (secondEntryLong && longTrigger)
            signal = SIGNAL_LONG;

        if (secondEntryShort && shortTrigger)
            signal = SIGNAL_SHORT;
    }

    // ===== BAR COLORING =====
    if (ShowBarColoring.GetYesNo())
    {
        if (state == TREND)
            BarColor.DataColor[i] = RGB(180,255,180);
        else
            BarColor.DataColor[i] = RGB(200,200,200);

        if (confidence > 0.75f)
            BarColor.DataColor[i] = RGB(0,200,0);
    }

    // ===== SIGNALS =====
    if (EnableSignals.GetYesNo())
    {
        if (signal == SIGNAL_LONG)
            BuySignal[i] = low - sc.TickSize * 2;

        if (signal == SIGNAL_SHORT)
            SellSignal[i] = high + sc.TickSize * 2;
    }

    // ===== LABELS =====
    if (secondEntryLong)
    {
        s_UseTool Tool;
        Tool.ChartNumber = sc.ChartNumber;
        Tool.DrawingType = DRAWING_TEXT;
        Tool.LineNumber = sc.Index + 10000;
        Tool.BeginIndex = i;
        Tool.BeginValue = low - sc.TickSize * 4;
        Tool.Color = RGB(0, 255, 0);
        Tool.AddMethod = UTAM_ADD_OR_ADJUST;
        Tool.Text = "M2 Long";
        sc.UseTool(Tool);
    }

    if (secondEntryShort)
    {
        s_UseTool Tool;
        Tool.ChartNumber = sc.ChartNumber;
        Tool.DrawingType = DRAWING_TEXT;
        Tool.LineNumber = sc.Index + 20000;
        Tool.BeginIndex = i;
        Tool.BeginValue = high + sc.TickSize * 4;
        Tool.Color = RGB(255, 0, 0);
        Tool.AddMethod = UTAM_ADD_OR_ADJUST;
        Tool.Text = "H2 Short";
        sc.UseTool(Tool);
    }
}