#pragma once
#include <vector>
#include <utility>
#include <string>
#include "MarketData.h"

// --- Original ---
float computeRSI(const std::vector<float>& prices, int period = 14);
std::pair<float, float> computeMACD(const std::vector<float>& prices);
float computeATR(const std::vector<Candle>& candles, int period = 14);
int detectCycle(const std::vector<float>& prices);

// --- New Analysis ---

// 1. Linear Forecast (Legacy)
float forecastPrice(const std::vector<float>& prices, int horizon = 30);

// 2. Polynomial Forecast (New - Curve Fitting)
// degree: 2 for Parabola/U-shape
float forecastPricePoly(const std::vector<float>& prices, int horizon = 30, int degree = 2);

// 3. On-Balance Volume (Momentum of Money)
float computeOBV(const std::vector<Candle>& candles);

// 4. Support and Resistance
struct SupportResistance {
    float support;
    float resistance;
};
SupportResistance identifyLevels(const std::vector<float>& prices, int period = 60);

// 5. Price Targets (Local Maxima/Minima)
// Returns a list of sorted price levels (targets)
// if findMaxima is true, returns local highs (Resistance) for selling.
// if findMaxima is false, returns local lows (Support) for buying/covering.
std::vector<float> findLocalExtrema(const std::vector<float>& prices, int period = 60, bool findMaxima = true);

// 6. Bollinger Bands (Volatility & Relative Value)
struct BollingerBands {
    float upper;
    float middle;
    float lower;
    float bandwidth; // (Upper - Lower) / Middle
};
BollingerBands computeBollingerBands(const std::vector<float>& prices, int period = 20, float multiplier = 2.0f);

// 7. Average Directional Index (Trend Strength)
struct ADXResult {
    float adx;     // 0-100. >25 implies strong trend
    float plusDI;  // Bullish pressure
    float minusDI; // Bearish pressure
};
ADXResult computeADX(const std::vector<Candle>& candles, int period = 14);

// 8. Candlestick Patterns
struct PatternResult {
    std::string name;
    float score; // positive (bullish), negative (bearish), 0 (none)
};
PatternResult detectCandlestickPattern(const std::vector<Candle>& candles);

// 9. Volatility Squeeze (Low Bandwidth)
// Returns true if current bandwidth is in the lowest 'percentile' of the last 'lookback' period
bool checkVolatilitySqueeze(const std::vector<float>& prices, int lookback = 120, float percentile = 0.10f);

// Template generic
template<int Period>
float computeSMA(const std::vector<float>& prices) {
    if (prices.size() < Period) return 0.0f;
    float sum = 0.0f;
    for (size_t i = prices.size() - Period; i < prices.size(); ++i) {
        sum += prices[i];
    }
    return sum / Period;
}
