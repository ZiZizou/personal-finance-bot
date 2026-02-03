#pragma once
#include <vector>
#include <map>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>

// Note: This implementation uses basic linear algebra.
// For production use, consider integrating with Eigen library.

// Portfolio optimization result
struct OptimizedPortfolio {
    std::vector<float> weights;
    std::vector<std::string> assets;
    float expectedReturn = 0.0f;
    float volatility = 0.0f;
    float sharpeRatio = 0.0f;
    bool converged = false;

    std::map<std::string, float> getWeightMap() const {
        std::map<std::string, float> result;
        for (size_t i = 0; i < assets.size() && i < weights.size(); ++i) {
            result[assets[i]] = weights[i];
        }
        return result;
    }
};

// Efficient frontier point
struct EfficientFrontierPoint {
    float targetReturn;
    float minVolatility;
    std::vector<float> weights;
};

// Portfolio Optimizer using Mean-Variance Optimization
class PortfolioOptimizer {
private:
    std::vector<std::string> assets_;
    std::vector<float> expectedReturns_;
    std::vector<std::vector<float>> covarianceMatrix_;
    float riskFreeRate_ = 0.04f;

    // Constraints
    float minWeight_ = 0.0f;
    float maxWeight_ = 1.0f;
    bool allowShort_ = false;

public:
    PortfolioOptimizer() = default;

    // Set asset data
    void setAssets(const std::vector<std::string>& assets,
                   const std::vector<float>& expectedReturns,
                   const std::vector<std::vector<float>>& covarianceMatrix) {
        assets_ = assets;
        expectedReturns_ = expectedReturns;
        covarianceMatrix_ = covarianceMatrix;
    }

    // Calculate covariance matrix from return series
    void calculateFromReturns(const std::vector<std::string>& assets,
                              const std::map<std::string, std::vector<float>>& returnSeries) {
        assets_ = assets;
        int n = assets.size();

        // Calculate expected returns
        expectedReturns_.resize(n);
        for (int i = 0; i < n; ++i) {
            const auto& returns = returnSeries.at(assets[i]);
            expectedReturns_[i] = std::accumulate(returns.begin(), returns.end(), 0.0f) /
                                  returns.size() * 252;  // Annualize
        }

        // Calculate covariance matrix
        covarianceMatrix_.resize(n, std::vector<float>(n, 0.0f));
        for (int i = 0; i < n; ++i) {
            for (int j = i; j < n; ++j) {
                const auto& r1 = returnSeries.at(assets[i]);
                const auto& r2 = returnSeries.at(assets[j]);

                size_t len = std::min(r1.size(), r2.size());
                float mean1 = expectedReturns_[i] / 252;
                float mean2 = expectedReturns_[j] / 252;

                float cov = 0.0f;
                for (size_t k = 0; k < len; ++k) {
                    cov += (r1[k] - mean1) * (r2[k] - mean2);
                }
                cov = cov / (len - 1) * 252;  // Annualize

                covarianceMatrix_[i][j] = cov;
                covarianceMatrix_[j][i] = cov;
            }
        }
    }

    void setConstraints(float minWeight, float maxWeight, bool allowShort) {
        minWeight_ = minWeight;
        maxWeight_ = maxWeight;
        allowShort_ = allowShort;
    }

    void setRiskFreeRate(float rate) { riskFreeRate_ = rate; }

    // Minimum Variance Portfolio
    OptimizedPortfolio minimumVariance() {
        OptimizedPortfolio result;
        result.assets = assets_;
        int n = assets_.size();

        if (n == 0 || covarianceMatrix_.empty()) return result;

        // Equal weight as starting point
        result.weights.resize(n, 1.0f / n);

        // Iterative optimization using gradient descent
        int maxIter = 1000;
        float learningRate = 0.01f;

        for (int iter = 0; iter < maxIter; ++iter) {
            // Calculate portfolio variance gradient
            std::vector<float> gradient(n, 0.0f);
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    gradient[i] += 2 * covarianceMatrix_[i][j] * result.weights[j];
                }
            }

            // Update weights
            float sumWeights = 0.0f;
            for (int i = 0; i < n; ++i) {
                result.weights[i] -= learningRate * gradient[i];

                // Apply constraints
                if (!allowShort_) {
                    result.weights[i] = std::max(minWeight_, result.weights[i]);
                }
                result.weights[i] = std::min(maxWeight_, result.weights[i]);

                sumWeights += result.weights[i];
            }

            // Normalize to sum to 1
            if (sumWeights > 0) {
                for (int i = 0; i < n; ++i) {
                    result.weights[i] /= sumWeights;
                }
            }
        }

        // Calculate final metrics
        result.expectedReturn = calculateReturn(result.weights);
        result.volatility = calculateVolatility(result.weights);
        result.sharpeRatio = (result.expectedReturn - riskFreeRate_) / result.volatility;
        result.converged = true;

        return result;
    }

    // Maximum Sharpe Ratio Portfolio (Tangency Portfolio)
    OptimizedPortfolio maxSharpe() {
        OptimizedPortfolio result;
        result.assets = assets_;
        int n = assets_.size();

        if (n == 0) return result;

        // Use numerical optimization
        result.weights.resize(n, 1.0f / n);

        int maxIter = 2000;
        float learningRate = 0.005f;

        for (int iter = 0; iter < maxIter; ++iter) {
            float currentRet = calculateReturn(result.weights);
            float currentVol = calculateVolatility(result.weights);

            if (currentVol < 1e-8f) break;

            // Numerical gradient for Sharpe ratio
            std::vector<float> gradient(n, 0.0f);
            float h = 0.0001f;

            for (int i = 0; i < n; ++i) {
                result.weights[i] += h;
                float sharpeHigh = (calculateReturn(result.weights) - riskFreeRate_) /
                                   calculateVolatility(result.weights);
                result.weights[i] -= 2 * h;
                float sharpeLow = (calculateReturn(result.weights) - riskFreeRate_) /
                                  calculateVolatility(result.weights);
                result.weights[i] += h;

                gradient[i] = (sharpeHigh - sharpeLow) / (2 * h);
            }

            // Update weights to maximize Sharpe (gradient ascent)
            float sumWeights = 0.0f;
            for (int i = 0; i < n; ++i) {
                result.weights[i] += learningRate * gradient[i];

                if (!allowShort_) {
                    result.weights[i] = std::max(minWeight_, result.weights[i]);
                }
                result.weights[i] = std::min(maxWeight_, result.weights[i]);
                sumWeights += result.weights[i];
            }

            if (sumWeights > 0) {
                for (int i = 0; i < n; ++i) {
                    result.weights[i] /= sumWeights;
                }
            }
        }

        result.expectedReturn = calculateReturn(result.weights);
        result.volatility = calculateVolatility(result.weights);
        result.sharpeRatio = (result.expectedReturn - riskFreeRate_) / result.volatility;
        result.converged = true;

        return result;
    }

    // Risk Parity Portfolio (Equal Risk Contribution)
    OptimizedPortfolio riskParity() {
        OptimizedPortfolio result;
        result.assets = assets_;
        int n = assets_.size();

        if (n == 0) return result;

        // Start with inverse volatility weights
        result.weights.resize(n);
        float sumInvVol = 0.0f;

        for (int i = 0; i < n; ++i) {
            float vol = std::sqrt(covarianceMatrix_[i][i]);
            if (vol > 0) {
                result.weights[i] = 1.0f / vol;
                sumInvVol += result.weights[i];
            } else {
                result.weights[i] = 0.0f;
            }
        }

        if (sumInvVol > 0) {
            for (int i = 0; i < n; ++i) {
                result.weights[i] /= sumInvVol;
            }
        }

        // Iterative risk parity optimization
        int maxIter = 500;
        for (int iter = 0; iter < maxIter; ++iter) {
            // Calculate marginal risk contributions
            float totalRisk = calculateVolatility(result.weights);
            if (totalRisk < 1e-8f) break;

            std::vector<float> marginalRisk(n, 0.0f);
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    marginalRisk[i] += covarianceMatrix_[i][j] * result.weights[j];
                }
                marginalRisk[i] /= totalRisk;
            }

            // Risk contribution = weight * marginal risk
            std::vector<float> riskContribution(n);
            float targetRisk = totalRisk / n;

            for (int i = 0; i < n; ++i) {
                riskContribution[i] = result.weights[i] * marginalRisk[i];

                // Adjust weights to equalize risk contribution
                if (marginalRisk[i] > 0) {
                    float adjustment = (targetRisk - riskContribution[i]) /
                                      marginalRisk[i] * 0.1f;
                    result.weights[i] += adjustment;
                }

                result.weights[i] = std::max(0.0f, result.weights[i]);
                result.weights[i] = std::min(maxWeight_, result.weights[i]);
            }

            // Normalize
            float sum = std::accumulate(result.weights.begin(), result.weights.end(), 0.0f);
            if (sum > 0) {
                for (int i = 0; i < n; ++i) {
                    result.weights[i] /= sum;
                }
            }
        }

        result.expectedReturn = calculateReturn(result.weights);
        result.volatility = calculateVolatility(result.weights);
        result.sharpeRatio = (result.expectedReturn - riskFreeRate_) / result.volatility;
        result.converged = true;

        return result;
    }

    // Target Return Portfolio (minimum variance for given return)
    OptimizedPortfolio targetReturn(float targetRet) {
        OptimizedPortfolio result;
        result.assets = assets_;
        int n = assets_.size();

        if (n == 0) return result;

        // Lagrangian optimization with return constraint
        result.weights.resize(n, 1.0f / n);

        int maxIter = 1000;
        float learningRate = 0.01f;
        float lambda = 1.0f;  // Lagrange multiplier

        for (int iter = 0; iter < maxIter; ++iter) {
            float currentRet = calculateReturn(result.weights);
            float returnError = currentRet - targetRet;

            // Calculate variance gradient + return constraint
            std::vector<float> gradient(n, 0.0f);
            for (int i = 0; i < n; ++i) {
                // Variance gradient
                for (int j = 0; j < n; ++j) {
                    gradient[i] += 2 * covarianceMatrix_[i][j] * result.weights[j];
                }
                // Return constraint gradient
                gradient[i] -= 2 * lambda * returnError * expectedReturns_[i];
            }

            // Update lambda based on constraint violation
            lambda += 0.01f * returnError;

            // Update weights
            float sumWeights = 0.0f;
            for (int i = 0; i < n; ++i) {
                result.weights[i] -= learningRate * gradient[i];

                if (!allowShort_) {
                    result.weights[i] = std::max(minWeight_, result.weights[i]);
                }
                result.weights[i] = std::min(maxWeight_, result.weights[i]);
                sumWeights += result.weights[i];
            }

            if (sumWeights > 0) {
                for (int i = 0; i < n; ++i) {
                    result.weights[i] /= sumWeights;
                }
            }

            if (std::abs(returnError) < 0.0001f) break;
        }

        result.expectedReturn = calculateReturn(result.weights);
        result.volatility = calculateVolatility(result.weights);
        result.sharpeRatio = (result.expectedReturn - riskFreeRate_) / result.volatility;
        result.converged = true;

        return result;
    }

    // Generate Efficient Frontier
    std::vector<EfficientFrontierPoint> efficientFrontier(int numPoints = 20) {
        std::vector<EfficientFrontierPoint> frontier;

        if (expectedReturns_.empty()) return frontier;

        float minRet = *std::min_element(expectedReturns_.begin(), expectedReturns_.end());
        float maxRet = *std::max_element(expectedReturns_.begin(), expectedReturns_.end());

        for (int i = 0; i < numPoints; ++i) {
            float target = minRet + (maxRet - minRet) * i / (numPoints - 1);
            auto portfolio = targetReturn(target);

            EfficientFrontierPoint point;
            point.targetReturn = portfolio.expectedReturn;
            point.minVolatility = portfolio.volatility;
            point.weights = portfolio.weights;

            frontier.push_back(point);
        }

        return frontier;
    }

    // Black-Litterman model for incorporating views
    OptimizedPortfolio blackLitterman(
        const std::vector<float>& marketWeights,
        const std::vector<std::pair<int, float>>& views,  // (asset_index, expected_return)
        float tau = 0.05f,
        float viewConfidence = 0.5f) {

        OptimizedPortfolio result;
        result.assets = assets_;
        int n = assets_.size();

        if (n == 0 || views.empty()) {
            return maxSharpe();  // Fall back to standard optimization
        }

        // Calculate implied equilibrium returns
        std::vector<float> pi(n, 0.0f);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                pi[i] += covarianceMatrix_[i][j] * marketWeights[j];
            }
            pi[i] *= 2.5f;  // Risk aversion coefficient
        }

        // Simplified Black-Litterman: blend equilibrium with views
        std::vector<float> adjustedReturns = pi;
        float viewWeight = viewConfidence;
        float priorWeight = 1.0f - viewConfidence;

        for (const auto& view : views) {
            int idx = view.first;
            float viewReturn = view.second;
            if (idx >= 0 && idx < n) {
                adjustedReturns[idx] = priorWeight * pi[idx] + viewWeight * viewReturn;
            }
        }

        // Store adjusted returns temporarily
        auto originalReturns = expectedReturns_;
        expectedReturns_ = adjustedReturns;

        // Optimize with adjusted returns
        result = maxSharpe();

        // Restore original returns
        expectedReturns_ = originalReturns;

        return result;
    }

private:
    float calculateReturn(const std::vector<float>& weights) const {
        float ret = 0.0f;
        for (size_t i = 0; i < weights.size() && i < expectedReturns_.size(); ++i) {
            ret += weights[i] * expectedReturns_[i];
        }
        return ret;
    }

    float calculateVolatility(const std::vector<float>& weights) const {
        float variance = 0.0f;
        int n = weights.size();

        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                if ((size_t)i < covarianceMatrix_.size() &&
                    (size_t)j < covarianceMatrix_[i].size()) {
                    variance += weights[i] * weights[j] * covarianceMatrix_[i][j];
                }
            }
        }

        return std::sqrt(std::max(0.0f, variance));
    }
};
