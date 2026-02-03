#pragma once
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>

// Lightweight Online Machine Learning for Time Series
// Implements Online Gradient Descent Linear Regressor with L2 Regularization
// Enhanced with feature engineering and optional neural network

class MLPredictor {
private:
    std::vector<float> weights;
    float bias;
    float learningRate;
    float regularization;  // L2 regularization strength

    // Training statistics for validation
    int trainSamples_ = 0;
    float runningMSE_ = 0.0f;
    float bestMSE_ = 1e10f;

    // Features:
    // 0: RSI
    // 1: MACD Hist (Normalized)
    // 2: Sentiment
    // 3: GARCH Volatility (Normalized)
    // 4: Cycle Phase (Cos)
    // 5-9: Lagged returns (1, 2, 3, 5, 10 days)
    // 10-14: Feature cross-products
    static const int BASE_FEATURE_COUNT = 5;
    static const int LAGGED_FEATURES = 5;
    static const int CROSS_FEATURES = 5;
    static const int TOTAL_FEATURE_COUNT = BASE_FEATURE_COUNT + LAGGED_FEATURES + CROSS_FEATURES;

    // For tracking lagged returns
    std::vector<float> returnHistory_;
    static const size_t MAX_HISTORY = 20;

public:
    MLPredictor();

    // Train on a single sample (online learning) with L2 regularization
    // target: Actual % change of price next day
    void train(const std::vector<float>& features, float target);

    // Batch training with train/validation split
    void trainBatch(const std::vector<std::vector<float>>& features,
                   const std::vector<float>& targets,
                   float validationSplit = 0.2f,
                   int epochs = 100);

    // Predict next % change
    float predict(const std::vector<float>& features) const;

    // Helper to extract features from raw data
    // Normalize inputs to -1..1 or 0..1 range roughly
    std::vector<float> extractFeatures(float rsi, float macdHist, float sentiment,
                                       float garchVol, int cyclePeriod, int dayIndex);

    // Add return to history for lagged features
    void addReturn(float dailyReturn);

    // Get/set hyperparameters
    float getLearningRate() const { return learningRate; }
    void setLearningRate(float lr) { learningRate = lr; }
    float getRegularization() const { return regularization; }
    void setRegularization(float reg) { regularization = reg; }

    // Get training statistics
    int getTrainSamples() const { return trainSamples_; }
    float getRunningMSE() const { return trainSamples_ > 0 ? runningMSE_ / trainSamples_ : 0.0f; }

    // Reset model
    void reset();

    // Get feature importance (absolute weight values)
    std::vector<std::pair<int, float>> getFeatureImportance() const;
};

// Simple 2-layer Neural Network for more complex patterns
class NeuralNetPredictor {
private:
    // Network architecture: input -> hidden -> output
    int inputSize_;
    int hiddenSize_;

    // Weights and biases
    std::vector<std::vector<float>> weightsInput_;   // inputSize x hiddenSize
    std::vector<float> biasHidden_;                   // hiddenSize
    std::vector<float> weightsOutput_;                // hiddenSize
    float biasOutput_;

    // Hidden layer activations (stored for backprop)
    std::vector<float> hiddenActivations_;

    // Hyperparameters
    float learningRate_ = 0.01f;
    float regularization_ = 0.001f;
    float momentum_ = 0.9f;

    // Momentum terms
    std::vector<std::vector<float>> velocityInput_;
    std::vector<float> velocityHidden_;
    std::vector<float> velocityOutput_;
    float velocityBiasOutput_ = 0.0f;

    // Training stats
    int trainSamples_ = 0;
    float runningMSE_ = 0.0f;

public:
    NeuralNetPredictor(int inputSize = 15, int hiddenSize = 8);

    // Forward pass
    float predict(const std::vector<float>& features);

    // Train on single sample
    void train(const std::vector<float>& features, float target);

    // Batch training
    void trainBatch(const std::vector<std::vector<float>>& features,
                   const std::vector<float>& targets,
                   int epochs = 100,
                   float validationSplit = 0.2f);

    // Reset weights
    void reset();

    void setLearningRate(float lr) { learningRate_ = lr; }
    void setRegularization(float reg) { regularization_ = reg; }
    void setMomentum(float mom) { momentum_ = mom; }

private:
    // Activation function (tanh for hidden, linear for output)
    float tanh_activation(float x) const {
        return std::tanh(x);
    }

    float tanh_derivative(float x) const {
        float t = std::tanh(x);
        return 1.0f - t * t;
    }

    void initializeWeights();
};

// Ridge Regression (Linear regression with L2 regularization) - Closed form solution
class RidgeRegression {
private:
    std::vector<float> weights_;
    float bias_ = 0.0f;
    float lambda_ = 0.1f;  // Regularization parameter
    bool isFitted_ = false;

public:
    RidgeRegression(float lambda = 0.1f) : lambda_(lambda) {}

    // Fit using normal equation with regularization
    // (X'X + lambda*I)^-1 * X'y
    void fit(const std::vector<std::vector<float>>& X,
            const std::vector<float>& y);

    float predict(const std::vector<float>& x) const;

    std::vector<float> predictBatch(const std::vector<std::vector<float>>& X) const;

    void setLambda(float lambda) { lambda_ = lambda; }
    float getLambda() const { return lambda_; }

    const std::vector<float>& getWeights() const { return weights_; }
    float getBias() const { return bias_; }

private:
    // Simple matrix operations (for small dimensions)
    std::vector<std::vector<float>> matMul(
        const std::vector<std::vector<float>>& A,
        const std::vector<std::vector<float>>& B) const;

    std::vector<std::vector<float>> transpose(
        const std::vector<std::vector<float>>& A) const;

    std::vector<std::vector<float>> inverse(
        std::vector<std::vector<float>> A) const;
};

// Ensemble predictor combining multiple models
class EnsemblePredictor {
private:
    MLPredictor linearModel_;
    NeuralNetPredictor neuralModel_;
    RidgeRegression ridgeModel_;

    float linearWeight_ = 0.4f;
    float neuralWeight_ = 0.3f;
    float ridgeWeight_ = 0.3f;

    std::vector<std::vector<float>> trainingFeatures_;
    std::vector<float> trainingTargets_;

public:
    EnsemblePredictor();

    // Add training sample
    void addSample(const std::vector<float>& features, float target);

    // Train all models
    void train(int epochs = 100);

    // Predict using weighted ensemble
    float predict(const std::vector<float>& features);

    // Extract features using linear model's method
    std::vector<float> extractFeatures(float rsi, float macdHist, float sentiment,
                                       float garchVol, int cyclePeriod, int dayIndex) {
        return linearModel_.extractFeatures(rsi, macdHist, sentiment, garchVol, cyclePeriod, dayIndex);
    }

    void setWeights(float linear, float neural, float ridge) {
        linearWeight_ = linear;
        neuralWeight_ = neural;
        ridgeWeight_ = ridge;
    }

    void reset();
};
