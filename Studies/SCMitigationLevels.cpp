#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "sierrachart.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

SCDLLName("SC Mitigation Levels")

enum class MitigationType
{
    Bullish = 0,
    Bearish = 1
};

struct MitigationLine
{
    int StartBar = 0;
    int FvgBar = 0;
    float Price = 0.0f;
    MitigationType Type = MitigationType::Bullish;
    bool IsMitigated = false;
    int EndBar = 0;
    bool IsRisky = false;
};

struct SimpleBar
{
    float Open = 0.0f;
    float Close = 0.0f;
    float High = 0.0f;
    float Low = 0.0f;
    int BarNumber = 0;

    bool IsBullish() const { return Close > Open; }
    bool IsBearish() const { return Close < Open; }
    float BodyHigh() const { return std::max(Open, Close); }
    float BodyLow() const { return std::min(Open, Close); }
    float BodySize() const { return std::fabs(Close - Open); }
};

static inline int ClampInt(int value, int low, int high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static void DetectFvgAndCreateLines(
    SCStudyInterfaceRef sc,
    int currentBar,
    float minFvgCandleSize,
    float minFvgSize,
    int maxLookbackBars,
    bool useFvgForDetection,
    std::vector<MitigationLine>& mitigationLines)
{
    if (currentBar < 2)
        return;

    const SimpleBar candle1{sc.Open[currentBar - 2], sc.Close[currentBar - 2], sc.High[currentBar - 2], sc.Low[currentBar - 2], currentBar - 2};
    const SimpleBar candle2{sc.Open[currentBar - 1], sc.Close[currentBar - 1], sc.High[currentBar - 1], sc.Low[currentBar - 1], currentBar - 1};
    const SimpleBar candle3{sc.Open[currentBar], sc.Close[currentBar], sc.High[currentBar], sc.Low[currentBar], currentBar};

    if (!candle2.IsBullish() && !candle2.IsBearish())
        return;

    const float gapSize = candle2.IsBullish() ? (candle3.Low - candle1.High) : (candle1.Low - candle3.High);
    if (gapSize < minFvgSize || candle2.BodySize() < minFvgCandleSize)
        return;

    const float rangeLow = useFvgForDetection
        ? (candle2.IsBullish() ? candle1.High : candle3.High)
        : candle2.BodyLow();

    const float rangeHigh = useFvgForDetection
        ? (candle2.IsBullish() ? candle3.Low : candle1.Low)
        : candle2.BodyHigh();

    const int detectionBarsAgo = 3;

    if (candle2.IsBullish())
    {
        float maxSoFar = -FLT_MAX;
        int barsChecked = 0;

        for (int barsAgo = detectionBarsAgo; barsAgo <= currentBar && barsChecked < maxLookbackBars; ++barsAgo)
        {
            ++barsChecked;
            const int barIndex = currentBar - barsAgo;
            const SimpleBar bar{sc.Open[barIndex], sc.Close[barIndex], sc.High[barIndex], sc.Low[barIndex], barIndex};

            if (!bar.IsBearish())
                continue;

            if (bar.High > rangeHigh)
                break;

            if (bar.High >= rangeLow && bar.High <= rangeHigh && bar.High > maxSoFar)
            {
                maxSoFar = bar.High;

                MitigationLine line;
                line.StartBar = bar.BarNumber;
                line.FvgBar = candle3.BarNumber;
                line.Price = bar.High;
                line.Type = MitigationType::Bullish;
                line.IsMitigated = false;
                line.EndBar = bar.BarNumber;
                line.IsRisky = false;
                mitigationLines.push_back(line);
            }
        }
    }

    if (candle2.IsBearish())
    {
        float minSoFar = FLT_MAX;
        int barsChecked = 0;

        for (int barsAgo = detectionBarsAgo; barsAgo <= currentBar && barsChecked < maxLookbackBars; ++barsAgo)
        {
            ++barsChecked;
            const int barIndex = currentBar - barsAgo;
            const SimpleBar bar{sc.Open[barIndex], sc.Close[barIndex], sc.High[barIndex], sc.Low[barIndex], barIndex};

            if (bar.Low < rangeLow)
                break;

            if (!bar.IsBullish())
                continue;

            if (bar.Low >= rangeLow && bar.Low <= rangeHigh && bar.Low < minSoFar)
            {
                minSoFar = bar.Low;

                MitigationLine line;
                line.StartBar = bar.BarNumber;
                line.FvgBar = candle3.BarNumber;
                line.Price = bar.Low;
                line.Type = MitigationType::Bearish;
                line.IsMitigated = false;
                line.EndBar = bar.BarNumber;
                line.IsRisky = false;
                mitigationLines.push_back(line);
            }
        }
    }
}

static void EvaluateLineBreaks(
    SCStudyInterfaceRef sc,
    int currentBar,
    float riskinessFactor,
    std::vector<MitigationLine>& mitigationLines)
{
    for (MitigationLine& line : mitigationLines)
    {
        if (line.IsMitigated)
            continue;

        if (currentBar <= line.FvgBar)
            continue;

        const bool hit = (line.Type == MitigationType::Bullish)
            ? (sc.Low[currentBar] <= line.Price)
            : (sc.High[currentBar] >= line.Price);

        if (hit)
        {
            line.IsMitigated = true;
            line.EndBar = currentBar;
        }
        else if (riskinessFactor > 0.0f)
        {
            const int fvgBar = line.FvgBar - 1;
            const int acceptableRiskBars = fvgBar - line.StartBar;
            const int barsSinceDetection = currentBar - fvgBar;
            if (barsSinceDetection > static_cast<int>(acceptableRiskBars * riskinessFactor))
                line.IsRisky = true;
        }
    }
}

static void DeleteAllDrawings(SCStudyInterfaceRef sc, int lineBaseNumber, int drawnCount)
{
    for (int idx = 0; idx < drawnCount; ++idx)
    {
        const int lineNumber = lineBaseNumber + idx * 2;
        const int labelNumber = lineNumber + 1;
        sc.DeleteACSChartDrawing(sc.ChartNumber, lineNumber, 0);
        sc.DeleteACSChartDrawing(sc.ChartNumber, labelNumber, 0);
    }
}

SCSFExport scsf_SCMitigationLevels(SCStudyInterfaceRef sc)
{
    SCInputRef Input_MinFvgCandleSize = sc.Input[0];
    SCInputRef Input_MinFvgSize = sc.Input[1];
    SCInputRef Input_MaxLookbackBars = sc.Input[2];
    SCInputRef Input_UseFvgForDetection = sc.Input[3];
    SCInputRef Input_RiskinessFactor = sc.Input[4];
    SCInputRef Input_BullishLineColor = sc.Input[5];
    SCInputRef Input_BearishLineColor = sc.Input[6];
    SCInputRef Input_Opacity = sc.Input[7];
    SCInputRef Input_LineThickness = sc.Input[8];
    SCInputRef Input_RemoveWhenMitigated = sc.Input[9];
    SCInputRef Input_ShowBuySellLabels = sc.Input[10];
    SCInputRef Input_MitigatedLineColor = sc.Input[11];

    int& priorDrawnCount = sc.GetPersistentInt(1);
    int& lastProcessedClosedBar = sc.GetPersistentInt(2);

    if (sc.SetDefaults)
    {
        sc.GraphName = "SC Mitigation Levels";
        sc.StudyDescription = "Draws mitigation levels when fair value gaps appear. "
                              "Scans backward from each FVG to find prior candle highs/lows "
                              "within the gap range and marks them as buy/sell mitigation levels. "
                              "Lines extend until price mitigates them or the chart ends.";
        sc.AutoLoop = 0;
        sc.GraphRegion = 0;

        Input_MinFvgCandleSize.Name = "Minimum FVG candle size";
        Input_MinFvgCandleSize.SetFloat(10.0f);
        Input_MinFvgCandleSize.SetDescription("Minimum body size of the middle FVG candle to qualify.");

        Input_MinFvgSize.Name = "Minimum FVG size";
        Input_MinFvgSize.SetFloat(5.0f);
        Input_MinFvgSize.SetDescription("Minimum gap size between candle 1 and candle 3 to qualify as an FVG.");

        Input_MaxLookbackBars.Name = "Maximum lookback bars";
        Input_MaxLookbackBars.SetInt(30);
        Input_MaxLookbackBars.SetIntLimits(1, 2000);
        Input_MaxLookbackBars.SetDescription("Maximum number of bars to scan backward from FVG for mitigation anchors.");

        Input_UseFvgForDetection.Name = "Use FVG for detection range";
        Input_UseFvgForDetection.SetYesNo(0);
        Input_UseFvgForDetection.SetDescription("When Yes, uses the FVG high/low as the detection range. When No, uses the middle candle body.");

        Input_RiskinessFactor.Name = "Riskiness factor";
        Input_RiskinessFactor.SetFloat(0.0f);
        Input_RiskinessFactor.SetDescription("When > 0, marks levels as Risky if too many bars pass without mitigation. 0 disables.");

        Input_BullishLineColor.Name = "Bullish line color";
        Input_BullishLineColor.SetColor(RGB(50, 205, 50));

        Input_BearishLineColor.Name = "Bearish line color";
        Input_BearishLineColor.SetColor(RGB(255, 0, 0));

        Input_Opacity.Name = "Opacity (1-100)";
        Input_Opacity.SetInt(100);
        Input_Opacity.SetIntLimits(1, 100);

        Input_LineThickness.Name = "Line thickness";
        Input_LineThickness.SetInt(1);
        Input_LineThickness.SetIntLimits(1, 10);

        Input_RemoveWhenMitigated.Name = "Remove when mitigated";
        Input_RemoveWhenMitigated.SetYesNo(0);
        Input_RemoveWhenMitigated.SetDescription("When Yes, removes the drawing entirely once price mitigates the level.");

        Input_ShowBuySellLabels.Name = "Show buy/sell labels";
        Input_ShowBuySellLabels.SetYesNo(1);
        Input_ShowBuySellLabels.SetDescription("When Yes, draws Buy/Sell text labels at the end of each mitigation line.");

        Input_MitigatedLineColor.Name = "Mitigated line color";
        Input_MitigatedLineColor.SetColor(RGB(105, 105, 105));

        return;
    }

    const int lineBaseNumber = 800000;
    std::vector<MitigationLine>* pLines = static_cast<std::vector<MitigationLine>*>(sc.GetPersistentPointer(1));

    if (pLines == nullptr)
    {
        pLines = new std::vector<MitigationLine>();
        sc.SetPersistentPointer(1, pLines);
        lastProcessedClosedBar = 1;
    }

    if (sc.LastCallToFunction)
    {
        DeleteAllDrawings(sc, lineBaseNumber, priorDrawnCount);
        priorDrawnCount = 0;
        lastProcessedClosedBar = 1;

        if (pLines != nullptr)
        {
            delete pLines;
            sc.SetPersistentPointer(1, nullptr);
        }
        return;
    }

    const int lastBar = sc.ArraySize - 1;
    if (lastBar < 2)
        return;

    int lastClosedBar = lastBar;
    if (sc.GetBarHasClosedStatus(lastBar) != BHCS_BAR_HAS_CLOSED)
        --lastClosedBar;

    if (lastClosedBar < 2)
        return;

    const float minFvgCandleSize = Input_MinFvgCandleSize.GetFloat();
    const float minFvgSize = Input_MinFvgSize.GetFloat();
    const int maxLookbackBars = Input_MaxLookbackBars.GetInt();
    const bool useFvgForDetection = Input_UseFvgForDetection.GetYesNo() != 0;
    const float riskinessFactor = Input_RiskinessFactor.GetFloat();
    const COLORREF bullishColor = Input_BullishLineColor.GetColor();
    const COLORREF bearishColor = Input_BearishLineColor.GetColor();
    const COLORREF mitigatedColor = Input_MitigatedLineColor.GetColor();
    const int opacity = ClampInt(Input_Opacity.GetInt(), 1, 100);
    const int lineThickness = std::max(1, Input_LineThickness.GetInt());
    const bool removeWhenMitigated = Input_RemoveWhenMitigated.GetYesNo() != 0;
    const bool showBuySellLabels = Input_ShowBuySellLabels.GetYesNo() != 0;

    // Full recalculation: clear all state and rebuild from scratch.
    if (sc.IsFullRecalculation && sc.UpdateStartIndex == 0)
    {
        DeleteAllDrawings(sc, lineBaseNumber, priorDrawnCount);
        priorDrawnCount = 0;
        pLines->clear();
        lastProcessedClosedBar = 1;
    }

    // Incremental: only process newly closed bars since last call.
    int startBar = lastProcessedClosedBar + 1;
    if (startBar < 2)
        startBar = 2;

    if (startBar <= lastClosedBar)
    {
        for (int bar = startBar; bar <= lastClosedBar; ++bar)
        {
            DetectFvgAndCreateLines(sc, bar, minFvgCandleSize, minFvgSize, maxLookbackBars, useFvgForDetection, *pLines);
            EvaluateLineBreaks(sc, bar, riskinessFactor, *pLines);
        }

        lastProcessedClosedBar = lastClosedBar;
    }

    // Draw / update all lines.
    int drawnCount = 0;

    for (size_t idx = 0; idx < pLines->size(); ++idx)
    {
        const MitigationLine& line = (*pLines)[idx];
        const int lineNumber = lineBaseNumber + static_cast<int>(idx) * 2;
        const int labelNumber = lineNumber + 1;

        if (removeWhenMitigated && line.IsMitigated)
        {
            sc.DeleteACSChartDrawing(sc.ChartNumber, lineNumber, 0);
            sc.DeleteACSChartDrawing(sc.ChartNumber, labelNumber, 0);
            ++drawnCount;
            continue;
        }

        const int endBar = line.IsMitigated ? line.EndBar : lastClosedBar;
        if (line.StartBar < 0 || endBar < line.StartBar)
        {
            sc.DeleteACSChartDrawing(sc.ChartNumber, lineNumber, 0);
            sc.DeleteACSChartDrawing(sc.ChartNumber, labelNumber, 0);
            ++drawnCount;
            continue;
        }

        const COLORREF drawColor = line.IsMitigated
            ? mitigatedColor
            : (line.Type == MitigationType::Bullish ? bullishColor : bearishColor);

        s_UseTool lineTool;
        lineTool.Clear();
        lineTool.ChartNumber = sc.ChartNumber;
        lineTool.DrawingType = DRAWING_LINE;
        lineTool.AddMethod = UTAM_ADD_OR_ADJUST;
        lineTool.LineNumber = lineNumber;
        lineTool.BeginIndex = line.StartBar;
        lineTool.BeginValue = line.Price;
        lineTool.EndIndex = endBar;
        lineTool.EndValue = line.Price;
        lineTool.Color = drawColor;
        lineTool.LineWidth = lineThickness;
        lineTool.LineStyle = line.IsMitigated ? LINESTYLE_DASH : LINESTYLE_SOLID;
        lineTool.TransparencyLevel = 100 - opacity;
        lineTool.AddAsUserDrawnDrawing = 0;
        sc.UseTool(lineTool);

        if (showBuySellLabels)
        {
            const char* sideText = (line.Type == MitigationType::Bullish) ? "Buy" : "Sell";
            SCString labelText = line.IsRisky ? "Risky " : "";
            labelText += sideText;

            s_UseTool textTool;
            textTool.Clear();
            textTool.ChartNumber = sc.ChartNumber;
            textTool.DrawingType = DRAWING_TEXT;
            textTool.AddMethod = UTAM_ADD_OR_ADJUST;
            textTool.LineNumber = labelNumber;
            textTool.BeginIndex = endBar;
            textTool.BeginValue = line.Price + (line.Type == MitigationType::Bullish ? sc.TickSize * 2.0f : -sc.TickSize * 2.0f);
            textTool.Color = drawColor;
            textTool.Text = labelText;
            textTool.FontSize = 9;
            textTool.TransparencyLevel = 100 - opacity;
            textTool.AddAsUserDrawnDrawing = 0;
            sc.UseTool(textTool);
        }
        else
        {
            sc.DeleteACSChartDrawing(sc.ChartNumber, labelNumber, 0);
        }

        ++drawnCount;
    }

    // Clean up any stale drawings from previous calls that had more lines.
    if (priorDrawnCount > drawnCount)
    {
        for (int idx = drawnCount; idx < priorDrawnCount; ++idx)
        {
            const int lineNumber = lineBaseNumber + idx * 2;
            const int labelNumber = lineNumber + 1;
            sc.DeleteACSChartDrawing(sc.ChartNumber, lineNumber, 0);
            sc.DeleteACSChartDrawing(sc.ChartNumber, labelNumber, 0);
        }
    }

    priorDrawnCount = drawnCount;
}
