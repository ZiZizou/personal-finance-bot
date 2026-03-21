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
    double rsiBuyThreshold_ = 30.0;
    double rsiSellThreshold_ = 70.0;
    int bbPeriod_ = 20;
    double bbMultiplier_ = 2.0;
    bool useAdaptiveRSI_ = false;
    double atrMultiplierForStop_ = 2.0;

    // Squeeze tracking
    bool prevSqueeze_ = false;

    // Candlestick confirmation
    bool useCandlestickConfirmation_ = true;

public:
    MeanReversionStrategy()
        : StrategyBase("MeanReversion", 60) {
        // Define optimizable parameters
        addParam("rsiPeriod", 14.0, 7.0, 28.0, 1.0);
        addParam("rsiBuyThreshold", 30.0, 20.0, 40.0, 5.0);
        addParam("rsiSellThreshold", 70.0, 60.0, 80.0, 5.0);
        addParam("bbPeriod", 20.0, 10.0, 30.0, 5.0);
        addParam("bbMultiplier", 2.0, 1.5, 3.0, 0.25);
    }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if (history.size() < 50) {
            return StrategySignal::hold("Insufficient data");
        }

        // Extract close prices
        std::vector<double> closes;
        for (const auto& c : history) closes.push_back(c.close);

        // Calculate indicators
        BollingerBands bb = computeBollingerBands(closes, bbPeriod_, bbMultiplier_);
        double rsi = useAdaptiveRSI_ ?
                    computeAdaptiveRSI(closes, rsiPeriod_) :
                    computeRSI(closes, rsiPeriod_);
        double current = closes.back();

        // Calculate ATR for stop-loss placement
        double atr = computeATR(history, 14);

        // Check volatility squeeze state
        bool currentSqueeze = (closes.size() >= 120) ?
            checkVolatilitySqueeze(closes) : false;
        bool squeezeBreakout = (prevSqueeze_ && !currentSqueeze);
        prevSqueeze_ = currentSqueeze;

        // Mean Reversion Logic
        // Buy conditions: Oversold
        if (rsi < rsiBuyThreshold_ || current < bb.lower) {
            double strength = 0.5;
            std::string reason = "RSI: " + std::to_string((int)rsi) + ", Below BB Lower";

            // Stronger signal if both conditions met
            if (rsi < rsiBuyThreshold_ && current < bb.lower) {
                strength = 0.8;
            }

            // Very strong if extremely oversold
            if (rsi < 20.0) {
                strength = 1.0;
            }

            // Volatility squeeze boost
            if (currentSqueeze) {
                strength = std::min(1.0, strength + 0.2);
                reason += ", Squeeze";
            } else if (squeezeBreakout) {
                strength = std::min(1.0, strength + 0.3);
                reason += ", SqueezeBreakout";
            }

            // Candlestick pattern confirmation
            if (useCandlestickConfirmation_ && history.size() >= 3) {
                PatternResult pattern = detectCandlestickPattern(history);
                if (!pattern.name.empty()) {
                    if (pattern.score > 0) {
                        // Confirming bullish pattern
                        strength = std::min(1.0, strength + 0.15);
                        reason += ", " + pattern.name;
                    } else if (pattern.score < 0) {
                        // Opposing bearish pattern
                        strength = std::max(0.0, strength - 0.1);
                        reason += ", opposing " + pattern.name;
                    }
                }
            }

            StrategySignal sig = StrategySignal::buy(strength, reason);

            // Set stop-loss and take-profit
            sig.stopLossPrice = current - (atr * atrMultiplierForStop_);
            sig.takeProfitPrice = bb.middle;  // Target the mean
            sig.confidence = strength;

            return sig;
        }

        // Sell conditions: Overbought
        if (rsi > rsiSellThreshold_ || current > bb.upper) {
            double strength = 0.5;
            std::string reason = "RSI: " + std::to_string((int)rsi) + ", Above BB Upper";

            if (rsi > rsiSellThreshold_ && current > bb.upper) {
                strength = 0.8;
            }

            if (rsi > 80.0) {
                strength = 1.0;
            }

            // Volatility squeeze boost
            if (currentSqueeze) {
                strength = std::min(1.0, strength + 0.2);
                reason += ", Squeeze";
            } else if (squeezeBreakout) {
                strength = std::min(1.0, strength + 0.3);
                reason += ", SqueezeBreakout";
            }

            // Candlestick pattern confirmation
            if (useCandlestickConfirmation_ && history.size() >= 3) {
                PatternResult pattern = detectCandlestickPattern(history);
                if (!pattern.name.empty()) {
                    if (pattern.score < 0) {
                        // Confirming bearish pattern
                        strength = std::min(1.0, strength + 0.15);
                        reason += ", " + pattern.name;
                    } else if (pattern.score > 0) {
                        // Opposing bullish pattern
                        strength = std::max(0.0, strength - 0.1);
                        reason += ", opposing " + pattern.name;
                    }
                }
            }

            StrategySignal sig = StrategySignal::sell(strength, reason);

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
        copy->prevSqueeze_ = prevSqueeze_;
        copy->useCandlestickConfirmation_ = useCandlestickConfirmation_;
        copy->params_ = params_;
        return copy;
    }

    void reset() override {
        prevSqueeze_ = false;
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
    void setRSIThresholds(double buy, double sell) {
        rsiBuyThreshold_ = buy;
        rsiSellThreshold_ = sell;
    }
    void setBollingerParams(int period, double multiplier) {
        bbPeriod_ = period;
        bbMultiplier_ = multiplier;
    }
    void setUseAdaptiveRSI(bool use) { useAdaptiveRSI_ = use; }
    void setATRStopMultiplier(double mult) { atrMultiplierForStop_ = mult; }
};

// Enhanced Mean Reversion with additional filters
class EnhancedMeanReversionStrategy : public MeanReversionStrategy {
private:
    bool useVolumeFilter_ = true;
    double volumeThreshold_ = 1.2;  // Volume > 120% of average
    bool useTrendFilter_ = true;
    int trendMAPeriod_ = 50;

public:
    EnhancedMeanReversionStrategy() {
        name_ = "EnhancedMeanReversion";
        warmupPeriod_ = 100;
        addParam("volumeThreshold", 1.2, 1.0, 2.0, 0.1);
        addParam("trendMAPeriod", 50.0, 20.0, 100.0, 10.0);
    }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        // First get base signal
        StrategySignal baseSig = MeanReversionStrategy::generateSignal(history, idx);

        if (baseSig.type == SignalType::Hold) {
            return baseSig;
        }

        // Apply volume filter
        if (useVolumeFilter_ && history.size() >= 20) {
            int64_t avgVolume = 0;
            for (size_t i = history.size() - 20; i < history.size(); ++i) {
                avgVolume += history[i].volume;
            }
            avgVolume /= 20;

            int64_t currentVolume = history.back().volume;
            if (currentVolume < static_cast<int64_t>(avgVolume * volumeThreshold_)) {
                return StrategySignal::hold("Volume filter: insufficient volume confirmation");
            }
        }

        // Apply trend filter (don't buy in downtrend, don't sell in uptrend)
        if (useTrendFilter_ && history.size() >= (size_t)trendMAPeriod_) {
            double maSum = 0.0;
            for (size_t i = history.size() - trendMAPeriod_; i < history.size(); ++i) {
                maSum += history[i].close;
            }
            double ma = maSum / trendMAPeriod_;
            double current = history.back().close;

            // In uptrend: only take buy signals
            if (current > ma && baseSig.type == SignalType::Sell) {
                baseSig.strength *= 0.5;  // Reduce sell signal strength in uptrend
                baseSig.reason += " (reduced: uptrend)";
            }

            // In downtrend: only take sell signals
            if (current < ma && baseSig.type == SignalType::Buy) {
                baseSig.strength *= 0.5;  // Reduce buy signal strength in downtrend
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
