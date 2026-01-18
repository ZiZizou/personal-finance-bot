#pragma once
#include <vector>
#include "MarketData.h"

struct BacktestResult {
    float totalReturn;
    float sharpeRatio;
    float maxDrawdown;
    int trades;
    int wins;
};

// Simulate strategy on historical data
// 'candles' is the full history. 
// 'windowSize' is the rolling window for each decision step.
class Backtester {
public:
    static BacktestResult run(const std::vector<Candle>& candles);
};
