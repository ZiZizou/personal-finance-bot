#pragma once
#include <vector>
#include <utility>
#include <string>
#include "MarketData.h"

// --- Original ---
// Updated RSI for Adaptive Logic
float computeRSI(const std::vector<float>& prices, int period = 14);
float computeAdaptiveRSI(const std::vector<float>& prices, int basePeriod = 14);

std::pair<float, float> computeMACD(const std::vector<float>& prices);
float computeATR(const std::vector<Candle>& candles, int period = 14);

// Updated Cycle Detection (Fourier)
int detectCycle(const std::vector<float>& prices);

// --- New Analysis ---

// 1. Linear Forecast (Legacy)
float forecastPrice(const std::vector<float>& prices, int horizon = 30);

// 2. Polynomial Forecast (New - Curve Fitting)
float forecastPricePoly(const std::vector<float>& prices, int horizon = 30, int degree = 2);

// 3. GARCH(1,1) Volatility Forecast
// Returns next day's estimated volatility (sigma^2 or sigma)
float computeGARCHVolatility(const std::vector<float>& returns);

// 4. Support and Resistance
struct SupportResistance {
    float support;
    float resistance;
};
SupportResistance identifyLevels(const std::vector<float>& prices, int period = 60);

// 5. Price Targets 
std::vector<float> findLocalExtrema(const std::vector<float>& prices, int period = 60, bool findMaxima = true);

// 6. Bollinger Bands 
struct BollingerBands {
    float upper;
    float middle;
    float lower;
    float bandwidth; 
};
BollingerBands computeBollingerBands(const std::vector<float>& prices, int period = 20, float multiplier = 2.0f);

// 7. ADX
struct ADXResult {
    float adx;     
    float plusDI;  
    float minusDI; 
};
ADXResult computeADX(const std::vector<Candle>& candles, int period = 14);

// 8. Candlestick Patterns
struct PatternResult {
    std::string name;
    float score; 
};
PatternResult detectCandlestickPattern(const std::vector<Candle>& candles);

// 9. Volatility Squeeze 
bool checkVolatilitySqueeze(const std::vector<float>& prices, int lookback = 120, float percentile = 0.10f);