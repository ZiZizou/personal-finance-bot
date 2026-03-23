#pragma once
#include "Result.h"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

// =============================================================================
// DEPRECATED: Broker.h - Portfolio management is now handled by Python API
// =============================================================================
// This header defines the IBroker interface and PaperBroker implementation.
//
// DEPRECATED: Trading-cpp should NOT manage portfolios. All portfolio management
// is handled by Python API via portfolio.json and REST endpoints.
//
// The Python API provides:
//   - GET  /api/portfolio           - Get portfolio
//   - POST /api/portfolio/position  - Add/update position
//   - DELETE /api/portfolio/position/{ticker} - Remove position
//   - POST /api/portfolio/execute/{trade_id}  - Execute accepted trade
//
// This header is kept for backwards compatibility but will be removed.
// =============================================================================

// Order side
enum class OrderSide { Buy, Sell };

// Order type
enum class OrderType { Market, Limit };

// Order status
enum class OrderStatus {
    Pending,
    Submitted,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected,
    Expired
};

// Order request (for placing new orders)
struct OrderRequest {
    std::string symbol;
    OrderSide side;
    OrderType type;
    int64_t qty;
    double limitPrice = 0.0;        // For limit orders
    bool allowExtendedHours = false;
    std::string clientOrderId;      // Idempotency key

    // Optional bracket order fields
    double stopLossPrice = 0.0;
    double takeProfitPrice = 0.0;
};

// Order result
struct Order {
    std::string orderId;
    std::string clientOrderId;
    std::string symbol;
    OrderSide side;
    OrderType type;
    OrderStatus status;
    int64_t qty;
    int64_t filledQty;
    double avgFillPrice;
    double limitPrice;
    std::string createdAt;
    std::string updatedAt;
};

// Account info
struct AccountInfo {
    double cash;
    double portfolioValue;
    double buyingPower;
    double equity;
    bool valid;
};

// Position
struct PositionInfo {
    std::string symbol;
    int64_t qty;
    double avgEntryPrice;
    double currentPrice;
    double marketValue;
    double unrealizedPnl;
    double unrealizedPnlPercent;
};

// Broker interface
class IBroker {
public:
    virtual ~IBroker() = default;

    // Account operations
    virtual Result<AccountInfo> getAccount() = 0;
    virtual Result<std::vector<PositionInfo>> getPositions() = 0;

    // Order operations
    virtual Result<std::string> placeOrder(const OrderRequest& req) = 0;
    virtual Result<Order> getOrder(const std::string& orderId) = 0;
    virtual Result<void> cancelOrder(const std::string& orderId) = 0;
    virtual Result<std::vector<Order>> getOpenOrders() = 0;

    // Get current price (for market orders/validation)
    virtual Result<double> getCurrentPrice(const std::string& symbol) = 0;
};

// DEPRECATED: Paper trading broker - portfolio management is now handled by Python API
class PaperBroker : public IBroker {
public:
    explicit PaperBroker(double initialCash = 100000.0);

    Result<AccountInfo> getAccount() override;
    Result<std::vector<PositionInfo>> getPositions() override;
    Result<std::string> placeOrder(const OrderRequest& req) override;
    Result<Order> getOrder(const std::string& orderId) override;
    Result<void> cancelOrder(const std::string& orderId) override;
    Result<std::vector<Order>> getOpenOrders() override;
    Result<double> getCurrentPrice(const std::string& symbol) override;

    // For testing: set prices
    void setPrice(const std::string& symbol, double price);

private:
    double cash_;
    std::map<std::string, int64_t> positions_;  // symbol -> qty
    std::map<std::string, double> avgPrices_;   // symbol -> avg entry price
    std::map<std::string, Order> orders_;
    std::map<std::string, double> prices_;
    int orderCounter_ = 0;

    std::string generateOrderId();
};
