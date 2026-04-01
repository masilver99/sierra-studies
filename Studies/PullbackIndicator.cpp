#include "sierrachart.h"

SCDLLName("Pullback Probability Study")

SCSFExport scsf_PullbackProbabilityStudy(SCStudyInterfaceRef sc)
{
    SCSubgraphRef Pullback1 = sc.Subgraph[0];
    SCSubgraphRef Pullback2 = sc.Subgraph[1];

    SCInputRef TrendBars = sc.Input[0];
    SCInputRef PullbackBars = sc.Input[1];
    SCInputRef EMALength = sc.Input[2];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Ali Pullback Detector";
        sc.AutoLoop = 1;

        Pullback1.Name = "Promising Pullback";
        Pullback1.DrawStyle = DRAWSTYLE_COLOR_BAR;
        Pullback1.PrimaryColor = RGB(255,255,0);

        Pullback2.Name = "Second Entry Pullback";
        Pullback2.DrawStyle = DRAWSTYLE_COLOR_BAR;
        Pullback2.PrimaryColor = RGB(0,255,0);

        TrendBars.Name = "Trend Strength Bars";
        TrendBars.SetInt(4);

        PullbackBars.Name = "Max Pullback Bars";
        PullbackBars.SetInt(3);

        EMALength.Name = "EMA Length";
        EMALength.SetInt(20);

        return;
    }

    int trendBars = TrendBars.GetInt();
    int maxPullback = PullbackBars.GetInt();
    int emaLen = EMALength.GetInt();

    sc.ExponentialMovAvg(sc.Close, sc.Subgraph[2], emaLen);

    bool strongBullTrend = true;

    for(int i = 1; i <= trendBars; i++)
    {
        if(sc.Close[sc.Index - i] <= sc.Open[sc.Index - i])
            strongBullTrend = false;
    }

    bool smallPullback = false;

    int pullbackCount = 0;

    for(int i = 1; i <= maxPullback; i++)
    {
        if(sc.Close[sc.Index - i] < sc.Open[sc.Index - i])
            pullbackCount++;
    }

    if(pullbackCount >= 1 && pullbackCount <= maxPullback)
        smallPullback = true;

    bool aboveEMA = sc.Close[sc.Index] > sc.Subgraph[2][sc.Index];

    if(strongBullTrend && smallPullback && aboveEMA)
    {
        Pullback1[sc.Index] = sc.Close[sc.Index];
    }

    bool secondEntry = false;

    if(strongBullTrend &&
       sc.Close[sc.Index-2] < sc.Open[sc.Index-2] &&
       sc.Close[sc.Index-1] < sc.Open[sc.Index-1] &&
       sc.Close[sc.Index] > sc.Open[sc.Index])
    {
        secondEntry = true;
    }

    if(secondEntry)
    {
        Pullback2[sc.Index] = sc.Close[sc.Index];
    }
}