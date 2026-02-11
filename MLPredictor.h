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
    std::vector<double> weights;
    double bias;
    double learningRate;
    double regularization;  // L2 regularization strength

    // Training statistics for validation
    int trainSamples_ = 0;
    double runningMSE_ = 0.0;
    double bestMSE_ = 1e10;

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
    std::vector<double> returnHistory_;
    static const size_t MAX_HISTORY = 20;

public:
    MLPredictor();

    // Train on a single sample (online learning) with L2 regularization
    // target: Actual % change of price next day
    void train(const std::vector<double>& features, double target);

    // Batch training with train/validation split
    void trainBatch(const std::vector<std::vector<double>>& features,
                   const std::vector<double>& targets,
                   double validationSplit = 0.2,
                   int epochs = 100);

    // Predict next % change
    double predict(const std::vector<double>& features) const;

    // Helper to extract features from raw data
    // Normalize inputs to -1..1 or 0..1 range roughly
    std::vector<double> extractFeatures(double rsi, double macdHist, double sentiment,
                                       double garchVol, int cyclePeriod, int dayIndex);

    // Add return to history for lagged features
    void addReturn(double dailyReturn);

    // Get/set hyperparameters
    double getLearningRate() const { return learningRate; }
    void setLearningRate(double lr) { learningRate = lr; }
    double getRegularization() const { return regularization; }
    void setRegularization(double reg) { regularization = reg; }

    // Get training statistics
    int getTrainSamples() const { return trainSamples_; }
    double getRunningMSE() const { return trainSamples_ > 0 ? runningMSE_ / trainSamples_ : 0.0; }

    // Reset model
    void reset();

    // Get feature importance (absolute weight values)
    std::vector<std::pair<int, double>> getFeatureImportance() const;
};

// Simple 2-layer Neural Network for more complex patterns
class NeuralNetPredictor {
private:
    // Network architecture: input -> hidden -> output
    int inputSize_;
    int hiddenSize_;

    // Weights and biases
    std::vector<std::vector<double>> weightsInput_;   // inputSize x hiddenSize
    std::vector<double> biasHidden_;                   // hiddenSize
    std::vector<double> weightsOutput_;                // hiddenSize
    double biasOutput_;

    // Hidden layer activations (stored for backprop)
    std::vector<double> hiddenActivations_;

    // Hyperparameters
    double learningRate_ = 0.01;
    double regularization_ = 0.001;
    double momentum_ = 0.9;

    // Momentum terms
    std::vector<std::vector<double>> velocityInput_;
    std::vector<double> velocityHidden_;
    std::vector<double> velocityOutput_;
    double velocityBiasOutput_ = 0.0;

    // Training stats
    int trainSamples_ = 0;
    double runningMSE_ = 0.0;

public:
    NeuralNetPredictor(int inputSize = 15, int hiddenSize = 8);

    // Forward pass
    double predict(const std::vector<double>& features);

    // Train on single sample
    void train(const std::vector<double>& features, double target);

    // Batch training
    void trainBatch(const std::vector<std::vector<double>>& features,
                   const std::vector<double>& targets,
                   int epochs = 100,
                   double validationSplit = 0.2);

    // Reset weights
    void reset();

    void setLearningRate(double lr) { learningRate_ = lr; }
    void setRegularization(double reg) { regularization_ = reg; }
    void setMomentum(double mom) { momentum_ = mom; }

private:
    // Activation function (tanh for hidden, linear for output)
    double tanh_activation(double x) const {
        return std::tanh(x);
    }

    double tanh_derivative(double x) const {
        double t = std::tanh(x);
        return 1.0 - t * t;
    }

    void initializeWeights();
};

// Ridge Regression (Linear regression with L2 regularization) - Closed form solution
class RidgeRegression {
private:
    std::vector<double> weights_;
    double bias_ = 0.0;
    double lambda_ = 0.1;  // Regularization parameter
    bool isFitted_ = false;

public:
    RidgeRegression(double lambda = 0.1) : lambda_(lambda) {}

    // Fit using normal equation with regularization
    // (X'X + lambda*I)^-1 * X'y
    void fit(const std::vector<std::vector<double>>& X,
            const std::vector<double>& y);

    double predict(const std::vector<double>& x) const;

    std::vector<double> predictBatch(const std::vector<std::vector<double>>& X) const;

    void setLambda(double lambda) { lambda_ = lambda; }
    double getLambda() const { return lambda_; }

    const std::vector<double>& getWeights() const { return weights_; }
    double getBias() const { return bias_; }

private:
    // Simple matrix operations (for small dimensions)
    std::vector<std::vector<double>> matMul(
        const std::vector<std::vector<double>>& A,
        const std::vector<std::vector<double>>& B) const;

    std::vector<std::vector<double>> transpose(
        const std::vector<std::vector<double>>& A) const;

    std::vector<std::vector<double>> inverse(
        std::vector<std::vector<double>> A) const;
};

// Ensemble predictor combining multiple models
class EnsemblePredictor {
private:
    MLPredictor linearModel_;
    NeuralNetPredictor neuralModel_;
    RidgeRegression ridgeModel_;

    double linearWeight_ = 0.4;
    double neuralWeight_ = 0.3;
    double ridgeWeight_ = 0.3;

    std::vector<std::vector<double>> trainingFeatures_;
    std::vector<double> trainingTargets_;

public:
    EnsemblePredictor();

    // Add training sample
    void addSample(const std::vector<double>& features, double target);

    // Train all models
    void train(int epochs = 100);

    // Predict using weighted ensemble
    double predict(const std::vector<double>& features);

    // Extract features using linear model's method
    std::vector<double> extractFeatures(double rsi, double macdHist, double sentiment,
                                       double garchVol, int cyclePeriod, int dayIndex) {
        return linearModel_.extractFeatures(rsi, macdHist, sentiment, garchVol, cyclePeriod, dayIndex);
    }

    void setWeights(double linear, double neural, double ridge) {
        linearWeight_ = linear;
        neuralWeight_ = neural;
        ridgeWeight_ = ridge;
    }

    void reset();
};
