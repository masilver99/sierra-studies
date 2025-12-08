#include "sierrachart.h"

SCDLLName("Wick Percentage Accumulator");

SCSFExport scsf_WickPercentageAccumulator(SCStudyInterfaceRef sc)
{
    // Define the Subgraph
    SCSubgraphRef Subgraph_AccumulatedWickPct = sc.Subgraph[0];

    if (sc.SetDefaults)
    {
        // Set the configuration and defaults
        sc.GraphName = "Accumulated Wick Percentage";
        sc.StudyDescription = "Adds up the percentage of candlesticks that are wicks, resetting daily at 9:30.";

        sc.AutoLoop = 1; // Auto-looping enabled

        // Configure the subgraph
        Subgraph_AccumulatedWickPct.Name = "Accumulated Wick %";
        Subgraph_AccumulatedWickPct.DrawStyle = DRAWSTYLE_LINE; 
        Subgraph_AccumulatedWickPct.PrimaryColor = RGB(0, 255, 0); // Green
        Subgraph_AccumulatedWickPct.LineWidth = 2;

        return;
    }

    // Data processing
    float High = sc.High[sc.Index];
    float Low = sc.Low[sc.Index];
    float Open = sc.Open[sc.Index];
    float Close = sc.Close[sc.Index];

    // Calculate ranges
    float TotalRange = High - Low;
    float BodyRange = fabs(Open - Close);
    float WickRange = TotalRange - BodyRange;

    // Calculate Wick Percentage for the current bar
    float CurrentBarWickPct = 0.0f;
    if (TotalRange > 0)
    {
        CurrentBarWickPct = (WickRange / TotalRange) * 100.0f;
    }

    // Determine if we should reset
    // We reset if the current bar's time is 09:30:00
    bool Reset = false;
    int CurrentTime = sc.BaseDateTimeIn[sc.Index].GetTime();
    
    // Check for exactly 9:30:00. 
    // Note: This assumes your chart timeframe allows for a bar to start exactly at 9:30.
    if (CurrentTime == HMS_TIME(9, 30, 0)) 
    {
        Reset = true;
    }

    // Calculate the average wick percentage
    if (Reset)
    {
        Subgraph_AccumulatedWickPct[sc.Index] = CurrentBarWickPct;
    }
    else
    {
        // Calculate average of all wick percentages since the reset
        float PreviousAverage = (sc.Index > 0) ? Subgraph_AccumulatedWickPct[sc.Index - 1] : 0.0f;
        int BarsSinceReset = 1;
        
        // Count bars since reset to calculate proper average
        for (int i = sc.Index; i > 0; i--)
        {
            int PrevTime = sc.BaseDateTimeIn[i - 1].GetTime();
            if (PrevTime == HMS_TIME(9, 30, 0))
            {
                break;
            }
            BarsSinceReset++;
        } 
        
        // New average = (previous_average * (bars - 1) + current_wick_pct) / bars
        Subgraph_AccumulatedWickPct[sc.Index] = (PreviousAverage * (BarsSinceReset - 1) + CurrentBarWickPct) / BarsSinceReset;
    }
}
