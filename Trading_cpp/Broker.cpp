#include "Broker.h"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <map>

// =============================================================================
// DEPRECATED: PaperBroker is deprecated.
// =============================================================================
// Portfolio management is now handled entirely by Python API via portfolio.json.
// The Python API provides REST endpoints for portfolio operations:
//   - GET  /api/portfolio           - Get portfolio
//   - POST /api/portfolio/position   - Add/update position
//   - DELETE /api/portfolio/position/{ticker} - Remove position
//   - POST /api/portfolio/execute/{trade_id}  - Execute accepted trade
//
// Trading-cpp should NOT manage portfolios. It should only:
//   - Perform ONNX inference when called by Python API
//   - Do technical analysis on tickers provided by Python
//   - Write signals to files for Python to read
//
// This file is kept for backwards compatibility but will be removed.
// =============================================================================

// PaperBroker implementation

PaperBroker::PaperBroker(double initialCash)
    : cash_(initialCash) {}

std::string PaperBroker::generateOrderId() {
    std::ostringstream oss;
    oss << "PAPER-" << std::setfill('0') << std::setw(8) << ++orderCounter_;
    return oss.str();
}

Result<AccountInfo> PaperBroker::getAccount() {
    AccountInfo info;
    info.cash = cash_;

    // Calculate portfolio value
    double portfolioValue = 0.0;
    for (const auto& [symbol, qty] : positions_) {
        auto priceIt = prices_.find(symbol);
        if (priceIt != prices_.end()) {
            portfolioValue += qty * priceIt->second;
        }
    }

    info.portfolioValue = portfolioValue;
    info.equity = cash_ + portfolioValue;
    info.buyingPower = cash_;  // Simplified - no margin
    info.valid = true;

    return Result<AccountInfo>::ok(info);
}

Result<std::vector<PositionInfo>> PaperBroker::getPositions() {
    std::vector<PositionInfo> result;

    for (const auto& [symbol, qty] : positions_) {
        if (qty == 0) continue;

        PositionInfo pos;
        pos.symbol = symbol;
        pos.qty = qty;
        pos.avgEntryPrice = avgPrices_[symbol];

        auto priceIt = prices_.find(symbol);
        if (priceIt != prices_.end()) {
            pos.currentPrice = priceIt->second;
            pos.marketValue = qty * pos.currentPrice;
            double entryValue = qty * pos.avgEntryPrice;
            pos.unrealizedPnl = pos.marketValue - entryValue;
            pos.unrealizedPnlPercent = (entryValue > 0) ?
                (pos.unrealizedPnl / entryValue) * 100.0 : 0.0;
        }

        result.push_back(pos);
    }

    return Result<std::vector<PositionInfo>>::ok(result);
}

Result<std::string> PaperBroker::placeOrder(const OrderRequest& req) {
    // Get current price
    auto priceIt = prices_.find(req.symbol);
    if (priceIt == prices_.end()) {
        return Result<std::string>::err(Error::validation("No price for symbol: " + req.symbol));
    }

    double price = (req.type == OrderType::Limit) ? req.limitPrice : priceIt->second;

    // Validate order
    if (req.side == OrderSide::Buy) {
        double cost = price * req.qty;
        if (cost > cash_) {
            return Result<std::string>::err(Error::validation("Insufficient funds"));
        }
    } else {  // Sell
        auto posIt = positions_.find(req.symbol);
        if (posIt == positions_.end() || posIt->second < req.qty) {
            return Result<std::string>::err(Error::validation("Insufficient position"));
        }
    }

    // Execute order immediately (paper trading = instant fill)
    std::string orderId = generateOrderId();

    Order order;
    order.orderId = orderId;
    order.clientOrderId = req.clientOrderId;
    order.symbol = req.symbol;
    order.side = req.side;
    order.type = req.type;
    order.status = OrderStatus::Filled;
    order.qty = req.qty;
    order.filledQty = req.qty;
    order.avgFillPrice = price;
    order.limitPrice = req.limitPrice;

    // Update positions and cash
    if (req.side == OrderSide::Buy) {
        cash_ -= price * req.qty;

        auto& posQty = positions_[req.symbol];
        auto& avgPrice = avgPrices_[req.symbol];

        // Update average price
        if (posQty > 0) {
            double totalValue = posQty * avgPrice + req.qty * price;
            posQty += req.qty;
            avgPrice = totalValue / posQty;
        } else {
            posQty = req.qty;
            avgPrice = price;
        }
    } else {  // Sell
        cash_ += price * req.qty;
        positions_[req.symbol] -= req.qty;

        if (positions_[req.symbol] == 0) {
            positions_.erase(req.symbol);
            avgPrices_.erase(req.symbol);
        }
    }

    orders_[orderId] = order;
    return Result<std::string>::ok(orderId);
}

Result<Order> PaperBroker::getOrder(const std::string& orderId) {
    auto it = orders_.find(orderId);
    if (it == orders_.end()) {
        return Result<Order>::err(Error::notFound("Order not found: " + orderId));
    }
    return Result<Order>::ok(it->second);
}

Result<void> PaperBroker::cancelOrder(const std::string& orderId) {
    auto it = orders_.find(orderId);
    if (it == orders_.end()) {
        return Result<void>::err(Error::notFound("Order not found"));
    }

    if (it->second.status == OrderStatus::Filled) {
        return Result<void>::err(Error::validation("Cannot cancel filled order"));
    }

    it->second.status = OrderStatus::Cancelled;
    return Result<void>::ok();
}

Result<std::vector<Order>> PaperBroker::getOpenOrders() {
    std::vector<Order> result;
    for (const auto& [id, order] : orders_) {
        if (order.status == OrderStatus::Pending ||
            order.status == OrderStatus::Submitted ||
            order.status == OrderStatus::PartiallyFilled) {
            result.push_back(order);
        }
    }
    return Result<std::vector<Order>>::ok(result);
}

Result<double> PaperBroker::getCurrentPrice(const std::string& symbol) {
    auto it = prices_.find(symbol);
    if (it == prices_.end()) {
        return Result<double>::err(Error::notFound("No price for symbol"));
    }
    return Result<double>::ok(it->second);
}

void PaperBroker::setPrice(const std::string& symbol, double price) {
    prices_[symbol] = price;
}
