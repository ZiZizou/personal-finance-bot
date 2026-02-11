#pragma once
#include <vector>
#include <utility>
#include <string>
#include "MarketData.h"

// --- Original ---
// Updated RSI for Adaptive Logic
double computeRSI(const std::vector<double>& prices, int period = 14);
double computeAdaptiveRSI(const std::vector<double>& prices, int basePeriod = 14);

std::pair<double, double> computeMACD(const std::vector<double>& prices);
double computeATR(const std::vector<Candle>& candles, int period = 14);

// Updated Cycle Detection (Fourier)
int detectCycle(const std::vector<double>& prices);

// --- New Analysis ---

// 1. Linear Forecast (Legacy)
double forecastPrice(const std::vector<double>& prices, int horizon = 30);

// 2. Polynomial Forecast (New - Curve Fitting)
double forecastPricePoly(const std::vector<double>& prices, int horizon = 30, int degree = 2);

// 3. GARCH(1,1) Volatility Forecast
// Returns next day's estimated volatility (sigma^2 or sigma)
double computeGARCHVolatility(const std::vector<double>& returns);

// 4. Support and Resistance
struct SupportResistance {
    double support;
    double resistance;
};
SupportResistance identifyLevels(const std::vector<double>& prices, int period = 60);

// 5. Price Targets
std::vector<double> findLocalExtrema(const std::vector<double>& prices, int period = 60, bool findMaxima = true);

// 6. Bollinger Bands
struct BollingerBands {
    double upper;
    double middle;
    double lower;
    double bandwidth;
};
BollingerBands computeBollingerBands(const std::vector<double>& prices, int period = 20, double multiplier = 2.0);

// 7. ADX
struct ADXResult {
    double adx;
    double plusDI;
    double minusDI;
};
ADXResult computeADX(const std::vector<Candle>& candles, int period = 14);

// 8. Candlestick Patterns
struct PatternResult {
    std::string name;
    double score;
};
PatternResult detectCandlestickPattern(const std::vector<Candle>& candles);

// 9. Volatility Squeeze
bool checkVolatilitySqueeze(const std::vector<double>& prices, int lookback = 120, double percentile = 0.10);