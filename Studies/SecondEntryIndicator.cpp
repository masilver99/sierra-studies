#include "sierrachart.h"
#include <vector>
#include <algorithm>
#include <cmath>

SCDLLName("Al Brooks Second Entry Indicator");

/*==========================================================================*/
/*
    Al Brooks Second Entry Indicator
    
    DESCRIPTION:
    This indicator identifies "Second Entry" trading setups based on Al Brooks'
    price action trading methodology. It marks H1/H2 (long) and L1/L2 (short)
    entry points that occur after two-legged pullbacks in trending markets.
    
    CONCEPTS:
    - H1 (High 1): First higher high in an uptrend after a pullback
    - H2 (High 2): Second higher high after another pullback (BEST ENTRY)
    - L1 (Low 1): First lower low in a downtrend after a pullback
    - L2 (Low 2): Second lower low after another pullback (BEST ENTRY)
    
    WHY SECOND ENTRIES WORK:
    According to Al Brooks, the first entry (H1 or L1) often traps early traders.
    The second entry (H2 or L2) has a higher probability of success because:
    1. Weak hands have been shaken out on the first attempt
    2. The trend has proven itself twice
    3. Counter-trend traders have failed twice
    
    USAGE:
    1. Add this study to your chart
    2. Configure the 20 EMA period (default) as trend filter
    3. Look for green arrows below price (H2 long signals)
    4. Look for red arrows above price (L2 short signals)
    5. Optional: Enable H1/L1 markers to see first entries
    6. Optional: Require pullback to test EMA (more conservative)
    
    TRADING RULES:
    - Only take long signals (H2) when price is above EMA (bullish trend)
    - Only take short signals (L2) when price is below EMA (bearish trend)
    - Enter on breakout above/below the signal bar
    - Use appropriate stop loss based on recent swing points
    - Consider higher time frame trend for confirmation
    
    PARAMETERS:
    - EMA Period: Default 20 (Al Brooks' preferred moving average)
    - Swing Bars: 2 bars on each side to confirm swing high/low
    - Lookback: 50 bars to search for pattern
    - Min/Max Pullback Bars: Define valid pullback duration
    - Require EMA Test: Only signal when pullback tests the EMA
    
    REFERENCES:
    - Al Brooks "Trading Price Action Trends"
    - Al Brooks "Reading Price Charts Bar by Bar"
    - Price Action Trading methodology
    
    AUTHOR: Based on Al Brooks' trading teachings
    VERSION: 1.0
    DATE: 2026-02-18
*/
/*==========================================================================*/

// Structure to track swing points
struct SwingPoint
{
    int Index;
    float Price;
    bool IsHigh;  // true for swing high, false for swing low
    bool IsFirstEntry;  // H1/L1
    bool IsSecondEntry; // H2/L2
};

/*==========================================================================*/
// Helper function to detect swing high
bool IsSwingHigh(SCStudyInterfaceRef sc, int index, int leftBars, int rightBars)
{
    if (index < leftBars || index + rightBars >= sc.ArraySize)
        return false;

    float high = sc.High[index];
    
    // Check left side
    for (int i = 1; i <= leftBars; i++)
    {
        if (sc.High[index - i] >= high)
            return false;
    }
    
    // Check right side
    for (int i = 1; i <= rightBars; i++)
    {
        if (sc.High[index + i] > high)
            return false;
    }
    
    return true;
}

/*==========================================================================*/
// Helper function to detect swing low
bool IsSwingLow(SCStudyInterfaceRef sc, int index, int leftBars, int rightBars)
{
    if (index < leftBars || index + rightBars >= sc.ArraySize)
        return false;

    float low = sc.Low[index];
    
    // Check left side
    for (int i = 1; i <= leftBars; i++)
    {
        if (sc.Low[index - i] <= low)
            return false;
    }
    
    // Check right side
    for (int i = 1; i <= rightBars; i++)
    {
        if (sc.Low[index + i] < low)
            return false;
    }
    
    return true;
}

/*==========================================================================*/
SCSFExport scsf_SecondEntryIndicator(SCStudyInterfaceRef sc)
{
    // Subgraphs for signals
    SCSubgraphRef Subgraph_H2Long = sc.Subgraph[0];
    SCSubgraphRef Subgraph_L2Short = sc.Subgraph[1];
    SCSubgraphRef Subgraph_H1 = sc.Subgraph[2];
    SCSubgraphRef Subgraph_L1 = sc.Subgraph[3];
    SCSubgraphRef Subgraph_EMA = sc.Subgraph[4];
    
    // Inputs
    SCInputRef Input_EMAPeriod = sc.Input[0];
    SCInputRef Input_SwingBars = sc.Input[1];
    SCInputRef Input_LookbackBars = sc.Input[2];
    SCInputRef Input_MinPullbackBars = sc.Input[3];
    SCInputRef Input_MaxPullbackBars = sc.Input[4];
    SCInputRef Input_ShowH1L1 = sc.Input[5];
    SCInputRef Input_ShowH2L2 = sc.Input[6];
    SCInputRef Input_RequireEMATest = sc.Input[7];
    SCInputRef Input_EMATestTicks = sc.Input[8];
    SCInputRef Input_MaxBarsAfterPullback = sc.Input[9];
    
    if (sc.SetDefaults)
    {
        sc.GraphName = "Al Brooks Second Entry Indicator";
        sc.StudyDescription = "Identifies second entry signals (H2/L2) for trend continuation based on Al Brooks' price action principles. "
                              "H1 = First higher high in uptrend after pullback, H2 = Second higher high (second entry long). "
                              "L1 = First lower low in downtrend after pullback, L2 = Second lower low (second entry short). "
                              "Best used with 20 EMA as trend filter.";
        
        sc.AutoLoop = 0;  // Manual loop for swing detection
        sc.GraphRegion = 0;  // Main price graph
        
        // Subgraph configurations
        Subgraph_H2Long.Name = "H2 Long Signal";
        Subgraph_H2Long.DrawStyle = DRAWSTYLE_ARROW_UP;
        Subgraph_H2Long.PrimaryColor = RGB(0, 255, 0);  // Green
        Subgraph_H2Long.LineWidth = 3;
        Subgraph_H2Long.DrawZeros = false;
        
        Subgraph_L2Short.Name = "L2 Short Signal";
        Subgraph_L2Short.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        Subgraph_L2Short.PrimaryColor = RGB(255, 0, 0);  // Red
        Subgraph_L2Short.LineWidth = 3;
        Subgraph_L2Short.DrawZeros = false;
        
        Subgraph_H1.Name = "H1 First Entry (Long)";
        Subgraph_H1.DrawStyle = DRAWSTYLE_POINT;
        Subgraph_H1.PrimaryColor = RGB(0, 200, 0);  // Light Green
        Subgraph_H1.LineWidth = 2;
        Subgraph_H1.DrawZeros = false;
        
        Subgraph_L1.Name = "L1 First Entry (Short)";
        Subgraph_L1.DrawStyle = DRAWSTYLE_POINT;
        Subgraph_L1.PrimaryColor = RGB(255, 100, 100);  // Light Red
        Subgraph_L1.LineWidth = 2;
        Subgraph_L1.DrawZeros = false;
        
        Subgraph_EMA.Name = "Trend EMA";
        Subgraph_EMA.DrawStyle = DRAWSTYLE_LINE;
        Subgraph_EMA.PrimaryColor = RGB(255, 255, 0);  // Yellow
        Subgraph_EMA.LineWidth = 2;
        Subgraph_EMA.DrawZeros = true;
        
        // Input configurations
        Input_EMAPeriod.Name = "EMA Period (Trend Filter)";
        Input_EMAPeriod.SetInt(20);
        Input_EMAPeriod.SetIntLimits(5, 200);
        Input_EMAPeriod.SetDescription("Exponential Moving Average period for trend direction filter. Typical value is 20.");
        
        Input_SwingBars.Name = "Swing Detection Bars (Left/Right)";
        Input_SwingBars.SetInt(2);
        Input_SwingBars.SetIntLimits(1, 10);
        Input_SwingBars.SetDescription("Number of bars on each side to confirm a swing high/low.");
        
        Input_LookbackBars.Name = "Lookback for Pattern Detection";
        Input_LookbackBars.SetInt(50);
        Input_LookbackBars.SetIntLimits(10, 500);
        Input_LookbackBars.SetDescription("Number of bars to look back for detecting pullback patterns.");
        
        Input_MinPullbackBars.Name = "Minimum Pullback Bars";
        Input_MinPullbackBars.SetInt(2);
        Input_MinPullbackBars.SetIntLimits(1, 20);
        Input_MinPullbackBars.SetDescription("Minimum bars required in a pullback (two-legged move).");
        
        Input_MaxPullbackBars.Name = "Maximum Pullback Bars";
        Input_MaxPullbackBars.SetInt(15);
        Input_MaxPullbackBars.SetIntLimits(3, 50);
        Input_MaxPullbackBars.SetDescription("Maximum bars allowed in a pullback before considering trend change.");
        
        Input_ShowH1L1.Name = "Show H1/L1 First Entries";
        Input_ShowH1L1.SetYesNo(true);
        Input_ShowH1L1.SetDescription("Display first entry signals (H1 for longs, L1 for shorts).");
        
        Input_ShowH2L2.Name = "Show H2/L2 Second Entries";
        Input_ShowH2L2.SetYesNo(true);
        Input_ShowH2L2.SetDescription("Display second entry signals (H2 for longs, L2 for shorts).");
        
        Input_RequireEMATest.Name = "Require Pullback to Test EMA";
        Input_RequireEMATest.SetYesNo(true);
        Input_RequireEMATest.SetDescription("Only signal when pullback tests the EMA (Al Brooks' preferred setup).");
        
        Input_EMATestTicks.Name = "EMA Test Distance (Ticks)";
        Input_EMATestTicks.SetInt(10);
        Input_EMATestTicks.SetIntLimits(1, 100);
        Input_EMATestTicks.SetDescription("Maximum distance in ticks from EMA to consider it a valid test.");
        
        Input_MaxBarsAfterPullback.Name = "Max Bars After Pullback for Signal";
        Input_MaxBarsAfterPullback.SetInt(10);
        Input_MaxBarsAfterPullback.SetIntLimits(1, 50);
        Input_MaxBarsAfterPullback.SetDescription("Maximum bars after pullback completion to look for entry signal.");
        
        return;
    }
    
    // Calculate EMA
    sc.ExponentialMovAvg(sc.Close, Subgraph_EMA, Input_EMAPeriod.GetInt());
    
    // Get parameters
    const int swingBars = Input_SwingBars.GetInt();
    const int lookback = Input_LookbackBars.GetInt();
    const int minPullbackBars = Input_MinPullbackBars.GetInt();
    const int maxPullbackBars = Input_MaxPullbackBars.GetInt();
    const bool showH1L1 = Input_ShowH1L1.GetYesNo();
    const bool showH2L2 = Input_ShowH2L2.GetYesNo();
    const bool requireEMATest = Input_RequireEMATest.GetYesNo();
    const int emaTestTicks = Input_EMATestTicks.GetInt();
    const int maxBarsAfterPullback = Input_MaxBarsAfterPullback.GetInt();
    
    // Manual loop through bars
    for (int i = sc.UpdateStartIndex; i < sc.ArraySize; i++)
    {
        // Clear previous values
        Subgraph_H2Long[i] = 0;
        Subgraph_L2Short[i] = 0;
        Subgraph_H1[i] = 0;
        Subgraph_L1[i] = 0;
        
        // Need enough bars for analysis
        const int minBarsForAnalysis = swingBars + 10;
        if (i < minBarsForAnalysis)
            continue;
        
        // Determine trend direction based on price relative to EMA
        bool bullishTrend = sc.Close[i] > Subgraph_EMA[i] && sc.Close[i-1] > Subgraph_EMA[i-1];
        bool bearishTrend = sc.Close[i] < Subgraph_EMA[i] && sc.Close[i-1] < Subgraph_EMA[i-1];
        
        // Track recent swing points
        std::vector<SwingPoint> recentSwings;
        
        // Look back for swing points
        int searchStart = std::max(swingBars, i - lookback);
        for (int j = searchStart; j <= i - swingBars; j++)
        {
            if (IsSwingHigh(sc, j, swingBars, swingBars))
            {
                SwingPoint sp;
                sp.Index = j;
                sp.Price = sc.High[j];
                sp.IsHigh = true;
                sp.IsFirstEntry = false;
                sp.IsSecondEntry = false;
                recentSwings.push_back(sp);
            }
            
            if (IsSwingLow(sc, j, swingBars, swingBars))
            {
                SwingPoint sp;
                sp.Index = j;
                sp.Price = sc.Low[j];
                sp.IsHigh = false;
                sp.IsFirstEntry = false;
                sp.IsSecondEntry = false;
                recentSwings.push_back(sp);
            }
        }
        
        // Sort swings by index
        std::sort(recentSwings.begin(), recentSwings.end(), 
                  [](const SwingPoint& a, const SwingPoint& b) { return a.Index < b.Index; });
        
        // Detect H1/H2 pattern for bullish trend
        if (bullishTrend && recentSwings.size() >= 4)
        {
            // Look for pattern: Swing High -> Pullback (lower swing) -> H1 (higher high) -> Pullback -> H2 (higher high)
            for (size_t idx = 0; idx < recentSwings.size() - 3; idx++)
            {
                // Find a sequence: high -> low -> high -> low -> high
                if (recentSwings[idx].IsHigh && !recentSwings[idx+1].IsHigh && 
                    recentSwings[idx+2].IsHigh && !recentSwings[idx+3].IsHigh)
                {
                    float initialHigh = recentSwings[idx].Price;
                    float h1High = recentSwings[idx+2].Price;
                    
                    int h1Index = recentSwings[idx+2].Index;
                    int pullback1Index = recentSwings[idx+1].Index;
                    int pullback2Index = recentSwings[idx+3].Index;
                    
                    // Check if H1 made a higher high
                    if (h1High > initialHigh)
                    {
                        // Check pullback duration
                        int pullback2Duration = pullback2Index - h1Index;
                        
                        if (pullback2Duration >= minPullbackBars && pullback2Duration <= maxPullbackBars)
                        {
                            // Optional: Check if pullback tested EMA (Al Brooks principle)
                            bool emaTestOk = true;
                            if (requireEMATest)
                            {
                                // Check if any bar in pullback came close to EMA
                                emaTestOk = false;
                                for (int k = pullback1Index; k <= pullback2Index; k++)
                                {
                                    float distToEMA = std::abs(sc.Low[k] - Subgraph_EMA[k]);
                                    if (distToEMA <= sc.TickSize * emaTestTicks)
                                    {
                                        emaTestOk = true;
                                        break;
                                    }
                                }
                            }
                            
                            // Check if current bar could be H2
                            if (emaTestOk && i >= pullback2Index + 1 && i <= pullback2Index + maxBarsAfterPullback)
                            {
                                // H2 should make higher high than H1
                                if (sc.High[i] > h1High)
                                {
                                    // Mark H1
                                    if (showH1L1 && Subgraph_H1[h1Index] == 0)
                                        Subgraph_H1[h1Index] = sc.Low[h1Index] - sc.TickSize * 5;
                                    
                                    // Mark H2 (Second Entry Long Signal)
                                    if (showH2L2)
                                        Subgraph_H2Long[i] = sc.Low[i] - sc.TickSize * 5;
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Detect L1/L2 pattern for bearish trend
        if (bearishTrend && recentSwings.size() >= 4)
        {
            // Look for pattern: Swing Low -> Pullback (higher swing) -> L1 (lower low) -> Pullback -> L2 (lower low)
            for (size_t idx = 0; idx < recentSwings.size() - 3; idx++)
            {
                // Find a sequence: low -> high -> low -> high -> low
                if (!recentSwings[idx].IsHigh && recentSwings[idx+1].IsHigh && 
                    !recentSwings[idx+2].IsHigh && recentSwings[idx+3].IsHigh)
                {
                    float initialLow = recentSwings[idx].Price;
                    float l1Low = recentSwings[idx+2].Price;
                    
                    int l1Index = recentSwings[idx+2].Index;
                    int pullback1Index = recentSwings[idx+1].Index;
                    int pullback2Index = recentSwings[idx+3].Index;
                    
                    // Check if L1 made a lower low
                    if (l1Low < initialLow)
                    {
                        // Check pullback duration
                        int pullback2Duration = pullback2Index - l1Index;
                        
                        if (pullback2Duration >= minPullbackBars && pullback2Duration <= maxPullbackBars)
                        {
                            // Optional: Check if pullback tested EMA (Al Brooks principle)
                            bool emaTestOk = true;
                            if (requireEMATest)
                            {
                                // Check if any bar in pullback came close to EMA
                                emaTestOk = false;
                                for (int k = pullback1Index; k <= pullback2Index; k++)
                                {
                                    float distToEMA = std::abs(sc.High[k] - Subgraph_EMA[k]);
                                    if (distToEMA <= sc.TickSize * emaTestTicks)
                                    {
                                        emaTestOk = true;
                                        break;
                                    }
                                }
                            }
                            
                            // Check if current bar could be L2
                            if (emaTestOk && i >= pullback2Index + 1 && i <= pullback2Index + maxBarsAfterPullback)
                            {
                                // L2 should make lower low than L1
                                if (sc.Low[i] < l1Low)
                                {
                                    // Mark L1
                                    if (showH1L1 && Subgraph_L1[l1Index] == 0)
                                        Subgraph_L1[l1Index] = sc.High[l1Index] + sc.TickSize * 5;
                                    
                                    // Mark L2 (Second Entry Short Signal)
                                    if (showH2L2)
                                        Subgraph_L2Short[i] = sc.High[i] + sc.TickSize * 5;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
