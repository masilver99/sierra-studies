# TrendStrength.cpp Design Recommendations

This document provides recommendations for implementing a TrendStrength indicator for Sierra Chart, addressing the requirements for incremental testing, trend strength criteria, and weighting strategies.

## 1. Enable/Disable Criteria Individually

To allow incremental testing of strength criteria, implement a configurable input system where each criterion can be toggled independently.

### Recommended Implementation Approach:

```cpp
// In SetDefaults section:
SCInputRef Input_EnablePriceMovement = sc.Input[0];
SCInputRef Input_EnableVolumeConfirmation = sc.Input[1];
SCInputRef Input_EnableHigherHighsLowerLows = sc.Input[2];
SCInputRef Input_EnableADX = sc.Input[3];
SCInputRef Input_EnableMovingAverageTrend = sc.Input[4];
SCInputRef Input_EnableATRMomentum = sc.Input[5];

Input_EnablePriceMovement.Name = "Enable Price Movement Criterion";
Input_EnablePriceMovement.SetYesNo(true);

Input_EnableVolumeConfirmation.Name = "Enable Volume Confirmation";
Input_EnableVolumeConfirmation.SetYesNo(false);

// ... etc for each criterion
```

### Benefits of This Approach:
- **Incremental Testing**: Start with one criterion, validate it works correctly, then enable the next
- **Debugging**: Isolate which criterion might be causing unexpected behavior
- **Flexibility**: Different market conditions may benefit from different combinations
- **User Control**: Allow end users to customize based on their trading style

### Testing Workflow:
1. Start with only `Input_EnablePriceMovement` set to true
2. Test and tune parameters (lookback period, thresholds)
3. Enable `Input_EnableVolumeConfirmation`, test the combination
4. Continue adding criteria one at a time
5. Once all are working, test various combinations

## 2. Additional Trend Strength Criteria Ideas

Here are multiple criteria that can be used to determine trend strength:

### A. Price-Based Criteria

1. **Higher Highs / Lower Lows Pattern**
   - Count consecutive higher highs in uptrend or lower lows in downtrend
   - Strong trend: 3+ consecutive patterns
   - Weak trend: Broken pattern or alternating

2. **Price Distance from Moving Average**
   - Measure how far price is from key MAs (20, 50, 200 period)
   - Strong trend: Price well separated from MA (>2 ATR)
   - Weak trend: Price hovering near MA

3. **Slope of Moving Average**
   - Calculate the angle/rate of change of the MA itself
   - Strong trend: Steep slope (>45 degrees equivalent)
   - Weak trend: Flat or slightly sloping MA

4. **Percentage Move from Swing Point**
   - Measure % move from last significant swing high/low
   - Strong trend: >5% move without significant pullback
   - Weak trend: Small percentage gains

### B. Volume-Based Criteria

5. **Volume Confirmation**
   - Compare volume on trend bars vs counter-trend bars
   - Strong trend: Volume increasing on trend direction bars
   - Weak trend: Volume higher on retracement bars

6. **Volume Moving Average Ratio**
   - Current volume vs 20-period average volume
   - Strong trend: Volume 1.5x+ above average
   - Weak trend: Below average volume

### C. Momentum Indicators

7. **ADX (Average Directional Index)**
   - Standard measure of trend strength
   - Strong trend: ADX > 25-30
   - Weak trend: ADX < 20
   - Consider +DI and -DI for direction confirmation

8. **Rate of Change (ROC)**
   - Percentage change over N periods
   - Strong trend: ROC consistently positive/negative
   - Weak trend: ROC oscillating around zero

9. **Momentum Oscillator**
   - Current price vs price N bars ago
   - Strong trend: Consistently positive/negative momentum
   - Include acceleration (change in momentum)

### D. Volatility-Based Criteria

10. **ATR Expansion**
    - Compare current ATR to longer-term ATR average
    - Strong trend: ATR expanding (trending markets more volatile)
    - Weak trend: ATR contracting

11. **Bollinger Band Width**
    - Measure the width of Bollinger Bands
    - Strong trend: Bands expanding with price riding upper/lower band
    - Weak trend: Narrow bands (consolidation)

### E. Time-Based Criteria

12. **Bars Since Last Swing**
    - Count bars since last significant high/low was broken
    - Strong trend: Many bars without counter-trend swing
    - Weak trend: Frequent swing highs/lows

13. **Time in Trend**
    - Duration of current trend phase
    - Strong trend: Sustained for extended period
    - Consider degrading strength over very long periods (exhaustion)

### F. Multi-Timeframe Criteria

14. **Alignment Across Timeframes**
    - Check if multiple timeframes show same trend direction
    - Strong trend: Daily, hourly, and 15-min all aligned
    - Weak trend: Mixed signals across timeframes

15. **Higher Timeframe Support/Resistance**
    - Distance from key levels on higher timeframe
    - Strong trend: Clear of higher timeframe resistance in uptrend
    - Weak trend: Approaching major resistance

### G. Market Structure

16. **Candle Body Size Ratio**
    - Body size vs total range (including wicks)
    - Strong trend: Large bodies (>70% of total range)
    - Weak trend: Small bodies with long wicks

17. **Successive Close Positioning**
    - Where closes are within the bar range
    - Strong uptrend: Closes in upper quartile of bars
    - Strong downtrend: Closes in lower quartile
    - Weak trend: Closes in middle of bars

## 3. Weighting Strategy Recommendations

The weighting of the indicator is crucial for responsiveness vs stability. Here are recommended approaches:

### Option A: Exponential Weighting (Recommended)

**Most Recent Bars Have Greater Impact**

```cpp
// Apply exponential moving average logic to strength calculation
float alpha = 2.0 / (lookbackPeriod + 1);
float weightedStrength = 0;
float totalWeight = 0;

for (int i = 0; i < lookbackPeriod; i++)
{
    float weight = pow(1 - alpha, i);  // Decreases exponentially
    weightedStrength += CalculateBarStrength(sc.Index - i) * weight;
    totalWeight += weight;
}

float finalStrength = weightedStrength / totalWeight;
```

**Advantages:**
- More responsive to recent price action
- Still considers historical context
- Smooth transition, not too choppy
- Similar to how traders naturally think (recent action matters more)

**Best For:**
- Day trading / shorter timeframes
- Fast-moving markets
- When you need quick signals

### Option B: Linear Decay Weighting

**Gradual Decrease in Weight**

```cpp
// Linear weight decay
float weightedStrength = 0;
float totalWeight = 0;

for (int i = 0; i < lookbackPeriod; i++)
{
    float weight = lookbackPeriod - i;  // Simple linear decay
    weightedStrength += CalculateBarStrength(sc.Index - i) * weight;
    totalWeight += weight;
}

float finalStrength = weightedStrength / totalWeight;
```

**Advantages:**
- More balanced than exponential
- Still gives preference to recent bars
- Less aggressive weighting
- Good middle ground

**Best For:**
- Swing trading
- Medium-term trend following
- Markets with moderate volatility

### Option C: Equal Weighting

**All Bars Count Equally**

```cpp
// Simple moving average approach
float totalStrength = 0;

for (int i = 0; i < lookbackPeriod; i++)
{
    totalStrength += CalculateBarStrength(sc.Index - i);
}

float finalStrength = totalStrength / lookbackPeriod;
```

**Advantages:**
- Most stable, least choppy
- Avoids overreaction to recent spikes
- Historical context fully considered
- Simplest to understand and debug

**Best For:**
- Position trading / longer timeframes
- Stable, slow-moving markets
- When you want to avoid false signals

### Option D: Adaptive Weighting (Advanced)

**Weight Based on Volatility**

```cpp
// Use ATR or volatility to determine weighting
// More volatile recent bars get more weight
float weightedStrength = 0;
float totalWeight = 0;

for (int i = 0; i < lookbackPeriod; i++)
{
    float barVolatility = sc.High[sc.Index - i] - sc.Low[sc.Index - i];
    float weight = (i == 0 ? 1.0 : 1.0 / (i + 1)) * barVolatility;
    weightedStrength += CalculateBarStrength(sc.Index - i) * weight;
    totalWeight += weight;
}

float finalStrength = weightedStrength / totalWeight;
```

**Advantages:**
- Adapts to market conditions
- High-momentum bars have more influence
- Can capture explosive moves better

**Best For:**
- Breakout trading
- Momentum strategies
- Markets with varying volatility patterns

### Recommended Approach: User-Selectable Weighting

Implement multiple weighting schemes with a user input:

```cpp
SCInputRef Input_WeightingMethod = sc.Input[20];

Input_WeightingMethod.Name = "Weighting Method";
Input_WeightingMethod.SetCustomInputStrings("Exponential;Linear;Equal;Adaptive");
Input_WeightingMethod.SetCustomInputIndex(0);  // Default to Exponential

// Then in calculation:
switch (Input_WeightingMethod.GetIndex())
{
    case 0: // Exponential
        strength = CalculateExponentialWeightedStrength();
        break;
    case 1: // Linear
        strength = CalculateLinearWeightedStrength();
        break;
    case 2: // Equal
        strength = CalculateEqualWeightedStrength();
        break;
    case 3: // Adaptive
        strength = CalculateAdaptiveWeightedStrength();
        break;
}
```

This allows users to experiment and find what works best for their trading style and market conditions.

## 4. Recommended Implementation Structure

Here's a suggested overall structure:

```cpp
SCSFExport scsf_TrendStrength(SCStudyInterfaceRef sc)
{
    // Subgraphs
    SCSubgraphRef Subgraph_TrendStrength = sc.Subgraph[0];
    SCSubgraphRef Subgraph_TrendDirection = sc.Subgraph[1];
    
    // Inputs for enabling/disabling criteria
    SCInputRef Input_EnablePriceMovement = sc.Input[0];
    SCInputRef Input_EnableVolume = sc.Input[1];
    // ... more criteria toggles
    
    // Inputs for parameters
    SCInputRef Input_LookbackPeriod = sc.Input[10];
    SCInputRef Input_WeightingMethod = sc.Input[11];
    SCInputRef Input_StrongThreshold = sc.Input[12];
    SCInputRef Input_WeakThreshold = sc.Input[13];
    
    if (sc.SetDefaults)
    {
        // Setup defaults
        return;
    }
    
    // Calculate individual strength components
    float priceStrength = 0;
    if (Input_EnablePriceMovement.GetYesNo())
        priceStrength = CalculatePriceStrength(sc);
        
    float volumeStrength = 0;
    if (Input_EnableVolume.GetYesNo())
        volumeStrength = CalculateVolumeStrength(sc);
    
    // ... calculate other components
    
    // Combine with weighting
    float totalStrength = CombineStrengthComponents(
        priceStrength, volumeStrength, /* others */);
    
    // Output
    Subgraph_TrendStrength[sc.Index] = totalStrength;
    
    // Set color based on strength
    if (totalStrength > Input_StrongThreshold.GetFloat())
        Subgraph_TrendStrength.DataColor[sc.Index] = RGB(0, 255, 0);
    else if (totalStrength < Input_WeakThreshold.GetFloat())
        Subgraph_TrendStrength.DataColor[sc.Index] = RGB(255, 0, 0);
    else
        Subgraph_TrendStrength.DataColor[sc.Index] = RGB(255, 255, 0);
}
```

## 5. Testing and Validation Strategy

1. **Start Simple**: Enable only one criterion, verify it behaves as expected
2. **Add Incrementally**: Add one criterion at a time, test combinations
3. **Use Replay**: Test on historical data with Sierra Chart's replay mode
4. **Compare Markets**: Test on different instruments (trending vs ranging)
5. **Optimize Thresholds**: Find the right threshold values for "strong" vs "weak"
6. **Backtest**: Once stable, run comprehensive backtests
7. **Forward Test**: Paper trade before live implementation

## 6. Additional Considerations

### Normalization
All strength criteria should be normalized to a common scale (e.g., 0-100 or -1 to +1) so they can be combined meaningfully.

### Performance
- Minimize calculations by caching intermediate results
- Use `sc.GetPersistent*` functions for maintaining state
- Consider computation cost of complex indicators (ADX, etc.)

### Visual Feedback
Consider adding:
- Color-coded strength levels (green = strong, yellow = moderate, red = weak)
- Separate subgraph for trend direction (+1 = up, -1 = down, 0 = neutral)
- Drawing horizontal lines at threshold levels
- Background coloring of chart based on strength

### Documentation
Document each criterion with:
- What it measures
- Expected range of values
- Recommended threshold settings
- When it works best (market conditions)

## Summary

The recommended approach is:
1. **Start with 2-3 core criteria** (price movement, MA slope, volume)
2. **Use exponential weighting** as the default (most responsive)
3. **Make everything user-configurable** (enable/disable, weights, thresholds)
4. **Test incrementally** (one criterion at a time)
5. **Normalize all values** to common scale before combining
6. **Provide visual feedback** through colors and subgraphs

This design allows for flexibility, ease of testing, and adaptability to different trading styles and market conditions.
