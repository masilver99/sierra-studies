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
    SCSubgraphRef Subgraph_AvgOverlap = sc.Subgraph[0];
    SCInputRef    In_LookbackPeriod   = sc.Input[0];

    if (sc.SetDefaults)
    {
        sc.GraphName        = "Average Bar Overlap";
        sc.StudyDescription = "Plots the moving average of the high-low overlap "
                              "in points between each bar and its immediately prior bar.";

        sc.AutoLoop   = 1;
        sc.GraphRegion = 1;

        Subgraph_AvgOverlap.Name         = "Avg Overlap (pts)";
        Subgraph_AvgOverlap.DrawStyle    = DRAWSTYLE_LINE;
        Subgraph_AvgOverlap.PrimaryColor = RGB(0, 200, 255);
        Subgraph_AvgOverlap.LineWidth    = 2;

        In_LookbackPeriod.Name = "Lookback Period (bars)";
        In_LookbackPeriod.SetInt(10);
        In_LookbackPeriod.SetIntLimits(1, 5000);

        return;
    }

    const int Index    = sc.Index;
    const int Lookback = In_LookbackPeriod.GetInt();

    // Not enough history for a pair of consecutive bars.
    if (Index < 1)
    {
        Subgraph_AvgOverlap[Index] = 0.0f;
        return;
    }

    // Compute the sum of per-bar overlap values over the lookback window.
    // We walk back up to Lookback bars, each time comparing bar[i] with bar[i-1].
    float    Sum      = 0.0f;
    int      Count    = 0;
    const int MaxBars = (Index < Lookback) ? Index : Lookback;

    for (int k = 0; k < MaxBars; ++k)
    {
        const int i = Index - k; // current bar of the pair
        const int j = i - 1;     // prior bar of the pair

        const float CurrentHigh = sc.High[i];
        const float CurrentLow  = sc.Low[i];
        const float PriorHigh   = sc.High[j];
        const float PriorLow    = sc.Low[j];

        const float OverlapHigh = (CurrentHigh < PriorHigh) ? CurrentHigh : PriorHigh;
        const float OverlapLow  = (CurrentLow  > PriorLow)  ? CurrentLow  : PriorLow;
        const float Overlap     = (OverlapHigh > OverlapLow) ? (OverlapHigh - OverlapLow) : 0.0f;

        Sum += Overlap;
        ++Count;
    }

    Subgraph_AvgOverlap[Index] = (Count > 0) ? (Sum / (float)Count) : 0.0f;
}
