#ifndef NOMINMAX
#define NOMINMAX 
#endif
#include "sierrachart.h"
#include <cfloat>
#include <cmath>

SCDLLName("Momentum Surge Alert");

// Detects rapid price movement accompanied by high volume and sounds a configurable alert.
// Three detection modes:
//   0 - Standard Deviation (z-score) based on bar OHLCV data
//   1 - Fixed Threshold based on bar OHLCV data
//   2 - Tick Window based on individual trade ticks (most accurate / real-time)
SCSFExport scsf_MomentumSurgeAlert(SCStudyInterfaceRef sc)
{
    // Subgraph references
    SCSubgraphRef SG_AlertSignal    = sc.Subgraph[0];
    SCSubgraphRef SG_PriceRangeZScore = sc.Subgraph[1];
    SCSubgraphRef SG_VolumeZScore   = sc.Subgraph[2];
    SCSubgraphRef SG_BuyMark        = sc.Subgraph[3];
    SCSubgraphRef SG_SellMark       = sc.Subgraph[4];

    // Input references
    SCInputRef In_DetectionMode         = sc.Input[0];
    SCInputRef In_LookbackPeriod        = sc.Input[1];
    SCInputRef In_PriceRangeZThreshold  = sc.Input[2];
    SCInputRef In_VolumeZThreshold      = sc.Input[3];
    SCInputRef In_FixedRangeTicks       = sc.Input[4];
    SCInputRef In_FixedVolumeThreshold  = sc.Input[5];
    SCInputRef In_AlertNumber           = sc.Input[6];
    SCInputRef In_AlertOncePerBar       = sc.Input[7];
    SCInputRef In_MinRangeTicksMode1    = sc.Input[8];
    SCInputRef In_CooldownBars          = sc.Input[9];
    // Tick Window mode (Mode 2) inputs
    SCInputRef In_TickWindowSize        = sc.Input[10];
    SCInputRef In_TickRangeThreshold    = sc.Input[11];
    SCInputRef In_TickVolumeThreshold   = sc.Input[12];

    if (sc.SetDefaults)
    {
        sc.GraphName        = "Momentum Surge Alert";
        sc.StudyDescription = "Detects rapid price movement with high volume and sounds a configurable alert. "
                              "Mode 0: z-score of bar range/volume vs rolling average. "
                              "Mode 1: fixed bar range and volume thresholds. "
                              "Mode 2 (Tick Window): analyzes individual trade ticks for real-time intra-bar detection.";
        sc.AutoLoop   = 1;
        sc.GraphRegion = 0; // Overlay on price chart so the alert dot appears on price

        // Alert dot: yellow point on the bar high
        SG_AlertSignal.Name         = "Alert Signal";
        SG_AlertSignal.DrawStyle    = DRAWSTYLE_POINT_ON_HIGH;
        SG_AlertSignal.PrimaryColor = RGB(255, 255, 0);
        SG_AlertSignal.LineWidth    = 5;

        // Diagnostic metrics – hidden by default (different scale from price)
        SG_PriceRangeZScore.Name         = "Range Metric";
        SG_PriceRangeZScore.DrawStyle    = DRAWSTYLE_IGNORE;
        SG_PriceRangeZScore.PrimaryColor = RGB(0, 200, 200);
        SG_PriceRangeZScore.LineWidth    = 1;

        SG_VolumeZScore.Name         = "Volume Metric";
        SG_VolumeZScore.DrawStyle    = DRAWSTYLE_IGNORE;
        SG_VolumeZScore.PrimaryColor = RGB(200, 0, 200);
        SG_VolumeZScore.LineWidth    = 1;

        // Directional buy/sell marks
        SG_BuyMark.Name         = "Buy Signal";
        SG_BuyMark.DrawStyle    = DRAWSTYLE_TRIANGLE_UP;
        SG_BuyMark.PrimaryColor = RGB(0, 200, 0);
        SG_BuyMark.LineWidth    = 5;
        SG_BuyMark.DrawZeros    = 0;

        SG_SellMark.Name         = "Sell Signal";
        SG_SellMark.DrawStyle    = DRAWSTYLE_TRIANGLE_DOWN;
        SG_SellMark.PrimaryColor = RGB(200, 0, 0);
        SG_SellMark.LineWidth    = 5;
        SG_SellMark.DrawZeros    = 0;

        // Detection mode dropdown
        In_DetectionMode.Name = "Detection Mode";
        In_DetectionMode.SetCustomInputStrings("Standard Deviation;Fixed Threshold;Tick Window");
        In_DetectionMode.SetCustomInputIndex(0);

        // Mode 0 – Standard Deviation settings
        In_LookbackPeriod.Name = "Lookback Period (bars)";
        In_LookbackPeriod.SetInt(50);
        In_LookbackPeriod.SetIntLimits(2, 5000);

        In_PriceRangeZThreshold.Name = "Price Range Z-Score Threshold";
        In_PriceRangeZThreshold.SetFloat(2.0f);
        In_PriceRangeZThreshold.SetFloatLimits(0.1f, 20.0f);

        In_VolumeZThreshold.Name = "Volume Z-Score Threshold";
        In_VolumeZThreshold.SetFloat(2.0f);
        In_VolumeZThreshold.SetFloatLimits(0.1f, 20.0f);

        // Mode 1 – Fixed Threshold settings
        In_FixedRangeTicks.Name = "Fixed Range Threshold (Ticks)";
        In_FixedRangeTicks.SetInt(20);
        In_FixedRangeTicks.SetIntLimits(1, 100000);

        In_FixedVolumeThreshold.Name = "Fixed Volume Threshold";
        In_FixedVolumeThreshold.SetInt(5000);
        In_FixedVolumeThreshold.SetIntLimits(1, 2000000000);

        // Alert settings
        In_AlertNumber.Name = "Alert Number (1-50)";
        In_AlertNumber.SetInt(1);
        In_AlertNumber.SetIntLimits(1, 50);

        In_AlertOncePerBar.Name = "Alert Only Once Per Bar";
        In_AlertOncePerBar.SetYesNo(1);

        // Mode 0 minimum tick floor to avoid false positives
        In_MinRangeTicksMode1.Name = "Minimum Bar Range (Ticks) for Standard Deviation Mode";
        In_MinRangeTicksMode1.SetInt(4);
        In_MinRangeTicksMode1.SetIntLimits(0, 100000);

        // Cooldown
        In_CooldownBars.Name = "Cooldown Bars (suppress alerts after trigger)";
        In_CooldownBars.SetInt(5);
        In_CooldownBars.SetIntLimits(0, 10000);

        // Mode 2 – Tick Window settings
        In_TickWindowSize.Name = "Tick Window Size (Mode 2 - number of ticks to analyse)";
        In_TickWindowSize.SetInt(100);
        In_TickWindowSize.SetIntLimits(2, 10000);

        In_TickRangeThreshold.Name = "Tick Range Threshold (Ticks, Mode 2)";
        In_TickRangeThreshold.SetInt(10);
        In_TickRangeThreshold.SetIntLimits(1, 100000);

        In_TickVolumeThreshold.Name = "Tick Volume Threshold (Mode 2)";
        In_TickVolumeThreshold.SetInt(2000);
        In_TickVolumeThreshold.SetIntLimits(1, 2000000000);

        return;
    }

    // -----------------------------------------------------------------------
    // Read inputs
    // -----------------------------------------------------------------------
    const int  detectionMode       = In_DetectionMode.GetIndex();
    const int  lookbackPeriod      = In_LookbackPeriod.GetInt();
    const float priceRangeZThresh  = In_PriceRangeZThreshold.GetFloat();
    const float volumeZThresh      = In_VolumeZThreshold.GetFloat();
    const int  fixedRangeTicks     = In_FixedRangeTicks.GetInt();
    const int  fixedVolumeThresh   = In_FixedVolumeThreshold.GetInt();
    const int  alertNumber         = In_AlertNumber.GetInt();
    const bool alertOncePerBar     = (In_AlertOncePerBar.GetYesNo() != 0);
    const int  minRangeTicksMode1  = In_MinRangeTicksMode1.GetInt();
    const int  cooldownBars        = In_CooldownBars.GetInt();
    const int  tickWindowSize      = In_TickWindowSize.GetInt();
    const int  tickRangeThreshold  = In_TickRangeThreshold.GetInt();
    const int  tickVolThreshold    = In_TickVolumeThreshold.GetInt();

    // Persistent state
    // [0] = last bar index that fired an alert (-1 means never)
    // [1] = cooldown bars remaining
    // [2] = last bar index that was fully processed (used to tick down cooldown once per bar)
    int& lastAlertBarIndex  = sc.GetPersistentInt(0);
    int& cooldownRemaining  = sc.GetPersistentInt(1);
    int& lastProcessedIndex = sc.GetPersistentInt(2);

    // Initialise persistent state on first full recalculation
    if (sc.IsFullRecalculation && sc.Index == 0)
    {
        lastAlertBarIndex  = -1;
        cooldownRemaining  = 0;
        lastProcessedIndex = -1;
    }

    // Guard: z-score mode (0) needs at least lookbackPeriod bars loaded before the current bar.
    // Tick Window mode (2) collects raw ticks and does not require a bar lookback.
    if (detectionMode == 0 && sc.Index < lookbackPeriod)
    {
        SG_AlertSignal[sc.Index]      = 0.0f;
        SG_PriceRangeZScore[sc.Index] = 0.0f;
        SG_VolumeZScore[sc.Index]     = 0.0f;
        SG_BuyMark[sc.Index]          = 0.0f;
        SG_SellMark[sc.Index]         = 0.0f;
        return;
    }

    // -----------------------------------------------------------------------
    // Current bar values
    // -----------------------------------------------------------------------
    const float tickSize      = sc.TickSize;
    const float currentRange  = sc.High[sc.Index] - sc.Low[sc.Index];
    const float currentVolume = sc.Volume[sc.Index];

    // -----------------------------------------------------------------------
    // Determine whether the alert should fire
    // -----------------------------------------------------------------------
    bool triggered = false;
    float priceRangeZScore = 0.0f;
    float volumeZScore     = 0.0f;

    if (detectionMode == 0)
    {
        // ------------------------------------------------------------------
        // Mode 0: Standard Deviation (z-score) based on bar OHLCV data
        // ------------------------------------------------------------------

        // Compute rolling mean and std dev over [sc.Index - lookbackPeriod, sc.Index - 1]
        double sumRange = 0.0, sumRange2 = 0.0;
        double sumVol   = 0.0, sumVol2   = 0.0;

        for (int i = sc.Index - lookbackPeriod; i < sc.Index; ++i)
        {
            const double r = sc.High[i] - sc.Low[i];
            const double v = sc.Volume[i];
            sumRange  += r;
            sumRange2 += r * r;
            sumVol    += v;
            sumVol2   += v * v;
        }

        const double n        = static_cast<double>(lookbackPeriod);
        const double avgRange = sumRange  / n;
        const double avgVol   = sumVol    / n;

        // Population standard deviation (variance = E[X^2] - (E[X])^2)
        const double varRange = (sumRange2 / n) - (avgRange * avgRange);
        const double varVol   = (sumVol2   / n) - (avgVol   * avgVol);

        const double stdRange = (varRange > 0.0) ? std::sqrt(varRange) : 0.0;
        const double stdVol   = (varVol   > 0.0) ? std::sqrt(varVol)   : 0.0;

        // Compute z-scores (guard against zero std dev)
        priceRangeZScore = (stdRange > 0.0)
            ? static_cast<float>((currentRange  - avgRange) / stdRange)
            : 0.0f;

        volumeZScore = (stdVol > 0.0)
            ? static_cast<float>((currentVolume - avgVol) / stdVol)
            : 0.0f;

        // Minimum tick range floor to avoid false positives on low-ATR instruments
        const float minRangePrice = static_cast<float>(minRangeTicksMode1) * tickSize;

        if (priceRangeZScore >= priceRangeZThresh &&
            volumeZScore     >= volumeZThresh      &&
            currentRange     >= minRangePrice)
        {
            triggered = true;
        }
    }
    else if (detectionMode == 1)
    {
        // ------------------------------------------------------------------
        // Mode 1: Fixed Threshold based on bar OHLCV data
        // ------------------------------------------------------------------
        const float minRangePrice = static_cast<float>(fixedRangeTicks) * tickSize;

        if (currentRange  >= minRangePrice &&
            currentVolume >= static_cast<float>(fixedVolumeThresh))
        {
            triggered = true;
        }
    }

    // -----------------------------------------------------------------------
    // Mode 2 (Tick Window): uses sc.GetNthTick() to examine individual trades
    // across recent bars, giving real-time intra-bar sensitivity rather than
    // waiting for a bar close.
    // During a full recalculation, skip all bars except the most recent to
    // avoid expensive repeated GetNthTick scanning for every historical bar.
    // -----------------------------------------------------------------------
    if (detectionMode == 2)
    {
        if (sc.IsFullRecalculation && sc.Index < sc.ArraySize - 1)
        {
            SG_AlertSignal[sc.Index]      = 0.0f;
            SG_PriceRangeZScore[sc.Index] = 0.0f;
            SG_VolumeZScore[sc.Index]     = 0.0f;
            return;
        }

        float    minTickPrice    = FLT_MAX;
        float    maxTickPrice    = -FLT_MAX;
        double   totalTickVolume = 0.0;
        int      ticksCollected  = 0;

        c_SCTimeAndSalesArray tsArray;
        sc.GetTimeAndSales(tsArray);
        const int tsCount = tsArray.Size();

        if (tsCount > 0)
        {
            // Walk backward from most recent tick, collecting up to tickWindowSize ticks.
            const int startIdx = tsCount - 1;
            const int limit = std::max(0, tsCount - tickWindowSize);

            for (int t = startIdx; t >= limit; --t)
            {
                const s_TimeAndSales& tick = tsArray[t];
                if (tick.Price < minTickPrice) minTickPrice = tick.Price;
                if (tick.Price > maxTickPrice) maxTickPrice = tick.Price;
                totalTickVolume += tick.Volume;
                ++ticksCollected;
            }
        }

        // Need at least 2 ticks to compute a meaningful range.
        if (ticksCollected < 2) 
        {
            SG_AlertSignal[sc.Index]      = 0.0f;
            SG_PriceRangeZScore[sc.Index] = 0.0f;
            SG_VolumeZScore[sc.Index]     = 0.0f;
            SG_BuyMark[sc.Index]          = 0.0f;
            SG_SellMark[sc.Index]         = 0.0f;
            return;
        }

        const float tickRange         = maxTickPrice - minTickPrice;
        const float minRangePrice     = static_cast<float>(tickRangeThreshold) * tickSize;

        // Reuse subgraphs: plot tick range in ticks and raw tick volume for diagnostics.
        priceRangeZScore = (tickSize > 0.0f) ? (tickRange / tickSize) : 0.0f;
        volumeZScore     = static_cast<float>(totalTickVolume);

        triggered = (tickRange >= minRangePrice &&
                     totalTickVolume >= static_cast<double>(tickVolThreshold));
    }

    // -----------------------------------------------------------------------
    // Write diagnostic subgraphs.
    // Modes 0/1: price range z-score and volume z-score.
    // Mode 2: tick range (in ticks) and total tick volume over the window.
    // -----------------------------------------------------------------------
    SG_PriceRangeZScore[sc.Index] = priceRangeZScore;
    SG_VolumeZScore[sc.Index]     = volumeZScore;

    // -----------------------------------------------------------------------
    // Alert / output logic
    // -----------------------------------------------------------------------

    // Tick down the cooldown counter exactly once per new bar
    if (sc.Index != lastProcessedIndex)
    {
        if (cooldownRemaining > 0)
            --cooldownRemaining;
        lastProcessedIndex = sc.Index;
    }

    // Suppress if within cooldown window
    if (cooldownRemaining > 0)
        triggered = false;

    // Suppress if already alerted on this bar and once-per-bar is enabled
    if (alertOncePerBar && sc.Index == lastAlertBarIndex)
        triggered = false;

    if (triggered)
    {
        // Plot the alert dot on the bar's high
        SG_AlertSignal[sc.Index] = 1.0f;

        // Directional buy/sell mark: triangle placed at the close price (alert price point).
        // Bullish bar → green triangle up; bearish bar → red triangle down.
        const bool isBullish = (sc.Close[sc.Index] >= sc.Open[sc.Index]);
        if (isBullish)
        {
            SG_BuyMark[sc.Index]  = sc.Close[sc.Index];
            SG_SellMark[sc.Index] = 0.0f;
        }
        else
        {
            SG_BuyMark[sc.Index]  = 0.0f;
            SG_SellMark[sc.Index] = sc.Close[sc.Index];
        }

        // Sound the configured Sierra Chart alert
        sc.SetAlert(alertNumber);

        // Update persistent state
        lastAlertBarIndex = sc.Index;
        cooldownRemaining = cooldownBars + 1;
    }
    else
    {
        SG_AlertSignal[sc.Index] = 0.0f;
        SG_BuyMark[sc.Index]     = 0.0f;
        SG_SellMark[sc.Index]    = 0.0f;
    }
}
