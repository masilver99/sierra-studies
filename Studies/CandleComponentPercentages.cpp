#include "sierrachart.h"
#include <algorithm>
#include <cmath>

SCDLLName("Candle Component Percentages")

namespace
{
    enum InputIds
    {
        INPUT_DISPLAY_MODE = 0,
        INPUT_TEXT_OFFSET_TICKS = 1,
        INPUT_TEXT_COLOR = 2,
        INPUT_CANDLE_SIZE_TEXT_COLOR = 3,
        INPUT_TEXT_SIZE = 4,
        INPUT_DECIMALS = 5
    };

    enum SubgraphIds
    {
        SG_WICK_PCT = 0,
        SG_TAIL_PCT = 1,
        SG_BODY_PCT = 2,
        SG_CANDLE_SIZE_PCT = 3
    };

    enum DisplayMode
    {
        DISPLAY_ON_CANDLES = 0,
        DISPLAY_HOVER_ONLY = 1
    };
}

SCSFExport scsf_CandleComponentPercentages(SCStudyInterfaceRef sc)
{
    SCSubgraphRef WickPct = sc.Subgraph[SG_WICK_PCT];
    SCSubgraphRef TailPct = sc.Subgraph[SG_TAIL_PCT];
    SCSubgraphRef BodyPct = sc.Subgraph[SG_BODY_PCT];
    SCSubgraphRef CandleSizePct = sc.Subgraph[SG_CANDLE_SIZE_PCT];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Candle Component Percentages";
        sc.StudyDescription = "Displays wick, tail, body, and full candle size percentages. Values can be shown above candles or via hover (Chart Values).";
        sc.AutoLoop = 1;
        sc.GraphRegion = 0;

        WickPct.Name = "Upper Wick %";
        WickPct.DrawStyle = DRAWSTYLE_IGNORE;
        WickPct.PrimaryColor = RGB(255, 215, 0);

        TailPct.Name = "Lower Tail %";
        TailPct.DrawStyle = DRAWSTYLE_IGNORE;
        TailPct.PrimaryColor = RGB(0, 191, 255);

        BodyPct.Name = "Body %";
        BodyPct.DrawStyle = DRAWSTYLE_IGNORE;
        BodyPct.PrimaryColor = RGB(0, 255, 0);

        CandleSizePct.Name = "Candle Size % (Range/Close)";
        CandleSizePct.DrawStyle = DRAWSTYLE_IGNORE;
        CandleSizePct.PrimaryColor = RGB(255, 105, 180);

        sc.Input[INPUT_DISPLAY_MODE].Name = "Display Mode";
        sc.Input[INPUT_DISPLAY_MODE].SetCustomInputStrings("On Candles;Hover Only");
        sc.Input[INPUT_DISPLAY_MODE].SetCustomInputIndex(DISPLAY_ON_CANDLES);

        sc.Input[INPUT_TEXT_OFFSET_TICKS].Name = "Text Vertical Offset (ticks)";
        sc.Input[INPUT_TEXT_OFFSET_TICKS].SetInt(2);

        sc.Input[INPUT_TEXT_COLOR].Name = "Text Color";
        sc.Input[INPUT_TEXT_COLOR].SetColor(RGB(255, 255, 255));

        sc.Input[INPUT_TEXT_COLOR].SetDescription("Color for wick/body/tail text.");

        sc.Input[INPUT_CANDLE_SIZE_TEXT_COLOR].Name = "Candle Size Text Color";
        sc.Input[INPUT_CANDLE_SIZE_TEXT_COLOR].SetColor(RGB(255, 165, 0));

        sc.Input[INPUT_TEXT_SIZE].Name = "Text Size";
        sc.Input[INPUT_TEXT_SIZE].SetInt(8);

        sc.Input[INPUT_DECIMALS].Name = "Percent Decimals";
        sc.Input[INPUT_DECIMALS].SetInt(1);

        return;
    }

    int& baseLineNumber = sc.GetPersistentInt(1);
    if (baseLineNumber == 0)
        baseLineNumber = 1000000;

    int& prevDisplayMode = sc.GetPersistentInt(2);
    const int displayMode = sc.Input[INPUT_DISPLAY_MODE].GetIndex();

    if (sc.LastCallToFunction)
    {
        if (prevDisplayMode == DISPLAY_ON_CANDLES)
        {
            for (int i = 0; i < sc.ArraySize; ++i)
            {
                sc.DeleteACSChartDrawing(sc.ChartNumber, baseLineNumber + (i * 4) + 0, 0);
                sc.DeleteACSChartDrawing(sc.ChartNumber, baseLineNumber + (i * 4) + 1, 0);
                sc.DeleteACSChartDrawing(sc.ChartNumber, baseLineNumber + (i * 4) + 2, 0);
                sc.DeleteACSChartDrawing(sc.ChartNumber, baseLineNumber + (i * 4) + 3, 0);
            }
        }
        return;
    }

    if (prevDisplayMode != displayMode)
    {
        if (prevDisplayMode == DISPLAY_ON_CANDLES)
        {
            for (int i = 0; i < sc.ArraySize; ++i)
            {
                sc.DeleteACSChartDrawing(sc.ChartNumber, baseLineNumber + (i * 4) + 0, 0);
                sc.DeleteACSChartDrawing(sc.ChartNumber, baseLineNumber + (i * 4) + 1, 0);
                sc.DeleteACSChartDrawing(sc.ChartNumber, baseLineNumber + (i * 4) + 2, 0);
                sc.DeleteACSChartDrawing(sc.ChartNumber, baseLineNumber + (i * 4) + 3, 0);
            }
        }
        prevDisplayMode = displayMode;
    }

    const float high = sc.High[sc.Index];
    const float low = sc.Low[sc.Index];
    const float open = sc.Open[sc.Index];
    const float close = sc.Close[sc.Index];

    const float totalRange = high - low;
    const float bodyRange = std::fabs(open - close);
    const float upperWickRange = high - ((open > close) ? open : close);
    const float lowerTailRange = ((open < close) ? open : close) - low;

    float upperWickPct = 0.0f;
    float lowerTailPct = 0.0f;
    float bodyPct = 0.0f;
    float candleSizePct = 0.0f;

    if (totalRange > 0.0f)
    {
        upperWickPct = (upperWickRange / totalRange) * 100.0f;
        lowerTailPct = (lowerTailRange / totalRange) * 100.0f;
        bodyPct = (bodyRange / totalRange) * 100.0f;
    }

    if (close != 0.0f)
    {
        candleSizePct = (totalRange / close) * 100.0f;
    }

    WickPct[sc.Index] = upperWickPct;
    TailPct[sc.Index] = lowerTailPct;
    BodyPct[sc.Index] = bodyPct;
    CandleSizePct[sc.Index] = candleSizePct;

    if (displayMode == DISPLAY_ON_CANDLES)
    {
        const int inputDecimals = sc.Input[INPUT_DECIMALS].GetInt();
        const int decimals = (inputDecimals < 0) ? 0 : inputDecimals;
        const int offsetTicks = sc.Input[INPUT_TEXT_OFFSET_TICKS].GetInt();
        const float tickSize = sc.TickSize;

        const float wickAnchor = high + tickSize * static_cast<float>(offsetTicks);
        const float candleSizeAnchor = wickAnchor + tickSize * 2.0f;
        const float bodyAnchor = (open + close) * 0.5f;
        const float tailAnchor = low - tickSize * static_cast<float>(offsetTicks);

        const int baseLine = baseLineNumber + (sc.Index * 4);
        const int textSize = sc.Input[INPUT_TEXT_SIZE].GetInt();
        const COLORREF mainColor = sc.Input[INPUT_TEXT_COLOR].GetColor();
        const COLORREF candleSizeColor = sc.Input[INPUT_CANDLE_SIZE_TEXT_COLOR].GetColor();

        SCString wickText;
        wickText.Format("%.*f%%", decimals, upperWickPct);

        s_UseTool wickTool;
        wickTool.Clear();
        wickTool.ChartNumber = sc.ChartNumber;
        wickTool.DrawingType = DRAWING_TEXT;
        wickTool.AddMethod = UTAM_ADD_OR_ADJUST;
        wickTool.LineNumber = baseLine + 0;
        wickTool.BeginIndex = sc.Index;
        wickTool.BeginValue = wickAnchor;
        wickTool.Color = mainColor;
        wickTool.Text = wickText;
        wickTool.FontSize = textSize;
        wickTool.AddAsUserDrawnDrawing = 0;
        wickTool.UseRelativeVerticalValues = 0;
        sc.UseTool(wickTool);

        SCString bodyText;
        bodyText.Format("%.*f%%", decimals, bodyPct);

        s_UseTool bodyTool;
        bodyTool.Clear();
        bodyTool.ChartNumber = sc.ChartNumber;
        bodyTool.DrawingType = DRAWING_TEXT;
        bodyTool.AddMethod = UTAM_ADD_OR_ADJUST;
        bodyTool.LineNumber = baseLine + 1;
        bodyTool.BeginIndex = sc.Index;
        bodyTool.BeginValue = bodyAnchor;
        bodyTool.Color = mainColor;
        bodyTool.Text = bodyText;
        bodyTool.FontSize = textSize;
        bodyTool.AddAsUserDrawnDrawing = 0;
        bodyTool.UseRelativeVerticalValues = 0;
        sc.UseTool(bodyTool);

        SCString tailText;
        tailText.Format("%.*f%%", decimals, lowerTailPct);

        s_UseTool tailTool;
        tailTool.Clear();
        tailTool.ChartNumber = sc.ChartNumber;
        tailTool.DrawingType = DRAWING_TEXT;
        tailTool.AddMethod = UTAM_ADD_OR_ADJUST;
        tailTool.LineNumber = baseLine + 2;
        tailTool.BeginIndex = sc.Index;
        tailTool.BeginValue = tailAnchor;
        tailTool.Color = mainColor;
        tailTool.Text = tailText;
        tailTool.FontSize = textSize;
        tailTool.AddAsUserDrawnDrawing = 0;
        tailTool.UseRelativeVerticalValues = 0;
        sc.UseTool(tailTool);

        SCString candleSizeText;
        candleSizeText.Format("%.*f%%", decimals, candleSizePct);

        s_UseTool candleSizeTool;
        candleSizeTool.Clear();
        candleSizeTool.ChartNumber = sc.ChartNumber;
        candleSizeTool.DrawingType = DRAWING_TEXT;
        candleSizeTool.AddMethod = UTAM_ADD_OR_ADJUST;
        candleSizeTool.LineNumber = baseLine + 3;
        candleSizeTool.BeginIndex = sc.Index;
        candleSizeTool.BeginValue = candleSizeAnchor;
        candleSizeTool.Color = candleSizeColor;
        candleSizeTool.Text = candleSizeText;
        candleSizeTool.FontSize = textSize;
        candleSizeTool.AddAsUserDrawnDrawing = 0;
        candleSizeTool.UseRelativeVerticalValues = 0;
        sc.UseTool(candleSizeTool);
    }
}
