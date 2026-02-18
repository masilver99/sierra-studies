#include "sierrachart.h"

SCDLLName("Trap Zone Highlight")

SCSFExport scsf_TrapZoneHighlight(SCStudyInterfaceRef sc)
{
    SCSubgraphRef LongTrap = sc.Subgraph[0];
    SCSubgraphRef ShortTrap = sc.Subgraph[1];

    SCInputRef SwingLookback = sc.Input[0];
    SCInputRef MaxFailureBars = sc.Input[1];
    SCInputRef RangeMultiplier = sc.Input[2];
    SCInputRef HighlightBars = sc.Input[3];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Trap Zone Highlight";
        sc.AutoLoop = 1;

        LongTrap.Name = "Long Trap";
        LongTrap.DrawStyle = DRAWSTYLE_BACKGROUND;
        LongTrap.PrimaryColor = RGB(0, 100, 0);

        ShortTrap.Name = "Short Trap";
        ShortTrap.DrawStyle = DRAWSTYLE_BACKGROUND;
        ShortTrap.PrimaryColor = RGB(120, 0, 0);

        SwingLookback.Name = "Swing Lookback";
        SwingLookback.SetInt(10);

        MaxFailureBars.Name = "Max Failure Bars";
        MaxFailureBars.SetInt(3);

        RangeMultiplier.Name = "Range Expansion Multiplier";
        RangeMultiplier.SetFloat(1.5f);

        HighlightBars.Name = "Highlight Duration";
        HighlightBars.SetInt(5);

        return;
    }

    int index = sc.Index;

    float highest = sc.GetHighest(sc.High, SwingLookback.GetInt(), index - 1);
    float lowest = sc.GetLowest(sc.Low, SwingLookback.GetInt(), index - 1);

    float range = sc.High[index] - sc.Low[index];

    float avgRange = 0.0f;
    int lookback = 10;
    for (int i = 1; i <= lookback; i++)
        avgRange += sc.High[index - i] - sc.Low[index - i];
    avgRange /= lookback;

    // Persistent variables
    int& breakoutBar = sc.GetPersistentInt(1);
    float& breakoutLevel = sc.GetPersistentFloat(1);
    int& breakoutDirection = sc.GetPersistentInt(2);

    // Detect breakout
    if (sc.High[index] > highest)
    {
        breakoutBar = index;
        breakoutLevel = highest;
        breakoutDirection = 1; // upside
    }
    else if (sc.Low[index] < lowest)
    {
        breakoutBar = index;
        breakoutLevel = lowest;
        breakoutDirection = -1; // downside
    }

    // Detect failure
    if (breakoutDirection == 1 &&
        index - breakoutBar <= MaxFailureBars.GetInt() &&
        sc.Close[index] < breakoutLevel &&
        range > avgRange * RangeMultiplier.GetFloat())
    {
        for (int i = 0; i < HighlightBars.GetInt(); i++)
            LongTrap[index + i] = 1;
        
        sc.SetAlert(1);
        breakoutDirection = 0;
    }

    if (breakoutDirection == -1 &&
        index - breakoutBar <= MaxFailureBars.GetInt() &&
        sc.Close[index] > breakoutLevel &&
        range > avgRange * RangeMultiplier.GetFloat())
    {
        for (int i = 0; i < HighlightBars.GetInt(); i++)
            ShortTrap[index + i] = 1;

        sc.SetAlert(2);
        breakoutDirection = 0;
    }
}
