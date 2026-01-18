#pragma once
#include <vector>
#include <string>

// Lightweight Online Machine Learning for Time Series
// Implements an Online Gradient Descent Linear Regressor
// Tries to predict the "Next Close" based on features.

class MLPredictor {
private:
    std::vector<float> weights;
    float bias;
    float learningRate;
    
    // Features:
    // 0: RSI
    // 1: MACD Hist (Normalized)
    // 2: Sentiment
    // 3: GARCH Volatility (Normalized)
    // 4: Cycle Phase (Cos)
    static const int FEATURE_COUNT = 5;

public:
    MLPredictor();

    // Train on a single sample (online learning)
    // target: Actual % change of price next day
    void train(const std::vector<float>& features, float target);

    // Predict next % change
    float predict(const std::vector<float>& features);

    // Helper to extract features from raw data
    // Normalize inputs to -1..1 or 0..1 range roughly
    std::vector<float> extractFeatures(float rsi, float macdHist, float sentiment, float garchVol, int cyclePeriod, int dayIndex);
};
