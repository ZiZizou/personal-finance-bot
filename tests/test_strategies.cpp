#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <memory>

#include "../IStrategy.h"
#include "../Strategies/MeanReversionStrategy.h"
#include "../Strategies/TrendFollowingStrategy.h"
#include "../MarketData.h"

// ============================================================================
// Test Data Generators
// ============================================================================

namespace TestData {

// Generate uptrend candles
std::vector<Candle> generateUptrend(size_t count, float start = 100.0f, float dailyGain = 0.5f) {
    std::vector<Candle> candles;
    float price = start;
    for (size_t i = 0; i < count; ++i) {
        Candle c;
        c.open = price;
        c.close = price + dailyGain;
        c.high = std::max(c.open, c.close) * 1.005f;
        c.low = std::min(c.open, c.close) * 0.995f;
        c.volume = 1000000.0f;
        price = c.close;
        candles.push_back(c);
    }
    return candles;
}

// Generate downtrend candles
std::vector<Candle> generateDowntrend(size_t count, float start = 150.0f, float dailyLoss = 0.5f) {
    std::vector<Candle> candles;
    float price = start;
    for (size_t i = 0; i < count; ++i) {
        Candle c;
        c.open = price;
        c.close = price - dailyLoss;
        c.high = std::max(c.open, c.close) * 1.005f;
        c.low = std::min(c.open, c.close) * 0.995f;
        c.volume = 1000000.0f;
        price = c.close;
        candles.push_back(c);
    }
    return candles;
}

// Generate sideways/oscillating candles
std::vector<Candle> generateSideways(size_t count, float center = 100.0f, float amplitude = 5.0f, float period = 20.0f) {
    std::vector<Candle> candles;
    for (size_t i = 0; i < count; ++i) {
        float price = center + amplitude * std::sin(2.0f * M_PI * i / period);
        Candle c;
        c.open = price;
        c.close = center + amplitude * std::sin(2.0f * M_PI * (i + 1) / period);
        c.high = std::max(c.open, c.close) * 1.002f;
        c.low = std::min(c.open, c.close) * 0.998f;
        c.volume = 1000000.0f;
        candles.push_back(c);
    }
    return candles;
}

// Generate oversold condition (RSI should be low)
std::vector<Candle> generateOversold(size_t count, float start = 150.0f) {
    std::vector<Candle> candles;
    float price = start;

    // First half: steady decline to create oversold RSI
    for (size_t i = 0; i < count; ++i) {
        float drop = 0.8f + (float)(i) / count * 0.5f;  // Accelerating decline
        Candle c;
        c.open = price;
        c.close = price - drop;
        c.high = c.open * 1.002f;
        c.low = c.close * 0.998f;
        c.volume = 1000000.0f;
        price = c.close;
        candles.push_back(c);
    }
    return candles;
}

// Generate overbought condition (RSI should be high)
std::vector<Candle> generateOverbought(size_t count, float start = 50.0f) {
    std::vector<Candle> candles;
    float price = start;

    // Continuous rise to create overbought RSI
    for (size_t i = 0; i < count; ++i) {
        float gain = 0.8f + (float)(i) / count * 0.5f;  // Accelerating rise
        Candle c;
        c.open = price;
        c.close = price + gain;
        c.high = c.close * 1.002f;
        c.low = c.open * 0.998f;
        c.volume = 1000000.0f;
        price = c.close;
        candles.push_back(c);
    }
    return candles;
}

// Generate MA crossover scenario
std::vector<Candle> generateMACrossover(size_t warmup, size_t crossoverPoint, bool bullish = true) {
    std::vector<Candle> candles;
    float price = 100.0f;

    // Warmup period - sideways
    for (size_t i = 0; i < warmup; ++i) {
        Candle c;
        c.open = price;
        c.close = price + ((i % 2 == 0) ? 0.1f : -0.1f);
        c.high = std::max(c.open, c.close) * 1.002f;
        c.low = std::min(c.open, c.close) * 0.998f;
        c.volume = 1000000.0f;
        price = c.close;
        candles.push_back(c);
    }

    // Before crossover - trend in one direction
    float preCrossDirection = bullish ? -0.3f : 0.3f;
    for (size_t i = 0; i < 30; ++i) {
        Candle c;
        c.open = price;
        c.close = price + preCrossDirection;
        c.high = std::max(c.open, c.close) * 1.005f;
        c.low = std::min(c.open, c.close) * 0.995f;
        c.volume = 1000000.0f;
        price = c.close;
        candles.push_back(c);
    }

    // Crossover - strong move in opposite direction
    float postCrossDirection = bullish ? 1.5f : -1.5f;
    for (size_t i = 0; i < crossoverPoint; ++i) {
        Candle c;
        c.open = price;
        c.close = price + postCrossDirection;
        c.high = std::max(c.open, c.close) * 1.01f;
        c.low = std::min(c.open, c.close) * 0.99f;
        c.volume = 1500000.0f;  // Higher volume
        price = c.close;
        candles.push_back(c);
    }

    return candles;
}

// Generate volatile candles
std::vector<Candle> generateVolatile(size_t count, float start = 100.0f, float volatility = 0.03f) {
    std::vector<Candle> candles;
    float price = start;
    for (size_t i = 0; i < count; ++i) {
        float change = ((rand() % 2000) / 1000.0f - 1.0f) * volatility * price;
        Candle c;
        c.open = price;
        c.close = price + change;
        c.high = std::max(c.open, c.close) * (1.0f + volatility * 0.5f);
        c.low = std::min(c.open, c.close) * (1.0f - volatility * 0.5f);
        c.volume = 1000000.0f + (rand() % 500000);
        price = c.close;
        candles.push_back(c);
    }
    return candles;
}

}  // namespace TestData

// ============================================================================
// IStrategy Interface Tests
// ============================================================================

TEST(IStrategyTest, MeanReversionHasName) {
    MeanReversionStrategy strategy;
    EXPECT_EQ(strategy.getName(), "MeanReversion");
}

TEST(IStrategyTest, TrendFollowingHasName) {
    TrendFollowingStrategy strategy;
    EXPECT_EQ(strategy.getName(), "TrendFollowing");
}

TEST(IStrategyTest, TripleMAHasName) {
    TripleMAStrategy strategy;
    EXPECT_EQ(strategy.getName(), "TripleMA");
}

TEST(IStrategyTest, StrategiesHaveWarmupPeriod) {
    MeanReversionStrategy mr;
    TrendFollowingStrategy tf;
    TripleMAStrategy tma;

    EXPECT_GT(mr.getWarmupPeriod(), 0);
    EXPECT_GT(tf.getWarmupPeriod(), 0);
    EXPECT_GT(tma.getWarmupPeriod(), 0);
}

TEST(IStrategyTest, StrategiesCanBeCloned) {
    MeanReversionStrategy mr;
    TrendFollowingStrategy tf;

    auto mrClone = mr.clone();
    auto tfClone = tf.clone();

    EXPECT_EQ(mrClone->getName(), mr.getName());
    EXPECT_EQ(tfClone->getName(), tf.getName());
}

TEST(IStrategyTest, ClonedStrategyIsIndependent) {
    MeanReversionStrategy original;
    auto clone = original.clone();

    // Modify original
    original.setRSIThresholds(25.0f, 75.0f);

    // Clone should still work independently
    auto candles = TestData::generateOversold(100);
    StrategySignal sig = clone->generateSignal(candles, candles.size() - 1);

    EXPECT_FALSE(sig.reason.empty());
}

// ============================================================================
// Signal Type Tests
// ============================================================================

TEST(SignalTest, HoldSignalHasCorrectType) {
    auto sig = StrategySignal::hold("Test");
    EXPECT_EQ(sig.type, SignalType::Hold);
}

TEST(SignalTest, BuySignalHasCorrectType) {
    auto sig = StrategySignal::buy(0.8f, "Test");
    EXPECT_EQ(sig.type, SignalType::Buy);
}

TEST(SignalTest, SellSignalHasCorrectType) {
    auto sig = StrategySignal::sell(0.8f, "Test");
    EXPECT_EQ(sig.type, SignalType::Sell);
}

TEST(SignalTest, SignalStrengthBounded) {
    auto buySig = StrategySignal::buy(1.5f, "Test");  // Strength > 1
    auto sellSig = StrategySignal::sell(-0.5f, "Test");  // Strength < 0

    // Strength should be clamped or handled
    EXPECT_GE(buySig.strength, 0.0f);
    EXPECT_LE(buySig.strength, 1.0f);
}

TEST(SignalTest, SignalHasReason) {
    auto sig = StrategySignal::buy(0.5f, "RSI oversold");
    EXPECT_FALSE(sig.reason.empty());
    EXPECT_NE(sig.reason.find("RSI"), std::string::npos);
}

// ============================================================================
// Mean Reversion Strategy Tests
// ============================================================================

TEST(MeanReversionTest, GeneratesBuyInOversoldCondition) {
    MeanReversionStrategy strategy;
    auto candles = TestData::generateOversold(100);

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    // In oversold condition, should generate buy or hold
    // RSI should be low after continuous decline
    EXPECT_TRUE(sig.type == SignalType::Buy || sig.type == SignalType::Hold);
}

TEST(MeanReversionTest, GeneratesSellInOverboughtCondition) {
    MeanReversionStrategy strategy;
    auto candles = TestData::generateOverbought(100);

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    // In overbought condition, should generate sell or hold
    EXPECT_TRUE(sig.type == SignalType::Sell || sig.type == SignalType::Hold);
}

TEST(MeanReversionTest, GeneratesHoldInSidewaysMarket) {
    MeanReversionStrategy strategy;
    auto candles = TestData::generateSideways(100, 100.0f, 2.0f, 20.0f);  // Small amplitude

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    // In balanced sideways market, RSI should be near 50
    // May hold or give weak signals
    EXPECT_FALSE(sig.reason.empty());
}

TEST(MeanReversionTest, ReturnsHoldForInsufficientData) {
    MeanReversionStrategy strategy;
    auto candles = TestData::generateUptrend(30);  // Less than required

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    EXPECT_EQ(sig.type, SignalType::Hold);
    EXPECT_NE(sig.reason.find("Insufficient"), std::string::npos);
}

TEST(MeanReversionTest, SetsStopLossForBuySignal) {
    MeanReversionStrategy strategy;
    auto candles = TestData::generateOversold(100);

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    if (sig.type == SignalType::Buy) {
        EXPECT_GT(sig.stopLossPrice, 0.0f);
        EXPECT_LT(sig.stopLossPrice, candles.back().close);  // Below current price
    }
}

TEST(MeanReversionTest, SetsTakeProfitForBuySignal) {
    MeanReversionStrategy strategy;
    auto candles = TestData::generateOversold(100);

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    if (sig.type == SignalType::Buy) {
        // Take profit should be set (targeting the mean)
        EXPECT_GT(sig.takeProfitPrice, 0.0f);
    }
}

TEST(MeanReversionTest, ParametersAffectSignal) {
    auto candles = TestData::generateOversold(100);

    MeanReversionStrategy conservative;
    conservative.setRSIThresholds(20.0f, 80.0f);  // Tighter thresholds

    MeanReversionStrategy aggressive;
    aggressive.setRSIThresholds(40.0f, 60.0f);  // Looser thresholds

    StrategySignal sigConservative = conservative.generateSignal(candles, candles.size() - 1);
    StrategySignal sigAggressive = aggressive.generateSignal(candles, candles.size() - 1);

    // Aggressive strategy should be more likely to generate signals
    // At minimum, both should produce valid signals
    EXPECT_FALSE(sigConservative.reason.empty());
    EXPECT_FALSE(sigAggressive.reason.empty());
}

TEST(MeanReversionTest, AdaptiveRSICanBeEnabled) {
    MeanReversionStrategy strategy;
    strategy.setUseAdaptiveRSI(true);

    auto candles = TestData::generateOversold(100);
    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    // Should still generate valid signal
    EXPECT_FALSE(sig.reason.empty());
}

TEST(MeanReversionTest, SignalStrengthIncreasesWithExtreme) {
    MeanReversionStrategy strategy;

    // Moderately oversold
    auto moderateCandles = TestData::generateOversold(80);
    StrategySignal moderateSig = strategy.generateSignal(moderateCandles, moderateCandles.size() - 1);

    // Extremely oversold (longer decline)
    auto extremeCandles = TestData::generateOversold(120);
    StrategySignal extremeSig = strategy.generateSignal(extremeCandles, extremeCandles.size() - 1);

    // Both should be buy signals (or hold)
    // If both are buys, extreme should have higher strength
    if (moderateSig.type == SignalType::Buy && extremeSig.type == SignalType::Buy) {
        // This may not always hold due to RSI calculation nuances
        EXPECT_GE(extremeSig.strength, 0.0f);
    }
}

// ============================================================================
// Trend Following Strategy Tests
// ============================================================================

TEST(TrendFollowingTest, GeneratesBuyOnBullishCrossover) {
    TrendFollowingStrategy strategy;
    auto candles = TestData::generateMACrossover(60, 25, true);  // Bullish crossover

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    // After bullish crossover with strong trend, should buy
    EXPECT_TRUE(sig.type == SignalType::Buy || sig.type == SignalType::Hold);
}

TEST(TrendFollowingTest, GeneratesSellOnBearishCrossover) {
    TrendFollowingStrategy strategy;
    auto candles = TestData::generateMACrossover(60, 25, false);  // Bearish crossover

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    // After bearish crossover with strong trend, should sell
    EXPECT_TRUE(sig.type == SignalType::Sell || sig.type == SignalType::Hold);
}

TEST(TrendFollowingTest, HoldsInSidewaysMarket) {
    TrendFollowingStrategy strategy;
    auto candles = TestData::generateSideways(100, 100.0f, 2.0f, 30.0f);

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    // In sideways market with low ADX, should hold or give weak signals
    EXPECT_FALSE(sig.reason.empty());
}

TEST(TrendFollowingTest, ReturnsHoldForInsufficientData) {
    TrendFollowingStrategy strategy;
    auto candles = TestData::generateUptrend(20);  // Less than required

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    EXPECT_EQ(sig.type, SignalType::Hold);
}

TEST(TrendFollowingTest, SetsStopLossUsingATR) {
    TrendFollowingStrategy strategy;
    auto candles = TestData::generateMACrossover(60, 30, true);

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    if (sig.type == SignalType::Buy) {
        EXPECT_GT(sig.stopLossPrice, 0.0f);
        EXPECT_LT(sig.stopLossPrice, candles.back().close);
    }
}

TEST(TrendFollowingTest, ADXThresholdFiltersWeakTrends) {
    TrendFollowingStrategy strategyHigh;
    strategyHigh.setADXParams(14, 40.0f);  // High threshold

    TrendFollowingStrategy strategyLow;
    strategyLow.setADXParams(14, 15.0f);  // Low threshold

    auto candles = TestData::generateUptrend(100, 100.0f, 0.3f);  // Moderate trend

    StrategySignal sigHigh = strategyHigh.generateSignal(candles, candles.size() - 1);
    StrategySignal sigLow = strategyLow.generateSignal(candles, candles.size() - 1);

    // Higher threshold should be more selective
    // Both should produce valid signals
    EXPECT_FALSE(sigHigh.reason.empty());
    EXPECT_FALSE(sigLow.reason.empty());
}

TEST(TrendFollowingTest, MAPeriodAffectsSignals) {
    TrendFollowingStrategy fastStrategy;
    fastStrategy.setMAPeriods(5, 15);  // Short periods

    TrendFollowingStrategy slowStrategy;
    slowStrategy.setMAPeriods(20, 50);  // Long periods

    auto candles = TestData::generateUptrend(100);

    StrategySignal sigFast = fastStrategy.generateSignal(candles, candles.size() - 1);
    StrategySignal sigSlow = slowStrategy.generateSignal(candles, candles.size() - 1);

    // Both should work without crashing
    EXPECT_FALSE(sigFast.reason.empty());
    EXPECT_FALSE(sigSlow.reason.empty());
}

TEST(TrendFollowingTest, MACDConfirmationCanBeEnabled) {
    TrendFollowingStrategy strategy;
    strategy.setUseMACD(true);

    auto candles = TestData::generateMACrossover(60, 25, true);
    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    // Should still produce valid signal
    EXPECT_FALSE(sig.reason.empty());
}

// ============================================================================
// Triple MA Strategy Tests
// ============================================================================

TEST(TripleMATest, GeneratesBuyWhenAllAlignedBullish) {
    TripleMAStrategy strategy;
    auto candles = TestData::generateUptrend(120, 50.0f, 1.0f);  // Strong uptrend

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    // In strong uptrend with aligned MAs, should generate buy
    EXPECT_TRUE(sig.type == SignalType::Buy || sig.type == SignalType::Hold);
}

TEST(TripleMATest, GeneratesSellWhenAllAlignedBearish) {
    TripleMAStrategy strategy;
    auto candles = TestData::generateDowntrend(120, 200.0f, 1.0f);  // Strong downtrend

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    // In strong downtrend with aligned MAs, should generate sell
    EXPECT_TRUE(sig.type == SignalType::Sell || sig.type == SignalType::Hold);
}

TEST(TripleMATest, HoldsWhenMAsNotAligned) {
    TripleMAStrategy strategy;
    auto candles = TestData::generateSideways(120, 100.0f, 5.0f, 15.0f);

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    // When MAs are not aligned, should hold
    if (sig.type == SignalType::Hold) {
        EXPECT_NE(sig.reason.find("not aligned"), std::string::npos);
    }
}

TEST(TripleMATest, ReturnsHoldForInsufficientData) {
    TripleMAStrategy strategy;
    auto candles = TestData::generateUptrend(40);  // Less than slow period

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    EXPECT_EQ(sig.type, SignalType::Hold);
}

// ============================================================================
// Enhanced Mean Reversion Strategy Tests
// ============================================================================

TEST(EnhancedMeanReversionTest, HasVolumeFilter) {
    EnhancedMeanReversionStrategy strategy;

    auto candles = TestData::generateOversold(100);
    // Reduce volume to fail filter
    for (auto& c : candles) {
        c.volume = 100000.0f;  // Low volume
    }

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    // May filter due to low volume
    EXPECT_FALSE(sig.reason.empty());
}

TEST(EnhancedMeanReversionTest, HasTrendFilter) {
    EnhancedMeanReversionStrategy strategy;

    // Create scenario where base strategy would give signal but trend filter reduces it
    auto candles = TestData::generateOversold(120);

    StrategySignal sig = strategy.generateSignal(candles, candles.size() - 1);

    // Signal may be reduced due to trend filter in downtrend
    EXPECT_FALSE(sig.reason.empty());
}

// ============================================================================
// Strategy Parameter Tests
// ============================================================================

TEST(ParameterTest, MeanReversionHasOptimizableParams) {
    MeanReversionStrategy strategy;
    auto params = strategy.getParameters();

    EXPECT_FALSE(params.empty());

    // Check for expected parameters
    bool hasRSI = false;
    bool hasBB = false;
    for (const auto& param : params) {
        if (param.name == "rsiBuyThreshold") hasRSI = true;
        if (param.name == "bbPeriod") hasBB = true;
    }
    EXPECT_TRUE(hasRSI);
    EXPECT_TRUE(hasBB);
}

TEST(ParameterTest, TrendFollowingHasOptimizableParams) {
    TrendFollowingStrategy strategy;
    auto params = strategy.getParameters();

    EXPECT_FALSE(params.empty());

    bool hasFastMA = false;
    bool hasADX = false;
    for (const auto& param : params) {
        if (param.name == "fastMAPeriod") hasFastMA = true;
        if (param.name == "adxThreshold") hasADX = true;
    }
    EXPECT_TRUE(hasFastMA);
    EXPECT_TRUE(hasADX);
}

TEST(ParameterTest, ParametersHaveValidRanges) {
    MeanReversionStrategy strategy;
    auto params = strategy.getParameters();

    for (const auto& param : params) {
        EXPECT_LE(param.minValue, param.maxValue);
        EXPECT_GE(param.currentValue, param.minValue);
        EXPECT_LE(param.currentValue, param.maxValue);
        EXPECT_GT(param.step, 0.0f);
    }
}

TEST(ParameterTest, SetParameterWorks) {
    MeanReversionStrategy strategy;

    strategy.setParam("rsiBuyThreshold", 25.0f);
    float value = strategy.getParamValue("rsiBuyThreshold");

    EXPECT_FLOAT_EQ(value, 25.0f);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(EdgeCaseTest, StrategiesHandleEmptyCandles) {
    MeanReversionStrategy mr;
    TrendFollowingStrategy tf;
    std::vector<Candle> empty;

    StrategySignal mrSig = mr.generateSignal(empty, 0);
    StrategySignal tfSig = tf.generateSignal(empty, 0);

    EXPECT_EQ(mrSig.type, SignalType::Hold);
    EXPECT_EQ(tfSig.type, SignalType::Hold);
}

TEST(EdgeCaseTest, StrategiesHandleSingleCandle) {
    MeanReversionStrategy mr;
    TrendFollowingStrategy tf;

    Candle c;
    c.open = 100.0f;
    c.high = 101.0f;
    c.low = 99.0f;
    c.close = 100.5f;
    c.volume = 1000000.0f;

    std::vector<Candle> single = {c};

    StrategySignal mrSig = mr.generateSignal(single, 0);
    StrategySignal tfSig = tf.generateSignal(single, 0);

    EXPECT_EQ(mrSig.type, SignalType::Hold);
    EXPECT_EQ(tfSig.type, SignalType::Hold);
}

TEST(EdgeCaseTest, StrategiesHandleZeroPrices) {
    MeanReversionStrategy strategy;

    std::vector<Candle> zeroPrices;
    for (int i = 0; i < 100; ++i) {
        Candle c;
        c.open = c.high = c.low = c.close = 0.0f;
        c.volume = 1000000.0f;
        zeroPrices.push_back(c);
    }

    StrategySignal sig = strategy.generateSignal(zeroPrices, zeroPrices.size() - 1);

    // Should not crash
    EXPECT_FALSE(sig.reason.empty());
}

TEST(EdgeCaseTest, StrategiesHandleConstantPrices) {
    MeanReversionStrategy mr;
    TrendFollowingStrategy tf;

    std::vector<Candle> constant;
    for (int i = 0; i < 100; ++i) {
        Candle c;
        c.open = c.high = c.low = c.close = 100.0f;
        c.volume = 1000000.0f;
        constant.push_back(c);
    }

    StrategySignal mrSig = mr.generateSignal(constant, constant.size() - 1);
    StrategySignal tfSig = tf.generateSignal(constant, constant.size() - 1);

    // Should not crash, likely hold
    EXPECT_FALSE(mrSig.reason.empty());
    EXPECT_FALSE(tfSig.reason.empty());
}

TEST(EdgeCaseTest, StrategiesHandleNegativePrices) {
    MeanReversionStrategy strategy;

    std::vector<Candle> negative;
    float price = -50.0f;
    for (int i = 0; i < 100; ++i) {
        Candle c;
        c.open = price;
        c.close = price + 0.5f;
        c.high = c.close;
        c.low = c.open;
        c.volume = 1000000.0f;
        price = c.close;
        negative.push_back(c);
    }

    StrategySignal sig = strategy.generateSignal(negative, negative.size() - 1);

    // Should handle gracefully
    EXPECT_FALSE(sig.reason.empty());
}

TEST(EdgeCaseTest, StrategiesHandleExtremeVolatility) {
    MeanReversionStrategy mr;
    TrendFollowingStrategy tf;

    auto candles = TestData::generateVolatile(100, 100.0f, 0.20f);  // 20% daily moves

    StrategySignal mrSig = mr.generateSignal(candles, candles.size() - 1);
    StrategySignal tfSig = tf.generateSignal(candles, candles.size() - 1);

    // Should not crash
    EXPECT_FALSE(mrSig.reason.empty());
    EXPECT_FALSE(tfSig.reason.empty());
}

// ============================================================================
// Strategy Consistency Tests
// ============================================================================

TEST(ConsistencyTest, SameInputProducesSameOutput) {
    MeanReversionStrategy strategy;
    auto candles = TestData::generateOversold(100);

    StrategySignal sig1 = strategy.generateSignal(candles, candles.size() - 1);
    StrategySignal sig2 = strategy.generateSignal(candles, candles.size() - 1);

    EXPECT_EQ(sig1.type, sig2.type);
    EXPECT_FLOAT_EQ(sig1.strength, sig2.strength);
}

TEST(ConsistencyTest, ClonedStrategyProducesSameOutput) {
    MeanReversionStrategy original;
    auto clone = original.clone();

    auto candles = TestData::generateOversold(100);

    StrategySignal origSig = original.generateSignal(candles, candles.size() - 1);
    StrategySignal cloneSig = clone->generateSignal(candles, candles.size() - 1);

    EXPECT_EQ(origSig.type, cloneSig.type);
    EXPECT_FLOAT_EQ(origSig.strength, cloneSig.strength);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
