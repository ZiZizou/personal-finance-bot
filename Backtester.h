#pragma once
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include "MarketData.h"
#include "BacktestConfig.h"
#include "IStrategy.h"

// Record of a single trade for detailed analysis
struct TradeRecord {
    size_t entryIndex = 0;          // Bar index of entry
    size_t exitIndex = 0;           // Bar index of exit
    std::string entryDate;          // Date of entry
    std::string exitDate;           // Date of exit
    float entryPrice = 0.0f;        // Entry price (after slippage)
    float exitPrice = 0.0f;         // Exit price (after slippage)
    float quantity = 0.0f;          // Number of shares/units
    float pnl = 0.0f;               // Profit/loss in currency
    float pnlPercent = 0.0f;        // Profit/loss as percentage
    float transactionCost = 0.0f;   // Total transaction costs
    float slippage = 0.0f;          // Total slippage cost
    std::string side;               // "long" or "short"
    std::string exitReason;         // "signal", "stop_loss", "take_profit", "trailing_stop", "end_of_data"
    int holdingPeriod = 0;          // Number of bars held

    bool isWin() const { return pnl > 0; }
    float returnOnInvestment() const {
        float invested = entryPrice * quantity + transactionCost;
        return invested > 0 ? pnl / invested : 0.0f;
    }
};

// Enhanced backtest result with comprehensive metrics
struct BacktestResult {
    // === Original fields (maintained for backward compatibility) ===
    float totalReturn = 0.0f;
    float sharpeRatio = 0.0f;
    float maxDrawdown = 0.0f;
    int trades = 0;
    int wins = 0;

    // === NEW: Enhanced Performance Metrics ===
    float annualizedReturn = 0.0f;      // CAGR
    float sortinoRatio = 0.0f;          // Risk-adjusted return (downside only)
    float calmarRatio = 0.0f;           // Return / Max Drawdown
    float winRate = 0.0f;               // Percentage of winning trades
    float avgWin = 0.0f;                // Average winning trade return
    float avgLoss = 0.0f;               // Average losing trade return (positive number)
    float profitFactor = 0.0f;          // Gross profit / Gross loss
    float expectancy = 0.0f;            // Expected value per trade
    float avgHoldingPeriod = 0.0f;      // Average bars per trade

    // === NEW: Cost Analysis ===
    float totalCommissions = 0.0f;      // Sum of all commission costs
    float totalSlippage = 0.0f;         // Sum of all slippage costs
    float totalCosts = 0.0f;            // totalCommissions + totalSlippage
    float costImpact = 0.0f;            // How much costs reduced returns

    // === NEW: Risk Metrics ===
    float volatility = 0.0f;            // Annualized volatility
    float downsideDeviation = 0.0f;     // Downside volatility
    float maxDrawdownDuration = 0.0f;   // Longest drawdown period (bars)
    float valueAtRisk95 = 0.0f;         // 95% VaR (daily)
    float cvar95 = 0.0f;                // Conditional VaR (Expected Shortfall)

    // === NEW: Trade Statistics ===
    int longTrades = 0;
    int shortTrades = 0;
    int winningLongTrades = 0;
    int winningShortTrades = 0;
    float largestWin = 0.0f;
    float largestLoss = 0.0f;
    int maxConsecutiveWins = 0;
    int maxConsecutiveLosses = 0;

    // === NEW: Detailed Records ===
    std::vector<TradeRecord> tradeLog;
    std::vector<float> equityCurve;     // Equity value at each bar
    std::vector<float> dailyReturns;    // Returns per bar
    std::vector<float> drawdownCurve;   // Drawdown at each bar

    // Helper methods
    float getLossRate() const { return trades > 0 ? (float)(trades - wins) / trades : 0.0f; }
    float getAvgReturn() const { return trades > 0 ? totalReturn / trades : 0.0f; }
    bool isValid() const { return trades > 0; }
};

// Position state during backtest
struct Position {
    bool isOpen = false;
    bool isLong = true;             // true = long, false = short
    float entryPrice = 0.0f;        // Entry price
    float quantity = 0.0f;          // Position size
    float stopLossPrice = 0.0f;     // Current stop-loss
    float takeProfitPrice = 0.0f;   // Current take-profit
    float highWaterMark = 0.0f;     // Highest price since entry (for trailing stop)
    float lowWaterMark = 0.0f;      // Lowest price since entry (for short trailing stop)
    size_t entryIndex = 0;          // Bar index of entry
    std::string entryDate;          // Date of entry
    float entryCost = 0.0f;         // Transaction cost at entry
    float entrySlippage = 0.0f;     // Slippage at entry
};

// Enhanced Backtester class (instance-based with config injection)
class Backtester {
public:
    // Constructor with configuration
    explicit Backtester(const BacktestConfig& config = BacktestConfig::defaultConfig());

    // Run backtest with a strategy
    BacktestResult run(const std::vector<Candle>& candles, IStrategy& strategy);

    // Run backtest with signal function (for compatibility/flexibility)
    using SignalFunction = std::function<StrategySignal(const std::vector<Candle>&, size_t)>;
    BacktestResult run(const std::vector<Candle>& candles, SignalFunction signalFunc, int warmupPeriod = 60);

    // Get/set configuration
    const BacktestConfig& getConfig() const { return config_; }
    void setConfig(const BacktestConfig& config) { config_ = config; }

    // === STATIC METHODS FOR BACKWARD COMPATIBILITY ===
    // Original static interface (uses default configuration)
    static BacktestResult run(const std::vector<Candle>& candles);

private:
    BacktestConfig config_;

    // Internal helpers
    void openPosition(Position& pos, const Candle& candle, size_t idx,
                     float capitalAvailable, const StrategySignal& signal, bool isLong);
    TradeRecord closePosition(Position& pos, const Candle& candle, size_t idx,
                             const std::string& reason);
    bool checkStopLoss(const Position& pos, const Candle& candle, float& exitPrice) const;
    bool checkTakeProfit(const Position& pos, const Candle& candle, float& exitPrice) const;
    void updateTrailingStop(Position& pos, const Candle& candle);
    void calculateMetrics(BacktestResult& result, const std::vector<float>& dailyReturns,
                         int totalBars) const;
    float calculateSharpeRatio(const std::vector<float>& returns) const;
    float calculateSortinoRatio(const std::vector<float>& returns) const;
    float calculateMaxDrawdown(const std::vector<float>& equityCurve,
                              float& maxDuration) const;
    float calculateVaR(std::vector<float> returns, float confidence) const;
    float calculateCVaR(std::vector<float> returns, float confidence) const;
};
