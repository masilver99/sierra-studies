#include "sierrachart.h"
#include "scstructures.h"
#include "scconstants.h"
#include <deque>
#include <cstdint>

SCDLLName("Price Speed Color")

namespace
{
    enum InputIds
    {
        INPUT_TICKS_REQUIRED = 0,
        INPUT_TIME_WINDOW_SECONDS = 1,
        INPUT_ALERT_NUMBER = 2
    };

    enum SubgraphIds
    {
        SG_UP_SPEED = 0,
        SG_DOWN_SPEED = 1
    };

    struct SpeedSample
    {
        SCDateTimeMS Time;
        float Price = 0.0f;
    };

    struct SpeedState
    {
        uint32_t LastSequence = 0;
        std::deque<SpeedSample> Samples;
    };
}

SCSFExport scsf_PriceSpeedColor(SCStudyInterfaceRef sc)
{
    SCSubgraphRef UpSpeed = sc.Subgraph[SG_UP_SPEED];
    SCSubgraphRef DownSpeed = sc.Subgraph[SG_DOWN_SPEED];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Price Speed Color";
        sc.StudyDescription = "Colors bars when price moves a configured number of ticks within a configured number of seconds.";
        sc.AutoLoop = 0;
        sc.GraphRegion = 0;
        sc.UpdateAlways = 1;
        sc.MaintainTradeStatisticsAndTradesData = true;

        UpSpeed.Name = "Up Speed";
        UpSpeed.DrawStyle = DRAWSTYLE_COLOR_BAR;
        UpSpeed.PrimaryColor = RGB(0, 200, 0);
        UpSpeed.DrawZeros = false;

        DownSpeed.Name = "Down Speed";
        DownSpeed.DrawStyle = DRAWSTYLE_COLOR_BAR;
        DownSpeed.PrimaryColor = RGB(220, 0, 0);
        DownSpeed.DrawZeros = false;

        sc.Input[INPUT_TICKS_REQUIRED].Name = "Ticks Required";
        sc.Input[INPUT_TICKS_REQUIRED].SetInt(8);
        sc.Input[INPUT_TICKS_REQUIRED].SetIntLimits(1, 1000);

        sc.Input[INPUT_TIME_WINDOW_SECONDS].Name = "Time Window (seconds)";
        sc.Input[INPUT_TIME_WINDOW_SECONDS].SetFloat(2.0f);
        sc.Input[INPUT_TIME_WINDOW_SECONDS].SetFloatLimits(0.05f, 600.0f);

        sc.Input[INPUT_ALERT_NUMBER].Name = "Alert Number (0 = disabled)";
        sc.Input[INPUT_ALERT_NUMBER].SetInt(0);
        sc.Input[INPUT_ALERT_NUMBER].SetIntLimits(0, 150);

        return;
    }

    SpeedState* state = static_cast<SpeedState*>(sc.GetPersistentPointer(1));
    if (state == nullptr)
    {
        state = new SpeedState();
        sc.SetPersistentPointer(1, state);
    }

    if (sc.LastCallToFunction)
    {
        if (state != nullptr)
        {
            delete state;
            sc.SetPersistentPointer(1, nullptr);
        }
        return;
    }

    if (sc.IsFullRecalculation && sc.UpdateStartIndex == 0)
    {
        state->LastSequence = 0;
        state->Samples.clear();

        for (int i = 0; i < sc.ArraySize; ++i)
        {
            UpSpeed[i] = 0.0f;
            DownSpeed[i] = 0.0f;
        }
    }

    if (sc.TickSize <= 0.0f)
    {
        return;
    }

    const int ticksRequired = sc.Input[INPUT_TICKS_REQUIRED].GetInt();
    const double timeWindowSeconds = sc.Input[INPUT_TIME_WINDOW_SECONDS].GetFloat();
    const int alertNumber = sc.Input[INPUT_ALERT_NUMBER].GetInt();

    c_SCTimeAndSalesArray timeSales;
    sc.GetTimeAndSales(timeSales);

    const int tsSize = timeSales.Size();
    if (tsSize == 0)
    {
        return;
    }

    if (state->LastSequence == 0)
    {
        state->LastSequence = timeSales[tsSize - 1].Sequence;
        return;
    }

    for (int i = 0; i < tsSize; ++i)
    {
        const s_TimeAndSales& rec = timeSales[i];
        if (rec.Sequence <= state->LastSequence)
            continue;

        state->LastSequence = rec.Sequence;

        if (rec.Type != SC_TS_ASK && rec.Type != SC_TS_BID)
            continue;

        state->Samples.push_back({ rec.DateTime, rec.Price });

        while (!state->Samples.empty())
        {
            const double elapsedSeconds = rec.DateTime.ToUNIXTimeWithMillisecondsFraction()
                - state->Samples.front().Time.ToUNIXTimeWithMillisecondsFraction();
            if (elapsedSeconds <= timeWindowSeconds)
                break;

            state->Samples.pop_front();
        }

        if (state->Samples.size() < 2)
            continue;

        const float priceMove = rec.Price - state->Samples.front().Price;
        const float ticksMove = priceMove / sc.TickSize;

        int barIndex = sc.ArraySize - 1;
        while (barIndex > 0 && sc.BaseDateTimeIn[barIndex] > rec.DateTime)
            --barIndex;

        if (ticksMove >= static_cast<float>(ticksRequired))
        {
            UpSpeed[barIndex] = sc.Close[barIndex];
            DownSpeed[barIndex] = 0.0f;

            if (alertNumber > 0)
            {
                SCString message;
                message.Format("Price speed UP: moved %.1f ticks within %.3f sec", ticksMove, timeWindowSeconds);
                sc.SetAlert(alertNumber, message);
            }
        }
        else if (ticksMove <= -static_cast<float>(ticksRequired))
        {
            DownSpeed[barIndex] = sc.Close[barIndex];
            UpSpeed[barIndex] = 0.0f;

            if (alertNumber > 0)
            {
                SCString message;
                message.Format("Price speed DOWN: moved %.1f ticks within %.3f sec", -ticksMove, timeWindowSeconds);
                sc.SetAlert(alertNumber, message);
            }
        }
    }
}
