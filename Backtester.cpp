#include "Backtester.h"
#include "TradingStrategy.h"
#include "TechnicalAnalysis.h" // For levels
#include "MLPredictor.h" 
#include <iostream>
#include <cmath>
#include <numeric>

// Dummy function to mimic generating signal on a slice
// We cannot easily call the full 'generateSignal' because it requires external dependencies 
// (Sentiment, Fund, OnChain) which are not easily sliced historically without huge data.
// For this prototype, we simulate a simplified version of the strategy logic 
// using only the technicals available in the candles.

int simulateDecision(const std::vector<Candle>& history) {
    if (history.size() < 50) return 0; 

    std::vector<float> closes;
    for(const auto& c : history) closes.push_back(c.close);
    
    // Indicators
    BollingerBands bb = computeBollingerBands(closes, 20, 2.0f);
    float rsi = computeRSI(closes, 14);
    float current = closes.back();
    
    // Strategy: Mean Reversion
    // Buy: Oversold (RSI < 30) OR Price below Lower Band
    if (rsi < 30.0f || current < bb.lower) return 1;
    
    // Sell: Overbought (RSI > 70) OR Price above Upper Band
    if (rsi > 70.0f || current > bb.upper) return -1;
    
    return 0;
}

BacktestResult Backtester::run(const std::vector<Candle>& candles) {
    BacktestResult res = {0.0f, 0.0f, 0.0f, 0, 0};
    if (candles.size() < 100) return res;

    float cash = 10000.0f;
    float holdings = 0.0f; 
    float initialBalance = cash;
    float entryPrice = 0.0f; // Track entry for win calc
    
    std::vector<float> dailyBalances;
    float peakBalance = initialBalance;

    for (size_t i = 60; i < candles.size(); ++i) {
        std::vector<Candle> history(candles.begin(), candles.begin() + i + 1);
        float price = candles[i].close;
        
        int action = simulateDecision(history);
        
        // Execute 
        if (action == 1 && cash > 0) {
            // Buy
            holdings = cash / price;
            cash = 0;
            entryPrice = price; 
            res.trades++;
        } else if (action == -1 && holdings > 0) {
            // Sell
            float proceeds = holdings * price;
            if (price > entryPrice) res.wins++; // Simple win check
            
            cash = proceeds;
            holdings = 0;
            res.trades++;
        }
        
        // Track Balance
        float currentBalance = cash + (holdings * price);
        dailyBalances.push_back(currentBalance);
        
        if (currentBalance > peakBalance) peakBalance = currentBalance;
        float drawdown = (peakBalance - currentBalance) / peakBalance;
        if (drawdown > res.maxDrawdown) res.maxDrawdown = drawdown;
    }
    
    float finalBalance = cash + (holdings * candles.back().close);
    res.totalReturn = (finalBalance - initialBalance) / initialBalance;

    // Compute Sharpe
    if (dailyBalances.size() > 1) {
        std::vector<float> returns;
        for(size_t i=1; i<dailyBalances.size(); ++i) {
            if (dailyBalances[i-1] > 0)
                returns.push_back((dailyBalances[i] - dailyBalances[i-1]) / dailyBalances[i-1]);
        }
        
        if (!returns.empty()) {
            float meanRet = std::accumulate(returns.begin(), returns.end(), 0.0f) / returns.size();
            float sqSum = 0.0f;
            for(float r : returns) sqSum += (r - meanRet) * (r - meanRet);
            float stdDev = std::sqrt(sqSum / returns.size());
            
            if (stdDev > 1e-9)
                res.sharpeRatio = (meanRet / stdDev) * std::sqrt(252.0f);
        }
    }

    return res;
}
