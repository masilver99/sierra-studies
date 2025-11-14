#include "sierrachart.h"

SCDLLName("Button Studies");

float GetTimeFrame(int TimeFrame, SCDateTime dt_EndTime, SCStudyInterfaceRef sc)
{
	SCDateTime dt_StartTime = dt_EndTime;
	dt_StartTime.AddMinutes(TimeFrame);
	float Open;
	float High;
	float Low;
	float Close;

	float NextOpen;

	sc.GetOHLCOfTimePeriod(dt_StartTime, dt_EndTime, Open, High, Low, Close, NextOpen);
	SCString Message;
	Message.Format
	("TimeFrame: %d, %d, %d: %f",
		dt_StartTime,
		dt_EndTime,
		TimeFrame,
		Close - Open
	);
	sc.AddMessageToLog(Message, 0);
	return Close - Open;
}

SCSFExport scsf_InsideBarTradingSystem(SCStudyInterfaceRef sc)
{
	SCSubgraphRef Subgraph_ATR = sc.Subgraph[0];
	SCInputRef Subgraph_Length = sc.Input[1];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Inside Bar Trading System";
		sc.StudyDescription = "Places two orders on each end of an inside bar with profit targets and stops.";
		sc.AutoLoop = 1;

		sc.GraphRegion = 1;
		sc.ValueFormat = VALUEFORMAT_INHERITED;

		Subgraph_ATR.Name = "ATR";
		Subgraph_ATR.DrawZeros = false;
		Subgraph_ATR.PrimaryColor = RGB(0, 255, 0);
		Subgraph_ATR.DrawStyle = DRAWSTYLE_LINE;

		Subgraph_Length.Name = "Moving Average Length";
		Subgraph_Length.SetInt(14);
		Subgraph_Length.SetIntLimits(1, MAX_STUDY_LENGTH);

		return;
	}

	int& LastBarIndexProcessed = sc.GetPersistentInt(1);
	int& BuyOrderId = sc.GetPersistentInt(2);
	int& SellOrderId = sc.GetPersistentInt(3);

	//Check if we have an open order, if not
	// then check if we have a filled order and active position, if not
	// clear order ids and start looking for inside bars again
	//Check if we have an open position

	if (BuyOrderId != 0 && SellOrderId != 0)
	{
		s_SCTradeOrder TradeOrder;
		sc.GetOrderByOrderID(BuyOrderId, TradeOrder);
		if (TradeOrder.InternalOrderID == BuyOrderId)
		{
			//sc.AddMessageToLog("Trade StatusNew Bar!", 1);
			bool IsOrderOpen = TradeOrder.OrderStatusCode == SCT_OSC_OPEN;
			if (IsOrderOpen) return;
		}

		s_SCPositionData PositionData;
		sc.GetTradePosition(PositionData);
		if (PositionData.PositionQuantity != 0)
		{
			return;
		}
		else
		{
			BuyOrderId = 0;
			SellOrderId = 0;
		}
	}

	if (sc.Index == 0 || sc.Index == 1 || sc.Index == LastBarIndexProcessed)
	{
		return;
	}
	else
	{
		LastBarIndexProcessed = sc.Index;
	}

	//sc.AddMessageToLog("New Bar!", 1);

	// Check if the current bar is an inside bar
	if (sc.High[sc.Index - 2] > sc.High[sc.Index - 1] && sc.Low[sc.Index - 2] < sc.Low[sc.Index - 1])
	{
		sc.DataStartIndex = Subgraph_Length.GetInt() - 1;
		sc.ATR(sc.BaseDataIn, Subgraph_ATR.Arrays[0], Subgraph_ATR, Subgraph_Length.GetInt(), MOVAVGTYPE_SIMPLE);
		float TrueRange = Subgraph_ATR.Arrays[0][sc.Index];

		s_SCNewOrder order;

		// Place a buy stop order at the high of the inside bar
		order.OrderType = SCT_ORDERTYPE_OCO_BUY_STOP_LIMIT_SELL_STOP_LIMIT;
		order.Price1 = sc.High[sc.Index - 1] + 1;
		order.Price2 = sc.Low[sc.Index - 1] - 1;
		order.Target1Price = sc.High[sc.Index - 1] + TrueRange / 2;
		//order.Target1Price_2 = sc.High[sc.Index - 1] + 22 * sc.TickSize;
		order.Target2Price = sc.Low[sc.Index - 1] - TrueRange / 2;
		//order.Target2Price_2 = sc.Low[sc.Index - 1] - 22 * sc.TickSize;
		order.Stop1Price = sc.High[sc.Index - 1] - 1;
		//order.Stop1Price_2 = sc.Low[sc.Index - 1];
		order.Stop2Price = sc.Low[sc.Index - 1] + 1;
		//order.Stop2Price_2 = sc.High[sc.Index - 1];
		order.OrderQuantity = 2;
		order.OCOGroup1Quantity = 1;
		order.OCOGroup2Quantity = 1;

		if (sc.SubmitOCOOrder(order) > 0)
		{
			BuyOrderId = order.InternalOrderID;
			SellOrderId = order.InternalOrderID2;
		}
		{
			BuyOrderId = order.InternalOrderID;
			SellOrderId = order.InternalOrderID2;
		}
		//order.Price2 = sc.Low[sc.Index];
		//sc.SellEntry(order);

		// Set profit targets and stops for the orders
		//sc.SetProfitTarget(order.InternalOrderID, sc.High[sc.Index] + 10 * sc.TickSize);
		//sc.SetStopLoss(order.InternalOrderID, sc.Low[sc.Index] - 10 * sc.TickSize);
	}
}

SCSFExport scsf_StratBrackets(SCStudyInterfaceRef sc)
{
	SCInputRef Input_HorizontalPosition = sc.Input[0];
	SCInputRef Input_VerticalPosition = sc.Input[1];

	SCInputRef Input_MinimumProfitTarget = sc.Input[2];
	SCInputRef Input_OrderOffset = sc.Input[3];
	SCInputRef Input_StopOffset = sc.Input[4];
	SCInputRef Input_SkipIfLessThan = sc.Input[5];
	SCInputRef Input_TrailingStop = sc.Input[6];

	auto CalculateMinShortTarget = [&](int minimumProfitTargetTicks)
	{
		float target2 = sc.Low[sc.Index - 2];
		auto minimumProfitTarget = sc.Low[sc.Index - 1] - minimumProfitTargetTicks * sc.TickSize;
		if (target2 > minimumProfitTarget)
		{
			return minimumProfitTarget;
		}
		else
		{
			return target2;
		}
	};

	auto CalculateShortStop = [&]()
	{
		return sc.High[sc.Index - 1] + Input_StopOffset.GetInt() * sc.TickSize;
	};

	auto CalculateLongStop = [&]()
	{
		return sc.Low[sc.Index - 1] - Input_StopOffset.GetInt() * sc.TickSize;
	};

	auto CalculateMinLongTarget = [&](SCStudyInterfaceRef sc, int minimumProfitTargetTicks)
	{
		float target1 = sc.High[sc.Index - 2];
		auto minimumProfitTarget = sc.High[sc.Index - 1] + minimumProfitTargetTicks * sc.TickSize;
		if (target1 < minimumProfitTarget)
		{
			return minimumProfitTarget;
		}
		else
		{
			return target1;
		}
	};

	auto PlaceLongBracket = [&]()
	{
		s_SCNewOrder order1;

		// Place a buy stop order at the high of the inside bar
		order1.OrderType = SCT_ORDERTYPE_STOP_LIMIT;

		// Add Buy order, PT and SL
		order1.Price1 = sc.High[sc.Index - 1];
		order1.AttachedOrderTarget1Type = SCT_ORDERTYPE_LIMIT;
		order1.Target1Price = CalculateMinLongTarget(sc, Input_MinimumProfitTarget.GetInt());

		order1.Stop1Price = CalculateLongStop();
		//order1.MoveToBreakEven.Type = MOVETOBE_BREAK_EVEN;

		if (Input_TrailingStop.GetBoolean())
		{
			order1.AttachedOrderStop1Type = SCT_ORDERTYPE_TRAILING_STOP;
		}
		else
		{
			order1.AttachedOrderStop1Type = SCT_ORDERTYPE_STOP;
		}

		order1.OrderQuantity = sc.TradeWindowOrderQuantity;

		sc.BuyEntry(order1);

		sc.SetCustomStudyControlBarButtonEnable(ACS_BUTTON_2, 0);
	};

	auto PlaceShortBracket = [&]()
	{
		s_SCNewOrder order1;

		// Place a buy stop order at the high of the inside bar
		order1.OrderType = SCT_ORDERTYPE_STOP_LIMIT;

		// Add Buy order, PT and SL
		order1.Price1 = sc.Low[sc.Index - 1];
		order1.AttachedOrderTarget1Type = SCT_ORDERTYPE_LIMIT;
		order1.Target1Price = CalculateMinShortTarget(Input_MinimumProfitTarget.GetInt());

		order1.Stop1Price = CalculateShortStop();

		if (Input_TrailingStop.GetBoolean())
		{
			order1.AttachedOrderStop1Type = SCT_ORDERTYPE_TRAILING_STOP;
		}
		else
		{
			order1.AttachedOrderStop1Type = SCT_ORDERTYPE_STOP;
		}

		order1.OrderQuantity = sc.TradeWindowOrderQuantity;

		sc.SellEntry(order1);

		sc.SetCustomStudyControlBarButtonEnable(ACS_BUTTON_3, 0);
	};


	auto PlaceDualBrackets = [&]()
	{
		s_SCNewOrder order1;
		s_SCNewOrder order2;

		// Place a buy stop order at the high of the inside bar
		order1.OrderType = SCT_ORDERTYPE_OCO_BUY_STOP_SELL_STOP;

		// Add Buy order, PT and SL
		order1.Price1 = sc.High[sc.Index - 1];
		order1.AttachedOrderTarget1Type = SCT_ORDERTYPE_LIMIT;
		order1.Target1Price = CalculateMinLongTarget(sc, Input_MinimumProfitTarget.GetInt());

		order1.Stop1Price = CalculateLongStop();
		//order1.MoveToBreakEven.Type = MOVETOBE_BREAK_EVEN;

		//Add sell order, PT and SL
		order1.Price2 = sc.Low[sc.Index - 1];
		order1.AttachedOrderTarget2Type = SCT_ORDERTYPE_LIMIT;
		order1.Target1Price_2 = CalculateMinShortTarget(Input_MinimumProfitTarget.GetInt());

		if (Input_TrailingStop.GetBoolean())
		{
			order1.AttachedOrderStop1Type = SCT_ORDERTYPE_TRAILING_STOP;
			order1.AttachedOrderStop2Type = SCT_ORDERTYPE_TRAILING_STOP;
			//order1.TrailingStopStep = 2;
		}
		else
		{
			order1.AttachedOrderStop1Type = SCT_ORDERTYPE_STOP;
			order1.AttachedOrderStop2Type = SCT_ORDERTYPE_STOP;
		}

		order1.Stop1Price_2 = CalculateShortStop();

		order1.OrderQuantity = sc.TradeWindowOrderQuantity;

		sc.SubmitOCOOrder(order1);

		sc.SetCustomStudyControlBarButtonEnable(ACS_BUTTON_1, 0);

	};

	if (sc.SetDefaults)
	{
		sc.GraphName = "Strat Brackets";
		sc.StudyDescription = "Places two orders on each end of an inside bar with profit targets and stops.";
		sc.AutoLoop = 1;
		sc.ReceivePointerEvents = ACS_RECEIVE_POINTER_EVENTS_WHEN_ACS_BUTTON_ENABLED;
		sc.AllowOppositeEntryWithOpposingPositionOrOrders = true;
		sc.MaintainTradeStatisticsAndTradesData = true;
		sc.SupportAttachedOrdersForTrading = true;
		sc.AllowEntryWithWorkingOrders = true;
		sc.CancelAllOrdersOnEntriesAndReversals = false;
		//sc.AllowOnlyOneTradePerBar = false;

		Input_MinimumProfitTarget.Name.Format("Minimum profit target in ticks", static_cast<int>(4));
		Input_MinimumProfitTarget.SetInt(0);
		Input_MinimumProfitTarget.SetIntLimits(0, 100);

		Input_OrderOffset.Name = "Order offset in ticks (0-20)";
		Input_OrderOffset.SetInt(0);
		Input_OrderOffset.SetIntLimits(0, 20);

		Input_StopOffset.Name = "Stop offset in ticks (0-20)";
		Input_StopOffset.SetInt(0);
		Input_StopOffset.SetIntLimits(0, 20);

		Input_TrailingStop.Name = "Use trailing stop";
		Input_TrailingStop.SetYesNo(false);

		Input_SkipIfLessThan.Name = "Skip order if less than x ticks (0-20)";
		Input_SkipIfLessThan.SetInt(0);
		Input_SkipIfLessThan.SetIntLimits(0, 20);

		return;
	}

	if (sc.PointerEventType == SC_ACS_BUTTON_ON)
	{
		if (sc.MenuEventID == ACS_BUTTON_1 || sc.MenuEventID == ACS_BUTTON_2 || sc.MenuEventID == ACS_BUTTON_3)
		{
			SCString Message;
			/*			Message.Format
						("Current Bar: %f, %f - Last Bar: %f, %f - Third Bar: %f, %f."
							, sc.High[sc.Index], sc.Low[sc.Index],
							sc.High[sc.Index - 1], sc.Low[sc.Index - 1],
							sc.High[sc.Index - 2], sc.Low[sc.Index - 2]
						);
						sc.AddMessageToLog(Message, 0);
			*/
			//Confirm setup is 2-1-2
			//Determine if there are two inside bars
			//Place two orders on each end of the inside bar
			if (sc.MenuEventID == ACS_BUTTON_1)
			{
				PlaceDualBrackets();
			}
			else if (sc.MenuEventID == ACS_BUTTON_2)
			{
				PlaceLongBracket();
			}
			else if (sc.MenuEventID == ACS_BUTTON_3)
			{
				PlaceShortBracket();
			}

			return;
		}
		return;
	}

	SCDateTime& TimeFrameLastUpdated = sc.GetPersistentSCDateTime(1);

	SCDateTime Time(0, 0, 5, 0); //5 secs


	if (sc.CurrentSystemDateTime > TimeFrameLastUpdated + Time)
	{
		// In this example these are not set to anything. You will need to
		// set them to the appropriate starting DateTime and ending DateTime
		SCDateTime dt_EndTime = sc.GetCurrentDateTime();

		//daily: 9 hour window
		//half a daily: 4 hour window
		//hourly: 1 hour window
		//15 minutes: 15 minute window
		float TimeFrame1 = GetTimeFrame(-9 * 60, dt_EndTime, sc);
		float TimeFrame2 = GetTimeFrame(-4 * 60, dt_EndTime, sc);
		float TimeFrame3 = GetTimeFrame(-60, dt_EndTime, sc);
		float TimeFrame4 = GetTimeFrame(-15, dt_EndTime, sc);
		TimeFrameLastUpdated = sc.CurrentSystemDateTime;
	}
}

