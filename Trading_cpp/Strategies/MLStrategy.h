#pragma once
#include "../IStrategy.h"
#include "../MLPredictor.h"
#include "../TechnicalAnalysis.h"
#include <cmath>
#include <memory>
#include <algorithm>

// Machine Learning based Strategy
// Wraps the MLPredictor for use with the backtester
class MLStrategy : public StrategyBase {
private:
    MLPredictor& predictor_;
    double buyThreshold_ = 0.005;     // 0.5% predicted return to buy
    double sellThreshold_ = -0.005;   // -0.5% predicted return to sell
    double confidenceThreshold_ = 0.3;
    int cyclePeriod_ = 30;
    bool trainOnline_ = false;
    double lastPrediction_ = 0.0;
    double lastActualReturn_ = 0.0;
    size_t lastTrainIdx_ = 0;

public:
    MLStrategy(MLPredictor& predictor)
        : StrategyBase("MLStrategy", 60), predictor_(predictor) {
        addParam("buyThreshold", 0.005, 0.001, 0.02, 0.001);
        addParam("sellThreshold", -0.005, -0.02, -0.001, 0.001);
        addParam("confidenceThreshold", 0.3, 0.1, 0.8, 0.1);
    }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if (history.size() < 60) {
            return StrategySignal::hold("Insufficient data for ML prediction");
        }

        // Extract features
        std::vector<double> closes;
        for (const auto& c : history) closes.push_back(c.close);

        double rsi = computeRSI(closes, 14);
        auto macd = computeMACD(closes);
        double macdHist = macd.first - macd.second;

        // Calculate log returns for GARCH
        std::vector<double> returns;
        for (size_t i = 1; i < closes.size(); ++i) {
            if (closes[i-1] > 0)
                returns.push_back(std::log(closes[i] / closes[i-1]));
        }
        double garchVol = computeGARCHVolatility(returns);

        // Detect cycle
        int detectedCycle = detectCycle(closes);
        if (detectedCycle > 0) cyclePeriod_ = detectedCycle;

        // Use external sentiment (default to neutral if not available)
        double sentiment = 0.0;

        // Extract features using predictor's method
        std::vector<double> features = predictor_.extractFeatures(
            rsi, macdHist, sentiment, garchVol, cyclePeriod_, (int)idx);

        // Get prediction
        double prediction = predictor_.predict(features);

        // Online training: train on previous prediction if available
        if (trainOnline_ && idx > lastTrainIdx_ + 1) {
            double actualReturn = (closes.back() - closes[closes.size() - 2]) / closes[closes.size() - 2];
            predictor_.train(features, actualReturn);
            lastActualReturn_ = actualReturn;
        }
        lastTrainIdx_ = idx;
        lastPrediction_ = prediction;

        // Calculate confidence based on prediction magnitude and technical alignment
        double confidence = std::abs(prediction) / 0.02;  // Normalize to ~1 at 2% prediction
        confidence = std::min(1.0, confidence);

        // Generate signal based on prediction
        if (prediction > buyThreshold_ && confidence >= confidenceThreshold_) {
            double strength = std::min(1.0, prediction / 0.02);

            // Boost confidence if technicals agree
            if (rsi < 40.0) {
                confidence = std::min(1.0, confidence + 0.1);
            }

            StrategySignal sig = StrategySignal::buy(strength,
                "ML Prediction: " + std::to_string(prediction * 100) + "% expected return");

            double atr = computeATR(history, 14);
            sig.stopLossPrice = closes.back() - (atr * 2.0);
            sig.takeProfitPrice = closes.back() * (1.0 + prediction * 2);  // 2x predicted move
            sig.confidence = confidence;

            return sig;
        }

        if (prediction < sellThreshold_ && confidence >= confidenceThreshold_) {
            double strength = std::min(1.0, std::abs(prediction) / 0.02);

            if (rsi > 60.0) {
                confidence = std::min(1.0, confidence + 0.1);
            }

            StrategySignal sig = StrategySignal::sell(strength,
                "ML Prediction: " + std::to_string(prediction * 100) + "% expected return");

            double atr = computeATR(history, 14);
            sig.stopLossPrice = closes.back() + (atr * 2.0);
            sig.takeProfitPrice = closes.back() * (1.0 + prediction * 2);
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
        lastPrediction_ = 0.0;
        lastActualReturn_ = 0.0;
        lastTrainIdx_ = 0;
    }

protected:
    void onParametersChanged() override {
        buyThreshold_ = getParamValue("buyThreshold");
        sellThreshold_ = getParamValue("sellThreshold");
        confidenceThreshold_ = getParamValue("confidenceThreshold");
    }

public:
    void setBuyThreshold(double t) { buyThreshold_ = t; }
    void setSellThreshold(double t) { sellThreshold_ = t; }
    void setConfidenceThreshold(double t) { confidenceThreshold_ = t; }
    void setTrainOnline(bool train) { trainOnline_ = train; }

    double getLastPrediction() const { return lastPrediction_; }
    double getLastActualReturn() const { return lastActualReturn_; }
};

// Combined ML + Technical Strategy
class HybridMLStrategy : public StrategyBase {
private:
    MLPredictor& predictor_;
    double mlWeight_ = 0.6;          // Weight given to ML signal
    double technicalWeight_ = 0.4;   // Weight given to technical signal
    double combinedThreshold_ = 0.4; // Minimum combined score to trade

public:
    HybridMLStrategy(MLPredictor& predictor)
        : StrategyBase("HybridML", 60), predictor_(predictor) {
        addParam("mlWeight", 0.6, 0.3, 0.9, 0.1);
        addParam("combinedThreshold", 0.4, 0.2, 0.7, 0.1);
    }

    StrategySignal generateSignal(const std::vector<Candle>& history, size_t idx) override {
        if (history.size() < 60) {
            return StrategySignal::hold("Insufficient data");
        }

        std::vector<double> closes;
        for (const auto& c : history) closes.push_back(c.close);

        // === ML Signal ===
        double rsi = computeRSI(closes, 14);
        auto macd = computeMACD(closes);
        double macdHist = macd.first - macd.second;

        std::vector<double> returns;
        for (size_t i = 1; i < closes.size(); ++i) {
            if (closes[i-1] > 0)
                returns.push_back(std::log(closes[i] / closes[i-1]));
        }
        double garchVol = computeGARCHVolatility(returns);

        std::vector<double> features = predictor_.extractFeatures(rsi, macdHist, 0.0, garchVol, 30, (int)idx);
        double mlPrediction = predictor_.predict(features);
        double mlSignal = std::tanh(mlPrediction * 50);  // Scale to -1 to 1

        // === Technical Signal ===
        BollingerBands bb = computeBollingerBands(closes, 20, 2.0);
        ADXResult adx = computeADX(history, 14);
        double current = closes.back();

        double technicalSignal = 0.0;

        // RSI contribution
        if (rsi < 30) technicalSignal += 0.3;
        else if (rsi > 70) technicalSignal -= 0.3;

        // Bollinger contribution
        double bbPosition = (current - bb.lower) / (bb.upper - bb.lower);
        technicalSignal += (0.5 - bbPosition) * 0.4;

        // MACD contribution
        technicalSignal += std::tanh(macdHist) * 0.3;

        // === Combine signals ===
        double combinedSignal = (mlSignal * mlWeight_) + (technicalSignal * technicalWeight_);

        // Calculate confidence
        double confidence = std::abs(combinedSignal);

        // Generate signal
        if (combinedSignal > combinedThreshold_) {
            StrategySignal sig = StrategySignal::buy(combinedSignal,
                "Hybrid: ML=" + std::to_string(mlSignal) + ", Tech=" + std::to_string(technicalSignal));

            double atr = computeATR(history, 14);
            sig.stopLossPrice = current - (atr * 2.0);
            sig.confidence = confidence;

            return sig;
        }

        if (combinedSignal < -combinedThreshold_) {
            StrategySignal sig = StrategySignal::sell(std::abs(combinedSignal),
                "Hybrid: ML=" + std::to_string(mlSignal) + ", Tech=" + std::to_string(technicalSignal));

            double atr = computeATR(history, 14);
            sig.stopLossPrice = current + (atr * 2.0);
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
        technicalWeight_ = 1.0 - mlWeight_;
        combinedThreshold_ = getParamValue("combinedThreshold");
    }
};