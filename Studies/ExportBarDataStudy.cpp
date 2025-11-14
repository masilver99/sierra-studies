#include "sierrachart.h"
#include <fstream>
#include <vector>
#include <string>
#include <ctime>

SCDLLName("Export Bar Data");

SCSFExport scsf_ExportBarDataToCSV(SCStudyInterfaceRef sc)
{
    // Declare configuration variables
    SCInputRef InputFilePath = sc.Input[0];
    SCInputRef InputExportNow = sc.Input[1];
    
    if (sc.SetDefaults)
    {
        sc.GraphName = "Export Bar Data to CSV";
        sc.StudyDescription = "Exports OHLC bar data to a CSV file";
        
        // Configure inputs
        InputFilePath.Name = "File Path";
        InputFilePath.SetString("C:\\SierraChart\\Data\\BarData_1m_Export.csv"); // Changed to csv extension
        
        sc.AutoLoop = 0;  // Manual looping
        sc.GraphRegion = 0;
        sc.UpdateAlways = 1;
        
        return;
    }
    
    // Get the filename from input
    const char* filePath = InputFilePath.GetString();
    
    // Get bar data
    int barCount = sc.ArraySize;
    
    // Open file for writing (text mode, not binary)
    std::ofstream outFile(filePath);
    if (!outFile.is_open()) {
        sc.AddMessageToLog("Failed to open output file", 1);
        return;
    }
    
    // Write CSV header
    outFile << "Date,Time,Open,High,Low,Close,Volume\n";
    
    if (!sc.IsFullRecalculation)
        return;

       // Write bar data as CSV
       for (int i = 0; i < barCount; ++i)
       {
           SCDateTime dateTime = sc.BaseDateTimeIn[i];
           
           // Format the date/time as string using the correct method
           int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
           dateTime.GetDateTimeYMDHMS(year, month, day, hour, minute, second);
           char dateString[64];
           char timeString[64];
           snprintf(dateString, sizeof(dateString), "%04d-%02d-%02d", 
                    year, month, day);
           snprintf(timeString, sizeof(timeString), "%02d:%02d", 
                    hour, minute);
           
           // Write the data as CSV row
           outFile << dateString << ","
                   << timeString << ","
                   << sc.Open[i] << ","
                   << sc.High[i] << ","
                   << sc.Low[i] << ","
                   << sc.Close[i] << ","
                   << sc.Volume[i] << "\n";
       }
    
    outFile.close();
    
    // Generate timestamp for log
    time_t now = time(nullptr);
    tm localTime;
    localtime_s(&localTime, &now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &localTime);
    
    // Log success
    std::string message = "Exported ";
    message += std::to_string(barCount);
    message += " bars to CSV file: ";
    message += filePath;
    message += " at ";
    message += timestamp;
    
    sc.AddMessageToLog(message.c_str(), 1);
}
