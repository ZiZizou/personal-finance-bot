#pragma once
#include "../IStrategy.h"
#include "../TechnicalAnalysis.h"
#include <cmath>
#include <numeric>

// Trend Following Strategy
// Uses MA crossover with ADX filter for trend strength
class TrendFollowingStrategy : public StrategyBase {
private:
    int fastMAPeriod_ = 10;
    int slowMAPeriod_ = 30;
    int adxPeriod_ = 14;
    float adxThreshold_ = 25.0f;  // Minimum ADX for trend confirmation
    bool useMACD_ = false;
    float atrMultiplierForStop_ = 2.5f;

    // Helper to compute SMA
    float computeSMA(const std::vector<float>& prices, int period, int endIdx) const {
        if (endIdx < period - 1) return 0.0f;
        float sum = 0.0f;
        for (int i = endIdx - period + 1; i <= endIdx; ++i) {
            sum += prices[i];
        }
        return sum / period;
    }

    // Helper to compute EMA
    std::vector<float> computeEMAVector(const std::vector<float>& data, int period) const {
        std::vector<float> ema(data.size(), 0.0f);
        if (data.empty() || (int)data.size() < period) return ema;

        float sum = 0.0f;
        for (int i = 0; i < period; ++i) sum += data[i];
        ema[period - 1] = sum / period;

        float multiplier = 2.0f / (period + 1.0f);
        for (size_t i = period; i < data.size(); ++i) {
            ema[i] = (data[i] - ema[i - 1]) * multiplier + ema[i - 1];
        }
        return ema;
    }

public:
    TrendFollowingStrategy()
        : StrategyBase("TrendFollowing", 60) {
        addParam("fastMAPeriod", 10.0f, 5.0f, 20.0f, 1.0f);
        addParam("slowMAPeriod", 30.0f, 20.0f, 50.0f, 5.0f);
        addParam("adxThreshold", 25.0f, 15.0f, 35.0f, 5.0f);
        addParam("adxPeriod", 14.0f, 10.0f, 20.0f, 2.0f);
    }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if ((int)history.size() < slowMAPeriod_ + adxPeriod_ + 5) {
            return StrategySignal::hold("Insufficient data for trend following");
        }

        // Extract close prices
        std::vector<float> closes;
        for (const auto& c : history) closes.push_back(c.close);

        int lastIdx = (int)closes.size() - 1;

        // Calculate moving averages
        float fastMA = computeSMA(closes, fastMAPeriod_, lastIdx);
        float slowMA = computeSMA(closes, slowMAPeriod_, lastIdx);
        float prevFastMA = computeSMA(closes, fastMAPeriod_, lastIdx - 1);
        float prevSlowMA = computeSMA(closes, slowMAPeriod_, lastIdx - 1);

        // Calculate ADX for trend strength
        ADXResult adx = computeADX(history, adxPeriod_);

        // Calculate ATR for stop placement
        float atr = computeATR(history, 14);
        float current = closes.back();

        // Check for MA crossover
        bool bullishCross = (prevFastMA <= prevSlowMA) && (fastMA > slowMA);
        bool bearishCross = (prevFastMA >= prevSlowMA) && (fastMA < slowMA);

        // Current trend state
        bool inUptrend = fastMA > slowMA;
        bool inDowntrend = fastMA < slowMA;

        // Additional MACD confirmation if enabled
        float macdStrength = 0.0f;
        if (useMACD_) {
            auto macd = computeMACD(closes);
            float macdLine = macd.first;
            float signalLine = macd.second;
            float histogram = macdLine - signalLine;
            macdStrength = std::tanh(histogram / atr);  // Normalize
        }

        // BUY: Bullish crossover with strong trend
        if (bullishCross && adx.adx >= adxThreshold_) {
            float strength = std::min(1.0f, (adx.adx - adxThreshold_) / 25.0f + 0.5f);

            // Boost if +DI > -DI (bullish momentum)
            if (adx.plusDI > adx.minusDI) {
                strength = std::min(1.0f, strength + 0.2f);
            }

            if (useMACD_ && macdStrength > 0) {
                strength = std::min(1.0f, strength + macdStrength * 0.2f);
            }

            StrategySignal sig = StrategySignal::buy(strength,
                "MA Cross Up, ADX: " + std::to_string((int)adx.adx) +
                ", +DI: " + std::to_string((int)adx.plusDI));

            sig.stopLossPrice = current - (atr * atrMultiplierForStop_);
            sig.takeProfitPrice = 0;  // Let trend run, use trailing stop instead
            sig.confidence = strength;

            return sig;
        }

        // SELL: Bearish crossover with strong trend
        if (bearishCross && adx.adx >= adxThreshold_) {
            float strength = std::min(1.0f, (adx.adx - adxThreshold_) / 25.0f + 0.5f);

            if (adx.minusDI > adx.plusDI) {
                strength = std::min(1.0f, strength + 0.2f);
            }

            if (useMACD_ && macdStrength < 0) {
                strength = std::min(1.0f, strength + std::abs(macdStrength) * 0.2f);
            }

            StrategySignal sig = StrategySignal::sell(strength,
                "MA Cross Down, ADX: " + std::to_string((int)adx.adx) +
                ", -DI: " + std::to_string((int)adx.minusDI));

            sig.stopLossPrice = current + (atr * atrMultiplierForStop_);
            sig.takeProfitPrice = 0;
            sig.confidence = strength;

            return sig;
        }

        // Exit existing positions if trend weakens significantly
        if (adx.adx < adxThreshold_ * 0.7f) {
            if (inUptrend) {
                // Weak uptrend, consider taking profits
                return StrategySignal::sell(0.3f, "Trend weakening, ADX: " + std::to_string((int)adx.adx));
            } else if (inDowntrend) {
                return StrategySignal::buy(0.3f, "Trend weakening, ADX: " + std::to_string((int)adx.adx));
            }
        }

        return StrategySignal::hold("No trend signal");
    }

    std::unique_ptr<IStrategy> clone() const override {
        auto copy = std::make_unique<TrendFollowingStrategy>();
        copy->fastMAPeriod_ = fastMAPeriod_;
        copy->slowMAPeriod_ = slowMAPeriod_;
        copy->adxPeriod_ = adxPeriod_;
        copy->adxThreshold_ = adxThreshold_;
        copy->useMACD_ = useMACD_;
        copy->atrMultiplierForStop_ = atrMultiplierForStop_;
        copy->params_ = params_;
        return copy;
    }

protected:
    void onParametersChanged() override {
        fastMAPeriod_ = (int)getParamValue("fastMAPeriod");
        slowMAPeriod_ = (int)getParamValue("slowMAPeriod");
        adxThreshold_ = getParamValue("adxThreshold");
        adxPeriod_ = (int)getParamValue("adxPeriod");
    }

public:
    void setMAPeriods(int fast, int slow) {
        fastMAPeriod_ = fast;
        slowMAPeriod_ = slow;
    }
    void setADXParams(int period, float threshold) {
        adxPeriod_ = period;
        adxThreshold_ = threshold;
    }
    void setUseMACD(bool use) { useMACD_ = use; }
    void setATRStopMultiplier(float mult) { atrMultiplierForStop_ = mult; }
};

// Triple Moving Average Strategy (more conservative)
class TripleMAStrategy : public StrategyBase {
private:
    int fastPeriod_ = 5;
    int mediumPeriod_ = 20;
    int slowPeriod_ = 50;
    int adxPeriod_ = 14;
    float adxThreshold_ = 20.0f;

    float computeSMA(const std::vector<float>& prices, int period, int endIdx) const {
        if (endIdx < period - 1) return 0.0f;
        float sum = 0.0f;
        for (int i = endIdx - period + 1; i <= endIdx; ++i) {
            sum += prices[i];
        }
        return sum / period;
    }

public:
    TripleMAStrategy()
        : StrategyBase("TripleMA", 80) {
        addParam("fastPeriod", 5.0f, 3.0f, 10.0f, 1.0f);
        addParam("mediumPeriod", 20.0f, 10.0f, 30.0f, 5.0f);
        addParam("slowPeriod", 50.0f, 40.0f, 100.0f, 10.0f);
    }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if ((int)history.size() < slowPeriod_ + 10) {
            return StrategySignal::hold("Insufficient data");
        }

        std::vector<float> closes;
        for (const auto& c : history) closes.push_back(c.close);

        int lastIdx = (int)closes.size() - 1;

        float fastMA = computeSMA(closes, fastPeriod_, lastIdx);
        float mediumMA = computeSMA(closes, mediumPeriod_, lastIdx);
        float slowMA = computeSMA(closes, slowPeriod_, lastIdx);

        ADXResult adx = computeADX(history, adxPeriod_);
        float atr = computeATR(history, 14);
        float current = closes.back();

        // Strong uptrend: fast > medium > slow
        bool strongUptrend = (fastMA > mediumMA) && (mediumMA > slowMA);
        // Strong downtrend: fast < medium < slow
        bool strongDowntrend = (fastMA < mediumMA) && (mediumMA < slowMA);

        // Check for alignment
        if (strongUptrend && adx.adx >= adxThreshold_) {
            float strength = std::min(1.0f, adx.adx / 50.0f);
            StrategySignal sig = StrategySignal::buy(strength,
                "Triple MA aligned bullish, ADX: " + std::to_string((int)adx.adx));
            sig.stopLossPrice = current - (atr * 2.5f);
            return sig;
        }

        if (strongDowntrend && adx.adx >= adxThreshold_) {
            float strength = std::min(1.0f, adx.adx / 50.0f);
            StrategySignal sig = StrategySignal::sell(strength,
                "Triple MA aligned bearish, ADX: " + std::to_string((int)adx.adx));
            sig.stopLossPrice = current + (atr * 2.5f);
            return sig;
        }

        return StrategySignal::hold("MAs not aligned");
    }

    std::unique_ptr<IStrategy> clone() const override {
        auto copy = std::make_unique<TripleMAStrategy>();
        copy->fastPeriod_ = fastPeriod_;
        copy->mediumPeriod_ = mediumPeriod_;
        copy->slowPeriod_ = slowPeriod_;
        copy->adxPeriod_ = adxPeriod_;
        copy->adxThreshold_ = adxThreshold_;
        copy->params_ = params_;
        return copy;
    }

protected:
    void onParametersChanged() override {
        fastPeriod_ = (int)getParamValue("fastPeriod");
        mediumPeriod_ = (int)getParamValue("mediumPeriod");
        slowPeriod_ = (int)getParamValue("slowPeriod");
    }
};
