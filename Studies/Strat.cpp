#include "sierrachart.h"

SCDLLName("Strat Bar Study");

SCSFExport scsf_CustomBarStudy(SCStudyInterfaceRef sc)
{
    SCSubgraphRef Subgraph_StudyReference = sc.Subgraph[0];

    SCFloatArrayRef Array_TextPosition = Subgraph_StudyReference.Arrays[0];

    SCInputRef Input_StudySubgraph1 = sc.Input[0];
    SCInputRef Input_DisplayAboveOrBelow = sc.Input[1];
    SCInputRef Input_OffsetInTicks = sc.Input[2];
    SCInputRef Input_DrawZeros = sc.Input[3];
    SCInputRef Input_AdditionalForwardColumns = sc.Input[4];
    SCInputRef Input_OffsetInTextHeights = sc.Input[5];//Graph.m_Arrays[SubgraphIndex][1].


    if (sc.SetDefaults)
    {
        sc.GraphName = "Strat Bar Study";
        sc.StudyDescription = "This study labels bars as Inside, Engulfing, or Other.";
        sc.AutoLoop = 1;

        Subgraph_StudyReference.Name = "Symbol Text";
        Subgraph_StudyReference.LineWidth = 12;
        Subgraph_StudyReference.DrawStyle = DRAWSTYLE_CUSTOM_VALUE_AT_Y;
        Subgraph_StudyReference.PrimaryColor = RGB(255, 127, 0); //Orange

        Input_StudySubgraph1.Name = "Input Study Subgraph";
        Input_StudySubgraph1.SetStudySubgraphValues(0, 0);

        Input_DisplayAboveOrBelow.Name = "Display Location";
        Input_DisplayAboveOrBelow.SetCustomInputStrings("Top of Bar;Bottom of Bar;Bottom of Chart Region");
        Input_DisplayAboveOrBelow.SetCustomInputIndex(0);

        Input_OffsetInTicks.Name = "Offset in Ticks";
        Input_OffsetInTicks.SetInt(1);

        Input_DrawZeros.Name = "Draw Zeros";
        Input_DrawZeros.SetYesNo(false);

        Input_AdditionalForwardColumns.Name = "Additional Forward Columns";
        Input_AdditionalForwardColumns.SetInt(0);

        Input_OffsetInTextHeights.Name = "Offset in Text Heights (+/-)";
        Input_OffsetInTextHeights.SetInt(0);

        return;
    }


    if (Input_DisplayAboveOrBelow.GetIndex() != 2)
    {
        sc.ScaleRangeType = SCALE_AUTO;
    }
    else
    {
        sc.ScaleRangeType = SCALE_USERDEFINED;
        sc.ScaleRangeTop = 100;
        sc.ScaleRangeBottom = 1;
    }

    if (sc.IsFullRecalculation)
        Subgraph_StudyReference.ExtendedArrayElementsToGraph = Input_AdditionalForwardColumns.GetInt();

    Subgraph_StudyReference.DrawZeros = Input_DrawZeros.GetYesNo();

    int barIndex = sc.Index;
    if (barIndex < 1) return; // Need at least one previous bar to compare

    float currentHigh = sc.High[barIndex];
    float currentLow = sc.Low[barIndex];
    float previousHigh = sc.High[barIndex - 1];
    float previousLow = sc.Low[barIndex - 1];

    SCString& PriorTextToDisplay = sc.GetPersistentSCString(1);

    SCString TextToDisplay;

    if (currentHigh <= previousHigh && currentLow >= previousLow)
    {
        // Inside bar
        TextToDisplay = "1";
        //sc.Subgraph[0][barIndex] = 1;
    }
    else if (currentHigh >= previousHigh && currentLow <= previousLow)
    {
        // Engulfing bar
        //sc.Subgraph[0][barIndex] = 3;
        TextToDisplay = "3";
    }
    else
    {
        // Other
        //sc.Subgraph[0][barIndex] = 2;
        TextToDisplay = "2";
    }
    
    bool TextHasChanged = PriorTextToDisplay != TextToDisplay;

    if (!TextHasChanged && !sc.IsFullRecalculation && !sc.LastCallToFunction)
        return;

    PriorTextToDisplay = TextToDisplay;

    //Subgraph_StudyReference[0] = TextToDisplay;
    //sc.AddAndManageSingleTextDrawingForStudy()

    sc.AddAndManageSingleTextUserDrawnDrawingForStudy(
        sc,
        false,
        1,
        1,
        Subgraph_StudyReference,//Subgraph_SymbolText, 
        1,// TransparentLabelBackground, 
        TextToDisplay,
        1,
        1);//DrawAboveMainPriceGraph, LockDrawing);*/
}
