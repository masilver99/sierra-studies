#include "sierrachart.h"

SCDLLName("DailySnapshots");

// To use this study to generate snapshots of daily charts, add this study to a chartbook and use the Chart | Replay Chart menu 
// command to replay the chart.  Use th fastest replay speed.
// Other methods won't generate new snapshots each day, only the last day repeated x number of times.
// This study will save a snapshot of the chart to the C:\SierraChart\Images folder each day at 4:10 PM.

SCSFExport scsf_DailySnapshots(SCStudyInterfaceRef sc)
{

    if (sc.SetDefaults)
    {
        sc.GraphName = "Snapshot Daily Charts";
        sc.StudyDescription = "";
        sc.AutoLoop = 1;
        sc.UpdateAlways = 1; 
		sc.GraphRegion = 1;
		sc.ValueFormat = VALUEFORMAT_INHERITED;
		return;
    }


	int& LastBarIndexProcessed = sc.GetPersistentInt(1);
	//int& BuyOrderId = sc.GetPersistentInt(2);
	//int& SellOrderId = sc.GetPersistentInt(3);

	//Check if we have an open order, if not
	// then check if we have a filled order and active position, if not
	// clear order ids and start looking for inside bars again
	//Check if we have an open position

	if (sc.Index == 0 || sc.Index == LastBarIndexProcessed)
	{
		return;
	}
	else
	{
	}

	//sc.AddMessageToLog("New Bar!", 1);
	
	int Hour = sc.CurrentDateTimeForReplay.GetHour();
	int Minute = sc.CurrentDateTimeForReplay.GetMinute();
	int Second = sc.CurrentDateTimeForReplay.GetSecond();
	
	SCDateTime TimeToCheckFor;

	//The first step is to get the current date.
	int CurrentDate = sc.BaseDateTimeIn[sc.ArraySize - 1].GetDate();

	//Apply the time. For this example we will use 12 PM
	TimeToCheckFor.SetDate(CurrentDate);
	TimeToCheckFor.SetTimeHMS(16, 10, 0);

	// TimeToCheckFor is contained within the current bar.
	if (sc.IsDateTimeContainedInBarIndex(TimeToCheckFor, sc.Index))
	{
	
	//if (Hour == 16 && Minute == 10 && Second == 0)
	//{
		
		int Day = sc.BaseDateTimeIn[sc.Index].GetDay();
		int Month = sc.BaseDateTimeIn[sc.Index].GetMonth();
		int Year = sc.BaseDateTimeIn[sc.Index].GetYear();
		//sc.SaveChartImageToFile = 1;

		SCString Buffer;
		SCString Message;

		Buffer.Format("C:\\SierraChart\\Images\\ES-%d-%d-%d.%d%d%d.PNG", Year, Month, Day, Hour, Minute, Second);
		Message.Format("Saving to file: %s", Buffer.GetChars());
		sc.AddMessageToLog(Message, 1);
		sc.SaveChartImageToFileExtended(sc.ChartNumber, Buffer, 0, 0, 0);
        //sc.SaveChartImageToFile = 1;
		LastBarIndexProcessed = sc.Index;
        return;

	}
}