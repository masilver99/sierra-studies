#include <climits>
#include "sierrachart.h"

SCDLLName("Advance Inside Bar Study")

SCSFExport scsf_AdvancedInsideBar(SCStudyInterfaceRef sc)
{
	SCSubgraphRef Subgraph_IB = sc.Subgraph[0];
	SCInputRef Input_MinInsideBars = sc.Input[0];
	
	// Set configuration variables
	
	if (sc.SetDefaults)
	{
		// Set the configuration and defaults
		
		sc.GraphName = "Inside Bar";
		
		sc.GraphRegion = 0;
		
		Subgraph_IB.Name = "IB";
		Subgraph_IB.DrawStyle = DRAWSTYLE_COLOR_BAR;
		Subgraph_IB.PrimaryColor = RGB(255,128,0);
		Subgraph_IB.DrawZeros = false;
		Input_MinInsideBars.Name = "Min Inside Bars In Row";
		Input_MinInsideBars.SetInt(1);
		Input_MinInsideBars.SetIntLimits(1, INT_MAX);
		
		sc.AutoLoop = 1;
		
		return;
	}
	
	// Array references
	SCFloatArrayRef High = sc.High;
	SCFloatArrayRef Low = sc.Low;

	int& ConsecutiveInsideBars = sc.GetPersistentInt(1);
	const int MinInsideBars = Input_MinInsideBars.GetInt();
	
	
	// Do data processing
	if (sc.Index == 0)
	{
		ConsecutiveInsideBars = 0;
		Subgraph_IB[sc.Index] = 0;
		return;
	}

	const bool IsInside = High[sc.Index] < High[sc.Index - 1] && Low[sc.Index] > Low[sc.Index - 1];

	if (IsInside)
		++ConsecutiveInsideBars;
	else
		ConsecutiveInsideBars = 0;

	if (IsInside && ConsecutiveInsideBars >= MinInsideBars)
		Subgraph_IB[sc.Index] = High[sc.Index];
	else
		Subgraph_IB[sc.Index] = 0;
}