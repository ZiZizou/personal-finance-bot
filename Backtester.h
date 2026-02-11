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
    double entryPrice = 0.0;        // Entry price (after slippage)
    double exitPrice = 0.0;         // Exit price (after slippage)
    double quantity = 0.0;          // Number of shares/units
    double pnl = 0.0;               // Profit/loss in currency
    double pnlPercent = 0.0;        // Profit/loss as percentage
    double transactionCost = 0.0;   // Total transaction costs
    double slippage = 0.0;          // Total slippage cost
    std::string side;               // "long" or "short"
    std::string exitReason;         // "signal", "stop_loss", "take_profit", "trailing_stop", "end_of_data"
    int holdingPeriod = 0;          // Number of bars held

    bool isWin() const { return pnl > 0; }
    double returnOnInvestment() const {
        double invested = entryPrice * quantity + transactionCost;
        return invested > 0 ? pnl / invested : 0.0;
    }
};

// Enhanced backtest result with comprehensive metrics
struct BacktestResult {
    // === Original fields (maintained for backward compatibility) ===
    double totalReturn = 0.0;
    double sharpeRatio = 0.0;
    double maxDrawdown = 0.0;
    int trades = 0;
    int wins = 0;

    // === NEW: Enhanced Performance Metrics ===
    double annualizedReturn = 0.0;      // CAGR
    double sortinoRatio = 0.0;          // Risk-adjusted return (downside only)
    double calmarRatio = 0.0;           // Return / Max Drawdown
    double winRate = 0.0;               // Percentage of winning trades
    double avgWin = 0.0;                // Average winning trade return
    double avgLoss = 0.0;               // Average losing trade return (positive number)
    double profitFactor = 0.0;          // Gross profit / Gross loss
    double expectancy = 0.0;            // Expected value per trade
    double avgHoldingPeriod = 0.0;      // Average bars per trade

    // === NEW: Cost Analysis ===
    double totalCommissions = 0.0;      // Sum of all commission costs
    double totalSlippage = 0.0;         // Sum of all slippage costs
    double totalCosts = 0.0;            // totalCommissions + totalSlippage
    double costImpact = 0.0;            // How much costs reduced returns

    // === NEW: Risk Metrics ===
    double volatility = 0.0;            // Annualized volatility
    double downsideDeviation = 0.0;     // Downside volatility
    double maxDrawdownDuration = 0.0;   // Longest drawdown period (bars)
    double valueAtRisk95 = 0.0;         // 95% VaR (daily)
    double cvar95 = 0.0;                // Conditional VaR (Expected Shortfall)

    // === NEW: Trade Statistics ===
    int longTrades = 0;
    int shortTrades = 0;
    int winningLongTrades = 0;
    int winningShortTrades = 0;
    double largestWin = 0.0;
    double largestLoss = 0.0;
    int maxConsecutiveWins = 0;
    int maxConsecutiveLosses = 0;

    // === NEW: Detailed Records ===
    std::vector<TradeRecord> tradeLog;
    std::vector<double> equityCurve;     // Equity value at each bar
    std::vector<double> dailyReturns;    // Returns per bar
    std::vector<double> drawdownCurve;   // Drawdown at each bar

    // Helper methods
    double getLossRate() const { return trades > 0 ? (double)(trades - wins) / trades : 0.0; }
    double getAvgReturn() const { return trades > 0 ? totalReturn / trades : 0.0; }
    bool isValid() const { return trades > 0; }
};

// Position state during backtest
struct Position {
    bool isOpen = false;
    bool isLong = true;             // true = long, false = short
    double entryPrice = 0.0;        // Entry price
    double quantity = 0.0;          // Position size
    double stopLossPrice = 0.0;     // Current stop-loss
    double takeProfitPrice = 0.0;   // Current take-profit
    double highWaterMark = 0.0;     // Highest price since entry (for trailing stop)
    double lowWaterMark = 0.0;      // Lowest price since entry (for short trailing stop)
    size_t entryIndex = 0;          // Bar index of entry
    std::string entryDate;          // Date of entry
    double entryCost = 0.0;         // Transaction cost at entry
    double entrySlippage = 0.0;     // Slippage at entry
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
                     double capitalAvailable, const StrategySignal& signal, bool isLong);
    TradeRecord closePosition(Position& pos, const Candle& candle, size_t idx,
                             const std::string& reason);
    bool checkStopLoss(const Position& pos, const Candle& candle, double& exitPrice) const;
    bool checkTakeProfit(const Position& pos, const Candle& candle, double& exitPrice) const;
    void updateTrailingStop(Position& pos, const Candle& candle);
    void calculateMetrics(BacktestResult& result, const std::vector<double>& dailyReturns,
                         int totalBars) const;
    double calculateSharpeRatio(const std::vector<double>& returns) const;
    double calculateSortinoRatio(const std::vector<double>& returns) const;
    double calculateMaxDrawdown(const std::vector<double>& equityCurve,
                              double& maxDuration) const;
    double calculateVaR(std::vector<double> returns, double confidence) const;
    double calculateCVaR(std::vector<double> returns, double confidence) const;
};
