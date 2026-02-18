#include "sierrachart.h"

SCDLLName("Trapped Trader Signals")

namespace
{
    enum InputIds
    {
        INPUT_LOOKBACK_BARS = 0,
        INPUT_MIN_BREAKOUT_TICKS = 1,
        INPUT_REQUIRE_REVERSAL_CLOSE = 2,
        INPUT_MARKER_OFFSET_TICKS = 3,
        INPUT_ALERT_NUMBER = 4,
        INPUT_ENABLE_FOOTPRINT_FILTER = 5,
        INPUT_FOOTPRINT_ZONE_PERCENT = 6,
        INPUT_FOOTPRINT_IMBALANCE_RATIO = 7,
        INPUT_FOOTPRINT_MIN_COMBINED_VOLUME = 8
    };

    enum SubgraphIds
    {
        SG_BUY_MARK = 0,
        SG_SELL_MARK = 1,
        SG_BEAR_TRAP_HIGHLIGHT = 2,
        SG_BULL_TRAP_HIGHLIGHT = 3
    };

    bool ComputeZoneBidAskSums(
        SCStudyInterfaceRef sc,
        const int barIndex,
        const bool topZone,
        const int zonePercent,
        unsigned int& bidSum,
        unsigned int& askSum)
    {
        bidSum = 0;
        askSum = 0;

        if (barIndex < 0 || sc.VolumeAtPriceForBars == nullptr)
            return false;

        const unsigned int levelCount = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(static_cast<unsigned int>(barIndex));
        if (levelCount == 0)
            return false;

        int safePercent = zonePercent;
        if (safePercent < 1)
            safePercent = 1;
        if (safePercent > 100)
            safePercent = 100;

        unsigned int zoneLevelCount = (levelCount * static_cast<unsigned int>(safePercent) + 99U) / 100U;
        if (zoneLevelCount < 1)
            zoneLevelCount = 1;
        if (zoneLevelCount > levelCount)
            zoneLevelCount = levelCount;

        unsigned int startIndex = topZone ? (levelCount - zoneLevelCount) : 0;
        unsigned int endIndexExclusive = topZone ? levelCount : zoneLevelCount;

        for (unsigned int levelIndex = startIndex; levelIndex < endIndexExclusive; ++levelIndex)
        {
            const s_VolumeAtPriceV2* pVAP = nullptr;
            if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(static_cast<unsigned int>(barIndex), static_cast<int>(levelIndex), &pVAP))
                continue;

            if (pVAP == nullptr)
                continue;

            bidSum += pVAP->BidVolume;
            askSum += pVAP->AskVolume;
        }

        return (bidSum + askSum) > 0;
    }
}

SCSFExport scsf_TrappedTraderSignals(SCStudyInterfaceRef sc)
{
    SCSubgraphRef BuyMark = sc.Subgraph[SG_BUY_MARK];
    SCSubgraphRef SellMark = sc.Subgraph[SG_SELL_MARK];
    SCSubgraphRef BearTrapHighlight = sc.Subgraph[SG_BEAR_TRAP_HIGHLIGHT];
    SCSubgraphRef BullTrapHighlight = sc.Subgraph[SG_BULL_TRAP_HIGHLIGHT];

    SCInputRef LookbackBars = sc.Input[INPUT_LOOKBACK_BARS];
    SCInputRef MinBreakoutTicks = sc.Input[INPUT_MIN_BREAKOUT_TICKS];
    SCInputRef RequireReversalClose = sc.Input[INPUT_REQUIRE_REVERSAL_CLOSE];
    SCInputRef MarkerOffsetTicks = sc.Input[INPUT_MARKER_OFFSET_TICKS];
    SCInputRef AlertNumber = sc.Input[INPUT_ALERT_NUMBER];
    SCInputRef EnableFootprintFilter = sc.Input[INPUT_ENABLE_FOOTPRINT_FILTER];
    SCInputRef FootprintZonePercent = sc.Input[INPUT_FOOTPRINT_ZONE_PERCENT];
    SCInputRef FootprintImbalanceRatio = sc.Input[INPUT_FOOTPRINT_IMBALANCE_RATIO];
    SCInputRef FootprintMinCombinedVolume = sc.Input[INPUT_FOOTPRINT_MIN_COMBINED_VOLUME];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Trapped Trader Signals";
        sc.StudyDescription = "Highlights likely bull/bear traps and plots sell (bull trap) and buy (bear trap) markers.";
        sc.AutoLoop = 1;
        sc.GraphRegion = 0;
        sc.MaintainVolumeAtPriceData = 1;

        BuyMark.Name = "Buy Mark (Bear Trap)";
        BuyMark.DrawStyle = DRAWSTYLE_ARROW_UP;
        BuyMark.PrimaryColor = RGB(0, 200, 0);
        BuyMark.LineWidth = 2;
        BuyMark.DrawZeros = false;

        SellMark.Name = "Sell Mark (Bull Trap)";
        SellMark.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        SellMark.PrimaryColor = RGB(220, 0, 0);
        SellMark.LineWidth = 2;
        SellMark.DrawZeros = false;

        BearTrapHighlight.Name = "Bear Trap Highlight";
        BearTrapHighlight.DrawStyle = DRAWSTYLE_COLOR_BAR;
        BearTrapHighlight.PrimaryColor = RGB(0, 160, 0);
        BearTrapHighlight.DrawZeros = false;

        BullTrapHighlight.Name = "Bull Trap Highlight";
        BullTrapHighlight.DrawStyle = DRAWSTYLE_COLOR_BAR;
        BullTrapHighlight.PrimaryColor = RGB(200, 80, 0);
        BullTrapHighlight.DrawZeros = false;

        LookbackBars.Name = "Breakout Lookback Bars";
        LookbackBars.SetInt(20);
        LookbackBars.SetIntLimits(1, 500);

        MinBreakoutTicks.Name = "Minimum Breakout Distance (ticks)";
        MinBreakoutTicks.SetInt(1);
        MinBreakoutTicks.SetIntLimits(0, 50);

        RequireReversalClose.Name = "Require Reversal Candle Close";
        RequireReversalClose.SetYesNo(1);

        MarkerOffsetTicks.Name = "Marker Offset (ticks)";
        MarkerOffsetTicks.SetInt(2);
        MarkerOffsetTicks.SetIntLimits(0, 100);

        AlertNumber.Name = "Alert Number (0 = disabled)";
        AlertNumber.SetInt(0);
        AlertNumber.SetIntLimits(0, 150);

        EnableFootprintFilter.Name = "Enable Footprint Imbalance Filter";
        EnableFootprintFilter.SetYesNo(0);

        FootprintZonePercent.Name = "Footprint Zone Size Percent (Prev Bar High/Low)";
        FootprintZonePercent.SetInt(20);
        FootprintZonePercent.SetIntLimits(1, 100);

        FootprintImbalanceRatio.Name = "Minimum Footprint Imbalance Ratio";
        FootprintImbalanceRatio.SetFloat(2.0f);
        FootprintImbalanceRatio.SetFloatLimits(1.0f, 50.0f);

        FootprintMinCombinedVolume.Name = "Minimum Footprint Zone Combined Volume";
        FootprintMinCombinedVolume.SetInt(50);
        FootprintMinCombinedVolume.SetIntLimits(0, 10000000);

        return;
    }

    const int index = sc.Index;
    BuyMark[index] = 0.0f;
    SellMark[index] = 0.0f;
    BearTrapHighlight[index] = 0.0f;
    BullTrapHighlight[index] = 0.0f;

    const int lookback = LookbackBars.GetInt();
    if (index <= lookback)
    {
        return;
    }

    const float minBreakout = static_cast<float>(MinBreakoutTicks.GetInt()) * sc.TickSize;
    const float markerOffset = static_cast<float>(MarkerOffsetTicks.GetInt()) * sc.TickSize;
    const bool requireReversalClose = RequireReversalClose.GetYesNo() != 0;
    const bool useFootprintFilter = EnableFootprintFilter.GetYesNo() != 0;

    float highestPrior = sc.High[index - lookback];
    float lowestPrior = sc.Low[index - lookback];
    for (int i = index - lookback + 1; i < index; ++i)
    {
        if (sc.High[i] > highestPrior)
            highestPrior = sc.High[i];

        if (sc.Low[i] < lowestPrior)
            lowestPrior = sc.Low[i];
    }

    const bool bullishClose = sc.Close[index] > sc.Open[index];
    const bool bearishClose = sc.Close[index] < sc.Open[index];

    bool bullFootprintPass = true;
    bool bearFootprintPass = true;
    if (useFootprintFilter)
    {
        const int previousBarIndex = index - 1;
        unsigned int topBidSum = 0;
        unsigned int topAskSum = 0;
        unsigned int bottomBidSum = 0;
        unsigned int bottomAskSum = 0;

        const bool hasTopZoneData = ComputeZoneBidAskSums(
            sc,
            previousBarIndex,
            true,
            FootprintZonePercent.GetInt(),
            topBidSum,
            topAskSum);

        const bool hasBottomZoneData = ComputeZoneBidAskSums(
            sc,
            previousBarIndex,
            false,
            FootprintZonePercent.GetInt(),
            bottomBidSum,
            bottomAskSum);

        const unsigned int minCombinedVolume = static_cast<unsigned int>(FootprintMinCombinedVolume.GetInt());
        const double imbalanceThreshold = static_cast<double>(FootprintImbalanceRatio.GetFloat());

        const unsigned int topCombinedVolume = topBidSum + topAskSum;
        const unsigned int bottomCombinedVolume = bottomBidSum + bottomAskSum;

        double topAskBidRatio = 0.0;
        if (topBidSum > 0)
            topAskBidRatio = static_cast<double>(topAskSum) / static_cast<double>(topBidSum);
        else if (topAskSum > 0)
            topAskBidRatio = 1000000.0;

        double bottomBidAskRatio = 0.0;
        if (bottomAskSum > 0)
            bottomBidAskRatio = static_cast<double>(bottomBidSum) / static_cast<double>(bottomAskSum);
        else if (bottomBidSum > 0)
            bottomBidAskRatio = 1000000.0;

        bullFootprintPass =
            hasTopZoneData
            && topCombinedVolume >= minCombinedVolume
            && topAskBidRatio >= imbalanceThreshold;

        bearFootprintPass =
            hasBottomZoneData
            && bottomCombinedVolume >= minCombinedVolume
            && bottomBidAskRatio >= imbalanceThreshold;
    }

    const bool bullTrap =
        sc.High[index] >= (highestPrior + minBreakout)
        && sc.Close[index] <= highestPrior
        && (!requireReversalClose || bearishClose)
        && bullFootprintPass;

    const bool bearTrap =
        sc.Low[index] <= (lowestPrior - minBreakout)
        && sc.Close[index] >= lowestPrior
        && (!requireReversalClose || bullishClose)
        && bearFootprintPass;

    const int alertNumber = AlertNumber.GetInt();

    if (bullTrap)
    {
        BullTrapHighlight[index] = sc.Close[index];
        SellMark[index] = sc.High[index] + markerOffset;

        if (alertNumber > 0)
        {
            SCString msg;
            msg.Format("Bull trap detected at %.2f (sell mark)", sc.Close[index]);
            sc.SetAlert(alertNumber, msg);
        }
    }

    if (bearTrap)
    {
        BearTrapHighlight[index] = sc.Close[index];
        BuyMark[index] = sc.Low[index] - markerOffset;

        if (alertNumber > 0)
        {
            SCString msg;
            msg.Format("Bear trap detected at %.2f (buy mark)", sc.Close[index]);
            sc.SetAlert(alertNumber, msg);
        }
    }
}
