#pragma once
#include <string>

// Transaction cost modeling for realistic backtesting
struct TransactionCostModel {
    float commissionPercent = 0.001f;  // 10 bps (0.1%)
    float slippagePercent = 0.0005f;   // 5 bps (0.05%)
    float minCommission = 1.0f;        // Minimum commission per trade

    // Calculate total transaction cost for a trade
    float calculateCost(float tradeValue) const {
        float commission = tradeValue * commissionPercent;
        return std::max(minCommission, commission);
    }

    // Calculate execution price after slippage
    float applySlippage(float price, bool isBuy) const {
        if (isBuy) {
            return price * (1.0f + slippagePercent);  // Pay more when buying
        } else {
            return price * (1.0f - slippagePercent);  // Receive less when selling
        }
    }
};

// Risk management settings
struct RiskManagement {
    // Stop-loss settings
    bool enableStopLoss = false;
    float stopLossPercent = 0.02f;     // 2% stop loss
    float stopLossATRMultiple = 2.0f;  // Or use ATR-based stops
    bool useATRStopLoss = false;       // If true, use ATR multiple instead of percent

    // Take-profit settings
    bool enableTakeProfit = false;
    float takeProfitPercent = 0.04f;   // 4% take profit
    float takeProfitATRMultiple = 3.0f;
    bool useATRTakeProfit = false;

    // Trailing stop settings
    bool enableTrailingStop = false;
    float trailingStopPercent = 0.015f; // 1.5% trailing stop
    float trailingStopATRMultiple = 1.5f;
    bool useATRTrailingStop = false;

    // Maximum drawdown circuit breaker
    bool enableMaxDrawdownStop = false;
    float maxDrawdownPercent = 0.20f;  // Stop trading if 20% drawdown reached
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
    float fixedFraction = 0.1f;        // 10% of capital per trade
    float maxPositionSize = 0.25f;     // Maximum 25% in any single position
    float kellyFraction = 0.5f;        // Half-Kelly for reduced risk
    float atrRiskPerTrade = 0.01f;     // Risk 1% of capital per ATR

    // Calculate position size as fraction of capital
    float calculateFraction(float winRate = 0.5f, float avgWinLossRatio = 1.0f,
                           float atr = 0.0f, float price = 0.0f) const {
        float fraction = fixedFraction;

        switch (method) {
            case Method::FixedFraction:
                fraction = fixedFraction;
                break;

            case Method::Kelly: {
                // Kelly formula: f* = (bp - q) / b
                // where b = win/loss ratio, p = win prob, q = loss prob
                float b = avgWinLossRatio;
                float p = winRate;
                float q = 1.0f - p;
                float kelly = (b * p - q) / b;
                fraction = std::max(0.0f, kelly * kellyFraction);  // Apply Kelly fraction
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
    float initialCapital = 10000.0f;

    // Cost model
    TransactionCostModel costs;

    // Risk management
    RiskManagement risk;

    // Position sizing
    PositionSizing sizing;

    // Warmup period (bars to skip before trading)
    int warmupPeriod = 60;

    // Risk-free rate for Sharpe/Sortino calculation (annualized)
    float riskFreeRate = 0.04f;  // 4% annual

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
        cfg.costs.commissionPercent = 0.001f;
        cfg.costs.slippagePercent = 0.0005f;
        cfg.risk.enableStopLoss = true;
        cfg.risk.stopLossPercent = 0.02f;
        cfg.risk.enableTakeProfit = true;
        cfg.risk.takeProfitPercent = 0.04f;
        cfg.sizing.method = PositionSizing::Method::FixedFraction;
        cfg.sizing.fixedFraction = 0.1f;
        return cfg;
    }

    static BacktestConfig zeroCostConfig() {
        BacktestConfig cfg;
        cfg.costs.commissionPercent = 0.0f;
        cfg.costs.slippagePercent = 0.0f;
        cfg.costs.minCommission = 0.0f;
        return cfg;
    }

    static BacktestConfig aggressiveConfig() {
        BacktestConfig cfg;
        cfg.sizing.method = PositionSizing::Method::Kelly;
        cfg.sizing.kellyFraction = 0.25f;  // Quarter-Kelly
        cfg.risk.enableStopLoss = true;
        cfg.risk.stopLossPercent = 0.05f;
        cfg.sizing.maxPositionSize = 0.5f;
        return cfg;
    }
};
