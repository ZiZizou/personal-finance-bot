#pragma once
#include <string>

// Transaction cost modeling for realistic backtesting
struct TransactionCostModel {
    double commissionPercent = 0.001;  // 10 bps (0.1%)
    double slippagePercent = 0.0005;   // 5 bps (0.05%)
    double minCommission = 1.0;        // Minimum commission per trade

    // Calculate total transaction cost for a trade
    double calculateCost(double tradeValue) const {
        double commission = tradeValue * commissionPercent;
        return std::max(minCommission, commission);
    }

    // Calculate execution price after slippage
    double applySlippage(double price, bool isBuy) const {
        if (isBuy) {
            return price * (1.0 + slippagePercent);  // Pay more when buying
        } else {
            return price * (1.0 - slippagePercent);  // Receive less when selling
        }
    }
};

// Risk management settings
struct RiskManagement {
    // Stop-loss settings
    bool enableStopLoss = false;
    double stopLossPercent = 0.02;     // 2% stop loss
    double stopLossATRMultiple = 2.0;  // Or use ATR-based stops
    bool useATRStopLoss = false;       // If true, use ATR multiple instead of percent

    // Take-profit settings
    bool enableTakeProfit = false;
    double takeProfitPercent = 0.04;   // 4% take profit
    double takeProfitATRMultiple = 3.0;
    bool useATRTakeProfit = false;

    // Trailing stop settings
    bool enableTrailingStop = false;
    double trailingStopPercent = 0.015; // 1.5% trailing stop
    double trailingStopATRMultiple = 1.5;
    bool useATRTrailingStop = false;

    // Maximum drawdown circuit breaker
    bool enableMaxDrawdownStop = false;
    double maxDrawdownPercent = 0.20;  // Stop trading if 20% drawdown reached
};

// Position sizing methods
struct PositionSizing {
    enum class Method {
        FixedFraction,  // Fixed percentage of capital per trade
        Kelly,          // Kelly criterion based sizing
        ATRBased,       // Position size based on ATR/volatility
        EqualWeight     // Equal weight across all positions
    };

    Method method = Method::FixedFraction;
    double fixedFraction = 0.1;        // 10% of capital per trade
    double maxPositionSize = 0.25;     // Maximum 25% in any single position
    double kellyFraction = 0.5;        // Half-Kelly for reduced risk
    double atrRiskPerTrade = 0.01;     // Risk 1% of capital per ATR

    // Calculate position size as fraction of capital
    double calculateFraction(double winRate = 0.5, double avgWinLossRatio = 1.0,
                           double atr = 0.0, double price = 0.0) const {
        double fraction = fixedFraction;

        switch (method) {
            case Method::FixedFraction:
                fraction = fixedFraction;
                break;

            case Method::Kelly: {
                // Kelly formula: f* = (bp - q) / b
                // where b = win/loss ratio, p = win prob, q = loss prob
                double b = avgWinLossRatio;
                double p = winRate;
                double q = 1.0 - p;
                double kelly = (b * p - q) / b;
                fraction = std::max(0.0, kelly * kellyFraction);  // Apply Kelly fraction
                break;
            }

            case Method::ATRBased: {
                // Size position so 1 ATR move = atrRiskPerTrade of capital
                if (atr > 0 && price > 0) {
                    fraction = (atrRiskPerTrade * price) / atr;
                }
                break;
            }

            case Method::EqualWeight:
                fraction = fixedFraction;
                break;
        }

        // Apply maximum position size constraint
        return std::min(fraction, maxPositionSize);
    }
};

// Main backtest configuration
struct BacktestConfig {
    // Capital
    double initialCapital = 10000.0;

    // Cost model
    TransactionCostModel costs;

    // Risk management
    RiskManagement risk;

    // Position sizing
    PositionSizing sizing;

    // Warmup period (bars to skip before trading)
    int warmupPeriod = 60;

    // Risk-free rate for Sharpe/Sortino calculation (annualized)
    double riskFreeRate = 0.04;  // 4% annual

    // Trading frequency assumption (for annualization)
    int tradingDaysPerYear = 252;

    // Whether to allow shorting
    bool allowShort = false;

    // Reinvest profits or use fixed capital
    bool reinvestProfits = true;

    // Minimum bars between trades (to avoid overtrading)
    int minBarsBetweenTrades = 0;

    // Static factory methods for common configurations
    static BacktestConfig defaultConfig() {
        return BacktestConfig();
    }

    static BacktestConfig realisticConfig() {
        BacktestConfig cfg;
        cfg.costs.commissionPercent = 0.001;
        cfg.costs.slippagePercent = 0.0005;
        cfg.risk.enableStopLoss = true;
        cfg.risk.stopLossPercent = 0.02;
        cfg.risk.enableTakeProfit = true;
        cfg.risk.takeProfitPercent = 0.04;
        cfg.sizing.method = PositionSizing::Method::FixedFraction;
        cfg.sizing.fixedFraction = 0.1;
        return cfg;
    }

    static BacktestConfig zeroCostConfig() {
        BacktestConfig cfg;
        cfg.costs.commissionPercent = 0.0;
        cfg.costs.slippagePercent = 0.0;
        cfg.costs.minCommission = 0.0;
        return cfg;
    }

    static BacktestConfig aggressiveConfig() {
        BacktestConfig cfg;
        cfg.sizing.method = PositionSizing::Method::Kelly;
        cfg.sizing.kellyFraction = 0.25;  // Quarter-Kelly
        cfg.risk.enableStopLoss = true;
        cfg.risk.stopLossPercent = 0.05;
        cfg.sizing.maxPositionSize = 0.5;
        return cfg;
    }
};
