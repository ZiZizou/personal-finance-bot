#pragma once
#include <vector>
#include <map>
#include <string>
#include "MarketData.h"
#include "Backtester.h"

// Portfolio configuration for multi-asset backtesting
struct PortfolioConfig {
    // Base backtest configuration
    BacktestConfig baseConfig;

    // Rebalancing settings
    enum class RebalanceFreq {
        Daily,
        Weekly,
        Monthly,
        Quarterly,
        Threshold,  // Rebalance when weights drift by threshold
        Never
    };
    RebalanceFreq rebalanceFreq = RebalanceFreq::Monthly;
    float rebalanceThreshold = 0.05f;  // 5% drift triggers rebalance

    // Position limits
    float maxSingleAssetWeight = 0.25f;  // Max 25% in any single asset
    float minSingleAssetWeight = 0.0f;   // Min 0% (can be fully out)
    int maxPositions = 10;                // Maximum number of positions

    // Cash management
    float minCashReserve = 0.05f;        // Keep 5% in cash minimum
    float targetCashReserve = 0.10f;     // Target 10% cash

    // Correlation-based constraints
    bool useCorrelationFilter = false;
    float maxCorrelation = 0.7f;         // Don't hold assets with correlation > 0.7

    // Static factory methods
    static PortfolioConfig defaultConfig() {
        return PortfolioConfig();
    }

    static PortfolioConfig conservativeConfig() {
        PortfolioConfig cfg;
        cfg.maxSingleAssetWeight = 0.15f;
        cfg.minCashReserve = 0.10f;
        cfg.rebalanceFreq = RebalanceFreq::Quarterly;
        return cfg;
    }

    static PortfolioConfig aggressiveConfig() {
        PortfolioConfig cfg;
        cfg.maxSingleAssetWeight = 0.40f;
        cfg.minCashReserve = 0.02f;
        cfg.rebalanceFreq = RebalanceFreq::Weekly;
        return cfg;
    }
};

// Holding information for a single asset
struct AssetHolding {
    std::string symbol;
    float quantity = 0.0f;
    float avgCostBasis = 0.0f;
    float currentPrice = 0.0f;
    float targetWeight = 0.0f;
    float currentWeight = 0.0f;
    float unrealizedPnL = 0.0f;
    float realizedPnL = 0.0f;
};

// Portfolio state at a point in time
struct PortfolioSnapshot {
    std::string date;
    float totalValue = 0.0f;
    float cashBalance = 0.0f;
    float investedValue = 0.0f;
    std::map<std::string, AssetHolding> holdings;
    std::map<std::string, float> weights;

    float getCashWeight() const {
        return totalValue > 0 ? cashBalance / totalValue : 1.0f;
    }

    float getInvestedWeight() const {
        return totalValue > 0 ? investedValue / totalValue : 0.0f;
    }
};

// Rebalance transaction record
struct RebalanceRecord {
    size_t barIndex;
    std::string date;
    std::map<std::string, float> beforeWeights;
    std::map<std::string, float> afterWeights;
    std::map<std::string, float> trades;  // Positive = buy, negative = sell
    float totalTurnover = 0.0f;
    float transactionCost = 0.0f;
};

// Result for portfolio backtest
struct PortfolioBacktestResult {
    // Aggregate metrics (same as single-asset)
    BacktestResult aggregateMetrics;

    // Per-asset results
    std::map<std::string, BacktestResult> perAssetResults;

    // Portfolio-specific metrics
    float diversificationRatio = 0.0f;   // Portfolio vol / weighted avg asset vol
    float concentrationIndex = 0.0f;     // Herfindahl index of weights
    int rebalanceCount = 0;
    float totalRebalanceCost = 0.0f;
    float avgTurnover = 0.0f;            // Average turnover per rebalance

    // Time series
    std::vector<PortfolioSnapshot> snapshots;
    std::vector<RebalanceRecord> rebalanceLog;

    // Correlation analysis
    std::map<std::pair<std::string, std::string>, float> correlationMatrix;

    // Attribution analysis
    std::map<std::string, float> returnContribution;  // Each asset's contribution to total return
    std::map<std::string, float> riskContribution;    // Each asset's contribution to portfolio risk

    // Helper methods
    float getEffectivePositions() const {
        // Effective number of positions (inverse of concentration)
        return concentrationIndex > 0 ? 1.0f / concentrationIndex : 0.0f;
    }

    bool isWellDiversified() const {
        return diversificationRatio > 1.1f && concentrationIndex < 0.25f;
    }
};

// Target allocation for portfolio construction
struct TargetAllocation {
    std::map<std::string, float> weights;  // Symbol -> target weight

    float totalWeight() const {
        float sum = 0.0f;
        for (const auto& [symbol, weight] : weights) {
            sum += weight;
        }
        return sum;
    }

    void normalize() {
        float total = totalWeight();
        if (total > 0) {
            for (auto& [symbol, weight] : weights) {
                weight /= total;
            }
        }
    }

    void setEqualWeight(const std::vector<std::string>& symbols) {
        weights.clear();
        float w = 1.0f / symbols.size();
        for (const auto& s : symbols) {
            weights[s] = w;
        }
    }

    void setWeight(const std::string& symbol, float weight) {
        weights[symbol] = weight;
    }

    float getWeight(const std::string& symbol) const {
        auto it = weights.find(symbol);
        return it != weights.end() ? it->second : 0.0f;
    }
};

// Portfolio position manager
class PortfolioManager {
private:
    PortfolioConfig config_;
    PortfolioSnapshot currentState_;
    std::vector<RebalanceRecord> rebalanceHistory_;

public:
    PortfolioManager(const PortfolioConfig& config = PortfolioConfig::defaultConfig())
        : config_(config) {
        currentState_.cashBalance = config_.baseConfig.initialCapital;
        currentState_.totalValue = config_.baseConfig.initialCapital;
    }

    // Update prices and calculate current weights
    void updatePrices(const std::map<std::string, float>& prices, const std::string& date) {
        currentState_.date = date;
        currentState_.investedValue = 0.0f;

        for (auto& [symbol, holding] : currentState_.holdings) {
            if (prices.count(symbol)) {
                holding.currentPrice = prices.at(symbol);
                float value = holding.quantity * holding.currentPrice;
                currentState_.investedValue += value;
                holding.unrealizedPnL = value - (holding.quantity * holding.avgCostBasis);
            }
        }

        currentState_.totalValue = currentState_.cashBalance + currentState_.investedValue;

        // Update weights
        for (auto& [symbol, holding] : currentState_.holdings) {
            float value = holding.quantity * holding.currentPrice;
            holding.currentWeight = currentState_.totalValue > 0 ?
                value / currentState_.totalValue : 0.0f;
            currentState_.weights[symbol] = holding.currentWeight;
        }
    }

    // Check if rebalancing is needed
    bool needsRebalance(const TargetAllocation& target, size_t barIndex) const {
        switch (config_.rebalanceFreq) {
            case PortfolioConfig::RebalanceFreq::Never:
                return false;

            case PortfolioConfig::RebalanceFreq::Daily:
                return true;

            case PortfolioConfig::RebalanceFreq::Weekly:
                return barIndex % 5 == 0;

            case PortfolioConfig::RebalanceFreq::Monthly:
                return barIndex % 21 == 0;

            case PortfolioConfig::RebalanceFreq::Quarterly:
                return barIndex % 63 == 0;

            case PortfolioConfig::RebalanceFreq::Threshold: {
                // Check if any position has drifted beyond threshold
                for (const auto& [symbol, weight] : target.weights) {
                    float currentWeight = currentState_.weights.count(symbol) ?
                        currentState_.weights.at(symbol) : 0.0f;
                    if (std::abs(currentWeight - weight) > config_.rebalanceThreshold) {
                        return true;
                    }
                }
                return false;
            }
        }
        return false;
    }

    // Execute rebalance to target allocation
    RebalanceRecord rebalance(const TargetAllocation& target,
                              const std::map<std::string, float>& prices,
                              size_t barIndex) {
        RebalanceRecord record;
        record.barIndex = barIndex;
        record.date = currentState_.date;
        record.beforeWeights = currentState_.weights;

        // Calculate required trades
        std::map<std::string, float> targetValues;
        float availableCash = currentState_.cashBalance;

        // First, calculate how much to sell
        for (const auto& [symbol, holding] : currentState_.holdings) {
            float targetWeight = target.getWeight(symbol);
            float targetValue = currentState_.totalValue * targetWeight;
            float currentValue = holding.quantity * holding.currentPrice;

            if (targetValue < currentValue) {
                // Need to sell
                float sellValue = currentValue - targetValue;
                float sellQty = sellValue / holding.currentPrice;

                // Apply transaction costs
                float cost = config_.baseConfig.costs.calculateCost(sellValue);
                float proceeds = sellValue - cost;

                record.trades[symbol] = -sellQty;
                record.transactionCost += cost;
                availableCash += proceeds;
            }

            targetValues[symbol] = targetValue;
        }

        // Then calculate buys
        for (const auto& [symbol, targetWeight] : target.weights) {
            float targetValue = currentState_.totalValue * targetWeight;
            float currentValue = 0.0f;

            if (currentState_.holdings.count(symbol)) {
                currentValue = currentState_.holdings[symbol].quantity *
                               currentState_.holdings[symbol].currentPrice;
            }

            if (targetValue > currentValue && prices.count(symbol)) {
                float buyValue = std::min(targetValue - currentValue, availableCash);
                float cost = config_.baseConfig.costs.calculateCost(buyValue);
                float investValue = buyValue - cost;
                float buyQty = investValue / prices.at(symbol);

                record.trades[symbol] = record.trades.count(symbol) ?
                    record.trades[symbol] + buyQty : buyQty;
                record.transactionCost += cost;
                availableCash -= buyValue;
            }
        }

        // Apply trades
        for (const auto& [symbol, qty] : record.trades) {
            if (!prices.count(symbol)) continue;

            if (qty > 0) {
                // Buy
                addPosition(symbol, qty, prices.at(symbol));
                currentState_.cashBalance -= qty * prices.at(symbol);
            } else {
                // Sell
                reducePosition(symbol, -qty, prices.at(symbol));
                currentState_.cashBalance += -qty * prices.at(symbol);
            }

            record.totalTurnover += std::abs(qty * prices.at(symbol));
        }

        // Update weights
        updatePrices(prices, currentState_.date);
        record.afterWeights = currentState_.weights;

        rebalanceHistory_.push_back(record);
        return record;
    }

    // Get current portfolio state
    const PortfolioSnapshot& getState() const { return currentState_; }

    // Get rebalance history
    const std::vector<RebalanceRecord>& getRebalanceHistory() const {
        return rebalanceHistory_;
    }

private:
    void addPosition(const std::string& symbol, float qty, float price) {
        if (!currentState_.holdings.count(symbol)) {
            currentState_.holdings[symbol] = AssetHolding();
            currentState_.holdings[symbol].symbol = symbol;
        }

        auto& holding = currentState_.holdings[symbol];
        float totalCost = holding.avgCostBasis * holding.quantity + price * qty;
        holding.quantity += qty;
        holding.avgCostBasis = holding.quantity > 0 ? totalCost / holding.quantity : 0.0f;
        holding.currentPrice = price;
    }

    void reducePosition(const std::string& symbol, float qty, float price) {
        if (!currentState_.holdings.count(symbol)) return;

        auto& holding = currentState_.holdings[symbol];
        float sellQty = std::min(qty, holding.quantity);
        float pnl = (price - holding.avgCostBasis) * sellQty;
        holding.realizedPnL += pnl;
        holding.quantity -= sellQty;

        if (holding.quantity <= 0) {
            currentState_.holdings.erase(symbol);
        }
    }
};
