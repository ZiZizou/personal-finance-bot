#include "MLPredictor.h"
#include <cmath>
#include <numeric>

MLPredictor::MLPredictor() {
    weights.resize(FEATURE_COUNT, 0.0f);
    bias = 0.0f;
    learningRate = 0.01f;
}

void MLPredictor::train(const std::vector<float>& features, float target) {
    if (features.size() != FEATURE_COUNT) return;

    // Forward pass
    float prediction = predict(features);

    // Error (MSE Derivative)
    // Loss = (pred - target)^2
    // dLoss/dPred = 2 * (pred - target)
    float error = prediction - target;

    // Backward pass (Update weights)
    for (int i = 0; i < FEATURE_COUNT; ++i) {
        weights[i] -= learningRate * error * features[i];
    }
    bias -= learningRate * error;
}

float MLPredictor::predict(const std::vector<float>& features) {
    if (features.size() != FEATURE_COUNT) return 0.0f;
    float sum = bias;
    for (int i = 0; i < FEATURE_COUNT; ++i) {
        sum += weights[i] * features[i];
    }
    return sum; // Returns expected % change (e.g., 0.01 for 1%)
}

std::vector<float> MLPredictor::extractFeatures(float rsi, float macdHist, float sentiment, float garchVol, int cyclePeriod, int dayIndex) {
    std::vector<float> f(FEATURE_COUNT);
    
    // 1. RSI (Scaled to -1..1)
    f[0] = (rsi - 50.0f) / 50.0f; 

    // 2. MACD Hist (Approx scaling, assuming range within -5..5 usually for stocks, varies widely though)
    // Let's use Tanh to squash it
    f[1] = std::tanh(macdHist); 

    // 3. Sentiment (-1..1)
    f[2] = sentiment;

    // 4. GARCH Volatility (Log scaling or simple scaling)
    // Daily vol is usually 0.01 to 0.05. 
    f[3] = garchVol * 20.0f; // Scale to approx 0-1

    // 5. Cycle Phase (Cosine of time in cycle)
    if (cyclePeriod > 0) {
        float angle = 2.0f * 3.14159f * (float)dayIndex / (float)cyclePeriod;
        f[4] = std::cos(angle);
    } else {
        f[4] = 0.0f;
    }

    return f;
}
