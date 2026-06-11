#include "sierrachart.h"

SCDLLName("Average Bar Overlap");

// Calculates the overlap in points between consecutive bars and plots
// the moving average of those per-bar overlap values over a user-defined
// lookback period.
//
// For each bar, the per-bar overlap with the prior bar is:
//   max(0, min(current_high, prior_high) - max(current_low, prior_low))
//
// The study then plots a simple moving average of those overlap values
// over the selected lookback length.

SCSFExport scsf_AverageBarOverlap(SCStudyInterfaceRef sc)
{
    // Subgraph[0]: plotted output — average overlap in points.
    SCSubgraphRef Subgraph_AvgOverlap    = sc.Subgraph[0];
    // Subgraph[1]: hidden working array that stores per-bar overlap values.
    // Sierra Chart persists these across AutoLoop calls, avoiding O(n*m) recomputation.
    SCSubgraphRef Subgraph_PerBarOverlap = sc.Subgraph[1];

    SCInputRef In_LookbackPeriod = sc.Input[0];

    if (sc.SetDefaults)
    {
        sc.GraphName        = "Average Bar Overlap";
        sc.StudyDescription = "Plots the moving average of the high-low overlap "
                              "in points between each bar and its immediately prior bar. "
                              "Uses a separate graph region because overlap values are "
                              "in points and do not share the price axis scale.";

        sc.AutoLoop    = 1;
        // Separate panel — overlap values are measured in points and do not share
        // the same numeric scale as the price axis on the main chart region.
        sc.GraphRegion = 1;

        Subgraph_AvgOverlap.Name         = "Avg Overlap (pts)";
        Subgraph_AvgOverlap.DrawStyle    = DRAWSTYLE_LINE;
        Subgraph_AvgOverlap.PrimaryColor = RGB(0, 200, 255);
        Subgraph_AvgOverlap.LineWidth    = 2;

        // Working array — kept hidden from the chart display.
        Subgraph_PerBarOverlap.Name      = "Per-Bar Overlap (internal)";
        Subgraph_PerBarOverlap.DrawStyle = DRAWSTYLE_IGNORE;

        In_LookbackPeriod.Name = "Lookback Period (bars)";
        In_LookbackPeriod.SetInt(10);
        In_LookbackPeriod.SetIntLimits(1, 5000);

        return;
    }

    const int Index = sc.Index;

    // Compute the per-bar overlap for this bar and store it in the working subgraph.
    // When there is no prior bar, overlap is zero.
    if (Index < 1)
    {
        Subgraph_PerBarOverlap[Index] = 0.0f;
        Subgraph_AvgOverlap[Index]    = 0.0f;
        return;
    }

    const float CurrentHigh = sc.High[Index];
    const float CurrentLow  = sc.Low[Index];
    const float PriorHigh   = sc.High[Index - 1];
    const float PriorLow    = sc.Low[Index - 1];

    const float OverlapHigh = (CurrentHigh < PriorHigh) ? CurrentHigh : PriorHigh;
    const float OverlapLow  = (CurrentLow  > PriorLow)  ? CurrentLow  : PriorLow;
    Subgraph_PerBarOverlap[Index] =
        (OverlapHigh > OverlapLow) ? (OverlapHigh - OverlapLow) : 0.0f;

    // Compute the simple moving average of per-bar overlap values using the
    // built-in Sierra Chart function. This operates on the already-populated
    // Subgraph_PerBarOverlap array, keeping each bar's work O(1).
    sc.SimpleMovAvg(Subgraph_PerBarOverlap, Subgraph_AvgOverlap, In_LookbackPeriod.GetInt());
}
