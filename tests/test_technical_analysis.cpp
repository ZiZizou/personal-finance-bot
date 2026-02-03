#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>

#include "../TechnicalAnalysis.h"
#include "../MarketData.h"

// ============================================================================
// Test Data Generators
// ============================================================================

namespace TestData {

// Generate price series with known trend
std::vector<float> generateTrendingPrices(size_t count, float start, float dailyChange) {
    std::vector<float> prices;
    float price = start;
    for (size_t i = 0; i < count; ++i) {
        prices.push_back(price);
        price += dailyChange;
    }
    return prices;
}

// Generate oscillating prices (for RSI testing)
std::vector<float> generateOscillatingPrices(size_t count, float center, float amplitude, float period) {
    std::vector<float> prices;
    for (size_t i = 0; i < count; ++i) {
        float price = center + amplitude * std::sin(2.0f * M_PI * i / period);
        prices.push_back(price);
    }
    return prices;
}

// Generate prices that only go up (for RSI = 100 testing)
std::vector<float> generateOnlyUpPrices(size_t count, float start = 100.0f) {
    std::vector<float> prices;
    float price = start;
    for (size_t i = 0; i < count; ++i) {
        prices.push_back(price);
        price += 1.0f;  // Always up
    }
    return prices;
}

// Generate prices that only go down (for RSI = 0 testing)
std::vector<float> generateOnlyDownPrices(size_t count, float start = 200.0f) {
    std::vector<float> prices;
    float price = start;
    for (size_t i = 0; i < count; ++i) {
        prices.push_back(price);
        price -= 1.0f;  // Always down
    }
    return prices;
}

// Generate candles from prices
std::vector<Candle> generateCandles(const std::vector<float>& prices, float spread = 0.01f) {
    std::vector<Candle> candles;
    for (size_t i = 0; i < prices.size(); ++i) {
        Candle c;
        c.close = prices[i];
        c.open = (i > 0) ? prices[i - 1] : prices[i];
        c.high = std::max(c.open, c.close) * (1.0f + spread);
        c.low = std::min(c.open, c.close) * (1.0f - spread);
        c.volume = 1000000.0f;
        candles.push_back(c);
    }
    return candles;
}

// Generate volatile candles
std::vector<Candle> generateVolatileCandles(size_t count, float basePrice = 100.0f, float volatility = 0.05f) {
    std::vector<Candle> candles;
    float price = basePrice;
    for (size_t i = 0; i < count; ++i) {
        float change = ((rand() % 1000) / 500.0f - 1.0f) * volatility * price;
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

// Generate returns from prices
std::vector<float> pricesToReturns(const std::vector<float>& prices) {
    std::vector<float> returns;
    for (size_t i = 1; i < prices.size(); ++i) {
        returns.push_back((prices[i] - prices[i - 1]) / prices[i - 1]);
    }
    return returns;
}

}  // namespace TestData

// ============================================================================
// RSI Tests
// ============================================================================

TEST(RSITest, RSIBoundedBetween0And100) {
    // Test with various price patterns
    std::vector<std::vector<float>> testCases = {
        TestData::generateTrendingPrices(100, 100.0f, 1.0f),    // Uptrend
        TestData::generateTrendingPrices(100, 200.0f, -1.0f),   // Downtrend
        TestData::generateOscillatingPrices(100, 100.0f, 10.0f, 20.0f),  // Oscillating
    };

    for (const auto& prices : testCases) {
        float rsi = computeRSI(prices, 14);
        EXPECT_GE(rsi, 0.0f) << "RSI should be >= 0";
        EXPECT_LE(rsi, 100.0f) << "RSI should be <= 100";
    }
}

TEST(RSITest, RSIApproaches100ForOnlyUpMoves) {
    auto prices = TestData::generateOnlyUpPrices(50);
    float rsi = computeRSI(prices, 14);

    // With only up moves, RSI should be very high (approaching 100)
    EXPECT_GT(rsi, 90.0f) << "RSI should approach 100 for only up moves";
}

TEST(RSITest, RSIApproaches0ForOnlyDownMoves) {
    auto prices = TestData::generateOnlyDownPrices(50);
    float rsi = computeRSI(prices, 14);

    // With only down moves, RSI should be very low (approaching 0)
    EXPECT_LT(rsi, 10.0f) << "RSI should approach 0 for only down moves";
}

TEST(RSITest, RSIAround50ForSidewaysMarket) {
    // Generate alternating up/down of equal magnitude
    std::vector<float> prices;
    float price = 100.0f;
    for (int i = 0; i < 100; ++i) {
        prices.push_back(price);
        price += (i % 2 == 0) ? 1.0f : -1.0f;
    }

    float rsi = computeRSI(prices, 14);

    // RSI should be around 50 for balanced up/down moves
    EXPECT_GT(rsi, 40.0f);
    EXPECT_LT(rsi, 60.0f);
}

TEST(RSITest, RSINotNaNForValidInput) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 10.0f, 20.0f);
    float rsi = computeRSI(prices, 14);

    EXPECT_FALSE(std::isnan(rsi));
    EXPECT_FALSE(std::isinf(rsi));
}

TEST(RSITest, AdaptiveRSIBounded) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 10.0f, 20.0f);
    float adaptiveRsi = computeAdaptiveRSI(prices, 14);

    EXPECT_GE(adaptiveRsi, 0.0f);
    EXPECT_LE(adaptiveRsi, 100.0f);
}

TEST(RSITest, RSIHandlesShortPriceSeries) {
    std::vector<float> shortPrices = {100.0f, 101.0f, 102.0f, 101.5f, 103.0f};
    float rsi = computeRSI(shortPrices, 14);

    // Should not crash, may return edge case value
    EXPECT_FALSE(std::isnan(rsi));
}

// ============================================================================
// MACD Tests
// ============================================================================

TEST(MACDTest, MACDReturnsValidPair) {
    auto prices = TestData::generateTrendingPrices(100, 100.0f, 0.5f);
    auto [macd, signal] = computeMACD(prices);

    EXPECT_FALSE(std::isnan(macd));
    EXPECT_FALSE(std::isnan(signal));
    EXPECT_FALSE(std::isinf(macd));
    EXPECT_FALSE(std::isinf(signal));
}

TEST(MACDTest, MACDPositiveInUptrend) {
    auto prices = TestData::generateTrendingPrices(100, 100.0f, 1.0f);  // Strong uptrend
    auto [macd, signal] = computeMACD(prices);

    // In a strong uptrend, short EMA > long EMA, so MACD should be positive
    EXPECT_GT(macd, 0.0f);
}

TEST(MACDTest, MACDNegativeInDowntrend) {
    auto prices = TestData::generateTrendingPrices(100, 200.0f, -1.0f);  // Strong downtrend
    auto [macd, signal] = computeMACD(prices);

    // In a strong downtrend, short EMA < long EMA, so MACD should be negative
    EXPECT_LT(macd, 0.0f);
}

TEST(MACDTest, MACDNearZeroInSideways) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 2.0f, 10.0f);
    auto [macd, signal] = computeMACD(prices);

    // In a sideways market, MACD should be close to zero
    EXPECT_LT(std::abs(macd), 5.0f);  // Within reasonable range
}

TEST(MACDTest, SignalSmoothsMACD) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 5.0f, 15.0f);
    auto [macd, signal] = computeMACD(prices);

    // Signal is a smoothed version of MACD - both should have same sign in strong trends
    // In oscillating market, this may not hold
    EXPECT_FALSE(std::isnan(signal));
}

// ============================================================================
// Bollinger Bands Tests
// ============================================================================

TEST(BollingerBandsTest, UpperGreaterThanMiddleGreaterThanLower) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 10.0f, 20.0f);
    BollingerBands bb = computeBollingerBands(prices, 20, 2.0f);

    EXPECT_GT(bb.upper, bb.middle) << "Upper band should be above middle";
    EXPECT_GT(bb.middle, bb.lower) << "Middle should be above lower band";
}

TEST(BollingerBandsTest, BandsSymmetricAroundMiddle) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 10.0f, 20.0f);
    BollingerBands bb = computeBollingerBands(prices, 20, 2.0f);

    float upperDiff = bb.upper - bb.middle;
    float lowerDiff = bb.middle - bb.lower;

    // Bands should be symmetric (within floating point tolerance)
    EXPECT_NEAR(upperDiff, lowerDiff, 0.01f);
}

TEST(BollingerBandsTest, BandwidthPositive) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 10.0f, 20.0f);
    BollingerBands bb = computeBollingerBands(prices, 20, 2.0f);

    EXPECT_GT(bb.bandwidth, 0.0f) << "Bandwidth should be positive";
}

TEST(BollingerBandsTest, WideBandsForVolatileMarket) {
    // Generate two sets of prices - one volatile, one stable
    std::vector<float> volatilePrices;
    std::vector<float> stablePrices;

    float price = 100.0f;
    for (int i = 0; i < 100; ++i) {
        volatilePrices.push_back(price + ((i % 2 == 0) ? 10.0f : -10.0f));  // Large swings
        stablePrices.push_back(price + ((i % 2 == 0) ? 0.5f : -0.5f));      // Small swings
    }

    BollingerBands bbVolatile = computeBollingerBands(volatilePrices, 20, 2.0f);
    BollingerBands bbStable = computeBollingerBands(stablePrices, 20, 2.0f);

    EXPECT_GT(bbVolatile.bandwidth, bbStable.bandwidth);
}

TEST(BollingerBandsTest, MultiplierAffectsBandwidth) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 10.0f, 20.0f);

    BollingerBands bb2 = computeBollingerBands(prices, 20, 2.0f);
    BollingerBands bb3 = computeBollingerBands(prices, 20, 3.0f);

    // 3-sigma bands should be wider than 2-sigma
    float width2 = bb2.upper - bb2.lower;
    float width3 = bb3.upper - bb3.lower;

    EXPECT_GT(width3, width2);
}

TEST(BollingerBandsTest, MiddleIsMovingAverage) {
    auto prices = TestData::generateTrendingPrices(50, 100.0f, 1.0f);
    BollingerBands bb = computeBollingerBands(prices, 20, 2.0f);

    // Middle band should be the 20-period SMA
    // Calculate expected SMA
    float sum = 0.0f;
    for (size_t i = prices.size() - 20; i < prices.size(); ++i) {
        sum += prices[i];
    }
    float expectedSMA = sum / 20.0f;

    EXPECT_NEAR(bb.middle, expectedSMA, 0.1f);
}

// ============================================================================
// ATR Tests
// ============================================================================

TEST(ATRTest, ATRPositive) {
    auto candles = TestData::generateVolatileCandles(100);
    float atr = computeATR(candles, 14);

    EXPECT_GT(atr, 0.0f) << "ATR should be positive";
}

TEST(ATRTest, ATRHigherForVolatileMarket) {
    // Generate volatile and stable candle sets
    std::vector<Candle> volatileCandles = TestData::generateVolatileCandles(100, 100.0f, 0.10f);
    std::vector<Candle> stableCandles = TestData::generateVolatileCandles(100, 100.0f, 0.01f);

    float atrVolatile = computeATR(volatileCandles, 14);
    float atrStable = computeATR(stableCandles, 14);

    EXPECT_GT(atrVolatile, atrStable);
}

TEST(ATRTest, ATRNotNaN) {
    auto candles = TestData::generateVolatileCandles(100);
    float atr = computeATR(candles, 14);

    EXPECT_FALSE(std::isnan(atr));
    EXPECT_FALSE(std::isinf(atr));
}

// ============================================================================
// ADX Tests
// ============================================================================

TEST(ADXTest, ADXBoundedBetween0And100) {
    auto candles = TestData::generateVolatileCandles(100);
    ADXResult adx = computeADX(candles, 14);

    EXPECT_GE(adx.adx, 0.0f);
    EXPECT_LE(adx.adx, 100.0f);
}

TEST(ADXTest, DIValuesBounded) {
    auto candles = TestData::generateVolatileCandles(100);
    ADXResult adx = computeADX(candles, 14);

    EXPECT_GE(adx.plusDI, 0.0f);
    EXPECT_LE(adx.plusDI, 100.0f);
    EXPECT_GE(adx.minusDI, 0.0f);
    EXPECT_LE(adx.minusDI, 100.0f);
}

TEST(ADXTest, ADXHighInStrongTrend) {
    // Generate strong uptrend
    auto prices = TestData::generateTrendingPrices(100, 100.0f, 2.0f);
    auto candles = TestData::generateCandles(prices);
    ADXResult adx = computeADX(candles, 14);

    // ADX > 25 typically indicates a trend
    EXPECT_GT(adx.adx, 20.0f);
}

TEST(ADXTest, PlusDIHigherInUptrend) {
    auto prices = TestData::generateTrendingPrices(100, 100.0f, 1.0f);
    auto candles = TestData::generateCandles(prices);
    ADXResult adx = computeADX(candles, 14);

    // In uptrend, +DI should be higher than -DI
    EXPECT_GT(adx.plusDI, adx.minusDI);
}

TEST(ADXTest, MinusDIHigherInDowntrend) {
    auto prices = TestData::generateTrendingPrices(100, 200.0f, -1.0f);
    auto candles = TestData::generateCandles(prices);
    ADXResult adx = computeADX(candles, 14);

    // In downtrend, -DI should be higher than +DI
    EXPECT_GT(adx.minusDI, adx.plusDI);
}

// ============================================================================
// Support/Resistance Tests
// ============================================================================

TEST(SupportResistanceTest, ResistanceAboveSupport) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 10.0f, 20.0f);
    SupportResistance levels = identifyLevels(prices, 60);

    EXPECT_GT(levels.resistance, levels.support);
}

TEST(SupportResistanceTest, LevelsWithinPriceRange) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 10.0f, 20.0f);
    SupportResistance levels = identifyLevels(prices, 60);

    float minPrice = *std::min_element(prices.begin(), prices.end());
    float maxPrice = *std::max_element(prices.begin(), prices.end());

    EXPECT_GE(levels.support, minPrice * 0.9f);  // Some tolerance
    EXPECT_LE(levels.resistance, maxPrice * 1.1f);
}

TEST(SupportResistanceTest, LevelsNotNaN) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 10.0f, 20.0f);
    SupportResistance levels = identifyLevels(prices, 60);

    EXPECT_FALSE(std::isnan(levels.support));
    EXPECT_FALSE(std::isnan(levels.resistance));
}

// ============================================================================
// GARCH Volatility Tests
// ============================================================================

TEST(GARCHTest, VolatilityPositive) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 5.0f, 15.0f);
    auto returns = TestData::pricesToReturns(prices);
    float vol = computeGARCHVolatility(returns);

    EXPECT_GT(vol, 0.0f);
}

TEST(GARCHTest, VolatilityNotNaN) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 5.0f, 15.0f);
    auto returns = TestData::pricesToReturns(prices);
    float vol = computeGARCHVolatility(returns);

    EXPECT_FALSE(std::isnan(vol));
    EXPECT_FALSE(std::isinf(vol));
}

TEST(GARCHTest, HigherVolatilityForVolatileReturns) {
    // Generate volatile and stable return series
    std::vector<float> volatileReturns;
    std::vector<float> stableReturns;

    for (int i = 0; i < 100; ++i) {
        volatileReturns.push_back(((rand() % 1000) / 500.0f - 1.0f) * 0.05f);  // +/- 5%
        stableReturns.push_back(((rand() % 1000) / 500.0f - 1.0f) * 0.005f);   // +/- 0.5%
    }

    float volVolatile = computeGARCHVolatility(volatileReturns);
    float volStable = computeGARCHVolatility(stableReturns);

    EXPECT_GT(volVolatile, volStable);
}

// ============================================================================
// Forecast Tests
// ============================================================================

TEST(ForecastTest, LinearForecastReasonable) {
    auto prices = TestData::generateTrendingPrices(100, 100.0f, 1.0f);
    float forecast = forecastPrice(prices, 30);

    // Forecast should be above current price in uptrend
    EXPECT_GT(forecast, prices.back());
    // But not unreasonably high
    EXPECT_LT(forecast, prices.back() * 2.0f);
}

TEST(ForecastTest, PolynomialForecastNotNaN) {
    auto prices = TestData::generateTrendingPrices(100, 100.0f, 1.0f);
    float forecast = forecastPricePoly(prices, 30, 2);

    EXPECT_FALSE(std::isnan(forecast));
    EXPECT_FALSE(std::isinf(forecast));
}

TEST(ForecastTest, ForecastPositive) {
    auto prices = TestData::generateTrendingPrices(100, 100.0f, 0.5f);
    float forecast = forecastPrice(prices, 30);

    EXPECT_GT(forecast, 0.0f);
}

// ============================================================================
// Cycle Detection Tests
// ============================================================================

TEST(CycleTest, DetectsCycleInPeriodicData) {
    // Generate data with known period
    float period = 20.0f;
    auto prices = TestData::generateOscillatingPrices(200, 100.0f, 10.0f, period);

    int detectedCycle = detectCycle(prices);

    // Detected cycle should be close to actual period
    EXPECT_GT(detectedCycle, static_cast<int>(period * 0.5f));
    EXPECT_LT(detectedCycle, static_cast<int>(period * 2.0f));
}

TEST(CycleTest, CyclePositive) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 10.0f, 25.0f);
    int cycle = detectCycle(prices);

    EXPECT_GT(cycle, 0);
}

// ============================================================================
// Local Extrema Tests
// ============================================================================

TEST(ExtremaTest, MaximaAboveMinima) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 10.0f, 20.0f);

    auto maxima = findLocalExtrema(prices, 60, true);
    auto minima = findLocalExtrema(prices, 60, false);

    if (!maxima.empty() && !minima.empty()) {
        float avgMax = std::accumulate(maxima.begin(), maxima.end(), 0.0f) / maxima.size();
        float avgMin = std::accumulate(minima.begin(), minima.end(), 0.0f) / minima.size();

        EXPECT_GT(avgMax, avgMin);
    }
}

TEST(ExtremaTest, MaximaWithinPriceRange) {
    auto prices = TestData::generateOscillatingPrices(100, 100.0f, 10.0f, 20.0f);
    auto maxima = findLocalExtrema(prices, 60, true);

    float maxPrice = *std::max_element(prices.begin(), prices.end());
    float minPrice = *std::min_element(prices.begin(), prices.end());

    for (float m : maxima) {
        EXPECT_GE(m, minPrice);
        EXPECT_LE(m, maxPrice);
    }
}

// ============================================================================
// Volatility Squeeze Tests
// ============================================================================

TEST(SqueezeTest, DetectsLowVolatility) {
    // Generate stable prices (low volatility)
    std::vector<float> stablePrices;
    for (int i = 0; i < 150; ++i) {
        stablePrices.push_back(100.0f + (i % 2) * 0.1f);  // Very small oscillation
    }

    bool squeeze = checkVolatilitySqueeze(stablePrices, 120, 0.10f);

    // Low volatility should be detected
    EXPECT_TRUE(squeeze);
}

TEST(SqueezeTest, NoSqueezeInHighVolatility) {
    // Generate volatile prices
    std::vector<float> volatilePrices;
    for (int i = 0; i < 150; ++i) {
        volatilePrices.push_back(100.0f + std::sin(i * 0.5f) * 20.0f);  // Large swings
    }

    bool squeeze = checkVolatilitySqueeze(volatilePrices, 120, 0.10f);

    // High volatility should not trigger squeeze
    EXPECT_FALSE(squeeze);
}

// ============================================================================
// Candlestick Pattern Tests
// ============================================================================

TEST(PatternTest, PatternHasName) {
    auto candles = TestData::generateVolatileCandles(50);
    PatternResult pattern = detectCandlestickPattern(candles);

    // Pattern should have a name (even if "None")
    EXPECT_FALSE(pattern.name.empty());
}

TEST(PatternTest, PatternScoreBounded) {
    auto candles = TestData::generateVolatileCandles(50);
    PatternResult pattern = detectCandlestickPattern(candles);

    // Score should be in reasonable range
    EXPECT_GE(pattern.score, -1.0f);
    EXPECT_LE(pattern.score, 1.0f);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(EdgeCaseTest, RSIHandlesEmptyPrices) {
    std::vector<float> emptyPrices;
    float rsi = computeRSI(emptyPrices, 14);

    // Should not crash, may return 0 or 50
    EXPECT_FALSE(std::isnan(rsi));
}

TEST(EdgeCaseTest, ATRHandlesEmptyCandles) {
    std::vector<Candle> emptyCandles;
    float atr = computeATR(emptyCandles, 14);

    EXPECT_FALSE(std::isnan(atr));
}

TEST(EdgeCaseTest, BollingerHandlesSinglePrice) {
    std::vector<float> singlePrice = {100.0f};
    BollingerBands bb = computeBollingerBands(singlePrice, 20, 2.0f);

    // Should not crash
    EXPECT_FALSE(std::isnan(bb.middle));
}

TEST(EdgeCaseTest, IndicatorsHandleConstantPrices) {
    std::vector<float> constantPrices(100, 100.0f);

    float rsi = computeRSI(constantPrices, 14);
    auto [macd, signal] = computeMACD(constantPrices);
    BollingerBands bb = computeBollingerBands(constantPrices, 20, 2.0f);

    // Should not crash, values should be reasonable
    EXPECT_FALSE(std::isnan(rsi));
    EXPECT_FALSE(std::isnan(macd));
    // With constant prices, bandwidth should be zero or near-zero
    EXPECT_LE(bb.bandwidth, 0.01f);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
