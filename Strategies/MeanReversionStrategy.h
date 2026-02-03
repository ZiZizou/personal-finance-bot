#pragma once
#include "../IStrategy.h"
#include "../TechnicalAnalysis.h"
#include <cmath>
#include <numeric>

// Mean Reversion Strategy
// Buys when price is oversold (low RSI or below Bollinger Band lower)
// Sells when price is overbought (high RSI or above Bollinger Band upper)
class MeanReversionStrategy : public StrategyBase {
private:
    // Strategy parameters
    int rsiPeriod_ = 14;
    float rsiBuyThreshold_ = 30.0f;
    float rsiSellThreshold_ = 70.0f;
    int bbPeriod_ = 20;
    float bbMultiplier_ = 2.0f;
    bool useAdaptiveRSI_ = false;
    float atrMultiplierForStop_ = 2.0f;

public:
    MeanReversionStrategy()
        : StrategyBase("MeanReversion", 60) {
        // Define optimizable parameters
        addParam("rsiPeriod", 14.0f, 7.0f, 28.0f, 1.0f);
        addParam("rsiBuyThreshold", 30.0f, 20.0f, 40.0f, 5.0f);
        addParam("rsiSellThreshold", 70.0f, 60.0f, 80.0f, 5.0f);
        addParam("bbPeriod", 20.0f, 10.0f, 30.0f, 5.0f);
        addParam("bbMultiplier", 2.0f, 1.5f, 3.0f, 0.25f);
    }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if (history.size() < 50) {
            return StrategySignal::hold("Insufficient data");
        }

        // Extract close prices
        std::vector<float> closes;
        for (const auto& c : history) closes.push_back(c.close);

        // Calculate indicators
        BollingerBands bb = computeBollingerBands(closes, bbPeriod_, bbMultiplier_);
        float rsi = useAdaptiveRSI_ ?
                    computeAdaptiveRSI(closes, rsiPeriod_) :
                    computeRSI(closes, rsiPeriod_);
        float current = closes.back();

        // Calculate ATR for stop-loss placement
        float atr = computeATR(history, 14);

        // Mean Reversion Logic
        // Buy conditions: Oversold
        if (rsi < rsiBuyThreshold_ || current < bb.lower) {
            float strength = 0.5f;

            // Stronger signal if both conditions met
            if (rsi < rsiBuyThreshold_ && current < bb.lower) {
                strength = 0.8f;
            }

            // Very strong if extremely oversold
            if (rsi < 20.0f) {
                strength = 1.0f;
            }

            StrategySignal sig = StrategySignal::buy(strength,
                "RSI: " + std::to_string((int)rsi) + ", Below BB Lower");

            // Set stop-loss and take-profit
            sig.stopLossPrice = current - (atr * atrMultiplierForStop_);
            sig.takeProfitPrice = bb.middle;  // Target the mean
            sig.confidence = strength;

            return sig;
        }

        // Sell conditions: Overbought
        if (rsi > rsiSellThreshold_ || current > bb.upper) {
            float strength = 0.5f;

            if (rsi > rsiSellThreshold_ && current > bb.upper) {
                strength = 0.8f;
            }

            if (rsi > 80.0f) {
                strength = 1.0f;
            }

            StrategySignal sig = StrategySignal::sell(strength,
                "RSI: " + std::to_string((int)rsi) + ", Above BB Upper");

            sig.stopLossPrice = current + (atr * atrMultiplierForStop_);
            sig.takeProfitPrice = bb.middle;
            sig.confidence = strength;

            return sig;
        }

        return StrategySignal::hold("No mean reversion signal");
    }

    std::unique_ptr<IStrategy> clone() const override {
        auto copy = std::make_unique<MeanReversionStrategy>();
        copy->rsiPeriod_ = rsiPeriod_;
        copy->rsiBuyThreshold_ = rsiBuyThreshold_;
        copy->rsiSellThreshold_ = rsiSellThreshold_;
        copy->bbPeriod_ = bbPeriod_;
        copy->bbMultiplier_ = bbMultiplier_;
        copy->useAdaptiveRSI_ = useAdaptiveRSI_;
        copy->params_ = params_;
        return copy;
    }

protected:
    void onParametersChanged() override {
        rsiPeriod_ = (int)getParamValue("rsiPeriod");
        rsiBuyThreshold_ = getParamValue("rsiBuyThreshold");
        rsiSellThreshold_ = getParamValue("rsiSellThreshold");
        bbPeriod_ = (int)getParamValue("bbPeriod");
        bbMultiplier_ = getParamValue("bbMultiplier");
    }

public:
    // Setters for direct configuration
    void setRSIPeriod(int period) { rsiPeriod_ = period; }
    void setRSIThresholds(float buy, float sell) {
        rsiBuyThreshold_ = buy;
        rsiSellThreshold_ = sell;
    }
    void setBollingerParams(int period, float multiplier) {
        bbPeriod_ = period;
        bbMultiplier_ = multiplier;
    }
    void setUseAdaptiveRSI(bool use) { useAdaptiveRSI_ = use; }
    void setATRStopMultiplier(float mult) { atrMultiplierForStop_ = mult; }
};

// Enhanced Mean Reversion with additional filters
class EnhancedMeanReversionStrategy : public MeanReversionStrategy {
private:
    bool useVolumeFilter_ = true;
    float volumeThreshold_ = 1.2f;  // Volume > 120% of average
    bool useTrendFilter_ = true;
    int trendMAPeriod_ = 50;

public:
    EnhancedMeanReversionStrategy() {
        name_ = "EnhancedMeanReversion";
        warmupPeriod_ = 100;
        addParam("volumeThreshold", 1.2f, 1.0f, 2.0f, 0.1f);
        addParam("trendMAPeriod", 50.0f, 20.0f, 100.0f, 10.0f);
    }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        // First get base signal
        StrategySignal baseSig = MeanReversionStrategy::generateSignal(history, idx);

        if (baseSig.type == SignalType::Hold) {
            return baseSig;
        }

        // Apply volume filter
        if (useVolumeFilter_ && history.size() >= 20) {
            long long avgVolume = 0;
            for (size_t i = history.size() - 20; i < history.size(); ++i) {
                avgVolume += history[i].volume;
            }
            avgVolume /= 20;

            long long currentVolume = history.back().volume;
            if (currentVolume < avgVolume * volumeThreshold_) {
                return StrategySignal::hold("Volume filter: insufficient volume confirmation");
            }
        }

        // Apply trend filter (don't buy in downtrend, don't sell in uptrend)
        if (useTrendFilter_ && history.size() >= (size_t)trendMAPeriod_) {
            float maSum = 0.0f;
            for (size_t i = history.size() - trendMAPeriod_; i < history.size(); ++i) {
                maSum += history[i].close;
            }
            float ma = maSum / trendMAPeriod_;
            float current = history.back().close;

            // In uptrend: only take buy signals
            if (current > ma && baseSig.type == SignalType::Sell) {
                baseSig.strength *= 0.5f;  // Reduce sell signal strength in uptrend
                baseSig.reason += " (reduced: uptrend)";
            }

            // In downtrend: only take sell signals
            if (current < ma && baseSig.type == SignalType::Buy) {
                baseSig.strength *= 0.5f;  // Reduce buy signal strength in downtrend
                baseSig.reason += " (reduced: downtrend)";
            }
        }

        return baseSig;
    }

    std::unique_ptr<IStrategy> clone() const override {
        auto copy = std::make_unique<EnhancedMeanReversionStrategy>();
        copy->useVolumeFilter_ = useVolumeFilter_;
        copy->volumeThreshold_ = volumeThreshold_;
        copy->useTrendFilter_ = useTrendFilter_;
        copy->trendMAPeriod_ = trendMAPeriod_;
        copy->params_ = params_;
        return copy;
    }

protected:
    void onParametersChanged() override {
        MeanReversionStrategy::onParametersChanged();
        volumeThreshold_ = getParamValue("volumeThreshold");
        trendMAPeriod_ = (int)getParamValue("trendMAPeriod");
    }
};
