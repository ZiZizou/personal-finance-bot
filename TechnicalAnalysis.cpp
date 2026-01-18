#include "TechnicalAnalysis.h"
#include <numeric>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <complex>
#include <vector>

const float PI = 3.14159265358979323846f;

// --- Helper: Simple Matrix Math for Regression (No Eigen) ---
// Solves Ax = B where A is N*N and B is N*1
// Uses Gaussian Elimination
bool solveSystem(std::vector<std::vector<float>>& A, std::vector<float>& B, std::vector<float>& x) {
    int n = A.size();
    x.resize(n);

    for (int i = 0; i < n; ++i) {
        // Pivot
        int maxRow = i;
        for (int k = i + 1; k < n; ++k) {
            if (std::abs(A[k][i]) > std::abs(A[maxRow][i])) maxRow = k;
        }
        std::swap(A[i], A[maxRow]);
        std::swap(B[i], B[maxRow]);

        if (std::abs(A[i][i]) < 1e-9) return false; // Singular

        for (int k = i + 1; k < n; ++k) {
            float c = -A[k][i] / A[i][i];
            for (int j = i; j < n; ++j) {
                if (i == j) A[k][j] = 0;
                else A[k][j] += c * A[i][j];
            }
            B[k] += c * B[i];
        }
    }

    // Back subst
    for (int i = n - 1; i >= 0; --i) {
        float sum = 0.0f;
        for (int j = i + 1; j < n; ++j) sum += A[i][j] * x[j];
        x[i] = (B[i] - sum) / A[i][i];
    }
    return true;
}

// Polynomial Regression using Normal Equation: (X^T * X) * Beta = X^T * Y
std::vector<float> polyFit(const std::vector<float>& y, int degree) {
    int n = y.size();
    int m = degree + 1;
    
    // Build X^T * X (Matrix A) and X^T * Y (Vector B)
    std::vector<std::vector<float>> A(m, std::vector<float>(m, 0.0f));
    std::vector<float> B(m, 0.0f);

    for (int i = 0; i < n; ++i) {
        float xi = (float)i;
        float val = 1.0f;
        std::vector<float> x_pow(m);
        for(int j=0; j<m; ++j) { x_pow[j] = val; val *= xi; }

        for (int row = 0; row < m; ++row) {
            for (int col = 0; col < m; ++col) {
                A[row][col] += x_pow[row] * x_pow[col];
            }
            B[row] += x_pow[row] * y[i];
        }
    }
    
    std::vector<float> coeffs;
    solveSystem(A, B, coeffs);
    return coeffs;
}

// --- Helper: Calculate Returns ---
std::vector<float> calculateLogReturns(const std::vector<float>& prices) {
    std::vector<float> returns;
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
float computeGARCHVolatility(const std::vector<float>& returns) {
    if (returns.empty()) return 0.0f;
    
    // 1. Calculate long-run variance (unconditional variance)
    float sum = 0.0f;
    float mean = std::accumulate(returns.begin(), returns.end(), 0.0f) / returns.size();
    for (float r : returns) sum += (r - mean) * (r - mean);
    float variance = sum / returns.size(); // Simple Variance

    // 2. GARCH Parameters (Typical defaults for daily data if not fitting)
    float alpha = 0.05f; // Reaction to recent shocks
    float beta = 0.90f;  // Persistence of volatility
    float omega = variance * (1.0f - alpha - beta); 
    
    // 3. Iterate through history to update sigma^2
    float currentSigma2 = variance; // Initialize with long-run var
    
    for (float r : returns) {
        float epsilon = r - mean; // Shock
        currentSigma2 = omega + (alpha * epsilon * epsilon) + (beta * currentSigma2);
    }
    
    return std::sqrt(currentSigma2); // Return volatility (std dev), not variance
}


// --- 2. Adaptive RSI ---
// Scales period based on Volatility (Standard Deviation)
// High Volatility -> Shorter Period (Catch fast moves)
// Low Volatility -> Longer Period (Avoid noise)
float computeAdaptiveRSI(const std::vector<float>& prices, int basePeriod) {
    if (prices.size() < 30) return computeRSI(prices, basePeriod);

    // Calculate recent volatility (last 20 days)
    int volPeriod = 20;
    float sum = 0.0f;
    for(size_t i=prices.size()-volPeriod; i<prices.size(); ++i) sum += prices[i];
    float mean = sum / volPeriod;
    float sqSum = 0.0f;
    for(size_t i=prices.size()-volPeriod; i<prices.size(); ++i) sqSum += (prices[i]-mean)*(prices[i]-mean);
    float stdDev = std::sqrt(sqSum / volPeriod);
    
    // Normalize StdDev relative to price (CV)
    float cv = stdDev / mean;
    
    // Scaling Factor: If CV is high (>2%), shorten period. If low (<1%), lengthen.
    // Base CV approx 0.015 for moderate stock.
    float scaler = 0.015f / (cv + 0.0001f); // Avoid div by zero
    
    int newPeriod = (int)(basePeriod * scaler);
    newPeriod = std::clamp(newPeriod, 7, 28); // Clamp between 7 and 28
    
    return computeRSI(prices, newPeriod);
}

// --- 3. Fourier Cycle Detection (Enhanced) ---
// Uses Discrete Fourier Transform (DFT) to find dominant frequency
int detectCycle(const std::vector<float>& prices) {
    size_t N = prices.size();
    if (N < 40) return 0; // Need enough data

    // Detrend data (Linear Regression removal)
    std::vector<float> detrended(N);
    float x_mean = (N - 1) / 2.0f;
    float y_mean = std::accumulate(prices.begin(), prices.end(), 0.0f) / N;
    
    float num = 0.0f, den = 0.0f;
    for(size_t i=0; i<N; ++i) {
        num += (i - x_mean) * (prices[i] - y_mean);
        den += (i - x_mean) * (i - x_mean);
    }
    float slope = num / den;
    float intercept = y_mean - slope * x_mean;
    
    for(size_t i=0; i<N; ++i) {
        detrended[i] = prices[i] - (slope * i + intercept);
    }

    // DFT for lower frequencies (Periods 10 to N/2)
    float maxPower = 0.0f;
    int dominantPeriod = 0;

    // We check periods from 5 to 60 days
    for (int P = 5; P <= 60 && P < (int)N/2; ++P) {
        float real = 0.0f;
        float imag = 0.0f;
        float k = (float)N / P; // Frequency index approx
        
        for (size_t n = 0; n < N; ++n) {
            float angle = 2.0f * PI * k * n / N;
            real += detrended[n] * std::cos(angle);
            imag -= detrended[n] * std::sin(angle);
        }
        float power = std::sqrt(real*real + imag*imag);
        if (power > maxPower) {
            maxPower = power;
            dominantPeriod = P;
        }
    }
    
    return dominantPeriod;
}


// --- Legacy Helpers (EMA/RSI/MACD/ATR) ---

std::vector<float> computeEMA(const std::vector<float>& data, int period) {
    std::vector<float> ema;
    if (data.empty() || (int)data.size() < period) return ema;
    ema.resize(data.size());
    float sum = 0.0f;
    for (int i = 0; i < period; ++i) sum += data[i];
    ema[period - 1] = sum / period;
    float multiplier = 2.0f / (period + 1.0f);
    for (size_t i = period; i < data.size(); ++i) {
        ema[i] = (data[i] - ema[i - 1]) * multiplier + ema[i - 1];
    }
    return ema;
}

float computeRSI(const std::vector<float>& prices, int period) {
    if (prices.size() <= (size_t)period) return 50.0f;
    float avgUp = 0.0f, avgDown = 0.0f;
    for (int i = 1; i <= period; ++i) {
        float diff = prices[i] - prices[i - 1];
        if (diff > 0) avgUp += diff; else avgDown -= diff;
    }
    avgUp /= period; avgDown /= period;
    for (size_t i = period + 1; i < prices.size(); ++i) {
        float diff = prices[i] - prices[i - 1];
        float up = (diff > 0) ? diff : 0.0f;
        float down = (diff < 0) ? -diff : 0.0f;
        avgUp = (avgUp * (period - 1) + up) / period;
        avgDown = (avgDown * (period - 1) + down) / period;
    }
    if (avgDown == 0.0f) return 100.0f;
    float rs = avgUp / avgDown;
    return 100.0f - (100.0f / (1.0f + rs));
}

std::pair<float, float> computeMACD(const std::vector<float>& prices) {
    if (prices.size() < 26) return {0.0f, 0.0f};
    std::vector<float> ema12 = computeEMA(prices, 12);
    std::vector<float> ema26 = computeEMA(prices, 26);
    std::vector<float> macdLine;
    macdLine.resize(prices.size());
    for (size_t i = 25; i < prices.size(); ++i) macdLine[i] = ema12[i] - ema26[i];
    std::vector<float> validMacd;
    for (size_t i = 25; i < macdLine.size(); ++i) validMacd.push_back(macdLine[i]);
    if (validMacd.empty()) return {0.0f, 0.0f};
    std::vector<float> signalLine = computeEMA(validMacd, 9);
    if (signalLine.empty()) return {0.0f, 0.0f};
    return {validMacd.back(), signalLine.back()};
}

float computeATR(const std::vector<Candle>& candles, int period) {
    if (candles.size() <= (size_t)period) return 0.0f;
    std::vector<float> trs;
    trs.push_back(candles[0].high - candles[0].low);
    for (size_t i = 1; i < candles.size(); ++i) {
        float hl = candles[i].high - candles[i].low;
        float hpc = std::abs(candles[i].high - candles[i-1].close);
        float lpc = std::abs(candles[i].low - candles[i-1].close);
        trs.push_back(std::max({hl, hpc, lpc}));
    }
    float atr = 0.0f;
    for (int i = 0; i < period; ++i) atr += trs[i];
    atr /= period;
    for (size_t i = period; i < trs.size(); ++i) {
        atr = (atr * (period - 1) + trs[i]) / period;
    }
    return atr;
}

float forecastPrice(const std::vector<float>& prices, int horizon) {
    if (prices.size() < 2) return prices.empty() ? 0.0f : prices.back();
    // Linear is Poly degree 1
    std::vector<float> coeffs = polyFit(prices, 1);
    if (coeffs.empty()) return prices.back();
    
    float x = (float)(prices.size() - 1 + horizon);
    return coeffs[0] + coeffs[1] * x;
}

// Polynomial Regression (Degree 2 = Parabola)
float forecastPricePoly(const std::vector<float>& prices, int horizon, int degree) {
    if (prices.size() < (size_t)degree + 1) return prices.empty() ? 0.0f : prices.back();
    
    std::vector<float> coeffs = polyFit(prices, degree);
    if (coeffs.empty()) return prices.back();
    
    float x = (float)(prices.size() - 1 + horizon);
    float val = 1.0f;
    float y = 0.0f;
    
    for (float c : coeffs) {
        y += c * val;
        val *= x;
    }
    return y;
}

// Support/Resistance (Min/Max of last Period)
SupportResistance identifyLevels(const std::vector<float>& prices, int period) {
    SupportResistance levels = {0.0f, 0.0f};
    if (prices.empty()) return levels;
    
    int start = std::max(0, (int)prices.size() - period);
    
    float minP = 1e9;
    float maxP = -1e9;
    
    for (int i = start; i < (int)prices.size(); ++i) {
        if (prices[i] < minP) minP = prices[i];
        if (prices[i] > maxP) maxP = prices[i];
    }
    levels.support = minP;
    levels.resistance = maxP;
    return levels;
}

// Find Local Extrema (Peaks and Valleys)
std::vector<float> findLocalExtrema(const std::vector<float>& prices, int period, bool findMaxima) {
    std::vector<float> targets;
    if (prices.size() < 10) return targets;

    int start = std::max(0, (int)prices.size() - period);
    int window = 5; // Check 5 days left and right to confirm significance

    for (int i = start + window; i < (int)prices.size() - window; ++i) {
        bool isExtremum = true;
        float current = prices[i];

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
    
    std::vector<float> uniqueTargets;
    uniqueTargets.push_back(targets[0]);
    
    for (size_t i = 1; i < targets.size(); ++i) {
        if (targets[i] > uniqueTargets.back() * 1.01f) {
            uniqueTargets.push_back(targets[i]);
        }
    }

    return uniqueTargets;
}

// Bollinger Bands Implementation
BollingerBands computeBollingerBands(const std::vector<float>& prices, int period, float multiplier) {
    BollingerBands bb = {0.0f, 0.0f, 0.0f, 0.0f};
    size_t n = prices.size();
    if (n < (size_t)period) return bb;

    // 1. Compute SMA (Middle Band)
    float sum = 0.0f;
    for (size_t i = n - period; i < n; ++i) sum += prices[i];
    bb.middle = sum / period;

    // 2. Compute Standard Deviation
    float varianceSum = 0.0f;
    for (size_t i = n - period; i < n; ++i) {
        float diff = prices[i] - bb.middle;
        varianceSum += diff * diff;
    }
    float stdDev = std::sqrt(varianceSum / period);

    // 3. Compute Bands
    bb.upper = bb.middle + (multiplier * stdDev);
    bb.lower = bb.middle - (multiplier * stdDev);
    
    if (bb.middle > 0) bb.bandwidth = (bb.upper - bb.lower) / bb.middle;

    return bb;
}

// ADX Implementation
ADXResult computeADX(const std::vector<Candle>& candles, int period) {
    ADXResult res = {0.0f, 0.0f, 0.0f};
    if (candles.size() < (size_t)period * 2) return res; // Need warmup

    std::vector<float> tr(candles.size(), 0.0f);
    std::vector<float> plusDM(candles.size(), 0.0f);
    std::vector<float> minusDM(candles.size(), 0.0f);

    // 1. Calculate TR, +DM, -DM per candle
    for (size_t i = 1; i < candles.size(); ++i) {
        float highDiff = candles[i].high - candles[i-1].high;
        float lowDiff = candles[i-1].low - candles[i].low;

        if (highDiff > lowDiff && highDiff > 0) plusDM[i] = highDiff;
        if (lowDiff > highDiff && lowDiff > 0) minusDM[i] = lowDiff;

        float hl = candles[i].high - candles[i].low;
        float hpc = std::abs(candles[i].high - candles[i-1].close);
        float lpc = std::abs(candles[i].low - candles[i-1].close);
        tr[i] = std::max({hl, hpc, lpc});
    }

    // 2. Initial Smooth (First 'period' sum)
    
    float smoothTR = 0.0f;
    float smoothPlusDM = 0.0f;
    float smoothMinusDM = 0.0f;

    for (int i = 1; i <= period; ++i) {
        smoothTR += tr[i];
        smoothPlusDM += plusDM[i];
        smoothMinusDM += minusDM[i];
    }
    
    // 3. Rolling Smooth & DX Calculation
    std::vector<float> dx;
    for (size_t i = period + 1; i < candles.size(); ++i) {
        // Wilder's Smoothing
        smoothTR = smoothTR - (smoothTR / period) + tr[i];
        smoothPlusDM = smoothPlusDM - (smoothPlusDM / period) + plusDM[i];
        smoothMinusDM = smoothMinusDM - (smoothMinusDM / period) + minusDM[i];

        float pDI = (smoothTR == 0) ? 0 : (100.0f * smoothPlusDM / smoothTR);
        float mDI = (smoothTR == 0) ? 0 : (100.0f * smoothMinusDM / smoothTR);
        
        float diSum = pDI + mDI;
        float dxVal = (diSum == 0) ? 0 : (100.0f * std::abs(pDI - mDI) / diSum);
        dx.push_back(dxVal);
        
        // Store last DI for return
        if (i == candles.size() - 1) {
            res.plusDI = pDI;
            res.minusDI = mDI;
        }
    }

    // 4. ADX is SMA of DX
    if (dx.size() < (size_t)period) return res;
    
    float adxSum = 0.0f;
    // Initial ADX
    for(int i=0; i<period; ++i) adxSum += dx[i];
    float finalADX = adxSum / period;
    
    // Smoothing ADX
    for(size_t i=period; i<dx.size(); ++i) {
         finalADX = ((finalADX * (period - 1)) + dx[i]) / period;
    }

    res.adx = finalADX;
    return res;
}

PatternResult detectCandlestickPattern(const std::vector<Candle>& candles) {
    PatternResult res = {"", 0.0f};
    if (candles.size() < 3) return res;

    // Get last candle
    Candle c = candles.back();
    Candle p = candles[candles.size() - 2]; // Previous

    float body = std::abs(c.close - c.open);
    float range = c.high - c.low;
    float upperShadow = c.high - std::max(c.open, c.close);
    float lowerShadow = std::min(c.open, c.close) - c.low;
    
    float avgBody = 0.0f;
    for(int i=1; i<=3; i++) avgBody += std::abs(candles[candles.size()-i].close - candles[candles.size()-i].open);
    avgBody /= 3.0f;

    bool isBullish = c.close > c.open;
    bool isBearish = c.close < c.open;

    // 1. Hammer 
    if (lowerShadow > 2.0f * body && upperShadow < body * 0.5f && isBullish) {
        res.name = "Hammer";
        res.score = 0.5f;
        return res;
    }

    // 2. Shooting Star 
    if (upperShadow > 2.0f * body && lowerShadow < body * 0.5f && isBearish) {
        res.name = "Shooting Star";
        res.score = -0.5f;
        return res;
    }

    // 3. Bullish Engulfing
    bool pBearish = p.close < p.open;
    if (pBearish && isBullish && c.close > p.open && c.open < p.close) {
         res.name = "Bullish Engulfing";
         res.score = 0.6f;
         return res;
    }

    // 4. Bearish Engulfing
    bool pBullish = p.close > p.open;
    if (pBullish && isBearish && c.close < p.open && c.open > p.close) {
        res.name = "Bearish Engulfing";
        res.score = -0.6f;
        return res;
    }

    // 5. Doji 
    if (body < 0.1f * range && range > avgBody) {
        res.name = "Doji";
        res.score = 0.0f; 
        return res;
    }

    return res;
}

bool checkVolatilitySqueeze(const std::vector<float>& prices, int lookback, float percentile) {
    if (prices.size() < (size_t)lookback) return false;
    
    std::vector<float> history;
    int bbPeriod = 20;
    
    for (int i = 0; i < lookback; ++i) {
        int endIdx = (int)prices.size() - 1 - i;
        if (endIdx < bbPeriod) break;
        
        // Compute BB for this index
        float sum = 0.0f; 
        for(int k=0; k<bbPeriod; ++k) sum += prices[endIdx - k];
        float sma = sum / bbPeriod;
        
        float varSum = 0.0f;
        for(int k=0; k<bbPeriod; ++k) {
            float d = prices[endIdx - k] - sma;
            varSum += d*d;
        }
        float stdDev = std::sqrt(varSum / bbPeriod);
        float upper = sma + 2*stdDev;
        float lower = sma - 2*stdDev;
        
        if (sma > 0) {
            float bw = (upper - lower) / sma;
            history.push_back(bw);
        }
    }
    
    if (history.empty()) return false;
    
    // Current bandwidth is history[0]
    float currentBW = history[0];
    
    // Sort history to find percentile
    std::sort(history.begin(), history.end());
    
    // If current is in the bottom 'percentile' portion
    size_t thresholdIdx = (size_t)(history.size() * percentile);
    if (thresholdIdx >= history.size()) thresholdIdx = history.size() - 1;
    
    float thresholdBW = history[thresholdIdx];
    
    return currentBW <= thresholdBW;
}
