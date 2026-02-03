#pragma once
#include "../IStrategy.h"
#include "../MLPredictor.h"
#include "../TechnicalAnalysis.h"
#include <cmath>
#include <memory>

// Machine Learning based Strategy
// Wraps the MLPredictor for use with the backtester
class MLStrategy : public StrategyBase {
private:
    MLPredictor& predictor_;
    float buyThreshold_ = 0.005f;     // 0.5% predicted return to buy
    float sellThreshold_ = -0.005f;   // -0.5% predicted return to sell
    float confidenceThreshold_ = 0.3f;
    int cyclePeriod_ = 30;
    bool trainOnline_ = false;
    float lastPrediction_ = 0.0f;
    float lastActualReturn_ = 0.0f;
    size_t lastTrainIdx_ = 0;

public:
    MLStrategy(MLPredictor& predictor)
        : StrategyBase("MLStrategy", 60), predictor_(predictor) {
        addParam("buyThreshold", 0.005f, 0.001f, 0.02f, 0.001f);
        addParam("sellThreshold", -0.005f, -0.02f, -0.001f, 0.001f);
        addParam("confidenceThreshold", 0.3f, 0.1f, 0.8f, 0.1f);
    }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if (history.size() < 60) {
            return StrategySignal::hold("Insufficient data for ML prediction");
        }

        // Extract features
        std::vector<float> closes;
        for (const auto& c : history) closes.push_back(c.close);

        float rsi = computeRSI(closes, 14);
        auto macd = computeMACD(closes);
        float macdHist = macd.first - macd.second;

        // Calculate log returns for GARCH
        std::vector<float> returns;
        for (size_t i = 1; i < closes.size(); ++i) {
            returns.push_back(std::log(closes[i] / closes[i-1]));
        }
        float garchVol = computeGARCHVolatility(returns);

        // Detect cycle
        int detectedCycle = detectCycle(closes);
        if (detectedCycle > 0) cyclePeriod_ = detectedCycle;

        // Use external sentiment (default to neutral if not available)
        float sentiment = 0.0f;

        // Extract features using predictor's method
        std::vector<float> features = predictor_.extractFeatures(
            rsi, macdHist, sentiment, garchVol, cyclePeriod_, (int)idx);

        // Get prediction
        float prediction = predictor_.predict(features);

        // Online training: train on previous prediction if available
        if (trainOnline_ && idx > lastTrainIdx_ + 1) {
            float actualReturn = (closes.back() - closes[closes.size() - 2]) / closes[closes.size() - 2];
            predictor_.train(features, actualReturn);
            lastActualReturn_ = actualReturn;
        }
        lastTrainIdx_ = idx;
        lastPrediction_ = prediction;

        // Calculate confidence based on prediction magnitude and technical alignment
        float confidence = std::abs(prediction) / 0.02f;  // Normalize to ~1 at 2% prediction
        confidence = std::min(1.0f, confidence);

        // Generate signal based on prediction
        if (prediction > buyThreshold_ && confidence >= confidenceThreshold_) {
            float strength = std::min(1.0f, prediction / 0.02f);

            // Boost confidence if technicals agree
            if (rsi < 40.0f) {
                confidence = std::min(1.0f, confidence + 0.1f);
            }

            StrategySignal sig = StrategySignal::buy(strength,
                "ML Prediction: " + std::to_string(prediction * 100) + "% expected return");

            float atr = computeATR(history, 14);
            sig.stopLossPrice = closes.back() - (atr * 2.0f);
            sig.takeProfitPrice = closes.back() * (1.0f + prediction * 2);  // 2x predicted move
            sig.confidence = confidence;

            return sig;
        }

        if (prediction < sellThreshold_ && confidence >= confidenceThreshold_) {
            float strength = std::min(1.0f, std::abs(prediction) / 0.02f);

            if (rsi > 60.0f) {
                confidence = std::min(1.0f, confidence + 0.1f);
            }

            StrategySignal sig = StrategySignal::sell(strength,
                "ML Prediction: " + std::to_string(prediction * 100) + "% expected return");

            float atr = computeATR(history, 14);
            sig.stopLossPrice = closes.back() + (atr * 2.0f);
            sig.takeProfitPrice = closes.back() * (1.0f + prediction * 2);
            sig.confidence = confidence;

            return sig;
        }

        return StrategySignal::hold("ML prediction below threshold: " +
            std::to_string(prediction * 100) + "%");
    }

    std::unique_ptr<IStrategy> clone() const override {
        // Note: Clone shares the same predictor reference
        auto copy = std::make_unique<MLStrategy>(predictor_);
        copy->buyThreshold_ = buyThreshold_;
        copy->sellThreshold_ = sellThreshold_;
        copy->confidenceThreshold_ = confidenceThreshold_;
        copy->trainOnline_ = trainOnline_;
        copy->params_ = params_;
        return copy;
    }

    void reset() override {
        lastPrediction_ = 0.0f;
        lastActualReturn_ = 0.0f;
        lastTrainIdx_ = 0;
    }

protected:
    void onParametersChanged() override {
        buyThreshold_ = getParamValue("buyThreshold");
        sellThreshold_ = getParamValue("sellThreshold");
        confidenceThreshold_ = getParamValue("confidenceThreshold");
    }

public:
    void setBuyThreshold(float t) { buyThreshold_ = t; }
    void setSellThreshold(float t) { sellThreshold_ = t; }
    void setConfidenceThreshold(float t) { confidenceThreshold_ = t; }
    void setTrainOnline(bool train) { trainOnline_ = train; }

    float getLastPrediction() const { return lastPrediction_; }
    float getLastActualReturn() const { return lastActualReturn_; }
};

// Combined ML + Technical Strategy
class HybridMLStrategy : public StrategyBase {
private:
    MLPredictor& predictor_;
    float mlWeight_ = 0.6f;          // Weight given to ML signal
    float technicalWeight_ = 0.4f;   // Weight given to technical signal
    float combinedThreshold_ = 0.4f; // Minimum combined score to trade

public:
    HybridMLStrategy(MLPredictor& predictor)
        : StrategyBase("HybridML", 60), predictor_(predictor) {
        addParam("mlWeight", 0.6f, 0.3f, 0.9f, 0.1f);
        addParam("combinedThreshold", 0.4f, 0.2f, 0.7f, 0.1f);
    }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if (history.size() < 60) {
            return StrategySignal::hold("Insufficient data");
        }

        std::vector<float> closes;
        for (const auto& c : history) closes.push_back(c.close);

        // === ML Signal ===
        float rsi = computeRSI(closes, 14);
        auto macd = computeMACD(closes);
        float macdHist = macd.first - macd.second;

        std::vector<float> returns;
        for (size_t i = 1; i < closes.size(); ++i) {
            returns.push_back(std::log(closes[i] / closes[i-1]));
        }
        float garchVol = computeGARCHVolatility(returns);

        std::vector<float> features = predictor_.extractFeatures(rsi, macdHist, 0.0f, garchVol, 30, (int)idx);
        float mlPrediction = predictor_.predict(features);
        float mlSignal = std::tanh(mlPrediction * 50);  // Scale to -1 to 1

        // === Technical Signal ===
        BollingerBands bb = computeBollingerBands(closes, 20, 2.0f);
        ADXResult adx = computeADX(history, 14);
        float current = closes.back();

        float technicalSignal = 0.0f;

        // RSI contribution
        if (rsi < 30) technicalSignal += 0.3f;
        else if (rsi > 70) technicalSignal -= 0.3f;

        // Bollinger contribution
        float bbPosition = (current - bb.lower) / (bb.upper - bb.lower);
        technicalSignal += (0.5f - bbPosition) * 0.4f;

        // MACD contribution
        technicalSignal += std::tanh(macdHist) * 0.3f;

        // === Combine signals ===
        float combinedSignal = (mlSignal * mlWeight_) + (technicalSignal * technicalWeight_);

        // Calculate confidence
        float confidence = std::abs(combinedSignal);

        // Generate signal
        if (combinedSignal > combinedThreshold_) {
            StrategySignal sig = StrategySignal::buy(combinedSignal,
                "Hybrid: ML=" + std::to_string(mlSignal) + ", Tech=" + std::to_string(technicalSignal));

            float atr = computeATR(history, 14);
            sig.stopLossPrice = current - (atr * 2.0f);
            sig.confidence = confidence;

            return sig;
        }

        if (combinedSignal < -combinedThreshold_) {
            StrategySignal sig = StrategySignal::sell(std::abs(combinedSignal),
                "Hybrid: ML=" + std::to_string(mlSignal) + ", Tech=" + std::to_string(technicalSignal));

            float atr = computeATR(history, 14);
            sig.stopLossPrice = current + (atr * 2.0f);
            sig.confidence = confidence;

            return sig;
        }

        return StrategySignal::hold("Combined signal below threshold: " + std::to_string(combinedSignal));
    }

    std::unique_ptr<IStrategy> clone() const override {
        auto copy = std::make_unique<HybridMLStrategy>(predictor_);
        copy->mlWeight_ = mlWeight_;
        copy->technicalWeight_ = technicalWeight_;
        copy->combinedThreshold_ = combinedThreshold_;
        copy->params_ = params_;
        return copy;
    }

protected:
    void onParametersChanged() override {
        mlWeight_ = getParamValue("mlWeight");
        technicalWeight_ = 1.0f - mlWeight_;
        combinedThreshold_ = getParamValue("combinedThreshold");
    }
};
