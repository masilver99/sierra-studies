#region Using declarations
using System;
using System.ComponentModel;
using System.ComponentModel.DataAnnotations;
using System.Windows.Media;
using System.Xml.Serialization;
using NinjaTrader.Data;
using NinjaTrader.Gui.Chart;
using NinjaTrader.NinjaScript;
using NinjaTrader.NinjaScript.DrawingTools;
#endregion

namespace NinjaTrader.NinjaScript.Indicators
{
    public class TrappedTraderSignals : Indicator
    {
        private Brush bearTrapBrush;
        private Brush bullTrapBrush;

        protected override void OnStateChange()
        {
            if (State == State.SetDefaults)
            {
                Description = "Highlights likely bull/bear traps and plots sell (bull trap) and buy (bear trap) markers.";
                Name = "TrappedTraderSignals";
                Calculate = Calculate.OnBarClose;
                IsOverlay = true;
                DisplayInDataBox = true;
                DrawOnPricePanel = true;
                PaintPriceMarkers = false;
                IsSuspendedWhileInactive = true;

                BreakoutLookbackBars = 20;
                MinimumBreakoutDistanceTicks = 1;
                RequireReversalCandleClose = true;
                MarkerOffsetTicks = 2;
                AlertNumber = 0;

                AddPlot(new Stroke(new SolidColorBrush(Color.FromRgb(0, 200, 0)), 2), PlotStyle.TriangleUp, "Buy Mark (Bear Trap)");
                AddPlot(new Stroke(new SolidColorBrush(Color.FromRgb(220, 0, 0)), 2), PlotStyle.TriangleDown, "Sell Mark (Bull Trap)");
                AddPlot(new Stroke(Brushes.Transparent, 1), PlotStyle.Hash, "Bear Trap Highlight");
                AddPlot(new Stroke(Brushes.Transparent, 1), PlotStyle.Hash, "Bull Trap Highlight");
            }
            else if (State == State.Configure)
            {
                bearTrapBrush = new SolidColorBrush(Color.FromRgb(0, 160, 0));
                bearTrapBrush.Freeze();

                bullTrapBrush = new SolidColorBrush(Color.FromRgb(200, 80, 0));
                bullTrapBrush.Freeze();
            }
        }

        protected override void OnBarUpdate()
        {
            BuyMark[0] = double.NaN;
            SellMark[0] = double.NaN;
            BearTrapHighlight[0] = double.NaN;
            BullTrapHighlight[0] = double.NaN;
            BarBrushes[0] = null;
            CandleOutlineBrushes[0] = null;

            if (CurrentBar <= BreakoutLookbackBars)
                return;

            double minBreakout = MinimumBreakoutDistanceTicks * TickSize;
            double markerOffset = MarkerOffsetTicks * TickSize;

            double highestPrior = High[1];
            double lowestPrior = Low[1];

            for (int barsAgo = 2; barsAgo <= BreakoutLookbackBars; barsAgo++)
            {
                if (High[barsAgo] > highestPrior)
                    highestPrior = High[barsAgo];

                if (Low[barsAgo] < lowestPrior)
                    lowestPrior = Low[barsAgo];
            }

            bool bullishClose = Close[0] > Open[0];
            bool bearishClose = Close[0] < Open[0];

            bool bullTrap =
                High[0] >= highestPrior + minBreakout
                && Close[0] <= highestPrior
                && (!RequireReversalCandleClose || bearishClose);

            bool bearTrap =
                Low[0] <= lowestPrior - minBreakout
                && Close[0] >= lowestPrior
                && (!RequireReversalCandleClose || bullishClose);

            if (bullTrap)
            {
                BullTrapHighlight[0] = Close[0];
                SellMark[0] = High[0] + markerOffset;
                BarBrushes[0] = bullTrapBrush;
                CandleOutlineBrushes[0] = bullTrapBrush;

                if (AlertNumber > 0 && State == State.Realtime)
                {
                    Alert($"TrappedTraderSignals_{AlertNumber}_Bull_{CurrentBar}", Priority.Medium,
                        $"Bull trap detected at {Close[0]:0.00} (sell mark)", "Alert1.wav", 0,
                        Brushes.Black, Brushes.OrangeRed);
                }
            }

            if (bearTrap)
            {
                BearTrapHighlight[0] = Close[0];
                BuyMark[0] = Low[0] - markerOffset;
                BarBrushes[0] = bearTrapBrush;
                CandleOutlineBrushes[0] = bearTrapBrush;

                if (AlertNumber > 0 && State == State.Realtime)
                {
                    Alert($"TrappedTraderSignals_{AlertNumber}_Bear_{CurrentBar}", Priority.Medium,
                        $"Bear trap detected at {Close[0]:0.00} (buy mark)", "Alert1.wav", 0,
                        Brushes.Black, Brushes.LimeGreen);
                }
            }
        }

        [NinjaScriptProperty]
        [Range(1, 500)]
        [Display(Name = "Breakout Lookback Bars", GroupName = "Parameters", Order = 0)]
        public int BreakoutLookbackBars { get; set; }

        [NinjaScriptProperty]
        [Range(0, 50)]
        [Display(Name = "Minimum Breakout Distance (ticks)", GroupName = "Parameters", Order = 1)]
        public int MinimumBreakoutDistanceTicks { get; set; }

        [NinjaScriptProperty]
        [Display(Name = "Require Reversal Candle Close", GroupName = "Parameters", Order = 2)]
        public bool RequireReversalCandleClose { get; set; }

        [NinjaScriptProperty]
        [Range(0, 100)]
        [Display(Name = "Marker Offset (ticks)", GroupName = "Parameters", Order = 3)]
        public int MarkerOffsetTicks { get; set; }

        [NinjaScriptProperty]
        [Range(0, 150)]
        [Display(Name = "Alert Number (0 = disabled)", GroupName = "Parameters", Order = 4)]
        public int AlertNumber { get; set; }

        [Browsable(false)]
        [XmlIgnore]
        public Series<double> BuyMark => Values[0];

        [Browsable(false)]
        [XmlIgnore]
        public Series<double> SellMark => Values[1];

        [Browsable(false)]
        [XmlIgnore]
        public Series<double> BearTrapHighlight => Values[2];

        [Browsable(false)]
        [XmlIgnore]
        public Series<double> BullTrapHighlight => Values[3];
    }
}
