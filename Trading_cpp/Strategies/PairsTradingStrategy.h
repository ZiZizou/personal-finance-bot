#pragma once
#include "../IStrategy.h"
#include "../TechnicalAnalysis.h"
#include "../StatisticalArbitrage.h"
#include <vector>
#include <string>
#include <memory>

// Pairs Trading Strategy using Kalman Filter
// Implements statistical arbitrage between two correlated assets
class PairsTradingStrategy : public StrategyBase {
private:
    // Asset pair configuration
    std::string asset1_;
    std::string asset2_;

    // Kalman filter for dynamic hedge ratio
    StatisticalArbitrage::KalmanFilterPairTrading kalmanFilter_;

    // Trading state
    bool hasPosition_ = false;
    bool isLongSpread_ = false;  // true = long asset1, short asset2
    double entryPrice1_ = 0.0;
    double entryPrice2_ = 0.0;
    int barsInPosition_ = 0;

    // Strategy parameters (synced with parent)
    double entryThreshold_ = 2.0;
    double exitThreshold_ = 0.5;
    double stopLossThreshold_ = 3.5;
    int maxHoldingPeriod_ = 20;
    int zScoreLookback_ = 60;
    double kalmanObsNoise_ = 1.0;
    double kalmanProcNoise_ = 0.001;

public:
    PairsTradingStrategy()
        : StrategyBase("PairsTrading", 60) {
        // Add optimizable parameters
        addParam("entryThreshold", 2.0, 1.5, 3.0, 0.25);
        addParam("exitThreshold", 0.5, 0.0, 1.0, 0.1);
        addParam("stopLossThreshold", 3.5, 2.5, 5.0, 0.25);
        addParam("maxHoldingPeriod", 20.0, 5.0, 40.0, 5.0);
        addParam("zScoreLookback", 60.0, 30.0, 120.0, 10.0);
        addParam("kalmanObsNoise", 1.0, 0.1, 5.0, 0.1);
        addParam("kalmanProcNoise", 0.001, 0.0001, 0.01, 0.0001);
    }

    // Set asset pair (call before using strategy)
    void setAssetPair(const std::string& asset1, const std::string& asset2) {
        asset1_ = asset1;
        asset2_ = asset2;
    }

    // Configure Kalman filter and trading parameters
    void configure(double entryThresh, double exitThresh, double stopThresh,
                   int maxHold, int zLookback) {
        entryThreshold_ = entryThresh;
        exitThreshold_ = exitThresh;
        stopLossThreshold_ = stopThresh;
        maxHoldingPeriod_ = maxHold;
        zScoreLookback_ = zLookback;

        kalmanFilter_.setTradingParams(entryThreshold_, exitThreshold_,
                                        stopLossThreshold_, maxHoldingPeriod_);
        kalmanFilter_.setZScoreLookback(zScoreLookback_);
    }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if (history.size() < 50) {
            return StrategySignal::hold("Insufficient data for pairs trading");
        }

        // Update Kalman filter with latest prices
        double price1 = history.back().close;
        double price2 = price1;  // Default if only one asset

        // For pairs trading, we need two price series
        // In a real implementation, this would come from a second data source
        // For now, we'll use the same asset with some modification
        // or require the strategy to be called with combined data

        // The strategy expects two price series - this would typically
        // be provided through a different mechanism in multi-asset backtesting

        // For single-asset backtesting, return hold
        return StrategySignal::hold("Pairs trading requires dual asset data");
    }

    // Generate signal with two price series (for dual-asset backtesting)
    // priceHistory1: vector of asset1 prices (most recent at back)
    // priceHistory2: vector of asset2 prices (most recent at back)
    StrategySignal generateSignalDualAsset(const std::vector<double>& priceHistory1,
                                           const std::vector<double>& priceHistory2) {
        if (priceHistory1.size() < 50 || priceHistory2.size() < 50) {
            return StrategySignal::hold("Insufficient data for pairs trading");
        }

        if (priceHistory1.size() != priceHistory2.size()) {
            return StrategySignal::hold("Price series must have same length");
        }

        // Update Kalman filter with latest prices
        double price1 = priceHistory1.back();
        double price2 = priceHistory2.back();
        kalmanFilter_.update(price1, price2);

        // Get current state
        double zScore = kalmanFilter_.getZScore();
        double hedgeRatio = kalmanFilter_.getHedgeRatio();

        // Check if we have a position
        if (hasPosition_) {
            barsInPosition_++;

            // Check exit conditions
            bool shouldExit = false;
            std::string reason;

            // Exit on mean reversion (Z-score near zero)
            if (std::fabs(zScore) < exitThreshold_) {
                shouldExit = true;
                reason = "Mean reversion: Z-score " + std::to_string(zScore) + " near zero";
            }
            // Exit on stop loss
            else if (std::fabs(zScore) > stopLossThreshold_) {
                shouldExit = true;
                reason = "Stop loss: Z-score " + std::to_string(zScore) + " exceeded threshold";
            }
            // Exit on max holding period
            else if (barsInPosition_ >= maxHoldingPeriod_) {
                shouldExit = true;
                reason = "Max holding period reached: " + std::to_string(barsInPosition_) + " bars";
            }

            if (shouldExit) {
                // Close position
                hasPosition_ = false;
                double pnl = 0.0;
                if (isLongSpread_) {
                    // Long spread: long asset1, short asset2
                    pnl = (price1 - entryPrice1_) - hedgeRatio * (price2 - entryPrice2_);
                } else {
                    // Short spread: short asset1, long asset2
                    pnl = -hedgeRatio * (price2 - entryPrice2_) + (price1 - entryPrice1_);
                }

                std::string exitReason = reason + " | PnL: " + std::to_string(pnl);
                StrategySignal sig = StrategySignal::sell(1.0, exitReason);
                sig.confidence = 1.0;
                return sig;
            }

            // Continue holding
            return StrategySignal::hold("Holding position: Z-score " +
                                        std::to_string(zScore));
        }

        // No position - check entry conditions
        if (zScore > entryThreshold_) {
            // Spread too high - expect mean reversion downward
            // Short spread: short asset1, long asset2
            hasPosition_ = true;
            isLongSpread_ = false;
            entryPrice1_ = price1;
            entryPrice2_ = price2;
            barsInPosition_ = 0;

            StrategySignal sig = StrategySignal::sell(0.9,
                "Short spread: Z-score " + std::to_string(zScore) +
                " > " + std::to_string(entryThreshold_) +
                ", hedge ratio: " + std::to_string(hedgeRatio));
            sig.confidence = std::min(1.0, (zScore - entryThreshold_) / 2.0);
            return sig;
        }
        else if (zScore < -entryThreshold_) {
            // Spread too low - expect mean reversion upward
            // Long spread: long asset1, short asset2
            hasPosition_ = true;
            isLongSpread_ = true;
            entryPrice1_ = price1;
            entryPrice2_ = price2;
            barsInPosition_ = 0;

            StrategySignal sig = StrategySignal::buy(0.9,
                "Long spread: Z-score " + std::to_string(zScore) +
                " < -" + std::to_string(entryThreshold_) +
                ", hedge ratio: " + std::to_string(hedgeRatio));
            sig.confidence = std::min(1.0, (-zScore - entryThreshold_) / 2.0);
            return sig;
        }

        return StrategySignal::hold("No entry: Z-score " + std::to_string(zScore) +
                                    " within thresholds");
    }

    std::unique_ptr<IStrategy> clone() const override {
        auto copy = std::make_unique<PairsTradingStrategy>();
        copy->asset1_ = asset1_;
        copy->asset2_ = asset2_;
        copy->entryThreshold_ = entryThreshold_;
        copy->exitThreshold_ = exitThreshold_;
        copy->stopLossThreshold_ = stopLossThreshold_;
        copy->maxHoldingPeriod_ = maxHoldingPeriod_;
        copy->zScoreLookback_ = zScoreLookback_;
        copy->kalmanObsNoise_ = kalmanObsNoise_;
        copy->kalmanProcNoise_ = kalmanProcNoise_;
        copy->hasPosition_ = hasPosition_;
        copy->isLongSpread_ = isLongSpread_;
        copy->entryPrice1_ = entryPrice1_;
        copy->entryPrice2_ = entryPrice2_;
        copy->barsInPosition_ = barsInPosition_;
        copy->params_ = params_;

        // Reconfigure Kalman filter
        copy->kalmanFilter_.configure(kalmanObsNoise_, kalmanProcNoise_);
        copy->kalmanFilter_.setTradingParams(entryThreshold_, exitThreshold_,
                                              stopLossThreshold_, maxHoldingPeriod_);
        copy->kalmanFilter_.setZScoreLookback(zScoreLookback_);

        return copy;
    }

    void reset() override {
        hasPosition_ = false;
        isLongSpread_ = false;
        entryPrice1_ = 0.0;
        entryPrice2_ = 0.0;
        barsInPosition_ = 0;
        kalmanFilter_.reset();
    }

protected:
    void onParametersChanged() override {
        entryThreshold_ = getParamValue("entryThreshold");
        exitThreshold_ = getParamValue("exitThreshold");
        stopLossThreshold_ = getParamValue("stopLossThreshold");
        maxHoldingPeriod_ = (int)getParamValue("maxHoldingPeriod");
        zScoreLookback_ = (int)getParamValue("zScoreLookback");
        kalmanObsNoise_ = getParamValue("kalmanObsNoise");
        kalmanProcNoise_ = getParamValue("kalmanProcNoise");

        kalmanFilter_.configure(kalmanObsNoise_, kalmanProcNoise_);
        kalmanFilter_.setTradingParams(entryThreshold_, exitThreshold_,
                                       stopLossThreshold_, maxHoldingPeriod_);
        kalmanFilter_.setZScoreLookback(zScoreLookback_);
    }

public:
    // Getters for state
    double getCurrentZScore() const { return kalmanFilter_.getZScore(); }
    double getHedgeRatio() const { return kalmanFilter_.getHedgeRatio(); }
    bool hasPosition() const { return hasPosition_; }

    // Get spread and Z-score for monitoring
    std::string getStatus() const {
        return "Z-Score: " + std::to_string(kalmanFilter_.getZScore()) +
               ", Hedge: " + std::to_string(kalmanFilter_.getHedgeRatio()) +
               ", Position: " + std::string(hasPosition_ ?
                   (isLongSpread_ ? "LONG SPREAD" : "SHORT SPREAD") : "FLAT");
    }
};
