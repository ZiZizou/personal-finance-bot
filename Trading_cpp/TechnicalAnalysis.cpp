#include "TechnicalAnalysis.h"
#include <numeric>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <complex>
#include <vector>

const double PI = 3.14159265358979323846;

// --- Helper: Simple Matrix Math for Regression (No Eigen) ---
// Solves Ax = B where A is N*N and B is N*1
// Uses Gaussian Elimination
bool solveSystem(std::vector<std::vector<double>>& A, std::vector<double>& B, std::vector<double>& x) {
    int n = static_cast<int>(A.size());
    x.resize(n);

    for (int i = 0; i < n; ++i) {
        // Pivot
        int maxRow = i;
        for (int k = i + 1; k < n; ++k) {
            if (std::abs(A[k][i]) > std::abs(A[maxRow][i])) maxRow = k;
        }
        std::swap(A[i], A[maxRow]);
        std::swap(B[i], B[maxRow]);

        if (std::abs(A[i][i]) < 1e-12) return false; // Singular

        for (int k = i + 1; k < n; ++k) {
            double c = -A[k][i] / A[i][i];
            for (int j = i; j < n; ++j) {
                if (i == j) A[k][j] = 0;
                else A[k][j] += c * A[i][j];
            }
            B[k] += c * B[i];
        }
    }

    // Back subst
    for (int i = n - 1; i >= 0; --i) {
        double sum = 0.0;
        for (int j = i + 1; j < n; ++j) sum += A[i][j] * x[j];
        x[i] = (B[i] - sum) / A[i][i];
    }
    return true;
}

// Polynomial Regression using Normal Equation: (X^T * X) * Beta = X^T * Y
std::vector<double> polyFit(const std::vector<double>& y, int degree) {
    int n = static_cast<int>(y.size());
    int m = degree + 1;

    // Build X^T * X (Matrix A) and X^T * Y (Vector B)
    std::vector<std::vector<double>> A(m, std::vector<double>(m, 0.0));
    std::vector<double> B(m, 0.0);

    for (int i = 0; i < n; ++i) {
        double xi = static_cast<double>(i);
        double val = 1.0;
        std::vector<double> x_pow(m);
        for(int j=0; j<m; ++j) { x_pow[j] = val; val *= xi; }

        for (int row = 0; row < m; ++row) {
            for (int col = 0; col < m; ++col) {
                A[row][col] += x_pow[row] * x_pow[col];
            }
            B[row] += x_pow[row] * y[i];
        }
    }

    std::vector<double> coeffs;
    solveSystem(A, B, coeffs);
    return coeffs;
}

// --- Helper: Calculate Returns ---
std::vector<double> calculateLogReturns(const std::vector<double>& prices) {
    std::vector<double> returns;
    if (prices.size() < 2) return returns;
    for (size_t i = 1; i < prices.size(); ++i) {
        returns.push_back(std::log(prices[i] / prices[i - 1]));
    }
    return returns;
}

// --- 1. GARCH(1,1) Volatility Forecast ---
// Simplified implementation estimating Sigma^2_t = omega + alpha * epsilon^2_{t-1} + beta * Sigma^2_{t-1}
// We use hardcoded params typical for daily asset returns if optimization is too heavy,
// or run a simple iterative MLE if feasible. For "low-overhead", we will use standard params
// often cited for financial time series: alpha=0.05, beta=0.9, omega=long_run_var*(1-alpha-beta).
double computeGARCHVolatility(const std::vector<double>& returns) {
    if (returns.empty()) return 0.0;

    // 1. Calculate long-run variance (unconditional variance)
    double sum = 0.0;
    double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    for (double r : returns) sum += (r - mean) * (r - mean);
    double variance = sum / returns.size(); // Simple Variance

    // 2. GARCH Parameters (Typical defaults for daily data if not fitting)
    double alpha = 0.05; // Reaction to recent shocks
    double beta = 0.90;  // Persistence of volatility
    double omega = variance * (1.0 - alpha - beta);

    // 3. Iterate through history to update sigma^2
    double currentSigma2 = variance; // Initialize with long-run var

    for (double r : returns) {
        double epsilon = r - mean; // Shock
        currentSigma2 = omega + (alpha * epsilon * epsilon) + (beta * currentSigma2);
    }

    return std::sqrt(currentSigma2); // Return volatility (std dev), not variance
}


// --- 2. Adaptive RSI ---
// Scales period based on Volatility (Standard Deviation)
// High Volatility -> Shorter Period (Catch fast moves)
// Low Volatility -> Longer Period (Avoid noise)
double computeAdaptiveRSI(const std::vector<double>& prices, int basePeriod) {
    if (prices.size() < 30) return computeRSI(prices, basePeriod);

    // Calculate recent volatility (last 20 days)
    int volPeriod = 20;
    double sum = 0.0;
    for(size_t i=prices.size()-volPeriod; i<prices.size(); ++i) sum += prices[i];
    double mean = sum / volPeriod;
    double sqSum = 0.0;
    for(size_t i=prices.size()-volPeriod; i<prices.size(); ++i) sqSum += (prices[i]-mean)*(prices[i]-mean);
    double stdDev = std::sqrt(sqSum / volPeriod);

    // Normalize StdDev relative to price (CV)
    double cv = stdDev / mean;

    // Scaling Factor: If CV is high (>2%), shorten period. If low (<1%), lengthen.
    // Base CV approx 0.015 for moderate stock.
    double scaler = 0.015 / (cv + 0.0001); // Avoid div by zero

    int newPeriod = static_cast<int>(basePeriod * scaler);
    newPeriod = std::clamp(newPeriod, 7, 28); // Clamp between 7 and 28

    return computeRSI(prices, newPeriod);
}

// --- 3. Fourier Cycle Detection (Enhanced) ---
// Uses Discrete Fourier Transform (DFT) to find dominant frequency
int detectCycle(const std::vector<double>& prices) {
    size_t N = prices.size();
    if (N < 40) return 0; // Need enough data

    // Detrend data (Linear Regression removal)
    std::vector<double> detrended(N);
    double x_mean = (N - 1) / 2.0;
    double y_mean = std::accumulate(prices.begin(), prices.end(), 0.0) / N;

    double num = 0.0, den = 0.0;
    for(size_t i=0; i<N; ++i) {
        num += (i - x_mean) * (prices[i] - y_mean);
        den += (i - x_mean) * (i - x_mean);
    }
    double slope = num / den;
    double intercept = y_mean - slope * x_mean;

    for(size_t i=0; i<N; ++i) {
        detrended[i] = prices[i] - (slope * i + intercept);
    }

    // DFT for lower frequencies (Periods 10 to N/2)
    double maxPower = 0.0;
    int dominantPeriod = 0;

    // We check periods from 5 to 60 days
    for (int P = 5; P <= 60 && P < (int)N/2; ++P) {
        double real = 0.0;
        double imag = 0.0;
        double k = static_cast<double>(N) / P; // Frequency index approx

        for (size_t n = 0; n < N; ++n) {
            double angle = 2.0 * PI * k * n / N;
            real += detrended[n] * std::cos(angle);
            imag -= detrended[n] * std::sin(angle);
        }
        double power = std::sqrt(real*real + imag*imag);
        if (power > maxPower) {
            maxPower = power;
            dominantPeriod = P;
        }
    }

    return dominantPeriod;
}


// --- Legacy Helpers (EMA/RSI/MACD/ATR) ---

std::vector<double> computeEMA(const std::vector<double>& data, int period) {
    std::vector<double> ema;
    if (data.empty() || (int)data.size() < period) return ema;
    ema.resize(data.size());
    double sum = 0.0;
    for (int i = 0; i < period; ++i) sum += data[i];
    ema[period - 1] = sum / period;
    double multiplier = 2.0 / (period + 1.0);
    for (size_t i = period; i < data.size(); ++i) {
        ema[i] = (data[i] - ema[i - 1]) * multiplier + ema[i - 1];
    }
    return ema;
}

double computeRSI(const std::vector<double>& prices, int period) {
    if (prices.size() <= (size_t)period) return 50.0;
    double avgUp = 0.0, avgDown = 0.0;
    for (int i = 1; i <= period; ++i) {
        double diff = prices[i] - prices[i - 1];
        if (diff > 0) avgUp += diff; else avgDown -= diff;
    }
    avgUp /= period; avgDown /= period;
    for (size_t i = period + 1; i < prices.size(); ++i) {
        double diff = prices[i] - prices[i - 1];
        double up = (diff > 0) ? diff : 0.0;
        double down = (diff < 0) ? -diff : 0.0;
        avgUp = (avgUp * (period - 1) + up) / period;
        avgDown = (avgDown * (period - 1) + down) / period;
    }
    if (avgDown == 0.0) return 100.0;
    double rs = avgUp / avgDown;
    return 100.0 - (100.0 / (1.0 + rs));
}

std::pair<double, double> computeMACD(const std::vector<double>& prices) {
    if (prices.size() < 26) return {0.0, 0.0};
    std::vector<double> ema12 = computeEMA(prices, 12);
    std::vector<double> ema26 = computeEMA(prices, 26);
    std::vector<double> macdLine;
    macdLine.resize(prices.size());
    for (size_t i = 25; i < prices.size(); ++i) macdLine[i] = ema12[i] - ema26[i];
    std::vector<double> validMacd;
    for (size_t i = 25; i < macdLine.size(); ++i) validMacd.push_back(macdLine[i]);
    if (validMacd.empty()) return {0.0, 0.0};
    std::vector<double> signalLine = computeEMA(validMacd, 9);
    if (signalLine.empty()) return {0.0, 0.0};
    return {validMacd.back(), signalLine.back()};
}

double computeATR(const std::vector<Candle>& candles, int period) {
    if (candles.size() <= (size_t)period) return 0.0;
    std::vector<double> trs;
    trs.push_back(candles[0].high - candles[0].low);
    for (size_t i = 1; i < candles.size(); ++i) {
        double hl = candles[i].high - candles[i].low;
        double hpc = std::abs(candles[i].high - candles[i-1].close);
        double lpc = std::abs(candles[i].low - candles[i-1].close);
        trs.push_back(std::max({hl, hpc, lpc}));
    }
    double atr = 0.0;
    for (int i = 0; i < period; ++i) atr += trs[i];
    atr /= period;
    for (size_t i = period; i < trs.size(); ++i) {
        atr = (atr * (period - 1) + trs[i]) / period;
    }
    return atr;
}

double forecastPrice(const std::vector<double>& prices, int horizon) {
    if (prices.size() < 2) return prices.empty() ? 0.0 : prices.back();
    // Linear is Poly degree 1
    std::vector<double> coeffs = polyFit(prices, 1);
    if (coeffs.empty()) return prices.back();

    double x = static_cast<double>(prices.size() - 1 + horizon);
    return coeffs[0] + coeffs[1] * x;
}

// Polynomial Regression (Degree 2 = Parabola)
double forecastPricePoly(const std::vector<double>& prices, int horizon, int degree) {
    if (prices.size() < (size_t)degree + 1) return prices.empty() ? 0.0 : prices.back();

    std::vector<double> coeffs = polyFit(prices, degree);
    if (coeffs.empty()) return prices.back();

    double x = static_cast<double>(prices.size() - 1 + horizon);
    double val = 1.0;
    double y = 0.0;

    for (double c : coeffs) {
        y += c * val;
        val *= x;
    }
    return y;
}

// Support/Resistance (Min/Max of last Period)
SupportResistance identifyLevels(const std::vector<double>& prices, int period) {
    SupportResistance levels = {0.0, 0.0};
    if (prices.empty()) return levels;

    int start = std::max(0, (int)prices.size() - period);

    double minP = 1e18;
    double maxP = -1e18;

    for (int i = start; i < (int)prices.size(); ++i) {
        if (prices[i] < minP) minP = prices[i];
        if (prices[i] > maxP) maxP = prices[i];
    }
    levels.support = minP;
    levels.resistance = maxP;
    return levels;
}

// Find Local Extrema (Peaks and Valleys)
std::vector<double> findLocalExtrema(const std::vector<double>& prices, int period, bool findMaxima) {
    std::vector<double> targets;
    if (prices.size() < 10) return targets;

    int start = std::max(0, (int)prices.size() - period);
    int window = 5; // Check 5 days left and right to confirm significance

    for (int i = start + window; i < (int)prices.size() - window; ++i) {
        bool isExtremum = true;
        double current = prices[i];

        for (int k = 1; k <= window; ++k) {
            if (findMaxima) {
                if (prices[i - k] > current || prices[i + k] > current) {
                    isExtremum = false; break;
                }
            } else {
                if (prices[i - k] < current || prices[i + k] < current) {
                    isExtremum = false; break;
                }
            }
        }

        if (isExtremum) {
            targets.push_back(current);
        }
    }

    // Sort targets
    std::sort(targets.begin(), targets.end());

    // Filter duplicates/too close values (within 1%)
    if (targets.empty()) return targets;

    std::vector<double> uniqueTargets;
    uniqueTargets.push_back(targets[0]);

    for (size_t i = 1; i < targets.size(); ++i) {
        if (targets[i] > uniqueTargets.back() * 1.01) {
            uniqueTargets.push_back(targets[i]);
        }
    }

    return uniqueTargets;
}

// Bollinger Bands Implementation
BollingerBands computeBollingerBands(const std::vector<double>& prices, int period, double multiplier) {
    BollingerBands bb = {0.0, 0.0, 0.0, 0.0};
    size_t n = prices.size();
    if (n < (size_t)period) return bb;

    // 1. Compute SMA (Middle Band)
    double sum = 0.0;
    for (size_t i = n - period; i < n; ++i) sum += prices[i];
    bb.middle = sum / period;

    // 2. Compute Standard Deviation
    double varianceSum = 0.0;
    for (size_t i = n - period; i < n; ++i) {
        double diff = prices[i] - bb.middle;
        varianceSum += diff * diff;
    }
    double stdDev = std::sqrt(varianceSum / period);

    // 3. Compute Bands
    bb.upper = bb.middle + (multiplier * stdDev);
    bb.lower = bb.middle - (multiplier * stdDev);

    if (bb.middle > 0) bb.bandwidth = (bb.upper - bb.lower) / bb.middle;

    return bb;
}

// ADX Implementation
ADXResult computeADX(const std::vector<Candle>& candles, int period) {
    ADXResult res = {0.0, 0.0, 0.0};
    if (candles.size() < (size_t)period * 2) return res; // Need warmup

    std::vector<double> tr(candles.size(), 0.0);
    std::vector<double> plusDM(candles.size(), 0.0);
    std::vector<double> minusDM(candles.size(), 0.0);

    // 1. Calculate TR, +DM, -DM per candle
    for (size_t i = 1; i < candles.size(); ++i) {
        double highDiff = candles[i].high - candles[i-1].high;
        double lowDiff = candles[i-1].low - candles[i].low;

        if (highDiff > lowDiff && highDiff > 0) plusDM[i] = highDiff;
        if (lowDiff > highDiff && lowDiff > 0) minusDM[i] = lowDiff;

        double hl = candles[i].high - candles[i].low;
        double hpc = std::abs(candles[i].high - candles[i-1].close);
        double lpc = std::abs(candles[i].low - candles[i-1].close);
        tr[i] = std::max({hl, hpc, lpc});
    }

    // 2. Initial Smooth (First 'period' sum)

    double smoothTR = 0.0;
    double smoothPlusDM = 0.0;
    double smoothMinusDM = 0.0;

    for (int i = 1; i <= period; ++i) {
        smoothTR += tr[i];
        smoothPlusDM += plusDM[i];
        smoothMinusDM += minusDM[i];
    }

    // 3. Rolling Smooth & DX Calculation
    std::vector<double> dx;
    for (size_t i = period + 1; i < candles.size(); ++i) {
        // Wilder's Smoothing
        smoothTR = smoothTR - (smoothTR / period) + tr[i];
        smoothPlusDM = smoothPlusDM - (smoothPlusDM / period) + plusDM[i];
        smoothMinusDM = smoothMinusDM - (smoothMinusDM / period) + minusDM[i];

        double pDI = (smoothTR == 0) ? 0 : (100.0 * smoothPlusDM / smoothTR);
        double mDI = (smoothTR == 0) ? 0 : (100.0 * smoothMinusDM / smoothTR);

        double diSum = pDI + mDI;
        double dxVal = (diSum == 0) ? 0 : (100.0 * std::abs(pDI - mDI) / diSum);
        dx.push_back(dxVal);

        // Store last DI for return
        if (i == candles.size() - 1) {
            res.plusDI = pDI;
            res.minusDI = mDI;
        }
    }

    // 4. ADX is SMA of DX
    if (dx.size() < (size_t)period) return res;

    double adxSum = 0.0;
    // Initial ADX
    for(int i=0; i<period; ++i) adxSum += dx[i];
    double finalADX = adxSum / period;

    // Smoothing ADX
    for(size_t i=period; i<dx.size(); ++i) {
         finalADX = ((finalADX * (period - 1)) + dx[i]) / period;
    }

    res.adx = finalADX;
    return res;
}

PatternResult detectCandlestickPattern(const std::vector<Candle>& candles) {
    PatternResult res = {"", 0.0};
    if (candles.size() < 3) return res;

    // Get last candle
    Candle c = candles.back();
    Candle p = candles[candles.size() - 2]; // Previous

    double body = std::abs(c.close - c.open);
    double range = c.high - c.low;
    double upperShadow = c.high - std::max(c.open, c.close);
    double lowerShadow = std::min(c.open, c.close) - c.low;

    double avgBody = 0.0;
    for(int i=1; i<=3; i++) avgBody += std::abs(candles[candles.size()-i].close - candles[candles.size()-i].open);
    avgBody /= 3.0;

    bool isBullish = c.close > c.open;
    bool isBearish = c.close < c.open;

    // 1. Hammer
    if (lowerShadow > 2.0 * body && upperShadow < body * 0.5 && isBullish) {
        res.name = "Hammer";
        res.score = 0.5;
        return res;
    }

    // 2. Shooting Star
    if (upperShadow > 2.0 * body && lowerShadow < body * 0.5 && isBearish) {
        res.name = "Shooting Star";
        res.score = -0.5;
        return res;
    }

    // 3. Bullish Engulfing
    bool pBearish = p.close < p.open;
    if (pBearish && isBullish && c.close > p.open && c.open < p.close) {
         res.name = "Bullish Engulfing";
         res.score = 0.6;
         return res;
    }

    // 4. Bearish Engulfing
    bool pBullish = p.close > p.open;
    if (pBullish && isBearish && c.close < p.open && c.open > p.close) {
        res.name = "Bearish Engulfing";
        res.score = -0.6;
        return res;
    }

    // 5. Doji
    if (body < 0.1 * range && range > avgBody) {
        res.name = "Doji";
        res.score = 0.0;
        return res;
    }

    return res;
}

double computeVWAP(const std::vector<Candle>& candles) {
    return computeVWAP(candles, static_cast<int>(candles.size()));
}

double computeVWAP(const std::vector<Candle>& candles, int lookback) {
    if (candles.empty()) return 0.0;

    int start = std::max(0, static_cast<int>(candles.size()) - lookback);
    double cumTPV = 0.0;  // cumulative(typicalPrice * volume)
    double cumVol = 0.0;

    for (int i = start; i < static_cast<int>(candles.size()); ++i) {
        double typicalPrice = (candles[i].high + candles[i].low + candles[i].close) / 3.0;
        double vol = static_cast<double>(candles[i].volume);
        cumTPV += typicalPrice * vol;
        cumVol += vol;
    }

    // Fall back to close price if no volume data
    if (cumVol < 1.0) {
        return candles.back().close;
    }

    return cumTPV / cumVol;
}

bool checkVolatilitySqueeze(const std::vector<double>& prices, int lookback, double percentile) {
    if (prices.size() < (size_t)lookback) return false;

    std::vector<double> history;
    int bbPeriod = 20;

    for (int i = 0; i < lookback; ++i) {
        int endIdx = (int)prices.size() - 1 - i;
        if (endIdx < bbPeriod) break;

        // Compute BB for this index
        double sum = 0.0;
        for(int k=0; k<bbPeriod; ++k) sum += prices[endIdx - k];
        double sma = sum / bbPeriod;

        double varSum = 0.0;
        for(int k=0; k<bbPeriod; ++k) {
            double d = prices[endIdx - k] - sma;
            varSum += d*d;
        }
        double stdDev = std::sqrt(varSum / bbPeriod);
        double upper = sma + 2*stdDev;
        double lower = sma - 2*stdDev;

        if (sma > 0) {
            double bw = (upper - lower) / sma;
            history.push_back(bw);
        }
    }

    if (history.empty()) return false;

    // Current bandwidth is history[0]
    double currentBW = history[0];

    // Sort history to find percentile
    std::sort(history.begin(), history.end());

    // If current is in the bottom 'percentile' portion
    size_t thresholdIdx = static_cast<size_t>(history.size() * percentile);
    if (thresholdIdx >= history.size()) thresholdIdx = history.size() - 1;

    double thresholdBW = history[thresholdIdx];

    return currentBW <= thresholdBW;
}
