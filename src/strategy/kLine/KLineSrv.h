#ifndef K_LINE_SRV_H
#define K_LINE_SRV_H

#include "../../global.h"
#include "../../libs/Redis.h"
#include "../../protos/KLineBlock.h"
#include <cmath>

class KLineSrv
{
private:

    QClient * _tradeLogicSrvClient;
    KLineBlock * _currentBlock;
    Redis * _store;

    int _index;
    int _kRange;

    string _logPath;

    bool _isBlockExist();
    bool _checkBlockClose(TickData);
    void _initBlock(TickData);
    void _updateBlock(TickData);
    void _closeBlock(TickData);
    void _transTick(TickData);

public:
    KLineSrv(int, int, string, int);
    ~KLineSrv();

    void onTickCome(TickData);
};
#endif
