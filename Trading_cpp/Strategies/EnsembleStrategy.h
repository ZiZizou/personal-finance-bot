#pragma once
#include "../IStrategy.h"
#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include <numeric>

// Ensemble Strategy: Multi-strategy consensus
// Only emits BUY/SELL when a configurable majority of child strategies agree.
// Confidence = agreement ratio (separates confidence from strength).
// Strength = confidence-weighted average of agreeing strategies' strength magnitudes.
class EnsembleStrategy : public StrategyBase {
private:
    struct ChildSignal {
        SignalType type;
        double absStrength;
        double confidence;
        double stopLoss;
        double takeProfit;
        std::string name;
    };

    std::vector<std::unique_ptr<IStrategy>> children_;
    double requiredAgreement_ = 0.5;  // Fraction of strategies that must agree

public:
    EnsembleStrategy(std::vector<std::unique_ptr<IStrategy>> children,
                     double requiredAgreement = 0.5)
        : StrategyBase("Ensemble", 0)
        , children_(std::move(children))
        , requiredAgreement_(requiredAgreement)
    {
        // Warmup = max across children
        for (const auto& child : children_) {
            warmupPeriod_ = std::max(warmupPeriod_, child->getWarmupPeriod());
        }
    }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if (children_.empty()) {
            return StrategySignal::hold("No child strategies");
        }

        // Collect signals from all children
        std::vector<ChildSignal> buySignals;
        std::vector<ChildSignal> sellSignals;

        for (auto& child : children_) {
            StrategySignal sig = child->generateSignal(history, idx);

            if (sig.type == SignalType::Buy) {
                buySignals.push_back({
                    sig.type,
                    std::abs(sig.strength),
                    sig.confidence,
                    sig.stopLossPrice,
                    sig.takeProfitPrice,
                    child->getName()
                });
            } else if (sig.type == SignalType::Sell) {
                sellSignals.push_back({
                    sig.type,
                    std::abs(sig.strength),
                    sig.confidence,
                    sig.stopLossPrice,
                    sig.takeProfitPrice,
                    child->getName()
                });
            }
        }

        int total = static_cast<int>(children_.size());
        int buyCount = static_cast<int>(buySignals.size());
        int sellCount = static_cast<int>(sellSignals.size());

        double buyRatio = static_cast<double>(buyCount) / total;
        double sellRatio = static_cast<double>(sellCount) / total;

        // Check if BUY consensus is met
        if (buyRatio >= requiredAgreement_ && buyCount >= sellCount) {
            return buildConsensusSignal(buySignals, SignalType::Buy, buyCount, total);
        }

        // Check if SELL consensus is met
        if (sellRatio >= requiredAgreement_ && sellCount > buyCount) {
            return buildConsensusSignal(sellSignals, SignalType::Sell, sellCount, total);
        }

        return StrategySignal::hold("No ensemble consensus (" +
            std::to_string(buyCount) + " buy, " +
            std::to_string(sellCount) + " sell of " +
            std::to_string(total) + ")");
    }

    std::unique_ptr<IStrategy> clone() const override {
        std::vector<std::unique_ptr<IStrategy>> clonedChildren;
        for (const auto& child : children_) {
            clonedChildren.push_back(child->clone());
        }
        return std::make_unique<EnsembleStrategy>(std::move(clonedChildren), requiredAgreement_);
    }

    void reset() override {
        for (auto& child : children_) {
            child->reset();
        }
    }

    int getWarmupPeriod() const override {
        int maxWarmup = 0;
        for (const auto& child : children_) {
            maxWarmup = std::max(maxWarmup, child->getWarmupPeriod());
        }
        return maxWarmup;
    }

private:
    StrategySignal buildConsensusSignal(
        const std::vector<ChildSignal>& agreeing,
        SignalType type,
        int agreeCount,
        int total
    ) const {
        // Confidence = agreement ratio
        double confidence = static_cast<double>(agreeCount) / total;

        // Strength = confidence-weighted average of agreeing strategies' strength magnitudes
        double weightedStrengthSum = 0.0;
        double confSum = 0.0;
        for (const auto& cs : agreeing) {
            double w = std::max(0.01, cs.confidence);  // Avoid zero weight
            weightedStrengthSum += cs.absStrength * w;
            confSum += w;
        }
        double strength = (confSum > 0) ? (weightedStrengthSum / confSum) : 0.5;

        // Stop-loss = tightest among agreeing strategies
        double stopLoss = 0.0;
        for (const auto& cs : agreeing) {
            if (cs.stopLoss > 0) {
                if (type == SignalType::Buy) {
                    // For buy, tightest = highest stop (closest to entry)
                    stopLoss = (stopLoss == 0) ? cs.stopLoss : std::max(stopLoss, cs.stopLoss);
                } else {
                    // For sell, tightest = lowest stop (closest to entry)
                    stopLoss = (stopLoss == 0) ? cs.stopLoss : std::min(stopLoss, cs.stopLoss);
                }
            }
        }

        // Take-profit = mean of agreeing strategies' TPs (excluding 0)
        double tpSum = 0.0;
        int tpCount = 0;
        for (const auto& cs : agreeing) {
            if (cs.takeProfit > 0) {
                tpSum += cs.takeProfit;
                tpCount++;
            }
        }
        double takeProfit = (tpCount > 0) ? (tpSum / tpCount) : 0.0;

        // Build reason string: "Ensemble(2/2): MeanReversion | TrendFollowing"
        std::string reason = "Ensemble(" + std::to_string(agreeCount) + "/" +
                           std::to_string(total) + "): ";
        for (size_t i = 0; i < agreeing.size(); ++i) {
            if (i > 0) reason += " | ";
            reason += agreeing[i].name;
        }

        // Create signal using factory methods (buy/sell set sign correctly)
        StrategySignal sig = (type == SignalType::Buy) ?
            StrategySignal::buy(strength, reason) :
            StrategySignal::sell(strength, reason);

        sig.stopLossPrice = stopLoss;
        sig.takeProfitPrice = takeProfit;
        sig.confidence = confidence;

        return sig;
    }
};
