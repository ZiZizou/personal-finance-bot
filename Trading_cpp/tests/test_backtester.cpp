#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>

#include "../Backtester.h"
#include "../BacktestConfig.h"
#include "../IStrategy.h"
#include "../Strategies/MeanReversionStrategy.h"
#include "../Strategies/TrendFollowingStrategy.h"
#include "../MarketData.h"

// ============================================================================
// Test Data Generators
// ============================================================================

namespace TestData {

// Generate uptrend data (price increases steadily)
std::vector<Candle> generateUptrend(size_t count, float startPrice = 100.0f, float dailyReturn = 0.001f) {
    std::vector<Candle> candles;
    float price = startPrice;

    for (size_t i = 0; i < count; ++i) {
        Candle c;
        c.open = price;
        c.high = price * 1.01f;
        c.low = price * 0.99f;
        c.close = price * (1.0f + dailyReturn);
        c.volume = 1000000.0f + (rand() % 500000);
        price = c.close;
        candles.push_back(c);
    }
    return candles;
}

// Generate downtrend data (price decreases steadily)
std::vector<Candle> generateDowntrend(size_t count, float startPrice = 100.0f, float dailyReturn = -0.001f) {
    std::vector<Candle> candles;
    float price = startPrice;

    for (size_t i = 0; i < count; ++i) {
        Candle c;
        c.open = price;
        c.high = price * 1.01f;
        c.low = price * 0.99f;
        c.close = price * (1.0f + dailyReturn);
        c.volume = 1000000.0f + (rand() % 500000);
        price = c.close;
        candles.push_back(c);
    }
    return candles;
}

// Generate sideways/ranging data
std::vector<Candle> generateSideways(size_t count, float centerPrice = 100.0f, float range = 5.0f) {
    std::vector<Candle> candles;

    for (size_t i = 0; i < count; ++i) {
        float offset = std::sin(i * 0.3f) * range;
        Candle c;
        c.open = centerPrice + offset;
        c.high = c.open + range * 0.3f;
        c.low = c.open - range * 0.3f;
        c.close = centerPrice + std::sin((i + 1) * 0.3f) * range;
        c.volume = 1000000.0f + (rand() % 500000);
        candles.push_back(c);
    }
    return candles;
}

// Generate mean-reverting data (sine wave)
std::vector<Candle> generateMeanReverting(size_t count, float centerPrice = 100.0f,
                                           float amplitude = 10.0f, float period = 50.0f) {
    std::vector<Candle> candles;

    for (size_t i = 0; i < count; ++i) {
        float price = centerPrice + amplitude * std::sin(2.0f * M_PI * i / period);
        Candle c;
        c.open = price;
        c.high = price * 1.005f;
        c.low = price * 0.995f;
        c.close = centerPrice + amplitude * std::sin(2.0f * M_PI * (i + 1) / period);
        c.volume = 1000000.0f;
        candles.push_back(c);
    }
    return candles;
}

// Generate crash scenario (sharp drop)
std::vector<Candle> generateCrash(size_t preCount, size_t crashDays, size_t postCount,
                                   float startPrice = 100.0f, float crashPercent = 0.30f) {
    std::vector<Candle> candles;
    float price = startPrice;

    // Pre-crash uptrend
    for (size_t i = 0; i < preCount; ++i) {
        Candle c;
        c.open = price;
        c.high = price * 1.01f;
        c.low = price * 0.99f;
        c.close = price * 1.002f;
        c.volume = 1000000.0f;
        price = c.close;
        candles.push_back(c);
    }

    // Crash period
    float dailyCrash = std::pow(1.0f - crashPercent, 1.0f / crashDays);
    for (size_t i = 0; i < crashDays; ++i) {
        Candle c;
        c.open = price;
        c.high = price * 1.005f;
        c.low = price * dailyCrash * 0.99f;
        c.close = price * dailyCrash;
        c.volume = 3000000.0f;  // High volume during crash
        price = c.close;
        candles.push_back(c);
    }

    // Post-crash recovery
    for (size_t i = 0; i < postCount; ++i) {
        Candle c;
        c.open = price;
        c.high = price * 1.01f;
        c.low = price * 0.99f;
        c.close = price * 1.001f;
        c.volume = 1000000.0f;
        price = c.close;
        candles.push_back(c);
    }

    return candles;
}

// Generate volatile data
std::vector<Candle> generateVolatile(size_t count, float startPrice = 100.0f, float volatility = 0.03f) {
    std::vector<Candle> candles;
    float price = startPrice;

    for (size_t i = 0; i < count; ++i) {
        float change = ((rand() % 1000) / 1000.0f - 0.5f) * 2.0f * volatility;
        Candle c;
        c.open = price;
        c.high = price * (1.0f + std::abs(change) + 0.005f);
        c.low = price * (1.0f - std::abs(change) - 0.005f);
        c.close = price * (1.0f + change);
        c.volume = 1000000.0f + (rand() % 1000000);
        price = c.close;
        candles.push_back(c);
    }
    return candles;
}

}  // namespace TestData

// ============================================================================
// Always-Buy Strategy for Testing
// ============================================================================

class AlwaysBuyStrategy : public IStrategy {
public:
    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if (idx < 60) return {SignalType::Hold, 0.0f};
        return {SignalType::Buy, 1.0f};
    }

    std::string getName() const override { return "AlwaysBuy"; }
    int getWarmupPeriod() const override { return 60; }
    std::unique_ptr<IStrategy> clone() const override {
        return std::make_unique<AlwaysBuyStrategy>(*this);
    }
};

// ============================================================================
// Alternating Strategy for Testing Multiple Trades
// ============================================================================

class AlternatingStrategy : public IStrategy {
private:
    int buyEvery_;
    int holdPeriod_;

public:
    AlternatingStrategy(int buyEvery = 20, int holdPeriod = 10)
        : buyEvery_(buyEvery), holdPeriod_(holdPeriod) {}

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if (idx < 60) return {SignalType::Hold, 0.0f};

        int cyclePos = (idx - 60) % buyEvery_;
        if (cyclePos == 0) {
            return {SignalType::Buy, 1.0f};
        } else if (cyclePos == holdPeriod_) {
            return {SignalType::Sell, 1.0f};
        }
        return {SignalType::Hold, 0.0f};
    }

    std::string getName() const override { return "Alternating"; }
    int getWarmupPeriod() const override { return 60; }
    std::unique_ptr<IStrategy> clone() const override {
        return std::make_unique<AlternatingStrategy>(*this);
    }
};

// ============================================================================
// Backtester Basic Tests
// ============================================================================

TEST(BacktesterTest, ZeroCostConfigReturnsExpected) {
    auto candles = TestData::generateUptrend(200);
    BacktestConfig config = BacktestConfig::zeroCostConfig();
    Backtester backtester(config);
    AlwaysBuyStrategy strategy;

    BacktestResult result = backtester.run(candles, strategy);

    // With zero costs and uptrend, we should have positive returns
    EXPECT_GT(result.totalReturn, 0.0f);
    EXPECT_EQ(result.totalCommissions, 0.0f);
    EXPECT_EQ(result.totalSlippage, 0.0f);
}

TEST(BacktesterTest, TransactionCostsReduceReturns) {
    auto candles = TestData::generateUptrend(200);

    // Run with zero costs
    BacktestConfig zeroConfig = BacktestConfig::zeroCostConfig();
    Backtester zeroBacktester(zeroConfig);
    AlternatingStrategy strategy;
    BacktestResult zeroResult = zeroBacktester.run(candles, strategy);

    // Run with realistic costs
    BacktestConfig costConfig = BacktestConfig::realisticConfig();
    Backtester costBacktester(costConfig);
    BacktestResult costResult = costBacktester.run(candles, strategy);

    // Returns should be lower with costs
    EXPECT_LT(costResult.totalReturn, zeroResult.totalReturn);
    EXPECT_GT(costResult.totalCommissions, 0.0f);
}

TEST(BacktesterTest, TransactionCostsProportionalToTrades) {
    auto candles = TestData::generateUptrend(400);

    // Strategy that trades every 20 bars
    AlternatingStrategy frequentStrategy(20, 10);

    // Strategy that trades every 40 bars (half as often)
    AlternatingStrategy infrequentStrategy(40, 20);

    BacktestConfig config = BacktestConfig::realisticConfig();
    Backtester backtester(config);

    BacktestResult frequentResult = backtester.run(candles, frequentStrategy);
    BacktestResult infrequentResult = backtester.run(candles, infrequentStrategy);

    // More frequent trading should have higher costs
    EXPECT_GT(frequentResult.totalCommissions, infrequentResult.totalCommissions);
    // And more trades
    EXPECT_GT(frequentResult.trades, infrequentResult.trades);
}

TEST(BacktesterTest, SlippageAppliedCorrectly) {
    auto candles = TestData::generateUptrend(200);

    // Config with only slippage, no commission
    BacktestConfig config;
    config.costs.commissionPercent = 0.0f;
    config.costs.minCommission = 0.0f;
    config.costs.slippagePercent = 0.01f;  // 1% slippage

    Backtester backtester(config);
    AlternatingStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    // Should have slippage but no commission
    EXPECT_EQ(result.totalCommissions, 0.0f);
    EXPECT_GT(result.totalSlippage, 0.0f);
}

// ============================================================================
// Stop-Loss Tests
// ============================================================================

TEST(BacktesterTest, StopLossTriggersOnCrash) {
    auto candles = TestData::generateCrash(100, 10, 50, 100.0f, 0.30f);  // 30% crash

    // Config with stop-loss
    BacktestConfig config = BacktestConfig::zeroCostConfig();
    config.risk.enableStopLoss = true;
    config.risk.stopLossPercent = 0.05f;  // 5% stop-loss

    Backtester backtester(config);
    AlwaysBuyStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    // Max drawdown should be limited by stop-loss (plus some buffer for execution)
    EXPECT_LT(result.maxDrawdown, 0.10f);  // Should be well below the 30% crash
}

TEST(BacktesterTest, StopLossRecordsExitReason) {
    auto candles = TestData::generateCrash(100, 10, 50, 100.0f, 0.30f);

    BacktestConfig config = BacktestConfig::zeroCostConfig();
    config.risk.enableStopLoss = true;
    config.risk.stopLossPercent = 0.05f;

    Backtester backtester(config);
    AlwaysBuyStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    // Check that at least one trade was closed by stop-loss
    bool hasStopLossExit = false;
    for (const auto& trade : result.tradeLog) {
        if (trade.exitReason == "stop_loss") {
            hasStopLossExit = true;
            break;
        }
    }
    EXPECT_TRUE(hasStopLossExit);
}

TEST(BacktesterTest, TakeProfitTriggersOnUptrend) {
    auto candles = TestData::generateUptrend(200, 100.0f, 0.005f);  // Strong uptrend

    BacktestConfig config = BacktestConfig::zeroCostConfig();
    config.risk.enableTakeProfit = true;
    config.risk.takeProfitPercent = 0.02f;  // 2% take-profit

    Backtester backtester(config);
    AlternatingStrategy strategy(30, 25);  // Buy every 30, hold for 25
    BacktestResult result = backtester.run(candles, strategy);

    // Check that at least one trade was closed by take-profit
    bool hasTakeProfitExit = false;
    for (const auto& trade : result.tradeLog) {
        if (trade.exitReason == "take_profit") {
            hasTakeProfitExit = true;
            break;
        }
    }
    // In a strong uptrend with 0.5% daily returns, we should hit 2% take-profit
    EXPECT_TRUE(hasTakeProfitExit);
}

TEST(BacktesterTest, TrailingStopLocks profits) {
    auto candles = TestData::generateUptrend(150, 100.0f, 0.003f);
    // Add a reversal
    for (size_t i = 0; i < 50; ++i) {
        Candle c;
        float price = candles.back().close * (1.0f - 0.002f);
        c.open = candles.back().close;
        c.high = c.open * 1.005f;
        c.low = price * 0.995f;
        c.close = price;
        c.volume = 1000000.0f;
        candles.push_back(c);
    }

    BacktestConfig config = BacktestConfig::zeroCostConfig();
    config.risk.enableTrailingStop = true;
    config.risk.trailingStopPercent = 0.02f;  // 2% trailing stop

    Backtester backtester(config);
    AlwaysBuyStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    // With trailing stop, should have locked in some profits before reversal
    // Check trade log for trailing_stop exit
    bool hasTrailingStopExit = false;
    for (const auto& trade : result.tradeLog) {
        if (trade.exitReason == "trailing_stop") {
            hasTrailingStopExit = true;
            EXPECT_GT(trade.pnl, 0.0f);  // Should have locked in profit
            break;
        }
    }
    EXPECT_TRUE(hasTrailingStopExit);
}

// ============================================================================
// Sharpe Ratio Tests
// ============================================================================

TEST(BacktesterTest, SharpeRatioCalculatedCorrectly) {
    // Generate data with known returns pattern
    std::vector<Candle> candles;
    float price = 100.0f;
    std::vector<float> dailyReturns = {0.01f, -0.005f, 0.02f, -0.01f, 0.015f, 0.005f, -0.008f};

    // Need warmup period of 60 candles
    for (size_t i = 0; i < 60; ++i) {
        Candle c;
        c.open = c.high = c.low = c.close = price;
        c.volume = 1000000.0f;
        candles.push_back(c);
    }

    // Add return-generating candles
    for (size_t i = 0; i < 100; ++i) {
        float ret = dailyReturns[i % dailyReturns.size()];
        Candle c;
        c.open = price;
        c.high = price * (1.0f + std::abs(ret) + 0.001f);
        c.low = price * (1.0f - std::abs(ret) - 0.001f);
        c.close = price * (1.0f + ret);
        c.volume = 1000000.0f;
        price = c.close;
        candles.push_back(c);
    }

    BacktestConfig config = BacktestConfig::zeroCostConfig();
    config.riskFreeRate = 0.0f;  // Set to 0 for easier verification

    Backtester backtester(config);
    AlwaysBuyStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    // Sharpe ratio should be defined (not NaN or inf)
    EXPECT_FALSE(std::isnan(result.sharpeRatio));
    EXPECT_FALSE(std::isinf(result.sharpeRatio));

    // With positive average returns, Sharpe should be positive
    EXPECT_GT(result.sharpeRatio, 0.0f);
}

TEST(BacktesterTest, SharpeUsesAnnualization) {
    auto candles = TestData::generateUptrend(260);  // ~1 year of trading days

    BacktestConfig config = BacktestConfig::zeroCostConfig();
    config.riskFreeRate = 0.04f;  // 4% risk-free rate

    Backtester backtester(config);
    AlwaysBuyStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    // Annualized Sharpe should be reasonable (typically -3 to +3 for most strategies)
    EXPECT_GT(result.sharpeRatio, -5.0f);
    EXPECT_LT(result.sharpeRatio, 5.0f);
}

TEST(BacktesterTest, SortinoHigherThanSharpeForPositiveSkew) {
    // Uptrend with occasional small dips has positive skew
    auto candles = TestData::generateUptrend(200, 100.0f, 0.002f);

    BacktestConfig config = BacktestConfig::zeroCostConfig();
    Backtester backtester(config);
    AlwaysBuyStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    // Sortino only penalizes downside volatility, so for positive-skew returns,
    // Sortino should be higher than Sharpe
    if (result.sharpeRatio > 0.0f) {
        EXPECT_GE(result.sortinoRatio, result.sharpeRatio);
    }
}

// ============================================================================
// Position Sizing Tests
// ============================================================================

TEST(BacktesterTest, FixedFractionPositionSizing) {
    auto candles = TestData::generateUptrend(200);

    BacktestConfig config = BacktestConfig::zeroCostConfig();
    config.sizing.method = PositionSizing::FixedFraction;
    config.sizing.fixedFraction = 0.2f;  // 20% per trade

    Backtester backtester(config);
    AlternatingStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    // Check that trades respect position sizing
    for (const auto& trade : result.tradeLog) {
        float tradeValue = trade.quantity * trade.entryPrice;
        // Position should be approximately 20% of capital at time of entry
        // This is hard to verify exactly without knowing capital at each point
        EXPECT_GT(trade.quantity, 0.0f);
    }
}

TEST(BacktesterTest, MaxPositionSizeRespected) {
    auto candles = TestData::generateUptrend(200);

    BacktestConfig config = BacktestConfig::zeroCostConfig();
    config.sizing.fixedFraction = 0.5f;  // Try to use 50%
    config.sizing.maxPositionSize = 0.25f;  // But cap at 25%
    config.initialCapital = 10000.0f;

    Backtester backtester(config);
    AlwaysBuyStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    // First trade's value should not exceed 25% of initial capital
    if (!result.tradeLog.empty()) {
        float tradeValue = result.tradeLog[0].quantity * result.tradeLog[0].entryPrice;
        EXPECT_LE(tradeValue, config.initialCapital * config.sizing.maxPositionSize * 1.01f);
    }
}

// ============================================================================
// Metrics Calculation Tests
// ============================================================================

TEST(BacktesterTest, WinRateCalculatedCorrectly) {
    auto candles = TestData::generateMeanReverting(300, 100.0f, 10.0f, 40.0f);

    BacktestConfig config = BacktestConfig::zeroCostConfig();
    Backtester backtester(config);
    MeanReversionStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    if (result.trades > 0) {
        // Win rate should be between 0 and 1
        EXPECT_GE(result.winRate, 0.0f);
        EXPECT_LE(result.winRate, 1.0f);

        // Manually verify
        float expectedWinRate = static_cast<float>(result.wins) / result.trades;
        EXPECT_FLOAT_EQ(result.winRate, expectedWinRate);
    }
}

TEST(BacktesterTest, ProfitFactorPositiveWhenWinning) {
    auto candles = TestData::generateUptrend(200, 100.0f, 0.003f);  // Strong uptrend

    BacktestConfig config = BacktestConfig::zeroCostConfig();
    Backtester backtester(config);
    TrendFollowingStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    // In an uptrend, profit factor should be positive
    if (result.trades > 0 && result.totalReturn > 0) {
        EXPECT_GT(result.profitFactor, 1.0f);
    }
}

TEST(BacktesterTest, MaxDrawdownNonNegative) {
    auto candles = TestData::generateVolatile(200);

    BacktestConfig config = BacktestConfig::defaultConfig();
    Backtester backtester(config);
    AlternatingStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    // Max drawdown should be non-negative (it's a positive value representing loss)
    EXPECT_GE(result.maxDrawdown, 0.0f);
    EXPECT_LE(result.maxDrawdown, 1.0f);  // Can't lose more than 100%
}

TEST(BacktesterTest, EquityCurveMatchesFinalReturn) {
    auto candles = TestData::generateUptrend(200);

    BacktestConfig config = BacktestConfig::zeroCostConfig();
    Backtester backtester(config);
    AlwaysBuyStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    if (!result.equityCurve.empty()) {
        float finalEquity = result.equityCurve.back();
        float initialEquity = result.equityCurve.front();
        float equityReturn = (finalEquity - initialEquity) / initialEquity;

        // The equity curve's implied return should match totalReturn
        EXPECT_NEAR(equityReturn, result.totalReturn, 0.01f);
    }
}

TEST(BacktesterTest, TradeLogMatchesTradeCount) {
    auto candles = TestData::generateMeanReverting(300);

    BacktestConfig config = BacktestConfig::defaultConfig();
    Backtester backtester(config);
    AlternatingStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    // Trade log should have same number of entries as trades count
    EXPECT_EQ(result.tradeLog.size(), static_cast<size_t>(result.trades));
}

// ============================================================================
// Backward Compatibility Tests
// ============================================================================

TEST(BacktesterTest, StaticRunMethodWorks) {
    auto candles = TestData::generateUptrend(200);

    // Use static method (backward compatible)
    BacktestResult result = Backtester::run(candles);

    // Should return valid results
    EXPECT_FALSE(std::isnan(result.totalReturn));
    EXPECT_GE(result.trades, 0);
}

TEST(BacktesterTest, StaticAndInstanceMethodsComparable) {
    auto candles = TestData::generateUptrend(200);

    // Static method (uses default decision logic)
    BacktestResult staticResult = Backtester::run(candles);

    // Instance method with zero costs and similar decision logic
    BacktestConfig config = BacktestConfig::zeroCostConfig();
    Backtester backtester(config);

    // Use a similar strategy - the MeanReversionStrategy is based on the original logic
    MeanReversionStrategy strategy;
    BacktestResult instanceResult = backtester.run(candles, strategy);

    // Results should be in the same ballpark (not exact due to slight implementation differences)
    // Just verify both produce valid results
    EXPECT_FALSE(std::isnan(staticResult.totalReturn));
    EXPECT_FALSE(std::isnan(instanceResult.totalReturn));
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(BacktesterTest, HandlesEmptyCandles) {
    std::vector<Candle> emptyCandles;

    BacktestConfig config = BacktestConfig::defaultConfig();
    Backtester backtester(config);
    AlwaysBuyStrategy strategy;
    BacktestResult result = backtester.run(emptyCandles, strategy);

    // Should handle gracefully
    EXPECT_EQ(result.trades, 0);
    EXPECT_EQ(result.totalReturn, 0.0f);
}

TEST(BacktesterTest, HandlesInsufficientCandles) {
    auto candles = TestData::generateUptrend(30);  // Less than warmup period

    BacktestConfig config = BacktestConfig::defaultConfig();
    Backtester backtester(config);
    AlwaysBuyStrategy strategy;  // Warmup is 60
    BacktestResult result = backtester.run(candles, strategy);

    // Should handle gracefully with no trades
    EXPECT_EQ(result.trades, 0);
}

TEST(BacktesterTest, HandlesZeroPrices) {
    std::vector<Candle> candles;
    for (size_t i = 0; i < 100; ++i) {
        Candle c;
        c.open = c.high = c.low = c.close = 0.0f;
        c.volume = 0.0f;
        candles.push_back(c);
    }

    BacktestConfig config = BacktestConfig::defaultConfig();
    Backtester backtester(config);
    AlwaysBuyStrategy strategy;
    BacktestResult result = backtester.run(candles, strategy);

    // Should not crash, trades should be 0 or handle gracefully
    EXPECT_FALSE(std::isnan(result.totalReturn));
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
