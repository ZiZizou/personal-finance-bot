#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

// GARCH(1,1) model parameters
struct GARCHParams {
    float omega = 0.0f;    // Long-run variance constant
    float alpha = 0.05f;   // Reaction to recent shocks (ARCH term)
    float beta = 0.90f;    // Persistence of volatility (GARCH term)

    // Stationarity constraint: alpha + beta < 1
    bool isStationary() const {
        return alpha + beta < 1.0f && alpha >= 0 && beta >= 0 && omega > 0;
    }

    // Long-run (unconditional) variance
    float longRunVariance() const {
        if (alpha + beta >= 1.0f) return 0.0f;
        return omega / (1.0f - alpha - beta);
    }

    // Half-life of volatility shocks (in periods)
    float halfLife() const {
        float persistence = alpha + beta;
        if (persistence >= 1.0f || persistence <= 0.0f) return std::numeric_limits<float>::infinity();
        return std::log(0.5f) / std::log(persistence);
    }
};

// EGARCH model parameters (for asymmetric volatility)
struct EGARCHParams {
    float omega = 0.0f;    // Constant
    float alpha = 0.0f;    // Magnitude effect
    float gamma = 0.0f;    // Asymmetry (leverage effect)
    float beta = 0.0f;     // Persistence

    // Leverage effect: negative gamma means negative shocks increase volatility more
    bool hasLeverageEffect() const {
        return gamma < 0;
    }
};

// Volatility forecast result
struct VolatilityForecast {
    float currentVol;           // Current estimated volatility
    std::vector<float> forecast;// N-period ahead forecasts
    float confidenceLow;        // Lower bound of forecast (e.g., 5th percentile)
    float confidenceHigh;       // Upper bound (e.g., 95th percentile)
};

// GARCH(1,1) Model with MLE Parameter Fitting
class GARCHModel {
private:
    GARCHParams params_;
    bool isFitted_ = false;
    std::vector<float> residuals_;
    std::vector<float> conditionalVariances_;

public:
    GARCHModel() = default;
    explicit GARCHModel(const GARCHParams& params) : params_(params), isFitted_(true) {}

    // Fit GARCH parameters using Maximum Likelihood Estimation
    bool fit(const std::vector<float>& returns, int maxIter = 100, float tolerance = 1e-6f) {
        if (returns.size() < 30) return false;

        // Calculate sample statistics
        float mean = std::accumulate(returns.begin(), returns.end(), 0.0f) / returns.size();
        float variance = 0.0f;
        for (float r : returns) {
            variance += (r - mean) * (r - mean);
        }
        variance /= returns.size();

        // Store residuals (demeaned returns)
        residuals_.clear();
        for (float r : returns) {
            residuals_.push_back(r - mean);
        }

        // Initial parameter guess
        params_.alpha = 0.05f;
        params_.beta = 0.90f;
        params_.omega = variance * (1.0f - params_.alpha - params_.beta);

        // Simple gradient descent optimization
        float learningRate = 0.001f;
        float prevLogLik = -1e10f;

        for (int iter = 0; iter < maxIter; ++iter) {
            // Calculate conditional variances and log-likelihood
            float logLik = 0.0f;
            conditionalVariances_.resize(residuals_.size());
            conditionalVariances_[0] = variance;

            for (size_t t = 1; t < residuals_.size(); ++t) {
                float eps2 = residuals_[t - 1] * residuals_[t - 1];
                conditionalVariances_[t] = params_.omega +
                    params_.alpha * eps2 +
                    params_.beta * conditionalVariances_[t - 1];

                // Ensure positive variance
                conditionalVariances_[t] = std::max(conditionalVariances_[t], 1e-10f);

                // Log-likelihood contribution (Gaussian)
                logLik -= 0.5f * (std::log(conditionalVariances_[t]) +
                         residuals_[t] * residuals_[t] / conditionalVariances_[t]);
            }

            // Check convergence
            if (std::abs(logLik - prevLogLik) < tolerance) {
                isFitted_ = true;
                return true;
            }
            prevLogLik = logLik;

            // Numerical gradient estimation and update
            float h = 0.0001f;

            // Gradient for alpha
            params_.alpha += h;
            float logLikAlphaPlus = calculateLogLikelihood();
            params_.alpha -= 2 * h;
            float logLikAlphaMinus = calculateLogLikelihood();
            params_.alpha += h;
            float gradAlpha = (logLikAlphaPlus - logLikAlphaMinus) / (2 * h);

            // Gradient for beta
            params_.beta += h;
            float logLikBetaPlus = calculateLogLikelihood();
            params_.beta -= 2 * h;
            float logLikBetaMinus = calculateLogLikelihood();
            params_.beta += h;
            float gradBeta = (logLikBetaPlus - logLikBetaMinus) / (2 * h);

            // Gradient for omega
            params_.omega += h;
            float logLikOmegaPlus = calculateLogLikelihood();
            params_.omega -= 2 * h;
            float logLikOmegaMinus = calculateLogLikelihood();
            params_.omega += h;
            float gradOmega = (logLikOmegaPlus - logLikOmegaMinus) / (2 * h);

            // Update parameters
            params_.alpha += learningRate * gradAlpha;
            params_.beta += learningRate * gradBeta;
            params_.omega += learningRate * gradOmega;

            // Apply constraints
            params_.alpha = std::max(0.0001f, std::min(0.5f, params_.alpha));
            params_.beta = std::max(0.0001f, std::min(0.999f - params_.alpha, params_.beta));
            params_.omega = std::max(1e-10f, params_.omega);
        }

        isFitted_ = true;
        return true;
    }

    // Forecast volatility N periods ahead
    VolatilityForecast forecast(int horizonPeriods = 10) const {
        VolatilityForecast result;
        if (!isFitted_ || conditionalVariances_.empty()) {
            result.currentVol = 0.0f;
            return result;
        }

        result.currentVol = std::sqrt(conditionalVariances_.back());

        // Multi-step ahead forecasts
        float h = conditionalVariances_.back();
        float longRunVar = params_.longRunVariance();

        result.forecast.resize(horizonPeriods);
        for (int t = 0; t < horizonPeriods; ++t) {
            // h_{t+k} = omega + (alpha + beta) * h_{t+k-1}
            // Converges to long-run variance
            h = params_.omega + (params_.alpha + params_.beta) * h;
            result.forecast[t] = std::sqrt(h);
        }

        // Simple confidence bounds (approximate)
        float uncertainty = result.currentVol * 0.2f * std::sqrt((float)horizonPeriods);
        result.confidenceLow = std::max(0.0f, result.forecast.back() - 1.96f * uncertainty);
        result.confidenceHigh = result.forecast.back() + 1.96f * uncertainty;

        return result;
    }

    // Get current volatility estimate
    float getCurrentVolatility() const {
        if (conditionalVariances_.empty()) return 0.0f;
        return std::sqrt(conditionalVariances_.back());
    }

    // Update with new observation (online update)
    void update(float newReturn, float mean = 0.0f) {
        float eps = newReturn - mean;
        residuals_.push_back(eps);

        float newVar;
        if (conditionalVariances_.empty()) {
            newVar = eps * eps;
        } else {
            newVar = params_.omega +
                params_.alpha * eps * eps +
                params_.beta * conditionalVariances_.back();
        }
        conditionalVariances_.push_back(std::max(newVar, 1e-10f));
    }

    const GARCHParams& getParams() const { return params_; }
    bool isFitted() const { return isFitted_; }

private:
    float calculateLogLikelihood() const {
        if (residuals_.empty()) return -1e10f;

        float variance = 0.0f;
        for (float r : residuals_) variance += r * r;
        variance /= residuals_.size();

        float logLik = 0.0f;
        float h = variance;

        for (size_t t = 1; t < residuals_.size(); ++t) {
            float eps2 = residuals_[t - 1] * residuals_[t - 1];
            h = params_.omega + params_.alpha * eps2 + params_.beta * h;
            h = std::max(h, 1e-10f);
            logLik -= 0.5f * (std::log(h) + residuals_[t] * residuals_[t] / h);
        }

        return logLik;
    }
};

// EGARCH Model for asymmetric volatility (leverage effect)
class EGARCHModel {
private:
    EGARCHParams params_;
    bool isFitted_ = false;
    std::vector<float> residuals_;
    std::vector<float> logVariances_;

public:
    EGARCHModel() = default;

    // Fit EGARCH parameters
    bool fit(const std::vector<float>& returns, int maxIter = 100) {
        if (returns.size() < 30) return false;

        float mean = std::accumulate(returns.begin(), returns.end(), 0.0f) / returns.size();
        float variance = 0.0f;
        for (float r : returns) variance += (r - mean) * (r - mean);
        variance /= returns.size();

        residuals_.clear();
        for (float r : returns) residuals_.push_back(r - mean);

        // Initial parameters
        params_.omega = std::log(variance);
        params_.alpha = 0.1f;
        params_.gamma = -0.05f;  // Negative for leverage effect
        params_.beta = 0.9f;

        // Simplified fitting using grid search
        float bestLogLik = -1e10f;
        EGARCHParams bestParams = params_;

        for (float alpha = 0.05f; alpha <= 0.3f; alpha += 0.05f) {
            for (float gamma = -0.2f; gamma <= 0.1f; gamma += 0.05f) {
                for (float beta = 0.7f; beta <= 0.95f; beta += 0.05f) {
                    params_.alpha = alpha;
                    params_.gamma = gamma;
                    params_.beta = beta;

                    float logLik = calculateLogLikelihood();
                    if (logLik > bestLogLik) {
                        bestLogLik = logLik;
                        bestParams = params_;
                    }
                }
            }
        }

        params_ = bestParams;
        isFitted_ = true;

        // Calculate final log variances
        calculateLogVariances();

        return true;
    }

    float getCurrentVolatility() const {
        if (logVariances_.empty()) return 0.0f;
        return std::sqrt(std::exp(logVariances_.back()));
    }

    VolatilityForecast forecast(int horizonPeriods = 10) const {
        VolatilityForecast result;
        if (!isFitted_ || logVariances_.empty()) {
            return result;
        }

        result.currentVol = getCurrentVolatility();
        float logH = logVariances_.back();

        result.forecast.resize(horizonPeriods);
        for (int t = 0; t < horizonPeriods; ++t) {
            // Simplified forecast assuming E[z] = 0, E[|z|] = sqrt(2/pi)
            logH = params_.omega + params_.beta * logH;
            result.forecast[t] = std::sqrt(std::exp(logH));
        }

        return result;
    }

    const EGARCHParams& getParams() const { return params_; }
    bool hasLeverageEffect() const { return params_.hasLeverageEffect(); }

private:
    float calculateLogLikelihood() const {
        if (residuals_.empty()) return -1e10f;

        float variance = 0.0f;
        for (float r : residuals_) variance += r * r;
        variance /= residuals_.size();

        float logLik = 0.0f;
        float logH = std::log(variance);
        const float sqrtTwoOverPi = std::sqrt(2.0f / 3.14159f);

        for (size_t t = 1; t < residuals_.size(); ++t) {
            float z = residuals_[t - 1] / std::sqrt(std::exp(logH));
            logH = params_.omega +
                   params_.alpha * (std::abs(z) - sqrtTwoOverPi) +
                   params_.gamma * z +
                   params_.beta * logH;

            float h = std::exp(logH);
            logLik -= 0.5f * (logH + residuals_[t] * residuals_[t] / h);
        }

        return logLik;
    }

    void calculateLogVariances() {
        if (residuals_.empty()) return;

        float variance = 0.0f;
        for (float r : residuals_) variance += r * r;
        variance /= residuals_.size();

        logVariances_.resize(residuals_.size());
        logVariances_[0] = std::log(variance);

        const float sqrtTwoOverPi = std::sqrt(2.0f / 3.14159f);

        for (size_t t = 1; t < residuals_.size(); ++t) {
            float z = residuals_[t - 1] / std::sqrt(std::exp(logVariances_[t - 1]));
            logVariances_[t] = params_.omega +
                               params_.alpha * (std::abs(z) - sqrtTwoOverPi) +
                               params_.gamma * z +
                               params_.beta * logVariances_[t - 1];
        }
    }
};

// Realized Volatility Calculator
class RealizedVolatility {
public:
    // Simple historical volatility
    static float calculate(const std::vector<float>& returns, int period = 20) {
        if ((int)returns.size() < period) return 0.0f;

        float sum = 0.0f;
        for (size_t i = returns.size() - period; i < returns.size(); ++i) {
            sum += returns[i];
        }
        float mean = sum / period;

        float variance = 0.0f;
        for (size_t i = returns.size() - period; i < returns.size(); ++i) {
            variance += (returns[i] - mean) * (returns[i] - mean);
        }
        variance /= (period - 1);

        return std::sqrt(variance);
    }

    // Parkinson volatility (using high-low range)
    static float parkinson(const std::vector<float>& highs, const std::vector<float>& lows,
                          int period = 20) {
        if ((int)highs.size() < period || (int)lows.size() < period) return 0.0f;

        float sum = 0.0f;
        for (size_t i = highs.size() - period; i < highs.size(); ++i) {
            float ratio = highs[i] / lows[i];
            if (ratio > 0) {
                float logRatio = std::log(ratio);
                sum += logRatio * logRatio;
            }
        }

        return std::sqrt(sum / (4.0f * std::log(2.0f) * period));
    }

    // Garman-Klass volatility (using OHLC)
    static float garmanKlass(const std::vector<float>& opens, const std::vector<float>& highs,
                            const std::vector<float>& lows, const std::vector<float>& closes,
                            int period = 20) {
        if ((int)opens.size() < period) return 0.0f;

        float sum = 0.0f;
        for (size_t i = opens.size() - period; i < opens.size(); ++i) {
            float hl = std::log(highs[i] / lows[i]);
            float co = std::log(closes[i] / opens[i]);
            sum += 0.5f * hl * hl - (2.0f * std::log(2.0f) - 1.0f) * co * co;
        }

        return std::sqrt(sum / period);
    }

    // Annualize volatility
    static float annualize(float dailyVol, int tradingDays = 252) {
        return dailyVol * std::sqrt((float)tradingDays);
    }
};

// Volatility Term Structure
class VolatilityTermStructure {
public:
    // Calculate implied volatility term structure from historical data
    static std::vector<float> calculate(const std::vector<float>& returns,
                                        const std::vector<int>& horizons) {
        std::vector<float> vols;

        for (int horizon : horizons) {
            // Calculate volatility at each horizon
            if ((int)returns.size() >= horizon) {
                float vol = RealizedVolatility::calculate(returns, horizon);
                vols.push_back(RealizedVolatility::annualize(vol));
            } else {
                vols.push_back(0.0f);
            }
        }

        return vols;
    }

    // Check if term structure is inverted (short-term > long-term)
    static bool isInverted(const std::vector<float>& termStructure) {
        if (termStructure.size() < 2) return false;
        return termStructure.front() > termStructure.back() * 1.1f;  // 10% threshold
    }
};
