#pragma once
#include "../IStrategy.h"
#include "../RegimeDetector.h"
#include "../TechnicalAnalysis.h"
#include <vector>
#include <string>
#include <memory>

// Regime-Switching Strategy
// A meta-strategy that adapts the underlying strategy based on detected market regime
class RegimeSwitchingStrategy : public StrategyBase {
private:
    // Underlying strategies
    std::unique_ptr<IStrategy> trendStrategy_;
    std::unique_ptr<IStrategy> meanRevStrategy_;

    // Regime detector
    RegimeDetection::RegimeDetector regimeDetector_;

    // Current regime
    RegimeDetection::MarketRegime currentRegime_;
    RegimeDetection::RegimeInfo regimeInfo_;

    // Position sizing modifier based on regime
    double positionSizeModifier_ = 1.0;

    // Training status
    bool isTrained_ = false;
    bool autoTrain_ = true;

    // Warmup for regime detection
    int regimeLookback_ = 60;

public:
    RegimeSwitchingStrategy()
        : StrategyBase("RegimeSwitching", 120),  // Need more warmup for regime
          currentRegime_(RegimeDetection::MarketRegime::Unknown) {

        // Add optimizable parameters
        addParam("positionSizeModifier", 1.0, 0.25, 2.0, 0.25);
        addParam("regimeLookback", 60.0, 30.0, 120.0, 10.0);
        addParam("autoTrain", 1.0, 0.0, 1.0, 1.0);
    }

    // Set underlying strategies
    void setStrategies(std::unique_ptr<IStrategy> trendStrategy,
                       std::unique_ptr<IStrategy> meanRevStrategy) {
        trendStrategy_ = std::move(trendStrategy);
        meanRevStrategy_ = std::move(meanRevStrategy);
    }

    // Train the regime detector
    bool trainRegimeDetector(const std::vector<double>& closes,
                              const std::vector<int64_t>& volumes) {
        isTrained_ = regimeDetector_.train(closes, volumes);
        return isTrained_;
    }

    // Check if regime detector is trained
    bool isTrained() const { return isTrained_; }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if (history.size() < (size_t)regimeLookback_) {
            return StrategySignal::hold("Insufficient data for regime detection");
        }

        // Extract close prices and volumes
        std::vector<double> closes;
        std::vector<int64_t> volumes;
        for (const auto& c : history) {
            closes.push_back(c.close);
            volumes.push_back(c.volume);
        }

        // Auto-train if enabled and not trained yet
        if (autoTrain_ && !isTrained_ && closes.size() >= 200) {
            trainRegimeDetector(closes, volumes);
        }

        // Detect current regime
        if (isTrained_) {
            regimeInfo_ = regimeDetector_.detectCurrentRegime(closes, volumes);
            currentRegime_ = regimeInfo_.regime;

            // Get regime-based recommendations
            auto rec = regimeDetector_.getRecommendations(currentRegime_);
            positionSizeModifier_ = rec.positionSize;
        } else {
            // Fallback: use feature-based regime detection
            currentRegime_ = detectRegimeFallback(closes, volumes);
            positionSizeModifier_ = 1.0;
            regimeInfo_.name = "Fallback";
        }

        // Select appropriate strategy based on regime
        IStrategy* activeStrategy = selectStrategy();

        if (!activeStrategy) {
            return StrategySignal::hold("No underlying strategy configured");
        }

        // Generate signal from selected strategy
        StrategySignal signal = activeStrategy->generateSignal(history, idx);

        // Apply regime-based position sizing
        if (signal.isActionable()) {
            signal.strength *= positionSizeModifier_;
            signal.strength = std::min(1.0, signal.strength);  // Cap at 1.0

            // Add regime information to signal reason
            signal.reason += " | Regime: " + regimeInfo_.name;
            signal.reason += " | Size Mod: " + std::to_string(positionSizeModifier_);

            // Adjust stop-loss based on regime
            if (signal.stopLossPrice > 0) {
                auto rec = regimeDetector_.getRecommendations(currentRegime_);
                double currentPrice = history.back().close;
                double atr = computeATR(history, 14);
                double currentStop = signal.stopLossPrice;

                if (signal.type == SignalType::Buy) {
                    double newStop = currentPrice - atr * rec.stopLossMultiplier;
                    signal.stopLossPrice = newStop;  // Use regime-adjusted stop
                } else if (signal.type == SignalType::Sell) {
                    double newStop = currentPrice + atr * rec.stopLossMultiplier;
                    signal.stopLossPrice = newStop;
                }
            }

            signal.confidence *= positionSizeModifier_;
        }

        return signal;
    }

    std::unique_ptr<IStrategy> clone() const override {
        auto copy = std::make_unique<RegimeSwitchingStrategy>();

        if (trendStrategy_) {
            copy->trendStrategy_ = trendStrategy_->clone();
        }
        if (meanRevStrategy_) {
            copy->meanRevStrategy_ = meanRevStrategy_->clone();
        }

        copy->positionSizeModifier_ = positionSizeModifier_;
        copy->regimeLookback_ = regimeLookback_;
        copy->autoTrain_ = autoTrain_;
        copy->currentRegime_ = currentRegime_;
        copy->isTrained_ = isTrained_;
        copy->params_ = params_;

        return copy;
    }

    void reset() override {
        if (trendStrategy_) trendStrategy_->reset();
        if (meanRevStrategy_) meanRevStrategy_->reset();
        currentRegime_ = RegimeDetection::MarketRegime::Unknown;
        positionSizeModifier_ = 1.0;
    }

    // Get current regime info
    RegimeDetection::RegimeInfo getRegimeInfo() const { return regimeInfo_; }
    RegimeDetection::MarketRegime getCurrentRegime() const { return currentRegime_; }

    // Get regime recommendations
    RegimeDetection::RegimeDetector::RegimeRecommendations getRecommendations() const {
        return regimeDetector_.getRecommendations(currentRegime_);
    }

protected:
    void onParametersChanged() override {
        positionSizeModifier_ = getParamValue("positionSizeModifier");
        regimeLookback_ = (int)getParamValue("regimeLookback");
        autoTrain_ = (getParamValue("autoTrain") > 0.5);
    }

private:
    // Select strategy based on current regime
    IStrategy* selectStrategy() {
        switch (currentRegime_) {
            case RegimeDetection::MarketRegime::BullLowVol:
            case RegimeDetection::MarketRegime::BullHighVol:
                // In bull markets, use trend following
                return trendStrategy_.get();

            case RegimeDetection::MarketRegime::BearLowVol:
            case RegimeDetection::MarketRegime::BearHighVol:
            case RegimeDetection::MarketRegime::Sideways:
                // In bear/sideways markets, use mean reversion
                return meanRevStrategy_.get();

            default:
                // Default to trend following
                return trendStrategy_.get();
        }
    }

    // Fallback regime detection without HMM training
    RegimeDetection::MarketRegime detectRegimeFallback(const std::vector<double>& closes,
                                                        const std::vector<int64_t>& volumes) {
        if (closes.size() < 30) {
            return RegimeDetection::MarketRegime::Sideways;
        }

        // Calculate 5-day return
        double return5d = (closes.back() - closes[closes.size() - 5]) / closes[closes.size() - 5];

        // Calculate 20-day volatility
        std::vector<double> returns;
        for (size_t i = closes.size() - 20; i < closes.size() - 1; ++i) {
            double r = (closes[i + 1] - closes[i]) / closes[i];
            returns.push_back(r);
        }

        double volatility = 0.0;
        if (!returns.empty()) {
            double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
            double var = 0.0;
            for (double r : returns) var += (r - mean) * (r - mean);
            volatility = std::sqrt(var / returns.size()) * std::sqrt(252.0);
        }

        // Classify
        bool isBullish = return5d > 0;
        bool isHighVol = volatility > 0.15;

        if (isBullish && !isHighVol) return RegimeDetection::MarketRegime::BullLowVol;
        if (isBullish && isHighVol) return RegimeDetection::MarketRegime::BullHighVol;
        if (!isBullish && !isHighVol) return RegimeDetection::MarketRegime::BearLowVol;
        if (!isBullish && isHighVol) return RegimeDetection::MarketRegime::BearHighVol;
        return RegimeDetection::MarketRegime::Sideways;
    }
};
