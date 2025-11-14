#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "sierrachart.h"
#include <algorithm>
#include <cmath>
#include <vector>

SCDLLName("Trend Strength Score (23-Criteria Proxy)");

// Small helpers
static inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
static inline int signf(float v) { return (v > 0.f) - (v < 0.f); }

// Direction enum proxy
enum class TrendDir : int { None = 0, Bull = 1, Bear = -1 };

// Computes consecutive count where Close stays strictly on one side of EMA
static int CountConsecutiveSideOfEMA(const SCFloatArrayRef& Close, const SCFloatArrayRef& EMA, int start, int step, int maxCount)
{
    int cnt = 0;
    for (int i = start; i >= 0 && i < Close.GetArraySize() && cnt < maxCount; i += step)
    {
        if ((Close[i] > EMA[i] && step < 0) || (Close[i] < EMA[i] && step > 0)) // logic direction depends on iteration sign
            break;
        ++cnt;
    }
    return cnt;
}

// Swing high/low check within window
static bool IsSwingHigh(const SCBaseDataRef& Base, int index, int left, int right)
{
    float h = Base[SC_HIGH][index];
    for (int i = 1; i <= left; ++i) if (Base[SC_HIGH][index - i] >= h) return false;
    for (int i = 1; i <= right; ++i) if (Base[SC_HIGH][index + i] > h) return false;
    return true;
}

static bool IsSwingLow(const SCBaseDataRef& Base, int index, int left, int right)
{
    float l = Base[SC_LOW][index];
    for (int i = 1; i <= left; ++i) if (Base[SC_LOW][index - i] <= l) return false;
    for (int i = 1; i <= right; ++i) if (Base[SC_LOW][index + i] < l) return false;
    return true;
}

// Body overlap between consecutive bars (0 = no overlap, 1 = full overlap) for direction
static float BodyOverlapRatio(const SCBaseDataRef& Base, int i)
{
    const float oc1 = std::min(Base[SC_OPEN][i - 1], Base[SC_CLOSE][i - 1]);
    const float oc2 = std::max(Base[SC_OPEN][i - 1], Base[SC_CLOSE][i - 1]);
    const float nc1 = std::min(Base[SC_OPEN][i],     Base[SC_CLOSE][i]);
    const float nc2 = std::max(Base[SC_OPEN][i],     Base[SC_CLOSE][i]);
    const float body1 = std::max(oc2 - oc1, 0.0f);
    const float body2 = std::max(nc2 - nc1, 0.0f);
    if (body1 <= 0.f || body2 <= 0.f) return 1.f; // treat doji-like as overlapping
    const float left  = std::max(oc1, nc1);
    const float right = std::min(oc2, nc2);
    const float overlap = std::max(right - left, 0.0f);
    const float denom = (body1 + body2) * 0.5f;
    return clamp01(denom > 0.f ? overlap / denom : 1.f);
}

// True body fraction vs range
static float BodyFraction(const SCBaseDataRef& Base, int i)
{
    const float body = std::fabs(Base[SC_CLOSE][i] - Base[SC_OPEN][i]);
    const float range = std::max(Base[SC_HIGH][i] - Base[SC_LOW][i], 0.000001f);
    return clamp01(body / range);
}

// Tail fractions (0..1 of bar range)
static float TopWickFrac(const SCBaseDataRef& Base, int i)
{
    const bool bull = Base[SC_CLOSE][i] >= Base[SC_OPEN][i];
    const float top = Base[SC_HIGH][i] - (bull ? Base[SC_CLOSE][i] : Base[SC_OPEN][i]);
    const float range = std::max(Base[SC_HIGH][i] - Base[SC_LOW][i], 0.000001f);
    return clamp01(top / range);
}

static float BotWickFrac(const SCBaseDataRef& Base, int i)
{
    const bool bull = Base[SC_CLOSE][i] >= Base[SC_OPEN][i];
    const float bot = (bull ? Base[SC_OPEN][i] : Base[SC_CLOSE][i]) - Base[SC_LOW][i];
    const float range = std::max(Base[SC_HIGH][i] - Base[SC_LOW][i], 0.000001f);
    return clamp01(bot / range);
}

// Return micro measuring gap condition around a strong bar
static bool HasMicroMeasuringGapBull(const SCBaseDataRef& Base, int iStrong)
{
    if (iStrong < 1 || iStrong + 1 >= Base[SC_OPEN].GetArraySize()) return false;
    const float prevHigh = Base[SC_HIGH][iStrong - 1];
    const float nextLow  = Base[SC_LOW][iStrong + 1];
    return nextLow >= prevHigh; // gap between body-neighbors
}

static bool HasMicroMeasuringGapBear(const SCBaseDataRef& Base, int iStrong)
{
    if (iStrong < 1 || iStrong + 1 >= Base[SC_OPEN].GetArraySize()) return false;
    const float prevLow = Base[SC_LOW][iStrong - 1];
    const float nextHigh  = Base[SC_HIGH][iStrong + 1];
    return nextHigh <= prevLow;
}

// Compute EMA slope over N bars (sign)
static float EmaSlope(const SCFloatArrayRef& EMA, int i, int slopeLen)
{
    const int j = std::max(0, i - slopeLen);
    const float d = EMA[i] - EMA[j];
    const float n = (float)std::max(1, i - j);
    return d / n;
}

// Tracks a breakout pivot and measures pullback behavior
struct BreakoutState
{
    int PivotIndex = -1;
    float PivotPrice = 0.f;
    bool IsBull = false;
};

SCSFExport scsf_TrendStrength(SCStudyInterfaceRef sc)
{
    // Subgraphs
    SCSubgraphRef SG_Strength = sc.Subgraph[0];
    SCSubgraphRef SG_Direction = sc.Subgraph[1];

    // Diagnostics 2..(2+N-1)
    const int DIAG_BASE = 2;
    const int DIAG_COUNT = 16; // Pack 23 concepts into 16 measurable buckets
    // We’ll map later which bucket approximates which criteria.

    // Inputs
    SCInputRef In_Lookback = sc.Input[0];
    SCInputRef In_EMALen   = sc.Input[1];
    SCInputRef In_ATRLen   = sc.Input[2];
    SCInputRef In_SlopeLen = sc.Input[3];
    SCInputRef In_MinBodyFracTrendBar = sc.Input[4];
    SCInputRef In_MaxTailFrac = sc.Input[5];
    SCInputRef In_MaxBodyOverlap = sc.Input[6];
    SCInputRef In_GapTicks = sc.Input[7];
    SCInputRef In_StrongBarBodyATR = sc.Input[8];
    SCInputRef In_UseRTHSession = sc.Input[9];

    // Weights for buckets (sum normalized to 1 internally)
    SCInputRef In_Weights_1 = sc.Input[10];
    SCInputRef In_Weights_2 = sc.Input[11];
    SCInputRef In_Weights_3 = sc.Input[12];
    SCInputRef In_Weights_4 = sc.Input[13];
    SCInputRef In_Weights_5 = sc.Input[14];
    SCInputRef In_Weights_6 = sc.Input[15];
    SCInputRef In_Weights_7 = sc.Input[16];
    SCInputRef In_Weights_8 = sc.Input[17];
    SCInputRef In_Weights_9 = sc.Input[18];
    SCInputRef In_Weights_10 = sc.Input[19];
    SCInputRef In_Weights_11 = sc.Input[20];
    SCInputRef In_Weights_12 = sc.Input[21];
    SCInputRef In_Weights_13 = sc.Input[22];
    SCInputRef In_Weights_14 = sc.Input[23];
    SCInputRef In_Weights_15 = sc.Input[24];
    SCInputRef In_Weights_16 = sc.Input[25];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Trend Strength Score (23-Criteria Proxy)";
        sc.StudyDescription = "Scores trend strength 0–100 using measurable proxies for 23 criteria (gaps, trend bars, tails, overlap, EMA gap sequence, pullbacks, etc.).";
        sc.AutoLoop = 1;
        sc.GraphRegion = 1;

        SG_Strength.Name = "Trend Strength Score";
        SG_Strength.DrawStyle = DRAWSTYLE_LINE;
        SG_Strength.PrimaryColor = RGB(0, 175, 0);
        SG_Strength.LineWidth = 2;

        SG_Direction.Name = "Trend Direction (+1 bull, -1 bear)";
        SG_Direction.DrawStyle = DRAWSTYLE_LINE;
        SG_Direction.PrimaryColor = RGB(0, 128, 255);

        for (int i = 0; i < DIAG_COUNT; ++i)
        {
            sc.Subgraph[DIAG_BASE + i].Name.Format("Diag %d", i + 1);
            sc.Subgraph[DIAG_BASE + i].DrawStyle = DRAWSTYLE_IGNORE; // enable to debug
            sc.Subgraph[DIAG_BASE + i].PrimaryColor = RGB(160, 160, 160);
        }

        In_Lookback.Name = "Lookback Bars";
        In_Lookback.SetInt(50);
        In_Lookback.SetIntLimits(10, 500);

        In_EMALen.Name = "EMA Length";
        In_EMALen.SetInt(20);
        In_EMALen.SetIntLimits(5, 200);

        In_ATRLen.Name = "ATR Length";
        In_ATRLen.SetInt(14);
        In_ATRLen.SetIntLimits(5, 200);

        In_SlopeLen.Name = "EMA Slope Lookback";
        In_SlopeLen.SetInt(10);
        In_SlopeLen.SetIntLimits(3, 100);

        In_MinBodyFracTrendBar.Name = "Min Body Fraction to Count Trend Bar";
        In_MinBodyFracTrendBar.SetFloat(0.55f);

        In_MaxTailFrac.Name = "Max Tail Fraction (each) to be Small";
        In_MaxTailFrac.SetFloat(0.2f);

        In_MaxBodyOverlap.Name = "Max Body Overlap to Count Non-Overlap";
        In_MaxBodyOverlap.SetFloat(0.3f);

        In_GapTicks.Name = "Gap Ticks Threshold";
        In_GapTicks.SetInt(1);

        In_StrongBarBodyATR.Name = "Strong Bar: Body >= N * ATR";
        In_StrongBarBodyATR.SetFloat(0.8f);

        In_UseRTHSession.Name = "Use RTH Session For Day Open (if available)";
        In_UseRTHSession.SetYesNo(1);

        // Equal weights default
        float defaultWeight = 1.0f;
        // Avoid arrays of SCInputRef (reference-like). Directly set via sc.Input index range [10..25].
        for (int wi = 0; wi < DIAG_COUNT; ++wi)
        {
            SCInputRef& inp = sc.Input[10 + wi];
            inp.Name.Format("Weight %d", wi + 1);
            inp.SetFloat(defaultWeight);
        }

        return;
    }

    const int i = sc.Index;
    if (i < 100) { SG_Strength[i] = 0.f; SG_Direction[i] = 0.f; return; }

    // Working arrays
    SCFloatArray EMA, TR, ATR;
    sc.ExponentialMovAvg(sc.Close, EMA, In_EMALen.GetInt());
    sc.ATR(sc.BaseDataIn, TR, ATR, In_ATRLen.GetInt(), MOVAVGTYPE_SIMPLE);

    // Direction proxy
    const float emaSlope = EmaSlope(EMA, i, In_SlopeLen.GetInt());
    int dir = 0;
    if (sc.Close[i] > EMA[i] && emaSlope > 0) dir = 1; else if (sc.Close[i] < EMA[i] && emaSlope < 0) dir = -1; else dir = 0;
    SG_Direction[i] = (float)dir;

    const int L = std::min(In_Lookback.GetInt(), i - 2);
    const int start = i - L;

    // Diagnostics accumulators (0..1)
    float diag[DIAG_COUNT] = {0};

    // Base references
    const SCBaseDataRef& B = sc.BaseDataIn;

    // Helper counters inside lookback window
    int strongTrendBars = 0; // criterion 3
    int smallTailBars = 0;   // criterion 5
    int lowOverlapPairs = 0; // criterion 4
    int bodyGaps = 0;        // criterion 6
    int strongAtStart = 0;   // criterion 7
    int largeBars = 0;       // criteria 10-11 (negative)
    int consecNoTouchEMA = 0;// criterion 14 (max sequence)
    int maxConsecNoTouchEMA = 0;
    int repeatedTwoLegPB = 0;// criterion 20
    int twoConsecOppMACloses = 0; // criterion 21 (negative)

    // Breakout state for measuring gaps (8) and micro measuring gaps (9)
    BreakoutState bo;

    // Prior day gap (criterion 1)
    float priorClose = sc.Close[start - 1];
    int ticks = std::max(1, sc.TickSize != 0 ? (int)std::round((B[SC_OPEN][start] - priorClose) / sc.TickSize) : 0);
    const float bigGapScore = (float)(std::abs(ticks) >= In_GapTicks.GetInt());

    // Trending highs/lows (criterion 2) – count HH/HL for bull, LL/LH for bear using simple swing detection
    int swingsDirCount = 0;
    {
        // simple last-swing tracker
        int left = 2, right = 2;
        int lastSwingIdx = -1; bool lastWasHigh = false; float lastSwingPrice = 0.f;
        for (int j = start + right; j <= i - right; ++j)
        {
            if (IsSwingHigh(B, j, left, right)) { lastSwingIdx = j; lastSwingPrice = B[SC_HIGH][j]; lastWasHigh = true; break; }
            if (IsSwingLow(B, j, left, right)) { lastSwingIdx = j; lastSwingPrice = B[SC_LOW][j];  lastWasHigh = false; break; }
        }
        for (int j = lastSwingIdx + 1; j <= i - right && lastSwingIdx > 0; ++j)
        {
            if (IsSwingHigh(B, j, left, right))
            {
                if (dir > 0 && lastWasHigh && B[SC_HIGH][j] > lastSwingPrice) ++swingsDirCount; // HH
                if (dir < 0 && !lastWasHigh) { /* ignore */ }
                lastWasHigh = true; lastSwingIdx = j; lastSwingPrice = B[SC_HIGH][j];
            }
            if (IsSwingLow(B, j, left, right))
            {
                if (dir > 0 && !lastWasHigh) { if (B[SC_LOW][j] > lastSwingPrice) ++swingsDirCount; } // HL
                if (dir < 0 && lastWasHigh && B[SC_LOW][j] < lastSwingPrice) ++swingsDirCount;        // LL
                lastWasHigh = false; lastSwingIdx = j; lastSwingPrice = B[SC_LOW][j];
            }
        }
    }

    // Iterate window for per-bar measures
    int legsInPullback = 0; // simple two-leg counter
    int lastPBDir = 0;      // +1 pullback up in bear trend; -1 pullback down in bull trend
    int legBars = 0;        // bars in current leg

    for (int j = start; j <= i; ++j)
    {
        const float bodyFrac = BodyFraction(B, j);
        const float topW = TopWickFrac(B, j);
        const float botW = BotWickFrac(B, j);
        const bool bullBar = B[SC_CLOSE][j] >= B[SC_OPEN][j];

        // Criterion 3: majority trend bars in dir
        if (dir > 0 && bullBar && bodyFrac >= In_MinBodyFracTrendBar.GetFloat()) ++strongTrendBars;
        if (dir < 0 && !bullBar && bodyFrac >= In_MinBodyFracTrendBar.GetFloat()) ++strongTrendBars;

        // Criterion 5: small tails showing urgency
        const float smallTail = (topW <= In_MaxTailFrac.GetFloat() && botW <= In_MaxTailFrac.GetFloat()) ? 1.f : 0.f;
        smallTailBars += (int)smallTail;

        // Criterion 6: body gaps (open > prior close for bull, open < prior close for bear)
        if (j > start)
        {
            if (dir > 0 && B[SC_OPEN][j] > B[SC_CLOSE][j - 1]) ++bodyGaps;
            if (dir < 0 && B[SC_OPEN][j] < B[SC_CLOSE][j - 1]) ++bodyGaps;

            // Criterion 4: little body overlap
            float o = BodyOverlapRatio(B, j);
            if (o <= In_MaxBodyOverlap.GetFloat()) ++lowOverlapPairs;
        }

        // Criterion 7: breakout gap at start – strong trend bar near beginning of window
        if (j <= start + 3)
        {
            const float body = std::fabs(B[SC_CLOSE][j] - B[SC_OPEN][j]);
            if (body >= In_StrongBarBodyATR.GetFloat() * ATR[j])
            {
                if ((dir > 0 && bullBar) || (dir < 0 && !bullBar)) ++strongAtStart;
            }
        }

        // Criterion 10-11: penalize big climaxes or too many large bars
        const float range = B[SC_HIGH][j] - B[SC_LOW][j];
        if (range >= 1.8f * ATR[j]) ++largeBars;

        // Criterion 14: 20-EMA gap bars sequence (no touch of EMA)
        const bool noTouch = (B[SC_LOW][j] > EMA[j]) || (B[SC_HIGH][j] < EMA[j]);
        if (noTouch) { ++consecNoTouchEMA; maxConsecNoTouchEMA = std::max(maxConsecNoTouchEMA, consecNoTouchEMA); }
        else consecNoTouchEMA = 0;

        // Criterion 21 (negative): two consecutive closes on opposite side of EMA
        if (j >= start + 2)
        {
            const bool opp1 = (B[SC_CLOSE][j - 1] > EMA[j - 1]);
            const bool opp2 = (B[SC_CLOSE][j] > EMA[j]);
            if (opp1 != opp2) ++twoConsecOppMACloses; // counts transitions; larger value is worse
        }

        // Track breakout/pullback for criteria 8,9 (measuring gaps) – simplistic state machine
        // Pick strongest bar as pivot in early sequence
        if (j <= start + 10)
        {
            const float body = std::fabs(B[SC_CLOSE][j] - B[SC_OPEN][j]);
            if (body >= In_StrongBarBodyATR.GetFloat() * ATR[j])
            {
                if ((dir > 0 && bullBar) || (dir < 0 && !bullBar))
                {
                    bo.PivotIndex = j;
                    bo.PivotPrice = dir > 0 ? B[SC_HIGH][j] : B[SC_LOW][j];
                    bo.IsBull = dir > 0;
                }
            }
        }

        // Simple pullback leg counting (criterion 20 – repeated two-legged PB)
        if (j > start)
        {
            int currDir = 0;
            if (dir > 0) currDir = (B[SC_CLOSE][j] < B[SC_CLOSE][j - 1]) ? -1 : +1; // down leg within bull trend
            else if (dir < 0) currDir = (B[SC_CLOSE][j] > B[SC_CLOSE][j - 1]) ? +1 : -1; // up leg within bear trend

            if (currDir == lastPBDir) ++legBars; else { if (lastPBDir != 0) ++legsInPullback; lastPBDir = currDir; legBars = 1; }
            if (legsInPullback >= 2) { ++repeatedTwoLegPB; legsInPullback = 0; }
        }
    }

    // Criterion 8: measuring gap – pullback does not overlap breakout point
    float measuringGapScore = 0.f;
    if (bo.PivotIndex > 0)
    {
        if (bo.IsBull)
        {
            float lowestSince = 1e30f;
            for (int j = bo.PivotIndex + 1; j <= i; ++j) lowestSince = std::min(lowestSince, B[SC_LOW][j]);
            measuringGapScore = lowestSince >= bo.PivotPrice ? 1.f : 0.f;
        }
        else
        {
            float highestSince = -1e30f;
            for (int j = bo.PivotIndex + 1; j <= i; ++j) highestSince = std::max(highestSince, B[SC_HIGH][j]);
            measuringGapScore = highestSince <= bo.PivotPrice ? 1.f : 0.f;
        }
    }

    // Criterion 9: micro measuring gap (neighbor gap around strong bar)
    float microMeasGapScore = 0.f;
    if (bo.PivotIndex > 0)
    {
        microMeasGapScore = bo.IsBull ? (HasMicroMeasuringGapBull(B, bo.PivotIndex) ? 1.f : 0.f)
                                      : (HasMicroMeasuringGapBear(B, bo.PivotIndex) ? 1.f : 0.f);
    }

    // Criterion 16: small, infrequent pullbacks – measure max pullback depth vs ATR and frequency
    float maxPB = 0.f; int pbCount = 0; float ref = sc.Close[start];
    for (int j = start + 1; j <= i; ++j)
    {
        if (dir > 0) { maxPB = std::max(maxPB, std::max(0.f, ref - sc.Close[j])); if (sc.Close[j] < sc.Close[j - 1]) ++pbCount; }
        else if (dir < 0) { maxPB = std::max(maxPB, std::max(0.f, sc.Close[j] - ref)); if (sc.Close[j] > sc.Close[j - 1]) ++pbCount; }
        ref = sc.Close[j];
    }
    float avgATR = 0.f; for (int j = start; j <= i; ++j) avgATR += ATR[j]; avgATR /= (float)(L + 1);
    const float smallPBScore = clamp01(1.f - (maxPB / std::max(avgATR * 1.5f, 0.0001f)) ) * clamp01(1.f - (pbCount / (float)(L * 0.6f + 1)));

    // Criterion 18–19: pullback setups. Score if pullback’s signal bars align with the idea.
    // Proxy: After a pullback, look for either strong reversal bars (criterion 18) or weak opposite-color signals in strongest trends (criterion 19).
    float pullbackSetupScore = 0.f;
    {
        int signals = 0; int good = 0; int weakOppSignals = 0;
        for (int j = start + 2; j <= i; ++j)
        {
            const bool bullBar = B[SC_CLOSE][j] >= B[SC_OPEN][j];
            const float bodyFrac = BodyFraction(B, j);
            // detect pullback end (simple): last two bars against trend, then this bar with-trend
            if (dir > 0)
            {
                if (B[SC_CLOSE][j - 2] < B[SC_CLOSE][j - 3] && B[SC_CLOSE][j - 1] < B[SC_CLOSE][j - 2])
                {
                    ++signals;
                    if (bullBar && bodyFrac >= 0.55f) ++good; // strong reversal bar
                    if (!bullBar && bodyFrac <= 0.35f) ++weakOppSignals; // weak opposing signal in strong trends
                }
            }
            else if (dir < 0)
            {
                if (B[SC_CLOSE][j - 2] > B[SC_CLOSE][j - 3] && B[SC_CLOSE][j - 1] > B[SC_CLOSE][j - 2])
                {
                    ++signals;
                    if (!bullBar && bodyFrac >= 0.55f) ++good;
                    if (bullBar && bodyFrac <= 0.35f) ++weakOppSignals;
                }
            }
        }
        if (signals > 0) pullbackSetupScore = clamp01((good / (float)signals) * 0.6f + (weakOppSignals / (float)std::max(1, signals)) * 0.4f);
    }

    // Criterion 12–13, 17, 22–23 are partially reflected by: EMA slope, few countertrend closes, limited overshoots (via large bar penalty), persistent direction, resistance breaks (EMA + swing highs/lows), urgency (small tails + low overlap + body gaps), and failed spikes (no follow-through approximated via body gaps and measuring gaps).

    // Pack diagnostics into buckets (16 buckets, each 0..1)
    // You can hover these with values if you enable their draw style in the Subgraphs list.
    int k = 0;
    diag[k++] = bigGapScore;                                     // 1) Big gap open
    diag[k++] = clamp01(swingsDirCount / (float)std::max(1, L/8)); // 2) Trending swings
    diag[k++] = clamp01(strongTrendBars / (float)(L + 1));       // 3) Trend bars majority
    diag[k++] = clamp01(lowOverlapPairs / (float)std::max(1, L)); // 4) Little body overlap
    diag[k++] = clamp01(smallTailBars / (float)(L + 1));         // 5) Small/no tails
    diag[k++] = clamp01(bodyGaps / (float)std::max(1, L/6));     // 6) Body gaps
    diag[k++] = clamp01(strongAtStart > 0 ? 1.f : 0.f);          // 7) Breakout gap at start
    diag[k++] = measuringGapScore;                               // 8) Measuring gaps
    diag[k++] = microMeasGapScore;                               // 9) Micro measuring gaps
    diag[k++] = clamp01(1.f - (largeBars / (float)std::max(1, L/5))); // 10-11) No big climaxes/many large bars
    diag[k++] = clamp01(maxConsecNoTouchEMA / 20.f);             // 14) EMA gap bars sequence
    diag[k++] = clamp01(1.f - (twoConsecOppMACloses / (float)std::max(1, L/4))); // 21) few opposite MA closes
    diag[k++] = smallPBScore;                                    // 16) Small/infrequent pullbacks
    diag[k++] = pullbackSetupScore;                              // 18-19) Pullback setup quality
    diag[k++] = clamp01((emaSlope * dir > 0 && std::fabs(sc.Close[i] - EMA[i]) > 0.2f * avgATR) ? 1.f : 0.f); // 17,22 proxy: urgency + distance across resistances
    diag[k++] = clamp01((bodyGaps > 0 || measuringGapScore > 0.f || microMeasGapScore > 0.f) ? 1.f : 0.f); // 23) failed reversals prob. low

    // Apply weights
    float W[DIAG_COUNT] = {
        In_Weights_1.GetFloat(), In_Weights_2.GetFloat(), In_Weights_3.GetFloat(), In_Weights_4.GetFloat(),
        In_Weights_5.GetFloat(), In_Weights_6.GetFloat(), In_Weights_7.GetFloat(), In_Weights_8.GetFloat(),
        In_Weights_9.GetFloat(), In_Weights_10.GetFloat(), In_Weights_11.GetFloat(), In_Weights_12.GetFloat(),
        In_Weights_13.GetFloat(), In_Weights_14.GetFloat(), In_Weights_15.GetFloat(), In_Weights_16.GetFloat()
    };
    float wsum = 0.f; for (int t = 0; t < DIAG_COUNT; ++t) wsum += std::max(0.f, W[t]); wsum = std::max(0.0001f, wsum);

    float score01 = 0.f;
    for (int t = 0; t < DIAG_COUNT; ++t)
    {
        const float contrib = diag[t] * std::max(0.f, W[t]) / wsum;
        score01 += contrib;
        sc.Subgraph[DIAG_BASE + t][i] = diag[t];
    }

    // Direction gating: if no clear dir, dampen score
    if (dir == 0) score01 *= 0.4f;

    SG_Strength[i] = clamp01(score01) * 100.f;
}