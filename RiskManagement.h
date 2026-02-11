#pragma once
#include "Broker.h"
#include "IStrategy.h"
#include "BarSeries.h"
#include "TechnicalAnalysis.h"
#include <optional>
#include <chrono>
#include <atomic>
#include <mutex>
#include <map>
#include <vector>
#include <cmath>
#include <algorithm>

// Risk guard configuration
struct RiskGuardConfig {
    // Daily loss limits
    bool enableDailyLossLimit = true;
    double maxDailyLossPct = 0.02;  // 2% max daily loss

    // Consecutive error handling
    int maxConsecutiveErrors = 5;

    // Order rate limiting
    int maxOrdersPerSymbolPerHour = 2;

    // Kill switch - if tripped, no new orders
    bool enableKillSwitch = true;

    // Auto-flatten on kill switch
    bool flattenOnKillSwitch = false;
};

// RiskGuard: Monitors risk conditions and can stop trading
class RiskGuard {
public:
    explicit RiskGuard(const RiskGuardConfig& config = RiskGuardConfig());

    // Check if trading is allowed
    bool isTradingAllowed() const;

    // Get reason why trading is blocked
    std::string getBlockReason() const;

    // Record an error
    void recordError();

    // Reset error count (call on successful operation)
    void resetErrors();

    // Record daily P&L update
    void updateDailyPnL(double unrealizedPnl, double startingEquity);

    // Record an order placement
    void recordOrder(const std::string& symbol);

    // Check if order is allowed for symbol
    bool canPlaceOrder(const std::string& symbol) const;

    // Manual kill switch control
    void tripKillSwitch(const std::string& reason);
    void resetKillSwitch();

    // Get current state
    int getErrorCount() const { return consecutiveErrors_; }
    bool isKillSwitchTripped() const { return killSwitchTripped_; }

private:
    RiskGuardConfig config_;
    mutable std::mutex mutex_;

    std::atomic<int> consecutiveErrors_{0};
    std::atomic<bool> killSwitchTripped_{false};
    std::string killSwitchReason_;

    double dailyPnL_ = 0.0;
    double startingEquity_ = 0.0;

    // Order tracking: symbol -> list of order timestamps
    std::map<std::string, std::vector<std::chrono::steady_clock::time_point>> orderHistory_;
};

// Position sizing: risk-based sizing instead of Kelly
struct PositionSizeResult {
    int64_t quantity;
    double riskAmount;
    double perShareRisk;
};

// Calculate position size based on risk per trade
inline PositionSizeResult calculateRiskBasedSize(
    double equity,
    double riskPct,
    double entryPrice,
    double stopPrice
) {
    PositionSizeResult result;

    double riskDollars = equity * riskPct;
    double perShareRisk = std::abs(entryPrice - stopPrice);

    if (perShareRisk < 0.01) {
        perShareRisk = 0.01;  // Minimum per-share risk
    }

    result.quantity = static_cast<int64_t>(std::floor(riskDollars / perShareRisk));
    result.riskAmount = riskDollars;
    result.perShareRisk = perShareRisk;

    return result;
}

// ATR-based stop calculation
inline double calculateATRStop(double entryPrice, double atr, double multiplier, bool isLong) {
    if (isLong) {
        return entryPrice - (atr * multiplier);
    } else {
        return entryPrice + (atr * multiplier);
    }
}

// DecisionEngine configuration
struct DecisionEngineConfig {
    double riskPerTrade = 0.01;       // 1% risk per trade
    double atrStopMultiplier = 2.0;   // ATR multiplier for stops
    int64_t maxPositionSize = 1000;   // Maximum shares per position
    double minDollarVolume = 100000;  // Minimum avg daily dollar volume
};

// DecisionEngine: Converts signals to order requests with risk checks
class DecisionEngine {
public:
    using Config = DecisionEngineConfig;

    explicit DecisionEngine(const Config& config = Config());

    // Evaluate a signal and return an order request if appropriate
    std::optional<OrderRequest> evaluate(
        const std::string& symbol,
        const BarSeries& series,
        const StrategySignal& signal,
        const AccountInfo& account,
        const std::vector<PositionInfo>& positions
    );

    // Check if we already have a position in this symbol
    bool hasPosition(const std::string& symbol,
                    const std::vector<PositionInfo>& positions) const;

    // Get current position quantity for a symbol
    int64_t getPositionQty(const std::string& symbol,
                          const std::vector<PositionInfo>& positions) const;

private:
    Config config_;

    // Check liquidity filter
    bool passesLiquidityCheck(const BarSeries& series) const;
};

// Order idea for signal output (not actual execution)
enum class OrderIdeaSide { Buy, Sell };

struct OrderIdea {
    OrderIdeaSide side;
    double limitPrice;
    double stopLossPrice;
    double takeProfitPrice;
    std::string timeInForce;  // "DAY", "GTC"
};

// Build order idea using ATR and support/resistance
inline OrderIdea buildBuyIdea(
    double close,
    double atr,
    double support,
    double stopFromSignal,
    double tpFromSignal,
    double resistance
) {
    // Limit price slightly below close
    double limit = close - 0.25 * atr;
    if (support > 0) {
        limit = std::min(limit, support * 1.005);  // 0.5% above support
    }
    limit = std::round(limit * 100.0) / 100.0;  // Round to cent

    // Stop loss
    double stop = (stopFromSignal > 0) ? stopFromSignal : (limit - 1.5 * atr);
    stop = std::round(stop * 100.0) / 100.0;

    // Take profit
    double tp = (tpFromSignal > 0) ? tpFromSignal : (limit + 2.0 * atr);
    if (resistance > 0) {
        tp = std::min(tp, resistance);
    }
    tp = std::round(tp * 100.0) / 100.0;

    return OrderIdea{OrderIdeaSide::Buy, limit, stop, tp, "DAY"};
}

inline OrderIdea buildSellIdea(
    double close,
    double atr,
    double resistance,
    double stopFromSignal,
    double tpFromSignal,
    double support
) {
    // For sell (exit-only), place limit slightly above close
    double limit = close + 0.10 * atr;
    if (resistance > 0) {
        limit = std::min(limit, resistance * 0.995);
    }
    limit = std::round(limit * 100.0) / 100.0;

    // No stop for exit orders (we're closing)
    double stop = (stopFromSignal > 0) ? stopFromSignal : 0.0;

    // Take profit = support for exits
    double tp = (tpFromSignal > 0) ? tpFromSignal : support;
    tp = std::round(tp * 100.0) / 100.0;

    return OrderIdea{OrderIdeaSide::Sell, limit, stop, tp, "DAY"};
}
