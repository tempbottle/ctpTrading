#include "TradeSrv.h"
#include "TraderSpi.h"

TradeSrv::TradeSrv(string brokerID, string userID, string password,
    string tradeFront, string instrumnetID, string flowPath, string logPath, int serviceID)
{
    _brokerID = brokerID;
    _userID = userID;
    _password = password;
    _tradeFront = tradeFront;
    _instrumnetID = instrumnetID;
    _flowPath = flowPath;
    _logPath = logPath;
    _reqID = 0;
    _closeYdReqID = 0;

    _store = new Redis("127.0.0.1", 6379, 1);
    _tradeStrategySrvClient = new QClient(serviceID, sizeof(MSG_TO_TRADE_STRATEGY));

}

TradeSrv::~TradeSrv()
{
    delete _store;
    delete _tradeStrategySrvClient;
    if (_tradeApi) {
        _tradeApi->Release();
        _tradeApi = NULL;
    }
    if (_traderSpi) {
        delete _traderSpi;
    }
    cout << "~TradeSrv" << endl;
}

void TradeSrv::setStatus(int status)
{
    _store->set("TRADE_STATUS", Lib::itos(status));
}

void TradeSrv::init()
{
    // 初始化交易接口
    _tradeApi = CThostFtdcTraderApi::CreateFtdcTraderApi(Lib::stoc(_flowPath));
    _traderSpi = new TraderSpi(this, _logPath); // 初始化回调实例
    _tradeApi->RegisterSpi(_traderSpi);
    // _tradeApi->SubscribePrivateTopic(THOST_TERT_QUICK);
    // _tradeApi->SubscribePublicTopic(THOST_TERT_QUICK);
    _tradeApi->SubscribePrivateTopic(THOST_TERT_RESUME);
    _tradeApi->SubscribePublicTopic(THOST_TERT_RESUME);

    _tradeApi->RegisterFront(Lib::stoc(_tradeFront));
    _tradeApi->Init();
}

void TradeSrv::login()
{
    CThostFtdcReqUserLoginField req = {0};

    strcpy(req.BrokerID, Lib::stoc(_brokerID));
    strcpy(req.UserID, Lib::stoc(_userID));
    strcpy(req.Password, Lib::stoc(_password));

    int res = _tradeApi->ReqUserLogin(&req, 0);
    Lib::sysReqLog(_logPath, "TradeSrv[login]", res);
}

void TradeSrv::onLogin(CThostFtdcRspUserLoginField * const rsp)
{
    if (!rsp) return;
    _frontID = rsp->FrontID;
    _sessionID = rsp->SessionID;
    _maxOrderRef = atoi(rsp->MaxOrderRef);
    _tradingDay = string(rsp->TradingDay);
}

void TradeSrv::getPosition()
{
    CThostFtdcQryInvestorPositionField req = {0};

    strcpy(req.BrokerID, Lib::stoc(_brokerID));
    strcpy(req.InvestorID, Lib::stoc(_userID));
    strcpy(req.InstrumentID, Lib::stoc(_instrumnetID));

    int res = _tradeApi->ReqQryInvestorPosition(&req, 0);
    Lib::sysReqLog(_logPath, "TradeSrv[getPosition]", res);
}

void TradeSrv::onPositionRtn(CThostFtdcInvestorPositionField * const rsp)
{
    if (!rsp) return;
    setStatus(TRADE_STATUS_NOTHING);
    _ydPostion = rsp->Position - rsp->TodayPosition;
    if (_ydPostion > 0) {
        // TODO 根据哪个字段判断具体仓位
    }
}

void TradeSrv::trade(double price, int total, bool isBuy, bool isOpen, int orderID)
{
    TThostFtdcOffsetFlagEnType flag = THOST_FTDC_OFEN_Open;
    if (!isOpen) {
        flag = THOST_FTDC_OFEN_CloseToday;
        if (_ydPostion > 0) {
            flag = THOST_FTDC_OFEN_Close;
            _closeYdReqID = orderID;
        }
    }
    CThostFtdcInputOrderField order = _createOrder(orderID, isBuy, total, price, flag,
            THOST_FTDC_HFEN_Speculation, THOST_FTDC_OPT_LimitPrice, THOST_FTDC_TC_GFD, THOST_FTDC_VC_CV);
    int res = _tradeApi->ReqOrderInsert(&order, 0);
    Lib::sysReqLog(_logPath, "TradeSrv[trade]", res);
}

void TradeSrv::onTraded(CThostFtdcTradeField * const rsp)
{
    if (!rsp) return;
    int orderID = _getOrderIDByRef(atoi(rsp->OrderRef));
    MSG_TO_TRADE_STRATEGY msg = {0};
    msg.msgType = MSG_TRADE_BACK_TRADED;
    msg.kIndex = orderID;
    _tradeStrategySrvClient->send((void *)&msg);
}

void TradeSrv::onOrderRtn(CThostFtdcOrderField * const rsp)
{
    if (!rsp) return;
    if (rsp->OrderStatus != THOST_FTDC_OST_Canceled) return;
    // 撤单情况
    int orderID = _getOrderIDByRef(atoi(rsp->OrderRef));
    MSG_TO_TRADE_STRATEGY msg = {0};
    msg.msgType = MSG_TRADE_BACK_CANCELED;
    msg.kIndex = orderID;
    _tradeStrategySrvClient->send((void *)&msg);
}

void TradeSrv::cancel(int orderID)
{
    CThostFtdcInputOrderActionField req = {0};

    strcpy(req.BrokerID,   Lib::stoc(_brokerID));
    strcpy(req.InvestorID, Lib::stoc(_userID));
    strcpy(req.UserID,     Lib::stoc(_userID));
    req.FrontID = _frontID;
    req.SessionID = _sessionID;
    sprintf(req.OrderRef, "%d", _getOrderRefByID(orderID));
    req.ActionFlag = THOST_FTDC_AF_Delete;

    int res = _tradeApi->ReqOrderAction(&req, 0);
    Lib::sysReqLog(_logPath, "TradeSrv[getPosition]", res);
}

void TradeSrv::onCancel(CThostFtdcInputOrderActionField * const rsp)
{
    if (!rsp) return;
    // int orderID = _getOrderIDByRef(atoi(rsp->OrderRef));
    // MSG_TO_TRADE_STRATEGY msg = {0};
    // msg.msgType = MSG_TRADE_BACK_CANCELED;
    // msg.kIndex = orderID;
    // _tradeStrategySrvClient->send((void *)&msg);
}

int TradeSrv::_getOrderRefByID(int orderID)
{
    _maxOrderRef++;
    string key = "ORDER_REF_" + Lib::itos(_maxOrderRef);
    _store->set(key, Lib::itos(orderID));
    return _maxOrderRef;
}

int TradeSrv::_getOrderIDByRef(int orderRef)
{
    string key = "ORDER_REF_" + Lib::itos(orderRef);
    string res = _store->get(key);
    return Lib::stoi(res);
}

CThostFtdcInputOrderField TradeSrv::_createOrder(int orderID, bool isBuy, int total, double price,
    // double stopPrice,
    TThostFtdcOffsetFlagEnType offsetFlag, // 开平标志
    TThostFtdcHedgeFlagEnType hedgeFlag, // 投机套保标志
    TThostFtdcOrderPriceTypeType priceType, // 报单价格条件
    TThostFtdcTimeConditionType timeCondition, // 有效期类型
    TThostFtdcVolumeConditionType volumeCondition, //成交量类型
    TThostFtdcContingentConditionType contingentCondition// 触发条件
    )
{
    CThostFtdcInputOrderField order = {0};

    strcpy(order.BrokerID, _brokerID.c_str()); ///经纪公司代码
    strcpy(order.InvestorID, _userID.c_str()); ///投资者代码
    strcpy(order.InstrumentID, _instrumnetID.c_str()); ///合约代码
    strcpy(order.UserID, _userID.c_str()); ///用户代码
    // strcpy(order.ExchangeID, "SHFE"); ///交易所代码

    order.MinVolume = 1;///最小成交量
    order.ForceCloseReason = THOST_FTDC_FCC_NotForceClose;///强平原因
    order.IsAutoSuspend = 0;///自动挂起标志
    order.UserForceClose = 0;///用户强评标志

    order.Direction = isBuy ? THOST_FTDC_D_Buy : THOST_FTDC_D_Sell; ///买卖方向
    order.VolumeTotalOriginal = total;///数量
    order.LimitPrice = price;///价格
    order.StopPrice = 0;///止损价

    ///组合开平标志
    //THOST_FTDC_OFEN_Open 开仓
    //THOST_FTDC_OFEN_Close 平仓
    //THOST_FTDC_OFEN_ForceClose 强平
    //THOST_FTDC_OFEN_CloseToday 平今
    //THOST_FTDC_OFEN_CloseYesterday 平昨
    //THOST_FTDC_OFEN_ForceOff 强减
    //THOST_FTDC_OFEN_LocalForceClose 本地强平
    order.CombOffsetFlag[0] = offsetFlag;
    if (THOST_FTDC_OFEN_ForceClose == offsetFlag) {
        order.ForceCloseReason = THOST_FTDC_FCC_Other; // 其他
        order.UserForceClose = 1;
    }

    ///组合投机套保标志
    // THOST_FTDC_HFEN_Speculation 投机
    // THOST_FTDC_HFEN_Arbitrage 套利
    // THOST_FTDC_HFEN_Hedge 套保
    order.CombHedgeFlag[0] = hedgeFlag;

    ///报单价格条件
    // THOST_FTDC_OPT_AnyPrice 任意价
    // THOST_FTDC_OPT_LimitPrice 限价
    // THOST_FTDC_OPT_BestPrice 最优价
    // THOST_FTDC_OPT_LastPrice 最新价
    // THOST_FTDC_OPT_LastPricePlusOneTicks 最新价浮动上浮1个ticks
    // THOST_FTDC_OPT_LastPricePlusTwoTicks 最新价浮动上浮2个ticks
    // THOST_FTDC_OPT_LastPricePlusThreeTicks 最新价浮动上浮3个ticks
    // THOST_FTDC_OPT_AskPrice1 卖一价
    // THOST_FTDC_OPT_AskPrice1PlusOneTicks 卖一价浮动上浮1个ticks
    // THOST_FTDC_OPT_AskPrice1PlusTwoTicks 卖一价浮动上浮2个ticks
    // THOST_FTDC_OPT_AskPrice1PlusThreeTicks 卖一价浮动上浮3个ticks
    // THOST_FTDC_OPT_BidPrice1 买一价
    // THOST_FTDC_OPT_BidPrice1PlusOneTicks 买一价浮动上浮1个ticks
    // THOST_FTDC_OPT_BidPrice1PlusTwoTicks 买一价浮动上浮2个ticks
    // THOST_FTDC_OPT_BidPrice1PlusThreeTicks 买一价浮动上浮3个ticks
    // THOST_FTDC_OPT_FiveLevelPrice 五档价
    order.OrderPriceType = priceType;

    ///有效期类型
    // THOST_FTDC_TC_IOC 立即完成，否则撤销
    // THOST_FTDC_TC_GFS 本节有效
    // THOST_FTDC_TC_GFD 当日有效
    // THOST_FTDC_TC_GTD 指定日期前有效
    // THOST_FTDC_TC_GTC 撤销前有效
    // THOST_FTDC_TC_GFA 集合竞价有效
    order.TimeCondition = timeCondition;

    ///成交量类型
    // THOST_FTDC_VC_AV 任何数量
    // THOST_FTDC_VC_MV 最小数量
    // THOST_FTDC_VC_CV 全部数量
    order.VolumeCondition = volumeCondition;

    ///触发条件
    // THOST_FTDC_CC_Immediately 立即
    // THOST_FTDC_CC_Touch 止损
    // THOST_FTDC_CC_TouchProfit 止赢
    // THOST_FTDC_CC_ParkedOrder 预埋单
    // THOST_FTDC_CC_LastPriceGreaterThanStopPrice 最新价大于条件价
    // THOST_FTDC_CC_LastPriceGreaterEqualStopPrice 最新价大于等于条件价
    // THOST_FTDC_CC_LastPriceLesserThanStopPrice 最新价小于条件价
    // THOST_FTDC_CC_LastPriceLesserEqualStopPrice 最新价小于等于条件价
    // THOST_FTDC_CC_AskPriceGreaterThanStopPrice 卖一价大于条件价
    // THOST_FTDC_CC_AskPriceGreaterEqualStopPrice 卖一价大于等于条件价
    // THOST_FTDC_CC_AskPriceLesserThanStopPrice 卖一价小于条件价
    // THOST_FTDC_CC_AskPriceLesserEqualStopPrice 卖一价小于等于条件价
    // THOST_FTDC_CC_BidPriceGreaterThanStopPrice 买一价大于条件价
    // THOST_FTDC_CC_BidPriceGreaterEqualStopPrice 买一价大于等于条件价
    // THOST_FTDC_CC_BidPriceLesserThanStopPrice 买一价小于条件价
    // THOST_FTDC_CC_BidPriceLesserEqualStopPrice 买一价小于等于条件价
    order.ContingentCondition = contingentCondition;

    ///报单引用
    sprintf(order.OrderRef, "%d", _getOrderRefByID(orderID));

    ///请求编号
    // _reqID++;
    // order.RequestID = _reqID;

    // order.GTDDate = ;///GTD日期
    // order.BusinessUnit = ;///业务单元
    // order.IsSwapOrder = ;///互换单标志
    // order.InvestUnitID = ;///投资单元代码
    // order.AccountID = ;///资金账号
    // order.CurrencyID = ;///币种代码
    // order.ClientID = ;///交易编码
    // order.IPAddress = ;///IP地址
    // order.MacAddress = ;///Mac地址

    return order;
}
