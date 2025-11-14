SCDLLName("Inside Bar Trading System");

SCSFExport scsf_InsideBarTradingSystem(SCStudyInterfaceRef sc)
{
    if (sc.SetDefaults)
    {
        sc.GraphName = "Inside Bar Trading System";
        sc.StudyDescription = "Places two orders on each end of an inside bar with profit targets and stops.";
        sc.AutoLoop = 1;

        return;
    }

    // Check if the current bar is an inside bar
    if (sc.High[sc.Index - 1] > sc.High[sc.Index] && sc.Low[sc.Index - 1] < sc.Low[sc.Index])
    {
        s_SCNewOrder order;

        // Place a buy stop order at the high of the inside bar
        order.OrderType = SCT_ORDERTYPE_BUY_STOP;
        order.Price1 = sc.High[sc.Index];
        order.Quantity = 1;
        sc.BuyEntry(order);

        // Place a sell stop order at the low of the inside bar
        order.OrderType = SCT_ORDERTYPE_SELL_STOP;
        order.Price1 = sc.Low[sc.Index];
        sc.SellEntry(order);

        // Set profit targets and stops for the orders
        sc.SetProfitTarget(order.InternalOrderID, sc.High[sc.Index] + 10 * sc.TickSize);
        sc.SetStopLoss(order.InternalOrderID, sc.Low[sc.Index] - 10 * sc.TickSize);
    }
}