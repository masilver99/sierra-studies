#include "sierrachart.h"
#include "scstructures.h"
#include <vector>
#include <algorithm>
#include <cmath>

SCDLLName("MAE Risk State Background")

// Persistent storage indexes
#define P_GREEN_MAE     1
#define P_YELLOW_MAE    2
#define P_RED_MAE       3
#define P_ENTRY_PRICE  4
#define P_WORST_PRICE  5
#define P_STATE        6
#define P_LAST_QTY     7
#define P_SESSION_DATE 8

enum MAEState
{
    STATE_NONE = 0,
    STATE_GREEN,
    STATE_YELLOW,
    STATE_RED
};

//---------------------------------------------

void ComputeSessionMAEStats(SCStudyInterfaceRef sc,
    SCInputRef GreenPercentile,
    SCInputRef YellowPercentile,
    SCInputRef RedPercentile)
{
    std::vector<double> maes;
    maes.reserve(256);

    const int tradeCount = sc.GetTradeListSize();
    for (int i = 0; i < tradeCount; ++i)
    {
        s_ACSTrade trade;
        sc.GetTradeListEntry(i, trade);

        if (trade.TradeProfitLoss <= 0.0)
            continue;

        const double maeCurrency = fabs(trade.MaximumOpenPositionLoss);
        if (maeCurrency <= 0.0)
            continue;

        if (maeCurrency > 0.0)
            maes.push_back(maeCurrency);
    }

    if (maes.empty())
    {
        sc.SetPersistentDouble(P_GREEN_MAE, 0.0);
        sc.SetPersistentDouble(P_YELLOW_MAE, 0.0);
        sc.SetPersistentDouble(P_RED_MAE, 0.0);
        return;
    }

    std::sort(maes.begin(), maes.end());

    auto percentileValue = [&](double p)
    {
        if (p < 0.0)
            p = 0.0;
        if (p > 1.0)
            p = 1.0;

        if (maes.size() == 1)
            return maes.front();

        const double pos = p * (static_cast<double>(maes.size() - 1));
        const int lo = static_cast<int>(floor(pos));
        const int hi = static_cast<int>(ceil(pos));
        const double weight = pos - lo;
        return maes[lo] + (maes[hi] - maes[lo]) * weight;
    };

    const double green = percentileValue(GreenPercentile.GetFloat());
    const double yellow = percentileValue(YellowPercentile.GetFloat());
    const double red = percentileValue(RedPercentile.GetFloat());

    sc.SetPersistentDouble(P_GREEN_MAE, green);
    sc.SetPersistentDouble(P_YELLOW_MAE, yellow);
    sc.SetPersistentDouble(P_RED_MAE, red);
}

//---------------------------------------------

SCSFExport scsf_MAERiskBackground(SCStudyInterfaceRef sc)
{
    SCInputRef RebuildStats = sc.Input[0];
    SCInputRef GreenPercentile = sc.Input[1];
    SCInputRef YellowPercentile = sc.Input[2];
    SCInputRef RedPercentile = sc.Input[3];

    SCSubgraphRef YellowLong = sc.Subgraph[0];
    SCSubgraphRef RedLong = sc.Subgraph[1];
    SCSubgraphRef YellowShort = sc.Subgraph[2];
    SCSubgraphRef RedShort = sc.Subgraph[3];

    if (sc.SetDefaults)
    {
        sc.GraphName = "MAE Risk State Background";
        sc.AutoLoop = 1;
        sc.GraphRegion = 0;
        sc.MaintainTradeStatisticsAndTradesData = true;

        RebuildStats.Name = "Rebuild MAE Stats";
        RebuildStats.SetYesNo(0);

        GreenPercentile.Name = "Green Percentile";
        GreenPercentile.SetFloat(0.50f);

        YellowPercentile.Name = "Yellow Percentile";
        YellowPercentile.SetFloat(0.75f);

        RedPercentile.Name = "Red Percentile";
        RedPercentile.SetFloat(0.85f);

        YellowLong.Name = "Yellow MAE (Long)";
        YellowLong.DrawStyle = DRAWSTYLE_TRIANGLE_UP;
        YellowLong.PrimaryColor = RGB(220, 180, 0);
        YellowLong.LineWidth = 2;
        YellowLong.DrawZeros = false;

        RedLong.Name = "Red MAE (Long)";
        RedLong.DrawStyle = DRAWSTYLE_TRIANGLE_UP;
        RedLong.PrimaryColor = RGB(160, 0, 0);
        RedLong.LineWidth = 2;
        RedLong.DrawZeros = false;

        YellowShort.Name = "Yellow MAE (Short)";
        YellowShort.DrawStyle = DRAWSTYLE_TRIANGLE_DOWN;
        YellowShort.PrimaryColor = RGB(220, 180, 0);
        YellowShort.LineWidth = 2;
        YellowShort.DrawZeros = false;

        RedShort.Name = "Red MAE (Short)";
        RedShort.DrawStyle = DRAWSTYLE_TRIANGLE_DOWN;
        RedShort.PrimaryColor = RGB(160, 0, 0);
        RedShort.LineWidth = 2;
        RedShort.DrawZeros = false;

        return;
    }

    // -------- SESSION RESET / STATS BUILD --------
    int today = sc.CurrentSystemDateTime.GetDate();
    int lastSession = sc.GetPersistentInt(P_SESSION_DATE);

    if (sc.IsFullRecalculation || RebuildStats.GetYesNo() || today != lastSession)
    {
        ComputeSessionMAEStats(sc, GreenPercentile, YellowPercentile, RedPercentile);
        sc.SetPersistentInt(P_SESSION_DATE, today);
    }

    // -------- POSITION STATE --------
    s_SCPositionData pos;
    sc.GetTradePosition(pos);

    int lastQty = sc.GetPersistentInt(P_LAST_QTY);

    if (pos.PositionQuantity == 0)
    {
        sc.SetPersistentInt(P_STATE, STATE_NONE);
        sc.SetPersistentInt(P_LAST_QTY, 0);
        YellowLong[sc.Index] = 0;
        RedLong[sc.Index] = 0;
        YellowShort[sc.Index] = 0;
        RedShort[sc.Index] = 0;
        return;
    }

    const bool directionFlip = (lastQty > 0 && pos.PositionQuantity < 0)
        || (lastQty < 0 && pos.PositionQuantity > 0);

    // New trade or direction change
    if (lastQty == 0 || directionFlip)
    {
        sc.SetPersistentDouble(P_ENTRY_PRICE, pos.AveragePrice);

        if (pos.PositionQuantity > 0)
            sc.SetPersistentDouble(P_WORST_PRICE, sc.Low[sc.Index]);
        else
            sc.SetPersistentDouble(P_WORST_PRICE, sc.High[sc.Index]);
    }

    sc.SetPersistentInt(P_LAST_QTY, static_cast<int>(pos.PositionQuantity));

    // -------- LIVE MAE TRACKING --------
    double entry = sc.GetPersistentDouble(P_ENTRY_PRICE);
    double worst = sc.GetPersistentDouble(P_WORST_PRICE);

    if (pos.PositionQuantity > 0)
    {
        const double low = sc.Low[sc.Index];
        worst = (low < worst) ? low : worst;
    }
    else
    {
        const double high = sc.High[sc.Index];
        worst = (high > worst) ? high : worst;
    }

    sc.SetPersistentDouble(P_WORST_PRICE, worst);

    // -------- MARKER LEVELS --------
    const double yellowLevel = sc.GetPersistentDouble(P_GREEN_MAE);
    const double redLevel = sc.GetPersistentDouble(P_RED_MAE);

    YellowLong[sc.Index] = 0;
    RedLong[sc.Index] = 0;
    YellowShort[sc.Index] = 0;
    RedShort[sc.Index] = 0;

    const double dollarsToPrice = (sc.CurrencyValuePerTick > 0.0 && sc.TickSize > 0.0)
        ? (sc.TickSize / sc.CurrencyValuePerTick)
        : 0.0;

    if (yellowLevel > 0.0 && dollarsToPrice > 0.0)
    {
        if (pos.PositionQuantity > 0)
            YellowLong[sc.Index] = static_cast<float>(entry - (yellowLevel * dollarsToPrice));
        else
            YellowShort[sc.Index] = static_cast<float>(entry + (yellowLevel * dollarsToPrice));
    }

    if (redLevel > 0.0 && dollarsToPrice > 0.0)
    {
        if (pos.PositionQuantity > 0)
            RedLong[sc.Index] = static_cast<float>(entry - (redLevel * dollarsToPrice));
        else
            RedShort[sc.Index] = static_cast<float>(entry + (redLevel * dollarsToPrice));
    }
}
