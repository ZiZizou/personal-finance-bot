#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

// GARCH(1,1) model parameters
struct GARCHParams {
    double omega = 0.0;    // Long-run variance constant
    double alpha = 0.05;   // Reaction to recent shocks (ARCH term)
    double beta = 0.90;    // Persistence of volatility (GARCH term)

    // Stationarity constraint: alpha + beta < 1
    bool isStationary() const {
        return alpha + beta < 1.0 && alpha >= 0 && beta >= 0 && omega > 0;
    }

    // Long-run (unconditional) variance
    double longRunVariance() const {
        if (alpha + beta >= 1.0) return 0.0;
        return omega / (1.0 - alpha - beta);
    }

    // Half-life of volatility shocks (in periods)
    double halfLife() const {
        double persistence = alpha + beta;
        if (persistence >= 1.0 || persistence <= 0.0) return std::numeric_limits<double>::infinity();
        return std::log(0.5) / std::log(persistence);
    }
};

// EGARCH model parameters (for asymmetric volatility)
struct EGARCHParams {
    double omega = 0.0;    // Constant
    double alpha = 0.0;    // Magnitude effect
    double gamma = 0.0;    // Asymmetry (leverage effect)
    double beta = 0.0;     // Persistence

    // Leverage effect: negative gamma means negative shocks increase volatility more
    bool hasLeverageEffect() const {
        return gamma < 0;
    }
};

// Volatility forecast result
struct VolatilityForecast {
    double currentVol;           // Current estimated volatility
    std::vector<double> forecast;// N-period ahead forecasts
    double confidenceLow;        // Lower bound of forecast (e.g., 5th percentile)
    double confidenceHigh;       // Upper bound (e.g., 95th percentile)
};

// GARCH(1,1) Model with MLE Parameter Fitting
class GARCHModel {
private:
    GARCHParams params_;
    bool isFitted_ = false;
    std::vector<double> residuals_;
    std::vector<double> conditionalVariances_;

public:
    GARCHModel() = default;
    explicit GARCHModel(const GARCHParams& params) : params_(params), isFitted_(true) {}

    // Fit GARCH parameters using Maximum Likelihood Estimation
    bool fit(const std::vector<double>& returns, int maxIter = 100, double tolerance = 1e-6) {
        if (returns.size() < 30) return false;

        // Calculate sample statistics
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        double variance = 0.0;
        for (double r : returns) {
            variance += (r - mean) * (r - mean);
        }
        variance /= returns.size();

        // Store residuals (demeaned returns)
        residuals_.clear();
        for (double r : returns) {
            residuals_.push_back(r - mean);
        }

        // Initial parameter guess
        params_.alpha = 0.05;
        params_.beta = 0.90;
        params_.omega = variance * (1.0 - params_.alpha - params_.beta);

        // Simple gradient descent optimization
        double learningRate = 0.001;
        double prevLogLik = -1e10;

        for (int iter = 0; iter < maxIter; ++iter) {
            // Calculate conditional variances and log-likelihood
            double logLik = 0.0;
            conditionalVariances_.resize(residuals_.size());
            conditionalVariances_[0] = variance;

            for (size_t t = 1; t < residuals_.size(); ++t) {
                double eps2 = residuals_[t - 1] * residuals_[t - 1];
                conditionalVariances_[t] = params_.omega +
                    params_.alpha * eps2 +
                    params_.beta * conditionalVariances_[t - 1];

                // Ensure positive variance
                conditionalVariances_[t] = std::max(conditionalVariances_[t], 1e-10);

                // Log-likelihood contribution (Gaussian)
                logLik -= 0.5 * (std::log(conditionalVariances_[t]) +
                         residuals_[t] * residuals_[t] / conditionalVariances_[t]);
            }

            // Check convergence
            if (std::abs(logLik - prevLogLik) < tolerance) {
                isFitted_ = true;
                return true;
            }
            prevLogLik = logLik;

            // Numerical gradient estimation and update
            double h = 0.0001;

            // Gradient for alpha
            params_.alpha += h;
            double logLikAlphaPlus = calculateLogLikelihood();
            params_.alpha -= 2 * h;
            double logLikAlphaMinus = calculateLogLikelihood();
            params_.alpha += h;
            double gradAlpha = (logLikAlphaPlus - logLikAlphaMinus) / (2 * h);

            // Gradient for beta
            params_.beta += h;
            double logLikBetaPlus = calculateLogLikelihood();
            params_.beta -= 2 * h;
            double logLikBetaMinus = calculateLogLikelihood();
            params_.beta += h;
            double gradBeta = (logLikBetaPlus - logLikBetaMinus) / (2 * h);

            // Gradient for omega
            params_.omega += h;
            double logLikOmegaPlus = calculateLogLikelihood();
            params_.omega -= 2 * h;
            double logLikOmegaMinus = calculateLogLikelihood();
            params_.omega += h;
            double gradOmega = (logLikOmegaPlus - logLikOmegaMinus) / (2 * h);

            // Update parameters
            params_.alpha += learningRate * gradAlpha;
            params_.beta += learningRate * gradBeta;
            params_.omega += learningRate * gradOmega;

            // Apply constraints
            params_.alpha = std::max(0.0001, std::min(0.5, params_.alpha));
            params_.beta = std::max(0.0001, std::min(0.999 - params_.alpha, params_.beta));
            params_.omega = std::max(1e-10, params_.omega);
        }

        isFitted_ = true;
        return true;
    }

    // Forecast volatility N periods ahead
    VolatilityForecast forecast(int horizonPeriods = 10) const {
        VolatilityForecast result;
        if (!isFitted_ || conditionalVariances_.empty()) {
            result.currentVol = 0.0;
            return result;
        }

        result.currentVol = std::sqrt(conditionalVariances_.back());

        // Multi-step ahead forecasts
        double h = conditionalVariances_.back();
        // double longRunVar = params_.longRunVariance();

        result.forecast.resize(horizonPeriods);
        for (int t = 0; t < horizonPeriods; ++t) {
            // h_{t+k} = omega + (alpha + beta) * h_{t+k-1}
            // Converges to long-run variance
            h = params_.omega + (params_.alpha + params_.beta) * h;
            result.forecast[t] = std::sqrt(h);
        }

        // Simple confidence bounds (approximate)
        double uncertainty = result.currentVol * 0.2 * std::sqrt((double)horizonPeriods);
        result.confidenceLow = std::max(0.0, result.forecast.back() - 1.96 * uncertainty);
        result.confidenceHigh = result.forecast.back() + 1.96 * uncertainty;

        return result;
    }

    // Get current volatility estimate
    double getCurrentVolatility() const {
        if (conditionalVariances_.empty()) return 0.0;
        return std::sqrt(conditionalVariances_.back());
    }

    // Update with new observation (online update)
    void update(double newReturn, double mean = 0.0) {
        double eps = newReturn - mean;
        residuals_.push_back(eps);

        double newVar;
        if (conditionalVariances_.empty()) {
            newVar = eps * eps;
        } else {
            newVar = params_.omega +
                params_.alpha * eps * eps +
                params_.beta * conditionalVariances_.back();
        }
        conditionalVariances_.push_back(std::max(newVar, 1e-10));
    }

    const GARCHParams& getParams() const { return params_; }
    bool isFitted() const { return isFitted_; }

private:
    double calculateLogLikelihood() const {
        if (residuals_.empty()) return -1e10;

        double variance = 0.0;
        for (double r : residuals_) variance += r * r;
        variance /= residuals_.size();

        double logLik = 0.0;
        double h = variance;

        for (size_t t = 1; t < residuals_.size(); ++t) {
            double eps2 = residuals_[t - 1] * residuals_[t - 1];
            h = params_.omega + params_.alpha * eps2 + params_.beta * h;
            h = std::max(h, 1e-10);
            logLik -= 0.5 * (std::log(h) + residuals_[t] * residuals_[t] / h);
        }

        return logLik;
    }
};

// EGARCH Model for asymmetric volatility (leverage effect)
class EGARCHModel {
private:
    EGARCHParams params_;
    bool isFitted_ = false;
    std::vector<double> residuals_;
    std::vector<double> logVariances_;

public:
    EGARCHModel() = default;

    // Fit EGARCH parameters
    bool fit(const std::vector<double>& returns, int maxIter = 100) {
        if (returns.size() < 30) return false;

        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        double variance = 0.0;
        for (double r : returns) variance += (r - mean) * (r - mean);
        variance /= returns.size();

        residuals_.clear();
        for (double r : returns) residuals_.push_back(r - mean);

        // Initial parameters
        params_.omega = std::log(variance);
        params_.alpha = 0.1;
        params_.gamma = -0.05;  // Negative for leverage effect
        params_.beta = 0.9;

        // Simplified fitting using grid search
        double bestLogLik = -1e10;
        EGARCHParams bestParams = params_;

        for (double alpha = 0.05; alpha <= 0.3; alpha += 0.05) {
            for (double gamma = -0.2; gamma <= 0.1; gamma += 0.05) {
                for (double beta = 0.7; beta <= 0.95; beta += 0.05) {
                    params_.alpha = alpha;
                    params_.gamma = gamma;
                    params_.beta = beta;

                    double logLik = calculateLogLikelihood();
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

    double getCurrentVolatility() const {
        if (logVariances_.empty()) return 0.0;
        return std::sqrt(std::exp(logVariances_.back()));
    }

    VolatilityForecast forecast(int horizonPeriods = 10) const {
        VolatilityForecast result;
        if (!isFitted_ || logVariances_.empty()) {
            return result;
        }

        result.currentVol = getCurrentVolatility();
        double logH = logVariances_.back();

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
    double calculateLogLikelihood() const {
        if (residuals_.empty()) return -1e10;

        double variance = 0.0;
        for (double r : residuals_) variance += r * r;
        variance /= residuals_.size();

        double logLik = 0.0;
        double logH = std::log(variance);
        const double sqrtTwoOverPi = std::sqrt(2.0 / 3.14159);

        for (size_t t = 1; t < residuals_.size(); ++t) {
            double z = residuals_[t - 1] / std::sqrt(std::exp(logH));
            logH = params_.omega +
                   params_.alpha * (std::abs(z) - sqrtTwoOverPi) +
                   params_.gamma * z +
                   params_.beta * logH;

            double h = std::exp(logH);
            logLik -= 0.5 * (logH + residuals_[t] * residuals_[t] / h);
        }

        return logLik;
    }

    void calculateLogVariances() {
        if (residuals_.empty()) return;

        double variance = 0.0;
        for (double r : residuals_) variance += r * r;
        variance /= residuals_.size();

        logVariances_.resize(residuals_.size());
        logVariances_[0] = std::log(variance);

        const double sqrtTwoOverPi = std::sqrt(2.0 / 3.14159);

        for (size_t t = 1; t < residuals_.size(); ++t) {
            double z = residuals_[t - 1] / std::sqrt(std::exp(logVariances_[t - 1]));
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
    static double calculate(const std::vector<double>& returns, int period = 20) {
        if ((int)returns.size() < period) return 0.0;

        double sum = 0.0;
        for (size_t i = returns.size() - period; i < returns.size(); ++i) {
            sum += returns[i];
        }
        double mean = sum / period;

        double variance = 0.0;
        for (size_t i = returns.size() - period; i < returns.size(); ++i) {
            variance += (returns[i] - mean) * (returns[i] - mean);
        }
        variance /= (period - 1);

        return std::sqrt(variance);
    }

    // Parkinson volatility (using high-low range)
    static double parkinson(const std::vector<double>& highs, const std::vector<double>& lows,
                          int period = 20) {
        if ((int)highs.size() < period || (int)lows.size() < period) return 0.0;

        double sum = 0.0;
        for (size_t i = highs.size() - period; i < highs.size(); ++i) {
            double ratio = highs[i] / lows[i];
            if (ratio > 0) {
                double logRatio = std::log(ratio);
                sum += logRatio * logRatio;
            }
        }

        return std::sqrt(sum / (4.0 * std::log(2.0) * period));
    }

    // Garman-Klass volatility (using OHLC)
    static double garmanKlass(const std::vector<double>& opens, const std::vector<double>& highs,
                            const std::vector<double>& lows, const std::vector<double>& closes,
                            int period = 20) {
        if ((int)opens.size() < period) return 0.0;

        double sum = 0.0;
        for (size_t i = opens.size() - period; i < opens.size(); ++i) {
            double hl = std::log(highs[i] / lows[i]);
            double co = std::log(closes[i] / opens[i]);
            sum += 0.5 * hl * hl - (2.0 * std::log(2.0) - 1.0) * co * co;
        }

        return std::sqrt(sum / period);
    }

    // Annualize volatility
    static double annualize(double dailyVol, int tradingDays = 252) {
        return dailyVol * std::sqrt((double)tradingDays);
    }
};

// Volatility Term Structure
class VolatilityTermStructure {
public:
    // Calculate implied volatility term structure from historical data
    static std::vector<double> calculate(const std::vector<double>& returns,
                                        const std::vector<int>& horizons) {
        std::vector<double> vols;

        for (int horizon : horizons) {
            // Calculate volatility at each horizon
            if ((int)returns.size() >= horizon) {
                double vol = RealizedVolatility::calculate(returns, horizon);
                vols.push_back(RealizedVolatility::annualize(vol));
            } else {
                vols.push_back(0.0);
            }
        }

        return vols;
    }

    // Check if term structure is inverted (short-term > long-term)
    static bool isInverted(const std::vector<double>& termStructure) {
        if (termStructure.size() < 2) return false;
        return termStructure.front() > termStructure.back() * 1.1;  // 10% threshold
    }
};