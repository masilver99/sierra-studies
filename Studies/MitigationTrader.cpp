// MitigationTrader.cpp
// ACSIL Study: Automated futures trading based on MitigationLevels detection.
// Places fade limit orders at mitigation levels with configurable PT/SL,
// time filtering, signal grouping, break-even management, and risk controls.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "sierrachart.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

SCDLLName("Mitigation Trader")

// ─────────────────────────────────────────────────────────────────────────────
// Shared types (embedded from MitigationLevels.cpp)
// ─────────────────────────────────────────────────────────────────────────────

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

    // Trading extension fields
    uint32_t OrderID = 0;  // Sierra Chart InternalOrderID for the pending limit order
    int OrderBarIndex = 0; // Bar index when order was placed (for expiry tracking)
    bool OrderFilled = false;
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
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

// ─────────────────────────────────────────────────────────────────────────────
// FVG Detection (from MitigationLevels.cpp)
// ─────────────────────────────────────────────────────────────────────────────

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

    const SimpleBar candle1{sc.Open[currentBar - 2], sc.Close[currentBar - 2],
                            sc.High[currentBar - 2], sc.Low[currentBar - 2], currentBar - 2};
    const SimpleBar candle2{sc.Open[currentBar - 1], sc.Close[currentBar - 1],
                            sc.High[currentBar - 1], sc.Low[currentBar - 1], currentBar - 1};
    const SimpleBar candle3{sc.Open[currentBar], sc.Close[currentBar],
                            sc.High[currentBar], sc.Low[currentBar], currentBar};

    if (!candle2.IsBullish() && !candle2.IsBearish())
        return;

    const float gapSize = candle2.IsBullish()
        ? (candle3.Low - candle1.High)
        : (candle1.Low - candle3.High);
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
            const SimpleBar bar{sc.Open[barIndex], sc.Close[barIndex],
                                sc.High[barIndex], sc.Low[barIndex], barIndex};

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
            const SimpleBar bar{sc.Open[barIndex], sc.Close[barIndex],
                                sc.High[barIndex], sc.Low[barIndex], barIndex};

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
                mitigationLines.push_back(line);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Mitigation Evaluation (from MitigationLevels.cpp)
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// Trading helpers
// ─────────────────────────────────────────────────────────────────────────────

enum PtSlMode
{
    PTSL_POINTS = 0,
    PTSL_ATR_MULT = 1
};

enum SignalFilterMode
{
    SIGNAL_ALL = 0,
    SIGNAL_FIRST = 1,
    SIGNAL_MIDDLE = 2,
    SIGNAL_LAST = 3
};

static float CalculateOffset(PtSlMode mode, float value, float atrValue)
{
    if (mode == PTSL_ATR_MULT)
        return atrValue * value;
    return value; // PTSL_POINTS
}

// Check if a bar time falls within the NY session window.
// startTime/endTime are seconds since midnight from sc.Input[].GetTime() / HMS_TIME().
static bool IsWithinSession(SCStudyInterfaceRef sc, int barIndex, int startTimeSecs, int endTimeSecs)
{
    int barTimeSecs = sc.BaseDateTimeIn[barIndex].GetTimeInSecondsWithoutMilliseconds();
    return barTimeSecs >= startTimeSecs && barTimeSecs < endTimeSecs;
}

// Count active orders (pending or filled) across all tracked lines.
static int CountActiveOrders(const std::vector<MitigationLine>& lines, SCStudyInterfaceRef sc)
{
    int count = 0;
    for (const MitigationLine& line : lines)
    {
        if (line.OrderID == 0)
            continue;

        if (line.OrderFilled)
        {
            ++count; // Filled position still active (managed by attached orders)
            continue;
        }

        // Check if the pending order is still open
        s_SCTradeOrder tradeOrder;
        if (sc.GetOrderByOrderID(line.OrderID, tradeOrder) != SCTRADING_ORDER_ERROR)
        {
            if (tradeOrder.OrderStatusCode == SCT_OSC_OPEN)
                ++count;
        }
    }
    return count;
}

// Apply signal grouping and filtering to a set of newly detected lines.
// Returns indices (into newLines) of the lines that should be traded.
static std::vector<size_t> FilterSignalsByGroup(
    const std::vector<MitigationLine>& newLines,
    const std::vector<size_t>& indices,
    float groupingDistance,
    SignalFilterMode filterMode)
{
    if (indices.empty())
        return {};

    if (filterMode == SIGNAL_ALL)
        return indices;

    // Sort indices by price
    std::vector<size_t> sorted = indices;
    std::sort(sorted.begin(), sorted.end(), [&](size_t a, size_t b) {
        return newLines[a].Price < newLines[b].Price;
    });

    // Group consecutive lines within groupingDistance
    std::vector<std::vector<size_t>> groups;
    groups.push_back({sorted[0]});

    for (size_t i = 1; i < sorted.size(); ++i)
    {
        float prevPrice = newLines[sorted[i - 1]].Price;
        float currPrice = newLines[sorted[i]].Price;
        if (currPrice - prevPrice <= groupingDistance)
            groups.back().push_back(sorted[i]);
        else
            groups.push_back({sorted[i]});
    }

    // Select from each group based on filter mode
    std::vector<size_t> result;
    for (const auto& group : groups)
    {
        if (group.empty())
            continue;

        if (filterMode == SIGNAL_FIRST)
        {
            result.push_back(group.front());
        }
        else if (filterMode == SIGNAL_LAST)
        {
            result.push_back(group.back());
        }
        else if (filterMode == SIGNAL_MIDDLE)
        {
            float minP = newLines[group.front()].Price;
            float maxP = newLines[group.back()].Price;
            float midPrice = (minP + maxP) * 0.5f;
            size_t bestIdx = group[0];
            float bestDist = std::fabs(newLines[bestIdx].Price - midPrice);
            for (size_t j = 1; j < group.size(); ++j)
            {
                float dist = std::fabs(newLines[group[j]].Price - midPrice);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestIdx = group[j];
                }
            }
            result.push_back(bestIdx);
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main study function
// ─────────────────────────────────────────────────────────────────────────────

SCSFExport scsf_MitigationTrader(SCStudyInterfaceRef sc)
{
    // ── Subgraphs ────────────────────────────────────────────────────────
    SCSubgraphRef Subgraph_ATR = sc.Subgraph[0];

    // ── Inputs: FVG Detection (0-4) ─────────────────────────────────────
    SCInputRef Input_MinFvgCandleSize   = sc.Input[0];
    SCInputRef Input_MinFvgSize         = sc.Input[1];
    SCInputRef Input_MaxLookbackBars    = sc.Input[2];
    SCInputRef Input_UseFvgForDetection = sc.Input[3];
    SCInputRef Input_RiskinessFactor    = sc.Input[4];

    // ── Inputs: PT/SL (5-8) ─────────────────────────────────────────────
    SCInputRef Input_PtMode  = sc.Input[5];
    SCInputRef Input_PtValue = sc.Input[6];
    SCInputRef Input_SlMode  = sc.Input[7];
    SCInputRef Input_SlValue = sc.Input[8];

    // ── Inputs: Session Filter (9-11) ───────────────────────────────────
    SCInputRef Input_TradeNYOnly    = sc.Input[9];
    SCInputRef Input_NYSessionStart = sc.Input[10];
    SCInputRef Input_NYSessionEnd   = sc.Input[11];

    // ── Inputs: Signal Filtering (12-13) ────────────────────────────────
    SCInputRef Input_SignalFilter   = sc.Input[12];
    SCInputRef Input_GroupDistance  = sc.Input[13];

    // ── Inputs: Order Management (14-18) ────────────────────────────────
    SCInputRef Input_ContractCount    = sc.Input[14];
    SCInputRef Input_BreakEvenPoints  = sc.Input[15];
    SCInputRef Input_MaxBarsToFill   = sc.Input[16];
    SCInputRef Input_SkipRisky       = sc.Input[17];
    SCInputRef Input_MaxOpenOrders   = sc.Input[18];

    // ── Persistent state ─────────────────────────────────────────────────
    int& lastProcessedClosedBar = sc.GetPersistentInt(1);
    int& lastSessionDate        = sc.GetPersistentInt(2);

    // ── SetDefaults ──────────────────────────────────────────────────────
    if (sc.SetDefaults)
    {
        sc.GraphName = "Mitigation Trader";
        sc.StudyDescription =
            "Automated futures trading based on FVG mitigation levels. "
            "Places fade limit buy orders at bullish levels and fade limit "
            "sell orders at bearish levels with configurable PT/SL, session "
            "filtering, signal grouping, break-even, and risk controls.";
        sc.AutoLoop = 0;
        sc.GraphRegion = 0;

        // Trading configuration
        sc.MaintainTradeStatisticsAndTradesData = true;
        sc.SupportAttachedOrdersForTrading = true;
        sc.AllowMultipleEntriesInSameDirection = true;
        sc.AllowOppositeEntryWithOpposingPositionOrOrders = true;
        sc.SendOrdersToTradeService = false;
        sc.CancelAllOrdersOnEntriesAndReversals = false;
        sc.AllowEntryWithWorkingOrders = true;

        // ATR subgraph
        Subgraph_ATR.Name = "ATR(5)";
        Subgraph_ATR.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_ATR.DrawZeros = false;

        // FVG Detection inputs (0-4)
        Input_MinFvgCandleSize.Name = "Min FVG Candle Size";
        Input_MinFvgCandleSize.SetFloat(10.0f);

        Input_MinFvgSize.Name = "Min FVG Size";
        Input_MinFvgSize.SetFloat(5.0f);

        Input_MaxLookbackBars.Name = "Max Lookback Bars";
        Input_MaxLookbackBars.SetInt(30);
        Input_MaxLookbackBars.SetIntLimits(1, 2000);

        Input_UseFvgForDetection.Name = "Use FVG for Detection Range";
        Input_UseFvgForDetection.SetYesNo(0);

        Input_RiskinessFactor.Name = "Riskiness Factor (0=disabled)";
        Input_RiskinessFactor.SetFloat(0.0f);

        // PT/SL inputs (5-8)
        Input_PtMode.Name = "PT Mode (0=Points, 1=ATR x N)";
        Input_PtMode.SetCustomInputStrings("Points;ATR x N");
        Input_PtMode.SetCustomInputIndex(0);

        Input_PtValue.Name = "PT Value";
        Input_PtValue.SetFloat(10.0f);

        Input_SlMode.Name = "SL Mode (0=Points, 1=ATR x N)";
        Input_SlMode.SetCustomInputStrings("Points;ATR x N");
        Input_SlMode.SetCustomInputIndex(0);

        Input_SlValue.Name = "SL Value";
        Input_SlValue.SetFloat(10.0f);

        // Session filter inputs (9-11)
        Input_TradeNYOnly.Name = "Trade NY Hours Only";
        Input_TradeNYOnly.SetYesNo(1);

        Input_NYSessionStart.Name = "NY Session Start";
        Input_NYSessionStart.SetTime(HMS_TIME(9, 30, 0));

        Input_NYSessionEnd.Name = "NY Session End";
        Input_NYSessionEnd.SetTime(HMS_TIME(16, 0, 0));

        // Signal filter inputs (12-13)
        Input_SignalFilter.Name = "Signal Filter";
        Input_SignalFilter.SetCustomInputStrings("All;First;Middle;Last");
        Input_SignalFilter.SetCustomInputIndex(0);

        Input_GroupDistance.Name = "Signal Grouping Distance (points)";
        Input_GroupDistance.SetFloat(5.0f);

        // Order management inputs (14-18)
        Input_ContractCount.Name = "Contract Count";
        Input_ContractCount.SetInt(1);
        Input_ContractCount.SetIntLimits(1, 100);

        Input_BreakEvenPoints.Name = "Move SL to BE After Points (0=off)";
        Input_BreakEvenPoints.SetFloat(0.0f);

        Input_MaxBarsToFill.Name = "Max Bars to Fill (0=no expiry)";
        Input_MaxBarsToFill.SetInt(0);
        Input_MaxBarsToFill.SetIntLimits(0, 10000);

        Input_SkipRisky.Name = "Skip Risky Signals";
        Input_SkipRisky.SetYesNo(0);

        Input_MaxOpenOrders.Name = "Max Open Orders (0=unlimited)";
        Input_MaxOpenOrders.SetInt(0);
        Input_MaxOpenOrders.SetIntLimits(0, 100);

        return;
    }

    // ── Persistent pointer for mitigation lines ──────────────────────────
    std::vector<MitigationLine>* pLines =
        static_cast<std::vector<MitigationLine>*>(sc.GetPersistentPointer(1));

    if (pLines == nullptr)
    {
        pLines = new std::vector<MitigationLine>();
        sc.SetPersistentPointer(1, pLines);
        lastProcessedClosedBar = 1;
    }

    // ── Cleanup on last call ─────────────────────────────────────────────
    if (sc.LastCallToFunction)
    {
        lastProcessedClosedBar = 1;
        if (pLines != nullptr)
        {
            delete pLines;
            sc.SetPersistentPointer(1, nullptr);
        }
        return;
    }

    // ── Basic bar validation ─────────────────────────────────────────────
    const int lastBar = sc.ArraySize - 1;
    if (lastBar < 2)
        return;

    int lastClosedBar = lastBar;
    if (sc.GetBarHasClosedStatus(lastBar) != BHCS_BAR_HAS_CLOSED)
        --lastClosedBar;
    if (lastClosedBar < 2)
        return;

    // ── Read inputs ──────────────────────────────────────────────────────
    const float minFvgCandleSize = Input_MinFvgCandleSize.GetFloat();
    const float minFvgSize       = Input_MinFvgSize.GetFloat();
    const int   maxLookbackBars  = Input_MaxLookbackBars.GetInt();
    const bool  useFvgForDetect  = Input_UseFvgForDetection.GetYesNo() != 0;
    const float riskinessFactor  = Input_RiskinessFactor.GetFloat();

    const PtSlMode ptMode     = static_cast<PtSlMode>(Input_PtMode.GetIndex());
    const float    ptValue    = Input_PtValue.GetFloat();
    const PtSlMode slMode     = static_cast<PtSlMode>(Input_SlMode.GetIndex());
    const float    slValue    = Input_SlValue.GetFloat();

    const bool tradeNYOnly     = Input_TradeNYOnly.GetYesNo() != 0;
    const int  nySessionStart  = Input_NYSessionStart.GetTime();
    const int  nySessionEnd    = Input_NYSessionEnd.GetTime();

    const SignalFilterMode signalFilter =
        static_cast<SignalFilterMode>(Input_SignalFilter.GetIndex());
    const float groupDistance = Input_GroupDistance.GetFloat();

    const int   contractCount    = Input_ContractCount.GetInt();
    const float breakEvenPoints  = Input_BreakEvenPoints.GetFloat();
    const int   maxBarsToFill    = Input_MaxBarsToFill.GetInt();
    const bool  skipRisky        = Input_SkipRisky.GetYesNo() != 0;
    const int   maxOpenOrders    = Input_MaxOpenOrders.GetInt();

    // ── Full recalculation: reset state ──────────────────────────────────
    if (sc.IsFullRecalculation && sc.UpdateStartIndex == 0)
    {
        // Cancel any pending orders from this study
        for (MitigationLine& line : *pLines)
        {
            if (line.OrderID != 0 && !line.OrderFilled)
                sc.CancelOrder(line.OrderID);
        }
        pLines->clear();
        lastProcessedClosedBar = 1;
    }

    // ── Compute ATR(5) across all bars ───────────────────────────────────
    sc.DataStartIndex = 5;
    sc.ATR(sc.BaseDataIn, Subgraph_ATR, 5, MOVAVGTYPE_SIMPLE);

    // ── Incremental detection loop ───────────────────────────────────────
    int startBar = lastProcessedClosedBar + 1;
    if (startBar < 2)
        startBar = 2;

    if (startBar <= lastClosedBar)
    {
        for (int bar = startBar; bar <= lastClosedBar; ++bar)
        {
            // ── Daily reset: clear previous day signals at session start ─
            int barDate = sc.BaseDateTimeIn[bar].GetDate();
            int barTimeSecs = sc.BaseDateTimeIn[bar].GetTimeInSecondsWithoutMilliseconds();
            if (barDate != lastSessionDate && barTimeSecs >= nySessionStart)
            {
                lastSessionDate = barDate;
                for (MitigationLine& line : *pLines)
                {
                    if (line.OrderID != 0 && !line.OrderFilled)
                        sc.CancelOrder(line.OrderID);
                }
                pLines->clear();
            }
            const size_t prevSize = pLines->size();

            // Detect new mitigation levels
            DetectFvgAndCreateLines(sc, bar, minFvgCandleSize, minFvgSize,
                                    maxLookbackBars, useFvgForDetect, *pLines);

            // Evaluate existing levels for mitigation (price hitting them)
            EvaluateLineBreaks(sc, bar, riskinessFactor, *pLines);

            // ── Cancel orders on mitigated levels ────────────────────────
            for (size_t i = 0; i < pLines->size(); ++i)
            {
                MitigationLine& line = (*pLines)[i];
                if (line.IsMitigated && line.OrderID != 0 && !line.OrderFilled)
                {
                    sc.CancelOrder(line.OrderID);
                    line.OrderID = 0;
                }
            }

            // ── Cancel expired orders (max bars to fill) ─────────────────
            if (maxBarsToFill > 0)
            {
                for (MitigationLine& line : *pLines)
                {
                    if (line.OrderID == 0 || line.OrderFilled)
                        continue;
                    if (bar - line.OrderBarIndex > maxBarsToFill)
                    {
                        sc.CancelOrder(line.OrderID);
                        line.OrderID = 0;
                    }
                }
            }

            // ── Update fill status for tracked orders ────────────────────
            for (MitigationLine& line : *pLines)
            {
                if (line.OrderID == 0 || line.OrderFilled)
                    continue;

                s_SCTradeOrder tradeOrder;
                if (sc.GetOrderByOrderID(line.OrderID, tradeOrder) != SCTRADING_ORDER_ERROR)
                {
                    if (tradeOrder.OrderStatusCode == SCT_OSC_FILLED)
                        line.OrderFilled = true;
                    else if (tradeOrder.OrderStatusCode != SCT_OSC_OPEN)
                        line.OrderID = 0; // Order was cancelled or rejected
                }
            }

            // ── Process new signals ──────────────────────────────────────
            const size_t newSize = pLines->size();
            if (newSize > prevSize)
            {
                // Time filter check: use the current bar's time
                if (tradeNYOnly && !IsWithinSession(sc, bar, nySessionStart, nySessionEnd))
                    continue; // Skip all new signals on this bar

                // Collect qualifying new signal indices
                std::vector<size_t> candidates;
                for (size_t i = prevSize; i < newSize; ++i)
                {
                    const MitigationLine& line = (*pLines)[i];

                    if (line.IsMitigated)
                        continue;
                    if (skipRisky && line.IsRisky)
                        continue;

                    candidates.push_back(i);
                }

                if (candidates.empty())
                    continue;

                // Separate candidates by type for independent grouping
                std::vector<size_t> bullishCandidates, bearishCandidates;
                for (size_t idx : candidates)
                {
                    if ((*pLines)[idx].Type == MitigationType::Bullish)
                        bullishCandidates.push_back(idx);
                    else
                        bearishCandidates.push_back(idx);
                }

                // Apply signal grouping/filtering per type
                std::vector<size_t> toTrade;
                {
                    auto filtered = FilterSignalsByGroup(*pLines, bullishCandidates,
                                                         groupDistance, signalFilter);
                    toTrade.insert(toTrade.end(), filtered.begin(), filtered.end());
                }
                {
                    auto filtered = FilterSignalsByGroup(*pLines, bearishCandidates,
                                                         groupDistance, signalFilter);
                    toTrade.insert(toTrade.end(), filtered.begin(), filtered.end());
                }

                // ── Place orders for selected signals ────────────────────
                const float atrValue = Subgraph_ATR[bar];

                for (size_t idx : toTrade)
                {
                    MitigationLine& line = (*pLines)[idx];

                    // Max open orders check
                    if (maxOpenOrders > 0 && CountActiveOrders(*pLines, sc) >= maxOpenOrders)
                        break;

                    const float ptOffset = CalculateOffset(ptMode, ptValue, atrValue);
                    const float slOffset = CalculateOffset(slMode, slValue, atrValue);

                    s_SCNewOrder order;

                    order.OrderType = SCT_ORDERTYPE_LIMIT;
                    order.Price1 = line.Price;
                    order.OrderQuantity = contractCount;

                    // Attached profit target
                    order.AttachedOrderTarget1Type = SCT_ORDERTYPE_LIMIT;

                    // Attached stop loss
                    order.AttachedOrderStop1Type = SCT_ORDERTYPE_STOP;

                    if (line.Type == MitigationType::Bullish)
                    {
                        order.Target1Price = line.Price + ptOffset;
                        order.Stop1Price   = line.Price - slOffset;
                    }
                    else
                    {
                        order.Target1Price = line.Price - ptOffset;
                        order.Stop1Price   = line.Price + slOffset;
                    }

                    // Break-even management via attached order
                    if (breakEvenPoints > 0.0f)
                    {
                        order.MoveToBreakEven.Type =
                            MOVETO_BE_ACTION_TYPE_OFFSET_TRIGGERED;
                        order.MoveToBreakEven.TriggerOffsetInTicks =
                            static_cast<int>(breakEvenPoints / sc.TickSize);
                    }

                    int result = 0;
                    if (line.Type == MitigationType::Bullish)
                        result = static_cast<int>(sc.BuyEntry(order));
                    else
                        result = static_cast<int>(sc.SellEntry(order));

                    if (result > 0)
                    {
                        line.OrderID = order.InternalOrderID;
                        line.OrderBarIndex = bar;
                    }
                }
            }
        }

        lastProcessedClosedBar = lastClosedBar;
    }
}
