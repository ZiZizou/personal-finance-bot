#pragma once
#include <vector>
#include <string>
#include <memory>
#include "MarketData.h"

// Signal types for strategy decisions
enum class SignalType {
    Buy,    // Open long position
    Sell,   // Close long position / Open short
    Hold    // No action
};

// Comprehensive strategy signal with additional metadata
struct StrategySignal {
    SignalType type = SignalType::Hold;
    float strength = 0.0f;        // Signal strength: -1 (strong sell) to +1 (strong buy)
    float stopLossPrice = 0.0f;   // Suggested stop-loss price (0 = not set)
    float takeProfitPrice = 0.0f; // Suggested take-profit price (0 = not set)
    float confidence = 0.5f;      // Confidence in signal (0 to 1)
    std::string reason;           // Human-readable reason for the signal

    // Helper to check if signal is actionable
    bool isActionable() const {
        return type != SignalType::Hold;
    }

    // Helper to check if signal is bullish
    bool isBullish() const {
        return type == SignalType::Buy || strength > 0;
    }

    // Helper to check if signal is bearish
    bool isBearish() const {
        return type == SignalType::Sell || strength < 0;
    }

    // Static factory methods
    static StrategySignal buy(float strength = 1.0f, const std::string& reason = "") {
        StrategySignal sig;
        sig.type = SignalType::Buy;
        sig.strength = std::min(1.0f, std::max(0.0f, strength));
        sig.reason = reason;
        return sig;
    }

    static StrategySignal sell(float strength = 1.0f, const std::string& reason = "") {
        StrategySignal sig;
        sig.type = SignalType::Sell;
        sig.strength = std::min(0.0f, std::max(-1.0f, -strength));
        sig.reason = reason;
        return sig;
    }

    static StrategySignal hold(const std::string& reason = "") {
        StrategySignal sig;
        sig.type = SignalType::Hold;
        sig.strength = 0.0f;
        sig.reason = reason;
        return sig;
    }
};

// Strategy parameters structure for optimization
struct StrategyParams {
    std::string name;
    float value;
    float minValue;
    float maxValue;
    float step;

    StrategyParams(const std::string& n, float v, float minV, float maxV, float s)
        : name(n), value(v), minValue(minV), maxValue(maxV), step(s) {}
};

// Abstract strategy interface
class IStrategy {
public:
    virtual ~IStrategy() = default;

    // Core method: Generate a trading signal given historical data
    // history: All candles up to and including the current bar
    // idx: Current bar index (always history.size() - 1 for live, variable for backtest)
    virtual StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) = 0;

    // Get the strategy name for logging/reporting
    virtual std::string getName() const = 0;

    // Get the warmup period (number of bars needed before strategy can generate signals)
    virtual int getWarmupPeriod() const = 0;

    // Optional: Get strategy parameters for optimization
    virtual std::vector<StrategyParams> getParameters() const {
        return {};
    }

    // Optional: Set strategy parameters (for optimization)
    virtual void setParameters(const std::vector<StrategyParams>& params) {
        // Default: do nothing
        (void)params;
    }

    // Optional: Reset strategy state (for walk-forward or new backtest)
    virtual void reset() {
        // Default: do nothing
    }

    // Optional: Called when a trade is executed (for learning strategies)
    virtual void onTradeExecuted(float entryPrice, float exitPrice, bool isWin) {
        // Default: do nothing
        (void)entryPrice;
        (void)exitPrice;
        (void)isWin;
    }

    // Clone the strategy (for parallel optimization)
    virtual std::unique_ptr<IStrategy> clone() const = 0;
};

// Base class with common functionality
class StrategyBase : public IStrategy {
protected:
    std::string name_;
    int warmupPeriod_;
    std::vector<StrategyParams> params_;

public:
    StrategyBase(const std::string& name, int warmupPeriod)
        : name_(name), warmupPeriod_(warmupPeriod) {}

    std::string getName() const override { return name_; }
    int getWarmupPeriod() const override { return warmupPeriod_; }

    std::vector<StrategyParams> getParameters() const override {
        return params_;
    }

    void setParameters(const std::vector<StrategyParams>& params) override {
        for (const auto& newParam : params) {
            for (auto& existing : params_) {
                if (existing.name == newParam.name) {
                    existing.value = newParam.value;
                    break;
                }
            }
        }
        onParametersChanged();
    }

protected:
    // Override this to respond to parameter changes
    virtual void onParametersChanged() {}

    // Helper to get parameter value by name
    float getParamValue(const std::string& name) const {
        for (const auto& p : params_) {
            if (p.name == name) return p.value;
        }
        return 0.0f;
    }

    // Helper to add a parameter
    void addParam(const std::string& name, float value, float minV, float maxV, float step) {
        params_.emplace_back(name, value, minV, maxV, step);
    }
};
