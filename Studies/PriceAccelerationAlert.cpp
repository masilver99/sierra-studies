#include "sierrachart.h"
#include <deque>

SCDLLName("Price Acceleration Alert")

namespace
{
    enum InputIds
    {
        INPUT_TRADES_REQUIRED = 0,
        INPUT_TIME_WINDOW_SECONDS = 1,
        INPUT_ALERT_NUMBER = 2
    };

    enum SubgraphIds
    {
        SG_UP_SIGNAL = 0,
        SG_DOWN_SIGNAL = 1
    };

    struct AccelState
    {
        int LastSequence = 0;
        float LastPrice = 0.0f;
        int CurrentDirection = 0;
        std::deque<SCDateTime> StreakTimes;
    };
}

SCSFExport scsf_PriceAccelerationAlert(SCStudyInterfaceRef sc)
{
    SCSubgraphRef UpSignal = sc.Subgraph[SG_UP_SIGNAL];
    SCSubgraphRef DownSignal = sc.Subgraph[SG_DOWN_SIGNAL];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Price Acceleration Alert";
        sc.StudyDescription = "Alerts when a configurable number of trades occur in the same direction within a specified time window.";
        sc.AutoLoop = 0;
        sc.GraphRegion = 0;
        sc.UpdateAlways = 1;
        sc.MaintainTradeStatisticsAndTradesData = true;

        UpSignal.Name = "Up Acceleration";
        UpSignal.DrawStyle = DRAWSTYLE_ARROW_UP;
        UpSignal.PrimaryColor = RGB(0, 255, 0);
        UpSignal.LineWidth = 2;
        UpSignal.DrawZeros = false;

        DownSignal.Name = "Down Acceleration";
        DownSignal.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        DownSignal.PrimaryColor = RGB(255, 0, 0);
        DownSignal.LineWidth = 2;
        DownSignal.DrawZeros = false;

        sc.Input[INPUT_TRADES_REQUIRED].Name = "Trades Required";
        sc.Input[INPUT_TRADES_REQUIRED].SetInt(3);
        sc.Input[INPUT_TRADES_REQUIRED].SetIntLimits(1, 100);

        sc.Input[INPUT_TIME_WINDOW_SECONDS].Name = "Time Window (seconds)";
        sc.Input[INPUT_TIME_WINDOW_SECONDS].SetFloat(1.0f);
        sc.Input[INPUT_TIME_WINDOW_SECONDS].SetFloatLimits(0.05f, 10.0f);

        sc.Input[INPUT_ALERT_NUMBER].Name = "Alert Number";
        sc.Input[INPUT_ALERT_NUMBER].SetInt(1);
        sc.Input[INPUT_ALERT_NUMBER].SetIntLimits(1, 100);

        return;
    }

    AccelState* state = static_cast<AccelState*>(sc.GetPersistentPointer(1));
    if (state == nullptr)
    {
        state = new AccelState();
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

    SCTimeAndSalesArray timeSales;
    sc.GetTimeAndSales(timeSales);

    const int tsSize = timeSales.GetArraySize();
    if (tsSize == 0)
    {
        return;
    }

    if (state->LastSequence == 0)
    {
        state->LastSequence = timeSales[tsSize - 1].Sequence;
        state->LastPrice = timeSales[tsSize - 1].Price;
        return;
    }

    const int tradesRequired = sc.Input[INPUT_TRADES_REQUIRED].GetInt();
    const double timeWindowSeconds = sc.Input[INPUT_TIME_WINDOW_SECONDS].GetFloat();
    const int alertNumber = sc.Input[INPUT_ALERT_NUMBER].GetInt();

    const int index = sc.ArraySize - 1;
    UpSignal[index] = 0.0f;
    DownSignal[index] = 0.0f;

    for (int i = 0; i < tsSize; ++i)
    {
        const s_TimeAndSales& rec = timeSales[i];
        if (rec.Sequence <= state->LastSequence)
        {
            continue;
        }

        if (rec.Type != SC_TS_ASK && rec.Type != SC_TS_BID && rec.Type != SC_TS_TRADE)
        {
            state->LastSequence = rec.Sequence;
            continue;
        }

        int direction = 0;
        if (rec.Type == SC_TS_ASK)
        {
            direction = 1;
        }
        else if (rec.Type == SC_TS_BID)
        {
            direction = -1;
        }
        else
        {
            if (state->LastPrice == 0.0f)
            {
                state->LastPrice = rec.Price;
                state->LastSequence = rec.Sequence;
                continue;
            }

            if (rec.Price > state->LastPrice)
            {
                direction = 1;
            }
            else if (rec.Price < state->LastPrice)
            {
                direction = -1;
            }
        }

        if (direction == 0)
        {
            state->LastPrice = rec.Price;
            state->LastSequence = rec.Sequence;
            continue;
        }

        if (direction != state->CurrentDirection)
        {
            state->CurrentDirection = direction;
            state->StreakTimes.clear();
        }

        state->StreakTimes.push_back(rec.DateTime);

        while (!state->StreakTimes.empty())
        {
            const double elapsedSeconds = (rec.DateTime - state->StreakTimes.front()) * 86400.0;
            if (elapsedSeconds <= timeWindowSeconds)
            {
                break;
            }
            state->StreakTimes.pop_front();
        }

        if (static_cast<int>(state->StreakTimes.size()) == tradesRequired)
        {
            SCString message;
            message.Format("Price acceleration %s: %d trades within %.3f sec at %.2f",
                direction > 0 ? "UP" : "DOWN",
                tradesRequired,
                timeWindowSeconds,
                rec.Price);
            sc.SetAlert(alertNumber, message);

            if (direction > 0)
            {
                UpSignal[index] = rec.Price;
            }
            else
            {
                DownSignal[index] = rec.Price;
            }
        }

        state->LastPrice = rec.Price;
        state->LastSequence = rec.Sequence;
    }
}
