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
    double adxThreshold_ = 25.0;  // Minimum ADX for trend confirmation
    bool useMACD_ = false;
    double atrMultiplierForStop_ = 2.5;
    bool signalOnContinuation_ = true;  // Emit signals during established trends

    // Helper to compute SMA
    double computeSMA(const std::vector<double>& prices, int period, int endIdx) const {
        if (endIdx < period - 1) return 0.0;
        double sum = 0.0;
        for (int i = endIdx - period + 1; i <= endIdx; ++i) {
            sum += prices[i];
        }
        return sum / period;
    }

    // Helper to compute EMA
    std::vector<double> computeEMAVector(const std::vector<double>& data, int period) const {
        std::vector<double> ema(data.size(), 0.0);
        if (data.empty() || (int)data.size() < period) return ema;

        double sum = 0.0;
        for (int i = 0; i < period; ++i) sum += data[i];
        ema[period - 1] = sum / period;

        double multiplier = 2.0 / (period + 1.0);
        for (size_t i = period; i < data.size(); ++i) {
            ema[i] = (data[i] - ema[i - 1]) * multiplier + ema[i - 1];
        }
        return ema;
    }

public:
    TrendFollowingStrategy()
        : StrategyBase("TrendFollowing", 60) {
        addParam("fastMAPeriod", 10.0, 5.0, 20.0, 1.0);
        addParam("slowMAPeriod", 30.0, 20.0, 50.0, 5.0);
        addParam("adxThreshold", 25.0, 15.0, 35.0, 5.0);
        addParam("adxPeriod", 14.0, 10.0, 20.0, 2.0);
    }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if ((int)history.size() < slowMAPeriod_ + adxPeriod_ + 5) {
            return StrategySignal::hold("Insufficient data for trend following");
        }

        // Extract close prices
        std::vector<double> closes;
        for (const auto& c : history) closes.push_back(c.close);

        int lastIdx = (int)closes.size() - 1;

        // Calculate moving averages
        double fastMA = computeSMA(closes, fastMAPeriod_, lastIdx);
        double slowMA = computeSMA(closes, slowMAPeriod_, lastIdx);
        double prevFastMA = computeSMA(closes, fastMAPeriod_, lastIdx - 1);
        double prevSlowMA = computeSMA(closes, slowMAPeriod_, lastIdx - 1);

        // Calculate ADX for trend strength
        ADXResult adx = computeADX(history, adxPeriod_);

        // Calculate ATR for stop placement
        double atr = computeATR(history, 14);
        double current = closes.back();

        // Check for MA crossover
        bool bullishCross = (prevFastMA <= prevSlowMA) && (fastMA > slowMA);
        bool bearishCross = (prevFastMA >= prevSlowMA) && (fastMA < slowMA);

        // Current trend state
        bool inUptrend = fastMA > slowMA;
        bool inDowntrend = fastMA < slowMA;

        // Additional MACD confirmation if enabled
        double macdStrength = 0.0;
        if (useMACD_) {
            auto macd = computeMACD(closes);
            double macdLine = macd.first;
            double signalLine = macd.second;
            double histogram = macdLine - signalLine;
            macdStrength = std::tanh(histogram / atr);  // Normalize
        }

        // BUY: Bullish crossover with strong trend
        if (bullishCross && adx.adx >= adxThreshold_) {
            double strength = std::min(1.0, (adx.adx - adxThreshold_) / 25.0 + 0.5);

            // Boost if +DI > -DI (bullish momentum)
            if (adx.plusDI > adx.minusDI) {
                strength = std::min(1.0, strength + 0.2);
            }

            if (useMACD_ && macdStrength > 0) {
                strength = std::min(1.0, strength + macdStrength * 0.2);
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
            double strength = std::min(1.0, (adx.adx - adxThreshold_) / 25.0 + 0.5);

            if (adx.minusDI > adx.plusDI) {
                strength = std::min(1.0, strength + 0.2);
            }

            if (useMACD_ && macdStrength < 0) {
                strength = std::min(1.0, strength + std::abs(macdStrength) * 0.2);
            }

            StrategySignal sig = StrategySignal::sell(strength,
                "MA Cross Down, ADX: " + std::to_string((int)adx.adx) +
                ", -DI: " + std::to_string((int)adx.minusDI));

            sig.stopLossPrice = current + (atr * atrMultiplierForStop_);
            sig.takeProfitPrice = 0;
            sig.confidence = strength;

            return sig;
        }

        // Trend continuation: signal during established trends (not just crossover)
        if (signalOnContinuation_ && adx.adx >= adxThreshold_) {
            // Scale strength by ADX: stronger trend → stronger signal, capped at 0.5
            double contStrength = std::min(0.5, 0.3 + (adx.adx - adxThreshold_) / 100.0);

            if (inUptrend && adx.plusDI > adx.minusDI) {
                StrategySignal sig = StrategySignal::buy(contStrength,
                    "Trend Continuation Up, ADX: " + std::to_string((int)adx.adx) +
                    ", +DI: " + std::to_string((int)adx.plusDI));
                sig.stopLossPrice = current - (atr * atrMultiplierForStop_);
                sig.takeProfitPrice = 0;  // Let trend run
                sig.confidence = contStrength;
                return sig;
            }

            if (inDowntrend && adx.minusDI > adx.plusDI) {
                StrategySignal sig = StrategySignal::sell(contStrength,
                    "Trend Continuation Down, ADX: " + std::to_string((int)adx.adx) +
                    ", -DI: " + std::to_string((int)adx.minusDI));
                sig.stopLossPrice = current + (atr * atrMultiplierForStop_);
                sig.takeProfitPrice = 0;
                sig.confidence = contStrength;
                return sig;
            }
        }

        // Exit existing positions if trend weakens significantly
        if (adx.adx < adxThreshold_ * 0.7) {
            if (inUptrend) {
                // Weak uptrend, consider taking profits
                return StrategySignal::sell(0.3, "Trend weakening, ADX: " + std::to_string((int)adx.adx));
            } else if (inDowntrend) {
                return StrategySignal::buy(0.3, "Trend weakening, ADX: " + std::to_string((int)adx.adx));
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
        copy->signalOnContinuation_ = signalOnContinuation_;
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
    void setADXParams(int period, double threshold) {
        adxPeriod_ = period;
        adxThreshold_ = threshold;
    }
    void setUseMACD(bool use) { useMACD_ = use; }
    void setATRStopMultiplier(double mult) { atrMultiplierForStop_ = mult; }
};

// Triple Moving Average Strategy (more conservative)
class TripleMAStrategy : public StrategyBase {
private:
    int fastPeriod_ = 5;
    int mediumPeriod_ = 20;
    int slowPeriod_ = 50;
    int adxPeriod_ = 14;
    double adxThreshold_ = 20.0;

    double computeSMA(const std::vector<double>& prices, int period, int endIdx) const {
        if (endIdx < period - 1) return 0.0;
        double sum = 0.0;
        for (int i = endIdx - period + 1; i <= endIdx; ++i) {
            sum += prices[i];
        }
        return sum / period;
    }

public:
    TripleMAStrategy()
        : StrategyBase("TripleMA", 80) {
        addParam("fastPeriod", 5.0, 3.0, 10.0, 1.0);
        addParam("mediumPeriod", 20.0, 10.0, 30.0, 5.0);
        addParam("slowPeriod", 50.0, 40.0, 100.0, 10.0);
    }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if ((int)history.size() < slowPeriod_ + 10) {
            return StrategySignal::hold("Insufficient data");
        }

        std::vector<double> closes;
        for (const auto& c : history) closes.push_back(c.close);

        int lastIdx = (int)closes.size() - 1;

        double fastMA = computeSMA(closes, fastPeriod_, lastIdx);
        double mediumMA = computeSMA(closes, mediumPeriod_, lastIdx);
        double slowMA = computeSMA(closes, slowPeriod_, lastIdx);

        ADXResult adx = computeADX(history, adxPeriod_);
        double atr = computeATR(history, 14);
        double current = closes.back();

        // Strong uptrend: fast > medium > slow
        bool strongUptrend = (fastMA > mediumMA) && (mediumMA > slowMA);
        // Strong downtrend: fast < medium < slow
        bool strongDowntrend = (fastMA < mediumMA) && (mediumMA < slowMA);

        // Check for alignment
        if (strongUptrend && adx.adx >= adxThreshold_) {
            double strength = std::min(1.0, adx.adx / 50.0);
            StrategySignal sig = StrategySignal::buy(strength,
                "Triple MA aligned bullish, ADX: " + std::to_string((int)adx.adx));
            sig.stopLossPrice = current - (atr * 2.5);
            return sig;
        }

        if (strongDowntrend && adx.adx >= adxThreshold_) {
            double strength = std::min(1.0, adx.adx / 50.0);
            StrategySignal sig = StrategySignal::sell(strength,
                "Triple MA aligned bearish, ADX: " + std::to_string((int)adx.adx));
            sig.stopLossPrice = current + (atr * 2.5);
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
