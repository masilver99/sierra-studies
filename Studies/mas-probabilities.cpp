#include "sierrachart.h"
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>
// #include "includes\\iso_week.h"
#include <ctime>

// Function to calculate the number of days from the new year
int days_from_new_year(const SCDateTime &tm)
{
    SCDateTime yearStart;
    yearStart.SetDateTimeYMDHMS(tm.GetYear(), 1, 1, 0, 0, 0);
    double seconds = tm.GetTimeInSeconds() - yearStart.GetTimeInSeconds();
    return static_cast<int>(seconds / (24 * 60 * 60));
}

int week_number(const SCDateTime &tm)
{

    constexpr int DAYS_PER_WEEK = 7;

    const int wday = tm.GetDayOfWeek();
    const int delta = wday ? wday - 1 : DAYS_PER_WEEK - 1;
    SCDateTime yearStart; // Locally defined SCDateTime variable.

    // MySCDateTime will contain, with this function call, the specified date time.
    yearStart.SetDateTimeYMDHMS(tm.GetYear(), 1, 1, 0, 0, 0);

    double seconds = tm.GetTimeInSeconds() - yearStart.GetTimeInSeconds();
    int days = static_cast<int>(seconds / (24 * 60 * 60));

    return (days + DAYS_PER_WEEK - delta) / DAYS_PER_WEEK;
}

SCDLLName("MAS Probabilities");

void SaveBarStatsToFile(std::vector<struct BarStats> barStatsVector, SCStudyInterfaceRef sc);
int GetStratNumber(int index, SCStudyInterfaceRef sc);
int GetBarColor(int index, SCStudyInterfaceRef sc);
float GetBodyToBarRatio(int index, SCStudyInterfaceRef sc);

const int MAX_STAT_BARS = 5;
std::ofstream file;

// Storage for pattern matching
// Keep in mind, the current bar is the bar we will be predicting.
// We will be looking at the previous bar to determine the pattern.
struct BarStats
{
    // What type of bar? 1, 2 (up), 3 or 4 (down)
    int barStratNo[MAX_STAT_BARS];

    // Standard Deviation of the bar size compared to ATR5
    float sdAtr5[MAX_STAT_BARS];

    // <25, 25-50, 50-75,  50 or 75 of the bar is a body
    // store the ratio, but we will group later.
    float bodyToBarRatio[MAX_STAT_BARS];

    float bodySize[MAX_STAT_BARS]; // How big is the body

    float bodySizePercent[MAX_STAT_BARS]; // How big is the body (use percents) 

    float topWickSize[MAX_STAT_BARS]; // How big is the top wick

    float topWickSizePercent[MAX_STAT_BARS]; // How big is the top wick (use percents)  

    float bottomWickSize[MAX_STAT_BARS]; // How big is the bottom wick

    float bottomWickSizePercent[MAX_STAT_BARS]; // How big is the bottom wick (use percents)

    // Negative gap size (if any) from last close to current low
    // Need to think about how best to reprenet.  Maybe an absolute value

    float barSize[MAX_STAT_BARS]; // How big is the bar

    int barColor[MAX_STAT_BARS]; // 0 = green, 1 = red, 2 = doji

    float sdEma20[MAX_STAT_BARS]; // How many SDs above or below 20EMA

    float sdEma50[MAX_STAT_BARS]; // How many SDs above or below 50EMA

    float sdEma200[MAX_STAT_BARS]; // How many SDs above or below 200EMA

    float Volume[MAX_STAT_BARS]; // How much volume was there

    float sdVolumeMA[MAX_STAT_BARS]; // How much volume was there (use percents)

    float bidVolumePercent[MAX_STAT_BARS];
    float askVolumePercent[MAX_STAT_BARS];

    SCString timeOfOpen; // Time of day of ANALYSIS BAR
    float openCloseDif;  // How much did the open and close differ, > 0 green bar, <0 red bar
    float openLow;       // How much did the open differ from the low
    float openHigh;      // How much did the open differ from the high

    int dayOfWeek;  // 0 = Sunday, 6 = Saturday
    int month;      // 1 = January, 12 = December
    int weekOfYear; // 1-52

    // We'll be using these for probability calculations
    float lowFromClose;
    float highFromClose; 
    float closeFromClose;

    // Gaps (array)
    // Gap from last high to low
    // Gap from last high to high
    // Gap from last close to low
    // Gap from last close to high
    // Grp from Close to last close
    // Gap from last close to current high

    // Current high from close in pts
    // Current low from cloase in pts
};

std::vector<struct BarStats> barStatsVector;
int replayRunning = 0;

// Is last bar an inside bar
// Is last bar dogi or strong? less or > 50% body
// Is last bar green or red?
// How many SDs did last bar close above or below 20EMA?
// How many SDs was last bar above ATR3?
// What was the time of day?
//  8-9:30 -> Pre-Market
//  9:30-9:45 -> Open
//  9:45-11:30 -> Morning
//  11:30-1:30 -> Lunch
//  1:30-3:00 -> Afternoon

SCSFExport scsf_ProbabilityDataScraper(SCStudyInterfaceRef sc)
{
    // SubGraphs
    SCSubgraphRef Subgraph_ATR3 = sc.Subgraph[0];
    SCSubgraphRef Subgraph_EMA20 = sc.Subgraph[1];
    SCSubgraphRef Subgraph_EMA50 = sc.Subgraph[2];
    SCSubgraphRef Subgraph_EMA200 = sc.Subgraph[3];
    SCSubgraphRef Subgraph_Volume = sc.Subgraph[4];
    SCSubgraphRef Subgraph_VolumeMA = sc.Subgraph[5];

    if (sc.SetDefaults)
    {
        sc.AutoLoop = 1; // true
        sc.GraphName = "Probability Data Scraper";
        sc.StudyDescription = "Pulls counts based on Calculates probabilities based on historical candlestick patterns";

        // Subgraph_ATR3.DrawStyle = IGNORE;
        // Subgraph_ATR3.Name = "ATR";

        // Initialize pattern database on first update
        barStatsVector.clear();

        return;
    }

    if (sc.IsReplayRunning() == 1 && replayRunning == 0)
    {
        replayRunning = 1;
        sc.AddMessageToLog("Start", 1);
        barStatsVector.clear();
    }

    if (sc.IsReplayRunning() == 0 && replayRunning == 1)
    {
        replayRunning = 0;
        sc.AddMessageToLog("End", 1);
        SaveBarStatsToFile(barStatsVector, sc);
        barStatsVector.clear();

    }


    if (sc.IsFullRecalculation || sc.DownloadingHistoricalData)
    {
        return;
    }

    // Study is done running
    if (sc.Index == sc.ArraySize - 1 && !file.is_open())
    {
        SCString message;
        sc.AddMessageToLog(message, 1);
        //SaveBarStatsToFile(barStatsVector, sc);
    }

    if (sc.GetBarHasClosedStatus() == BHCS_BAR_HAS_NOT_CLOSED)
    {
        return;
    }

    // Exit if bar has not closed
    // if (sc.Index >= sc.ArraySize - 1)
    {
        //    return;
    }

    if (sc.Index <= MAX_STAT_BARS)
    {
        return;
    }

    sc.ExponentialMovAvg(sc.Close, Subgraph_EMA20, 20);
    sc.ExponentialMovAvg(sc.Close, Subgraph_EMA50, 50);
    sc.ExponentialMovAvg(sc.Close, Subgraph_EMA200, 200);
    sc.ATR(sc.BaseDataIn, Subgraph_ATR3, 3, MOVAVGTYPE_SIMPLE);
    sc.SimpleMovAvg(sc.Volume, Subgraph_VolumeMA, 20);

    int analysisBar = sc.Index - 1;
    float TrueRange = Subgraph_ATR3[analysisBar];
    float EMA20 = Subgraph_EMA20[analysisBar];
    float EMA50 = Subgraph_EMA50[analysisBar];
    float EMA200 = Subgraph_EMA200[analysisBar];
    float VolumeMA = Subgraph_VolumeMA[analysisBar];

    SCString log;
    log.Format("ATR: %f, EMA20: %f, EMA50: %f, EMA200: %f, VolumeMA: %f", TrueRange, EMA20, EMA50, EMA200, VolumeMA);
    sc.AddMessageToLog(log, 1);

    // Calculate bar stats
    struct BarStats barStats;

    for (int i = 0; i < MAX_STAT_BARS; i++)
    {
        barStats.barStratNo[i] = GetStratNumber(analysisBar - i, sc);
        barStats.bodyToBarRatio[i] = GetBodyToBarRatio(analysisBar - i, sc);
        barStats.bodySize[i] = std::abs(sc.Close[analysisBar - i] - sc.Open[analysisBar - i]);
        barStats.barSize[i] = sc.High[analysisBar - i] - sc.Low[analysisBar - i];
        barStats.barColor[i] = GetBarColor(analysisBar - i, sc);
        barStats.sdEma20[i] = (sc.Close[analysisBar - i] - Subgraph_EMA20[analysisBar - i]);
        barStats.sdEma50[i] = (sc.Close[analysisBar - i] - Subgraph_EMA50[analysisBar - i]);
        barStats.sdEma200[i] = (sc.Close[analysisBar - i] - Subgraph_EMA200[analysisBar - i]);
        barStats.Volume[i] = sc.Volume[analysisBar - i];
        barStats.sdVolumeMA[i] = (sc.Volume[analysisBar - i] - Subgraph_VolumeMA[analysisBar - i]);
        barStats.sdAtr5[i] = (sc.High[analysisBar - i] - sc.Low[analysisBar - i]) / TrueRange;
        barStats.bidVolumePercent[i] = sc.BidVolume[analysisBar - i] / sc.Volume[analysisBar - i];
        barStats.askVolumePercent[i] = sc.AskVolume[analysisBar - i] / sc.Volume[analysisBar - i];
    }
    // What percentage of the ATR is the current bar 0+ = bigger, 0- = smaller

    barStats.timeOfOpen = sc.DateTimeToString(sc.BaseDateTimeIn[analysisBar], FLAG_DT_COMPLETE_DATETIME);
    barStats.dayOfWeek = sc.BaseDateTimeIn[analysisBar].GetDayOfWeek();
    barStats.month = sc.BaseDateTimeIn[analysisBar].GetMonth();
    barStats.weekOfYear = week_number(sc.BaseDateTimeIn[analysisBar]);
    int daysFromNewYear = days_from_new_year(sc.BaseDateTimeIn[analysisBar]);

    barStats.openCloseDif = sc.Close[analysisBar] - sc.Open[analysisBar];
    barStats.openLow = sc.Open[analysisBar] - sc.Low[analysisBar];
    barStats.openHigh = sc.Open[analysisBar] - sc.High[analysisBar];

    barStats.lowFromClose = sc.Close[sc.Index]  - sc.Low[analysisBar];
    barStats.highFromClose = sc.Close[sc.Index] - sc.High[analysisBar];
    barStats.closeFromClose = sc.Close[sc.Index] - sc.Close[analysisBar];

    barStatsVector.push_back(barStats);
}

void SaveBarStatsToFile(std::vector<struct BarStats> barStatsVector, SCStudyInterfaceRef sc)
{
    file.open("c:\\temp\\barStats.csv", std::ios::trunc);
    if (!file)
    {
        SCString message = "Could not create file at C:\\temp\\barStats.csv";
        return;
    }

    // Write header
    file << "TimeOfOpen,DayOfWeek,Month,WeekOfYear,";

    // Arrays with indices
    for (int i = 0; i < MAX_STAT_BARS; i++)
    {
        file << "BarStratNo" << i << ",";
    }

    for (int i = 0; i < MAX_STAT_BARS; i++)
    {
        file << "SdAtr5_" << i << ",";
    }

    for (int i = 0; i < MAX_STAT_BARS; i++)
    {
        file << "BodyToBarRatio" << i << ",";
    }

    for (int i = 0; i < MAX_STAT_BARS; i++)
    {
        file << "BodySize" << i << ",";
    }

    for (int i = 0; i < MAX_STAT_BARS; i++)
    {
        file << "BarSize" << i << ",";
    }

    for (int i = 0; i < MAX_STAT_BARS; i++)
    {
        file << "BarColor" << i << ",";
    }

    for (int i = 0; i < MAX_STAT_BARS; i++)
    {
        file << "SdEma20_" << i << ",";
    }

    for (int i = 0; i < MAX_STAT_BARS; i++)
    {
        file << "SdEma50_" << i << ",";
    }

    for (int i = 0; i < MAX_STAT_BARS; i++)
    {
        file << "SdEma200_" << i << ",";
    }

    for (int i = 0; i < MAX_STAT_BARS; i++)
    {
        file << "Volume" << i << ",";
    }

    for (int i = 0; i < MAX_STAT_BARS; i++)
    {
        file << "SdVolumeMA" << i << ",";
    }

    for (int i = 0; i < MAX_STAT_BARS; i++)
    {
        file << "BidVolumePercent" << i << ",";
    }

    for (int i = 0; i < MAX_STAT_BARS; i++)
    {
        file << "AskVolumePercent" << i << ",";
    }

    // Scalar fields
    file << "OpenCloseDif,OpenLow,OpenHigh,LowFromClose,HighlowFromClose,CloseFromClose\n";

    // Write data
    for (const auto &barStats : barStatsVector)
    {
        file << barStats.timeOfOpen << ","
             << barStats.dayOfWeek << ","
             << barStats.month << ","
             << barStats.weekOfYear << ",";

        // Arrays with indices
        for (int i = 0; i < MAX_STAT_BARS; i++)
        {
            file << barStats.barStratNo[i] << ",";
        }

        for (int i = 0; i < MAX_STAT_BARS; i++)
        {
            file << barStats.sdAtr5[i] << ",";
        }

        for (int i = 0; i < MAX_STAT_BARS; i++)
        {
            file << barStats.bodyToBarRatio[i] << ",";
        }

        for (int i = 0; i < MAX_STAT_BARS; i++)
        {
            file << barStats.bodySize[i] << ",";
        }

        for (int i = 0; i < MAX_STAT_BARS; i++)
        {
            file << barStats.barSize[i] << ",";
        }

        for (int i = 0; i < MAX_STAT_BARS; i++)
        {
            file << barStats.barColor[i] << ",";
        }

        for (int i = 0; i < MAX_STAT_BARS; i++)
        {
            file << barStats.sdEma20[i] << ",";
        }

        for (int i = 0; i < MAX_STAT_BARS; i++)
        {
            file << barStats.sdEma50[i] << ",";
        }

        for (int i = 0; i < MAX_STAT_BARS; i++)
        {
            file << barStats.sdEma200[i] << ",";
        }

        for (int i = 0; i < MAX_STAT_BARS; i++)
        {
            file << barStats.Volume[i] << ",";
        }

        for (int i = 0; i < MAX_STAT_BARS; i++)
        {
            file << barStats.sdVolumeMA[i] << ",";
        }

        for (int i = 0; i < MAX_STAT_BARS; i++)
        {
            file << barStats.bidVolumePercent[i] << ",";
        }

        for (int i = 0; i < MAX_STAT_BARS; i++)
        {
            file << barStats.askVolumePercent[i] << ",";
        }

        // Scalar fields
        file << barStats.openCloseDif << ","
             << barStats.openLow << ","
             << barStats.openHigh << ","
             << barStats.lowFromClose << ","
             << barStats.highFromClose << ","
             << barStats.closeFromClose << "\n";
    }
    file.close();
}

int GetStratNumber(int index, SCStudyInterfaceRef sc)
{
    // Check if the bar is an inside bar
    if (sc.High[index] <= sc.High[index - 1] && sc.Low[index] >= sc.Low[index - 1])
    {
        return 1; // Inside bar
    }

    // Check if the bar is an outside bar
    if (sc.High[index] >= sc.High[index - 1] && sc.Low[index] <= sc.Low[index - 1])
    {
        return 3; // Outside bar
    }

    // Check if the bar's high is above the previous bar's high
    if (sc.High[index] > sc.High[index - 1])
    {
        return 2; // High is above previous bar's high
    }

    // Check if the bar's close is below the previous bar's close
    if (sc.Low[index] < sc.Low[index - 1])
    {
        return 4; // Close is below previous bar's close
    }

    return 0;
}

float GetBodyToBarRatio(int index, SCStudyInterfaceRef sc)
{
    return std::abs(sc.Close[index] - sc.Open[index]) / (sc.High[index] - sc.Low[index]);
}

int GetBarColor(int index, SCStudyInterfaceRef sc)
{
    if (sc.Close[index] > sc.Open[index])
    {
        return 0; // Green bar
    }

    if (sc.Close[index] < sc.Open[index])
    {
        return 1; // Red bar
    }

    return 2; // Doji bar
}