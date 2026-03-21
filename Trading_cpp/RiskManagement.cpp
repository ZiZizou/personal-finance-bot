#include "RiskManagement.h"
#include <algorithm>

// RiskGuard implementation

RiskGuard::RiskGuard(const RiskGuardConfig& config)
    : config_(config) {}

bool RiskGuard::isTradingAllowed() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (killSwitchTripped_) {
        return false;
    }

    if (consecutiveErrors_ >= config_.maxConsecutiveErrors) {
        return false;
    }

    // Check daily loss limit
    if (config_.enableDailyLossLimit && startingEquity_ > 0) {
        double lossRatio = -dailyPnL_ / startingEquity_;
        if (lossRatio >= config_.maxDailyLossPct) {
            return false;
        }
    }

    return true;
}

std::string RiskGuard::getBlockReason() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (killSwitchTripped_) {
        return "Kill switch tripped: " + killSwitchReason_;
    }

    if (consecutiveErrors_ >= config_.maxConsecutiveErrors) {
        return "Too many consecutive errors: " + std::to_string(consecutiveErrors_);
    }

    if (config_.enableDailyLossLimit && startingEquity_ > 0) {
        double lossRatio = -dailyPnL_ / startingEquity_;
        if (lossRatio >= config_.maxDailyLossPct) {
            return "Daily loss limit reached: " + std::to_string(lossRatio * 100) + "%";
        }
    }

    return "";
}

void RiskGuard::recordError() {
    ++consecutiveErrors_;

    if (config_.enableKillSwitch && consecutiveErrors_ >= config_.maxConsecutiveErrors) {
        tripKillSwitch("Too many consecutive errors");
    }
}

void RiskGuard::resetErrors() {
    consecutiveErrors_ = 0;
}

void RiskGuard::updateDailyPnL(double unrealizedPnl, double startingEquity) {
    std::lock_guard<std::mutex> lock(mutex_);
    dailyPnL_ = unrealizedPnl;
    if (startingEquity_ == 0.0) {
        startingEquity_ = startingEquity;
    }
}

void RiskGuard::recordOrder(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    orderHistory_[symbol].push_back(now);

    // Clean up old entries (older than 1 hour)
    auto oneHourAgo = now - std::chrono::hours(1);
    auto& orders = orderHistory_[symbol];
    orders.erase(
        std::remove_if(orders.begin(), orders.end(),
            [oneHourAgo](const auto& t) { return t < oneHourAgo; }),
        orders.end()
    );
}

bool RiskGuard::canPlaceOrder(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = orderHistory_.find(symbol);
    if (it == orderHistory_.end()) {
        return true;
    }

    // Count orders in last hour
    auto now = std::chrono::steady_clock::now();
    auto oneHourAgo = now - std::chrono::hours(1);

    int count = 0;
    for (const auto& t : it->second) {
        if (t >= oneHourAgo) {
            ++count;
        }
    }

    return count < config_.maxOrdersPerSymbolPerHour;
}

void RiskGuard::tripKillSwitch(const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    killSwitchTripped_ = true;
    killSwitchReason_ = reason;
}

void RiskGuard::resetKillSwitch() {
    std::lock_guard<std::mutex> lock(mutex_);
    killSwitchTripped_ = false;
    killSwitchReason_ = "";
    consecutiveErrors_ = 0;
}

// DecisionEngine implementation

DecisionEngine::DecisionEngine(const Config& config)
    : config_(config) {}

std::optional<OrderRequest> DecisionEngine::evaluate(
    const std::string& symbol,
    const BarSeries& series,
    const StrategySignal& signal,
    const AccountInfo& account,
    const std::vector<PositionInfo>& positions
) {
    // Only process actionable signals
    if (!signal.isActionable()) {
        return std::nullopt;
    }

    // Check liquidity
    if (!passesLiquidityCheck(series)) {
        return std::nullopt;
    }

    bool hasPos = hasPosition(symbol, positions);
    int64_t currentQty = getPositionQty(symbol, positions);

    // Buy signal
    if (signal.type == SignalType::Buy && !hasPos) {
        // Calculate ATR for position sizing
        double atr = computeATR(std::vector<Candle>(series.bars().begin(), series.bars().end()), 14);
        double entryPrice = series.bars().back().close;

        // Calculate stop loss
        double stopPrice = signal.stopLossPrice > 0 ?
            signal.stopLossPrice :
            calculateATRStop(entryPrice, atr, config_.atrStopMultiplier, true);

        // Scale risk per trade by signal confidence
        double adjustedRisk = config_.riskPerTrade * (0.5 + 0.5 * signal.confidence);

        // Calculate position size based on risk
        auto sizing = calculateRiskBasedSize(
            account.equity,
            adjustedRisk,
            entryPrice,
            stopPrice
        );

        // Apply position limits
        int64_t qty = std::min(sizing.quantity, config_.maxPositionSize);
        if (qty <= 0) {
            return std::nullopt;
        }

        OrderRequest req;
        req.symbol = symbol;
        req.side = OrderSide::Buy;
        req.type = OrderType::Market;
        req.qty = qty;
        req.stopLossPrice = stopPrice;
        req.takeProfitPrice = signal.takeProfitPrice;

        return req;
    }

    // Sell signal (exit only for equities)
    if (signal.type == SignalType::Sell && hasPos && currentQty > 0) {
        OrderRequest req;
        req.symbol = symbol;
        req.side = OrderSide::Sell;
        req.type = OrderType::Market;
        req.qty = currentQty;  // Close entire position

        return req;
    }

    return std::nullopt;
}

bool DecisionEngine::hasPosition(
    const std::string& symbol,
    const std::vector<PositionInfo>& positions
) const {
    for (const auto& pos : positions) {
        if (pos.symbol == symbol && pos.qty != 0) {
            return true;
        }
    }
    return false;
}

int64_t DecisionEngine::getPositionQty(
    const std::string& symbol,
    const std::vector<PositionInfo>& positions
) const {
    for (const auto& pos : positions) {
        if (pos.symbol == symbol) {
            return pos.qty;
        }
    }
    return 0;
}

bool DecisionEngine::passesLiquidityCheck(const BarSeries& series) const {
    if (series.size() < 20) return false;

    // Calculate average dollar volume over last 20 bars
    double totalDollarVolume = 0.0;
    auto bars = series.lastN(20);
    for (const auto& bar : bars) {
        totalDollarVolume += bar.close * bar.volume;
    }
    double avgDollarVolume = totalDollarVolume / 20.0;

    return avgDollarVolume >= config_.minDollarVolume;
}
