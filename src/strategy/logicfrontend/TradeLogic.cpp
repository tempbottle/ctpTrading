#include "TradeLogic.h"

TradeLogic::TradeLogic(int peroid, double threshold,
    int serviceID, string logPath, int isHistoryBack, int db)
{
    _peroid = peroid;
    _threshold = threshold;


    _logPath = logPath;
    _isHistoryBack = isHistoryBack;

    _store = new Redis("127.0.0.1", 6379, db);
    _tradeStrategySrvClient = new QClient(serviceID, sizeof(MSG_TO_TRADE_STRATEGY));

}

TradeLogic::~TradeLogic()
{
    // delete _store;
    // delete _tradeStrategySrvClient;
    cout << "~TradeLogicSrv" << endl;
}

void TradeLogic::init()
{

}

void TradeLogic::_tick(TickData tick)
{
    // 如果相邻tick没变化，则忽略
    if (_tickGroup.size() > 0) {
        TickData last = *(_tickGroup.begin());
        if (last.price == tick.price) return;
    }

    // 保存tick
    _tickGroup.push_front(tick);
    while (_tickGroup.size() >= 4) {
        _tickGroup.pop_back();
    }

    // tick足够三个，计算一组转换
    if (_tickGroup.size() < 3) return;
    TickData t[3];
    list<TickData>::iterator i;
    int j = 2;
    for (i = _tickGroup.begin(); i != _tickGroup.end(); i++) {
        t[j--] = *i;
    }
    if (t[0].price > t[1].price) {
        if (t[1].price > t[2].price) {
            _countDown2Down++;
            _transTypeList.push_front(TRANS_TYPE_DOWN2DOWN);
        } else {
            _countDown2Up++;
            _transTypeList.push_front(TRANS_TYPE_DOWN2UP);
        }
    } else {
        if (t[1].price > t[2].price) {
            _countUp2Up++;
            _transTypeList.push_front(TRANS_TYPE_UP2UP);
        } else {
            _countUp2Down++;
            _transTypeList.push_front(TRANS_TYPE_UP2DOWN);
        }
    }
    // 检查转换列表是否够用，够用删除响应的记录
    while (_transTypeList.size() > _peroid) {
        int type = *(_transTypeList.end());
        _transTypeList.pop_back();
        switch (type) {
            case TRANS_TYPE_DOWN2DOWN:
                _countDown2Down--;
                break;
            case TRANS_TYPE_UP2DOWN:
                _countUp2Down--;
                break;
            case TRANS_TYPE_DOWN2UP:
                _countDown2Up--;
                break;
            case TRANS_TYPE_UP2UP:
                _countUp2Up--;
                break;
            default:
                break;
        }
    }

}

void TradeLogic::onTick(TickData tick)
{
    _tick(tick);
}

void TradeLogic::_calculateUp()
{
    _pUp2Up = _countUp2Up / (_countUp2Up + _countDown2Up);
    _pUp2Down = _countUp2Down / (_countUp2Down + _countDown2Down);
    //log
    ofstream info;
    Lib::initInfoLogHandle(_logPath, info);
    info << "TradeLogicSrv[calculateUp]";
    info << "|pUp2Up|" << _pUp2Up;
    info << "|pUp2Down|" << _pUp2Down;
    info << "|kIndex|" << _kIndex << endl;
    info.close();
}

void TradeLogic::_calculateDown()
{
    _pDown2Up = _countDown2Up / (_countDown2Up + _countUp2Up);
    _pDown2Down = _countDown2Down / (_countDown2Down + _countUp2Down);
    //log
    ofstream info;
    Lib::initInfoLogHandle(_logPath, info);
    info << "TradeLogicSrv[calculateDown]";
    info << "|pDown2Up|" << _pDown2Up;
    info << "|pDown2Down|" << _pDown2Down;
    info << "|kIndex|" << _kIndex << endl;
    info.close();
}

void TradeLogic::onKLineClose(KLineBlock block, TickData tick)
{
    if (_transTypeList.size() < _peroid) return; // 计算转义概率条件不足，不做操作
    _kIndex = block.getIndex();
    TickData last = *(_tickGroup.begin());
    bool isUp = true;
    if (last.price > tick.price) isUp = false;

    if (isUp) {
        _calculateUp();
    } else {
        _calculateDown();
    }

    int status = _getStatus();
    switch (status) {

        case TRADE_STATUS_NOTHING: // 空仓，判断是否开仓
        case TRADE_STATUS_BUYOPENING: // 状态为正在买开仓，说明从上一个K线关闭一直没买成功，则放弃重新买
        case TRADE_STATUS_SELLOPENING: // 同上

            if (status == TRADE_STATUS_BUYOPENING || status == TRADE_STATUS_SELLOPENING) // 操作中，先取消
                _sendMsg(MSG_TRADE_CANCEL);

            if (isUp) {
                if (_pUp2Up > _threshold) { // 买开
                    _sendMsg(MSG_TRADE_BUYOPEN, tick.price);
                    break;
                }
                if (_pUp2Down > _threshold) { // 卖开
                    _sendMsg(MSG_TRADE_SELLOPEN, tick.price);
                    break;
                }
            } else {
                if (_pDown2Down > _threshold) { // 卖开
                    _sendMsg(MSG_TRADE_SELLOPEN, tick.price);
                    break;
                }
                if (_pDown2Up > _threshold) { // 买开
                    _sendMsg(MSG_TRADE_BUYOPEN, tick.price);
                    break;
                }
            }
            break;

        case TRADE_STATUS_BUYOPENED:
        case TRADE_STATUS_SELLCLOSING:

            if (status == TRADE_STATUS_SELLCLOSING) //
                _sendMsg(MSG_TRADE_CANCEL);

            if (isUp) {
                if (_pUp2Up <= _threshold) { // 不满足买开，平
                    _sendMsg(MSG_TRADE_SELLCLOSE, tick.bidPrice1);
                    break;
                }
            } else {
                if (_pDown2Up <= _threshold) { // 不满足买开，平
                    _sendMsg(MSG_TRADE_SELLCLOSE, tick.bidPrice1);
                    break;
                }
            }

            break;

        case TRADE_STATUS_SELLOPENED:
        case TRADE_STATUS_BUYCLOSING:

            if (status == TRADE_STATUS_BUYCLOSING)
                _sendMsg(MSG_TRADE_CANCEL);

            if (isUp) {
                if (_pUp2Down <= _threshold) { // 卖开
                    _sendMsg(MSG_TRADE_BUYCLOSE, tick.askPrice1);
                    break;
                }
            } else {
                if (_pDown2Down <= _threshold) { // 卖开
                    _sendMsg(MSG_TRADE_BUYCLOSE, tick.askPrice1);
                    break;
                }
            }

            break;

        default:
            break;
    }
    _tick(tick);
}

int TradeLogic::_getStatus()
{
    string status = _store->get("TRADE_STATUS");
    return Lib::stoi(status);
}

void TradeLogic::_sendMsg(int msgType, double price)
{
    MSG_TO_TRADE_STRATEGY msg = {0};
    msg.msgType = msgType;
    msg.price = price;
    msg.kIndex = _kIndex;
    if (msgType == MSG_TRADE_CANCEL)
        msg.kIndex--;
    _tradeStrategySrvClient->send((void *)&msg);

    //log
    ofstream info;
    Lib::initInfoLogHandle(_logPath, info);
    info << "TradeLogicSrv[sendMsg]";
    info << "|action|" << msgType;
    info << "|price|" << price;
    info << "|kIndex|" << msg.kIndex << endl;
    info.close();
}
