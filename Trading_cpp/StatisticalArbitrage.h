#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include "CointegrationTests.h"

// Kalman Filter-based pair trading implementation
namespace StatisticalArbitrage {

// Kalman Filter state for dynamic hedge ratio estimation
class KalmanFilterPairTrading {
private:
    // State variables
    double hedgeRatio_ = 1.0;      // Current estimate of hedge ratio (beta)
    double intercept_ = 0.0;        // Current estimate of intercept (alpha)
    double spread_;                  // Current spread value

    // Kalman filter parameters
    double observationNoise_;       // R: Measurement noise (variance of spread)
    double processNoise_;           // Q: Process noise (variance of hedge ratio changes)
    double stateVariance_;          // P: State covariance

    // Z-score calculation
    int zScoreLookback_ = 60;
    std::vector<double> spreadHistory_;
    double zScore_ = 0.0;
    double spreadMean_ = 0.0;
    double spreadStd_ = 1.0;

    // Position tracking
    bool hasPosition_ = false;
    bool isLongSpread_ = false;  // true = long spread (long asset1, short asset2)
    double entrySpread_ = 0.0;
    int barsInPosition_ = 0;

    // Trading parameters
    double entryThreshold_ = 2.0;   // Z-score threshold for entry
    double exitThreshold_ = 0.5;   // Z-score threshold for exit
    double stopLossThreshold_ = 3.5; // Z-score threshold for stop loss
    int maxHoldingPeriod_ = 20;    // Maximum bars to hold position
    bool useFixedHedgeRatio_ = false;

public:
    KalmanFilterPairTrading()
        : observationNoise_(1.0),
          processNoise_(0.001),
          stateVariance_(1.0) {
    }

    // Configure Kalman filter parameters
    void configure(double observationNoise, double processNoise) {
        observationNoise_ = observationNoise;
        processNoise_ = processNoise;
    }

    // Configure trading parameters
    void setTradingParams(double entryThreshold, double exitThreshold,
                          double stopLossThreshold, int maxHoldingPeriod) {
        entryThreshold_ = entryThreshold;
        exitThreshold_ = exitThreshold;
        stopLossThreshold_ = stopLossThreshold;
        maxHoldingPeriod_ = maxHoldingPeriod;
    }

    // Configure Z-score lookback
    void setZScoreLookback(int lookback) {
        zScoreLookback_ = lookback;
    }

    // Use fixed hedge ratio instead of Kalman filter
    void setFixedHedgeRatio(double ratio) {
        useFixedHedgeRatio_ = true;
        hedgeRatio_ = ratio;
    }

    // Get current hedge ratio
    double getHedgeRatio() const { return hedgeRatio_; }
    double getIntercept() const { return intercept_; }
    double getSpread() const { return spread_; }
    double getZScore() const { return zScore_; }
    bool hasPosition() const { return hasPosition_; }
    bool isLongSpread() const { return isLongSpread_; }

    // Update Kalman filter with new price pair
    void update(double price1, double price2) {
        // Prediction step
        // State (hedge ratio) evolves with process noise
        stateVariance_ += processNoise_;

        // Calculate current spread using current hedge ratio
        // spread = price1 - hedgeRatio * price2 - intercept
        spread_ = price1 - hedgeRatio_ * price2 - intercept_;

        // Update step (measurement update)
        // Observation: spread = price1 - hedgeRatio * price2
        // This is equivalent to observing: price1 = hedgeRatio * price2 + spread

        // We observe the spread directly
        double y = price1 - price2 * hedgeRatio_;  // Residual

        // Kalman gain
        double kalmanGain = stateVariance_ / (stateVariance_ + observationNoise_);

        // Update hedge ratio estimate
        // The "innovation" is the difference between observed and predicted spread
        double innovation = spread_;  // Current spread - expected spread (0)
        hedgeRatio_ += kalmanGain * innovation / price2;  // Adjust hedge ratio

        // Update intercept to center the spread
        // Simple mean-reversion on intercept
        if (spreadHistory_.size() >= 20) {
            double recentMean = 0.0;
            for (size_t i = spreadHistory_.size() - 20; i < spreadHistory_.size(); ++i) {
                recentMean += spreadHistory_[i];
            }
            intercept_ = intercept_ + 0.1 * (recentMean / 20.0 - intercept_);
        }

        // Update state covariance
        stateVariance_ = (1.0 - kalmanGain) * stateVariance_;

        // Update spread history for Z-score calculation
        spreadHistory_.push_back(spread_);
        if ((int)spreadHistory_.size() > zScoreLookback_ * 2) {
            spreadHistory_.erase(spreadHistory_.begin());
        }

        // Recalculate Z-score
        updateZScore();

        // Update position tracking
        if (hasPosition_) {
            barsInPosition_++;
        }
    }

    // Update with pre-calculated spread
    void updateSpread(double spread) {
        spread_ = spread;

        // Update spread history for Z-score calculation
        spreadHistory_.push_back(spread_);
        if ((int)spreadHistory_.size() > zScoreLookback_ * 2) {
            spreadHistory_.erase(spreadHistory_.begin());
        }

        // Recalculate Z-score
        updateZScore();

        if (hasPosition_) {
            barsInPosition_++;
        }
    }

    // Update with fixed hedge ratio
    void updateWithFixedHedgeRatio(double price1, double price2) {
        spread_ = price1 - hedgeRatio_ * price2 - intercept_;

        spreadHistory_.push_back(spread_);
        if ((int)spreadHistory_.size() > zScoreLookback_ * 2) {
            spreadHistory_.erase(spreadHistory_.begin());
        }

        updateZScore();

        if (hasPosition_) {
            barsInPosition_++;
        }
    }

private:
    void updateZScore() {
        if ((int)spreadHistory_.size() < zScoreLookback_) {
            zScore_ = 0.0;
            return;
        }

        size_t startIdx = spreadHistory_.size() - zScoreLookback_;

        // Calculate mean
        double sum = 0.0;
        for (size_t i = startIdx; i < spreadHistory_.size(); ++i) {
            sum += spreadHistory_[i];
        }
        spreadMean_ = sum / zScoreLookback_;

        // Calculate standard deviation
        double sqDiffSum = 0.0;
        for (size_t i = startIdx; i < spreadHistory_.size(); ++i) {
            double diff = spreadHistory_[i] - spreadMean_;
            sqDiffSum += diff * diff;
        }
        spreadStd_ = std::sqrt(sqDiffSum / (zScoreLookback_ - 1));

        // Calculate z-score
        if (spreadStd_ > 1e-10) {
            zScore_ = (spread_ - spreadMean_) / spreadStd_;
        }
    }

public:
    // Generate trading signal
    // Returns: 1 = Long spread, -1 = Short spread, 0 = No signal
    int generateSignal() {
        // If we have a position, check exit conditions
        if (hasPosition_) {
            // Check exit threshold
            if (std::fabs(zScore_) < exitThreshold_) {
                return 0;  // Exit signal (will be handled by strategy)
            }

            // Check stop loss
            if (std::fabs(zScore_) > stopLossThreshold_) {
                return 0;  // Stop loss triggered
            }

            // Check max holding period
            if (barsInPosition_ >= maxHoldingPeriod_) {
                return 0;  // Time exit
            }

            // Continue holding
            return isLongSpread_ ? 1 : -1;
        }

        // No position - check entry conditions
        if (zScore_ > entryThreshold_) {
            // Spread is too high - expect mean reversion
            // Short the spread (short price1, long price2)
            return -1;
        } else if (zScore_ < -entryThreshold_) {
            // Spread is too low - expect mean reversion
            // Long the spread (long price1, short price2)
            return 1;
        }

        return 0;  // No signal
    }

    // Enter a position
    void enterPosition(bool longSpread) {
        hasPosition_ = true;
        isLongSpread_ = longSpread;
        entrySpread_ = spread_;
        barsInPosition_ = 0;
    }

    // Exit position
    void exitPosition() {
        hasPosition_ = false;
        isLongSpread_ = false;
        entrySpread_ = 0.0;
        barsInPosition_ = 0;
    }

    // Reset state
    void reset() {
        hedgeRatio_ = 1.0;
        intercept_ = 0.0;
        spread_ = 0.0;
        spreadHistory_.clear();
        zScore_ = 0.0;
        spreadMean_ = 0.0;
        spreadStd_ = 1.0;
        hasPosition_ = false;
        isLongSpread_ = false;
        entrySpread_ = 0.0;
        barsInPosition_ = 0;
    }

    // Get position info for logging
    std::string getPositionInfo() const {
        std::string info = "Spread: " + std::to_string(spread_) +
                          ", Z-Score: " + std::to_string(zScore_) +
                          ", Hedge Ratio: " + std::to_string(hedgeRatio_);
        if (hasPosition_) {
            info += ", Position: " + std::string(isLongSpread_ ? "LONG" : "SHORT");
            info += ", Bars: " + std::to_string(barsInPosition_);
        }
        return info;
    }
};

// Simple pair scanner for finding cointegrated pairs
struct PairCandidate {
    std::string symbol1;
    std::string symbol2;
    double hedgeRatio;
    double tStatistic;
    double pValue;
    bool isCointegrated;
    double halfLife;
    double correlation;
};

class PairScanner {
public:
    // Scan two price series for cointegration
    static PairCandidate scanPair(const std::vector<double>& prices1,
                                   const std::vector<double>& prices2,
                                   const std::string& sym1,
                                   const std::string& sym2) {
        PairCandidate candidate;
        candidate.symbol1 = sym1;
        candidate.symbol2 = sym2;
        candidate.hedgeRatio = 1.0;
        candidate.tStatistic = 0.0;
        candidate.pValue = 1.0;
        candidate.isCointegrated = false;
        candidate.halfLife = -1.0;
        candidate.correlation = 0.0;

        if (prices1.size() != prices2.size() || prices1.size() < 60) {
            return candidate;
        }

        // Run cointegration test
        auto cointegration = CointegrationTests::engleGrangerTest(prices1, prices2);
        candidate.hedgeRatio = cointegration.hedgeRatio;
        candidate.tStatistic = cointegration.tStatistic;
        candidate.pValue = cointegration.pValue;
        candidate.isCointegrated = cointegration.isCointegrated;
        candidate.halfLife = cointegration.halfLife;

        // Calculate correlation
        candidate.correlation = CointegrationTests::calculateRollingCorrelation(prices1, prices2);

        return candidate;
    }

    // Filter pairs based on criteria
    static bool isViableCandidate(const PairCandidate& candidate,
                                  double maxPValue = 0.05,
                                  double minCorrelation = 0.5,
                                  double maxHalfLife = 60) {
        if (!candidate.isCointegrated) return false;
        if (candidate.pValue > maxPValue) return false;
        if (candidate.correlation < minCorrelation) return false;
        if (candidate.halfLife > maxHalfLife || candidate.halfLife < 2) return false;

        return true;
    }
};

} // namespace StatisticalArbitrage
