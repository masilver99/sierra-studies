#include "sierrachart.h"

SCDLLName("HH/LL Count Since Time");

SCSFExport scsf_HHLLCountSinceTime(SCStudyInterfaceRef sc)
{
	SCSubgraphRef Subgraph_HigherHighCount = sc.Subgraph[0];
	SCSubgraphRef Subgraph_LowerLowCount = sc.Subgraph[1];

	SCInputRef Input_StartTime = sc.Input[0];

	if (sc.SetDefaults)
	{
		sc.GraphName = "HH/LL Count Since Time";
		sc.StudyDescription = "Counts new higher highs and lower lows starting at a chosen time each day.";
		sc.AutoLoop = 1;
		sc.GraphRegion = 1;
		sc.ValueFormat = VALUEFORMAT_INHERITED;

		Subgraph_HigherHighCount.Name = "Higher High Count";
		Subgraph_HigherHighCount.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_HigherHighCount.PrimaryColor = RGB(0, 128, 255);
		Subgraph_HigherHighCount.DrawZeros = false;

		Subgraph_LowerLowCount.Name = "Lower Low Count";
		Subgraph_LowerLowCount.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_LowerLowCount.PrimaryColor = RGB(255, 64, 64);
		Subgraph_LowerLowCount.DrawZeros = false;

		Input_StartTime.Name = "Start Time (HH:MM:SS)";
		Input_StartTime.SetTime(HMS_TIME(9, 30, 0));

		return;
	}

	const int startTime = Input_StartTime.GetTime();
	const int currentTime = sc.BaseDateTimeIn[sc.Index].GetTime();
	const int currentDate = sc.BaseDateTimeIn[sc.Index].GetDate();

	int& higherHighCount = sc.GetPersistentInt(1);
	int& lowerLowCount = sc.GetPersistentInt(2);

	const bool crossedStart =
		currentTime >= startTime &&
		(
			sc.Index == 0 ||
			sc.BaseDateTimeIn[sc.Index - 1].GetDate() != currentDate ||
			sc.BaseDateTimeIn[sc.Index - 1].GetTime() < startTime
		);

	if (sc.Index == 0 || crossedStart)
	{
		higherHighCount = 0;
		lowerLowCount = 0;
		Subgraph_HigherHighCount[sc.Index] = 0.0f;
		Subgraph_LowerLowCount[sc.Index] = 0.0f;
		return;
	}

	const bool active = currentTime >= startTime;

	if (!active)
	{
		Subgraph_HigherHighCount[sc.Index] = 0.0f;
		Subgraph_LowerLowCount[sc.Index] = 0.0f;
		return;
	}

	const bool prevBarSameDay = sc.Index > 0 && sc.BaseDateTimeIn[sc.Index - 1].GetDate() == currentDate;
	const bool prevBarActive = prevBarSameDay && sc.BaseDateTimeIn[sc.Index - 1].GetTime() >= startTime;

	if (prevBarActive)
	{
		if (sc.High[sc.Index] > sc.High[sc.Index - 1])
		{
			++higherHighCount;
		}

		if (sc.Low[sc.Index] < sc.Low[sc.Index - 1])
		{
			++lowerLowCount;
		}
	}

	Subgraph_HigherHighCount[sc.Index] = static_cast<float>(higherHighCount);
	Subgraph_LowerLowCount[sc.Index] = static_cast<float>(lowerLowCount);
}
 