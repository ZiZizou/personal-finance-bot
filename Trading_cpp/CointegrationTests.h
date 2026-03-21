#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>

// Cointegration and statistical tests for pairs trading
namespace CointegrationTests {

// Simple linear regression (OLS) for hedge ratio calculation
struct LinearRegressionResult {
    double slope;          // Hedge ratio (beta)
    double intercept;      // Alpha
    double rSquared;
    double stdError;       // Standard error of regression
    double residualsStd;   // Standard deviation of residuals
};

// Perform OLS regression: y = alpha + beta * x
LinearRegressionResult olsRegression(const std::vector<double>& y, const std::vector<double>& x) {
    LinearRegressionResult result;
    result.slope = 0.0;
    result.intercept = 0.0;
    result.rSquared = 0.0;
    result.stdError = 0.0;
    result.residualsStd = 0.0;

    if (y.size() != x.size() || y.size() < 2) {
        return result;
    }

    size_t n = y.size();
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumXX = 0.0, sumYY = 0.0;

    for (size_t i = 0; i < n; ++i) {
        sumX += x[i];
        sumY += y[i];
        sumXY += x[i] * y[i];
        sumXX += x[i] * x[i];
        sumYY += y[i] * y[i];
    }

    double xMean = sumX / n;
    double yMean = sumY / n;

    // Calculate slope (beta) and intercept (alpha)
    double denominator = n * sumXX - sumX * sumX;
    if (std::fabs(denominator) < 1e-10) {
        return result;  // Avoid division by zero
    }

    result.slope = (n * sumXY - sumX * sumY) / denominator;
    result.intercept = (sumY - result.slope * sumX) / n;

    // Calculate R-squared
    double ssTot = 0.0, ssRes = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double yPred = result.intercept + result.slope * x[i];
        ssTot += (y[i] - yMean) * (y[i] - yMean);
        ssRes += (y[i] - yPred) * (y[i] - yPred);
    }

    if (ssTot > 1e-10) {
        result.rSquared = 1.0 - (ssRes / ssTot);
    }

    // Standard error of estimate
    if (n > 2) {
        result.stdError = std::sqrt(ssRes / (n - 2));
    }

    // Residuals standard deviation
    result.residualsStd = std::sqrt(ssRes / n);

    return result;
}

// Calculate spread between two price series using hedge ratio
std::vector<double> calculateSpread(const std::vector<double>& price1,
                                     const std::vector<double>& price2,
                                     double hedgeRatio) {
    std::vector<double> spread;
    spread.reserve(price1.size());

    for (size_t i = 0; i < price1.size(); ++i) {
        spread.push_back(price1[i] - hedgeRatio * price2[i]);
    }

    return spread;
}

// Engle-Granger test for cointegration
// Returns: {t-statistic, p-value, isCointegrated}
struct CointegrationTestResult {
    double tStatistic;
    double pValue;
    bool isCointegrated;
    double hedgeRatio;
    double halfLife;  // Mean reversion half-life in periods
};

// Simplified Engle-Granger cointegration test
// Uses ADF (Augmented Dickey-Fuller) test on residuals
CointegrationTestResult engleGrangerTest(const std::vector<double>& price1,
                                           const std::vector<double>& price2,
                                           double significanceLevel = 0.05) {
    CointegrationTestResult result;
    result.tStatistic = 0.0;
    result.pValue = 1.0;
    result.isCointegrated = false;
    result.hedgeRatio = 1.0;
    result.halfLife = -1.0;

    if (price1.size() != price2.size() || price1.size() < 30) {
        return result;
    }

    // Step 1: Calculate hedge ratio using OLS
    LinearRegressionResult ols = olsRegression(price1, price2);
    result.hedgeRatio = ols.slope;

    // Step 2: Calculate spread (residuals)
    std::vector<double> spread = calculateSpread(price1, price2, ols.slope);

    // Step 3: ADF test on spread
    // delta(spread) = alpha + gamma * spread(t-1) + sum(beta_i * delta(spread(t-i))) + epsilon

    if (spread.size() < 20) {
        return result;
    }

    // Calculate first difference of spread
    std::vector<double> deltaSpread;
    for (size_t i = 1; i < spread.size(); ++i) {
        deltaSpread.push_back(spread[i] - spread[i - 1]);
    }

    // Lag-1 spread for regression
    std::vector<double> spreadLag1;
    for (size_t i = 1; i < spread.size(); ++i) {
        spreadLag1.push_back(spread[i - 1]);
    }

    // Run regression: deltaSpread = gamma * spreadLag1
    // This is a simplified ADF with no lags
    LinearRegressionResult adf = olsRegression(deltaSpread, spreadLag1);

    // Calculate t-statistic for gamma (should be negative for stationarity)
    if (adf.stdError > 1e-10) {
        result.tStatistic = adf.slope / adf.stdError;
    }

    // Approximate p-value (simplified - using critical values)
    // For n > 30, critical values are approximately:
    // 1%: -3.96, 5%: -3.41, 10%: -3.12
    double t = std::fabs(result.tStatistic);
    if (t > 3.96) {
        result.pValue = 0.01;
    } else if (t > 3.41) {
        result.pValue = 0.05;
    } else if (t > 3.12) {
        result.pValue = 0.10;
    } else {
        result.pValue = 0.20;
    }

    result.isCointegrated = (result.pValue <= significanceLevel);

    // Calculate half-life of mean reversion
    // halfLife = -log(2) / lambda where lambda is the coefficient from ADF
    if (adf.slope < 0 && adf.slope > -1.0) {
        result.halfLife = -std::log(2.0) / std::log(1.0 + adf.slope);
    }

    return result;
}

// Calculate Z-score of spread
struct ZScoreResult {
    double zScore;
    double mean;
    double stdDev;
};

ZScoreResult calculateZScore(const std::vector<double>& spread, int lookback = 60) {
    ZScoreResult result;
    result.zScore = 0.0;
    result.mean = 0.0;
    result.stdDev = 1.0;

    if ((int)spread.size() < lookback || lookback <= 0) {
        lookback = (int)spread.size();
    }

    if (lookback < 2) {
        return result;
    }

    // Use the most recent 'lookback' values
    size_t startIdx = spread.size() - lookback;

    // Calculate mean
    double sum = 0.0;
    for (size_t i = startIdx; i < spread.size(); ++i) {
        sum += spread[i];
    }
    result.mean = sum / lookback;

    // Calculate standard deviation
    double sqDiffSum = 0.0;
    for (size_t i = startIdx; i < spread.size(); ++i) {
        double diff = spread[i] - result.mean;
        sqDiffSum += diff * diff;
    }
    result.stdDev = std::sqrt(sqDiffSum / (lookback - 1));

    // Calculate z-score of current spread
    if (result.stdDev > 1e-10) {
        result.zScore = (spread.back() - result.mean) / result.stdDev;
    }

    return result;
}

// Hurst exponent calculation for mean reversion testing
// H < 0.5 indicates mean-reverting behavior
// H > 0.5 indicates trending behavior
// H = 0.5 indicates random walk
double calculateHurstExponent(const std::vector<double>& prices, int maxLag = 100) {
    if (prices.size() < 100) {
        return 0.5;  // Default to random walk for insufficient data
    }

    int n = (int)prices.size();
    int lag = std::min(maxLag, n / 2);

    std::vector<double> rsValues;

    for (int l = 10; l <= lag; l += 5) {  // Use lags from 10 to lag
        // Calculate mean
        double sum = 0.0;
        for (int i = 0; i < l; ++i) {
            sum += prices[n - l + i];
        }
        double mean = sum / l;

        // Calculate cumulative deviation
        std::vector<double> cumDev(l, 0.0);
        double maxDev = -1e10;
        double minDev = 1e10;

        for (int i = 0; i < l; ++i) {
            if (i == 0) {
                cumDev[i] = prices[n - l + i] - mean;
            } else {
                cumDev[i] = cumDev[i - 1] + prices[n - l + i] - mean;
            }
            maxDev = std::max(maxDev, cumDev[i]);
            minDev = std::min(minDev, cumDev[i]);
        }

        // Range (max - min)
        double range = maxDev - minDev;

        // Calculate standard deviation
        double varSum = 0.0;
        for (int i = 0; i < l; ++i) {
            double dev = prices[n - l + i] - mean;
            varSum += dev * dev;
        }
        double stdDev = std::sqrt(varSum / l);

        // R/S statistic
        if (stdDev > 1e-10) {
            rsValues.push_back(range / stdDev);
        }
    }

    if (rsValues.empty()) {
        return 0.5;
    }

    // Log-log regression: log(RS) = H * log(lag) + c
    std::vector<double> logRs, logLags;
    for (size_t i = 0; i < rsValues.size(); ++i) {
        double lagVal = 10.0 + i * 5.0;
        if (rsValues[i] > 0 && lagVal > 0) {
            logRs.push_back(std::log(rsValues[i]));
            logLags.push_back(std::log(lagVal));
        }
    }

    if (logRs.size() < 5) {
        return 0.5;
    }

    // Simple linear regression for Hurst exponent
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumXX = 0.0;
    int m = (int)logRs.size();

    for (int i = 0; i < m; ++i) {
        sumX += logLags[i];
        sumY += logRs[i];
        sumXY += logLags[i] * logRs[i];
        sumXX += logLags[i] * logLags[i];
    }

    double denominator = m * sumXX - sumX * sumX;
    if (std::fabs(denominator) < 1e-10) {
        return 0.5;
    }

    double hurst = (m * sumXY - sumX * sumY) / denominator;

    // Clamp to reasonable range
    return std::max(0.0, std::min(1.0, hurst));
}

// Calculate rolling correlation between two series
double calculateRollingCorrelation(const std::vector<double>& series1,
                                    const std::vector<double>& series2,
                                    int lookback = 60) {
    if (series1.size() != series2.size() || series1.size() < (size_t)lookback) {
        return 0.0;
    }

    size_t startIdx = series1.size() - lookback;

    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumXX = 0.0, sumYY = 0.0;
    for (size_t i = startIdx; i < series1.size(); ++i) {
        sumX += series1[i];
        sumY += series2[i];
        sumXY += series1[i] * series2[i];
        sumXX += series1[i] * series1[i];
        sumYY += series2[i] * series2[i];
    }

    double n = lookback;
    double numerator = n * sumXY - sumX * sumY;
    double denominator = std::sqrt((n * sumXX - sumX * sumX) * (n * sumYY - sumY * sumY));

    if (std::fabs(denominator) < 1e-10) {
        return 0.0;
    }

    return numerator / denominator;
}

} // namespace CointegrationTests
