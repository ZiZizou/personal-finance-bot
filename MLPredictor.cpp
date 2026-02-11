#include "MLPredictor.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <random>

// ============= MLPredictor Implementation =============

MLPredictor::MLPredictor() {
    weights.resize(TOTAL_FEATURE_COUNT, 0.0);
    bias = 0.0;
    learningRate = 0.01;
    regularization = 0.001;  // L2 regularization
}

void MLPredictor::train(const std::vector<double>& features, double target) {
    if (features.size() < BASE_FEATURE_COUNT) return;

    // Pad features if needed
    std::vector<double> paddedFeatures = features;
    while (paddedFeatures.size() < (size_t)TOTAL_FEATURE_COUNT) {
        paddedFeatures.push_back(0.0);
    }

    // Forward pass
    double prediction = predict(paddedFeatures);

    // Error
    double error = prediction - target;

    // Update MSE tracking
    runningMSE_ += error * error;
    trainSamples_++;

    // Backward pass with L2 regularization
    // dL/dw = 2 * error * feature + 2 * lambda * weight
    for (size_t i = 0; i < (size_t)TOTAL_FEATURE_COUNT && i < paddedFeatures.size(); ++i) {
        double gradient = error * paddedFeatures[i] + regularization * weights[i];
        weights[i] -= learningRate * gradient;
    }
    bias -= learningRate * error;
}

void MLPredictor::trainBatch(const std::vector<std::vector<double>>& features,
                             const std::vector<double>& targets,
                             double validationSplit,
                             int epochs) {
    if (features.empty() || features.size() != targets.size()) return;

    // Split data
    size_t n = features.size();
    size_t valSize = (size_t)(n * validationSplit);
    if (valSize == 0 && validationSplit > 0) valSize = 1;
    size_t trainSize = n - valSize;

    // Shuffle indices
    std::vector<size_t> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(indices.begin(), indices.end(), g);

    double prevValMSE = 1e10;
    int patience = 10;
    int noImproveCount = 0;

    for (int epoch = 0; epoch < epochs; ++epoch) {
        // Shuffle training data each epoch
        std::shuffle(indices.begin(), indices.begin() + trainSize, g);

        // Train
        for (size_t i = 0; i < trainSize; ++i) {
            size_t idx = indices[i];
            train(features[idx], targets[idx]);
        }

        // Validate
        if (valSize > 0) {
            double valMSE = 0.0;
            for (size_t i = trainSize; i < n; ++i) {
                size_t idx = indices[i];
                double pred = predict(features[idx]);
                double err = pred - targets[idx];
                valMSE += err * err;
            }
            valMSE /= valSize;

            // Early stopping
            if (valMSE < prevValMSE - 1e-6) {
                prevValMSE = valMSE;
                noImproveCount = 0;
                if (valMSE < bestMSE_) {
                    bestMSE_ = valMSE;
                }
            } else {
                noImproveCount++;
                if (noImproveCount >= patience) {
                    break;  // Early stop
                }
            }
        }
    }
}

double MLPredictor::predict(const std::vector<double>& features) const {
    if (features.empty()) return 0.0;
    
    double dotProduct = 0.0;
    for (size_t i = 0; i < weights.size() && i < features.size(); ++i) {
        dotProduct += weights[i] * features[i];
    }
    
    return dotProduct + bias;
}

std::vector<double> MLPredictor::extractFeatures(double rsi, double macdHist, double sentiment,
                                               double garchVol, int cyclePeriod, int dayIndex) {
    std::vector<double> f;
    f.reserve(TOTAL_FEATURE_COUNT);

    // 1. Base Indicators (normalized)
    f.push_back(rsi / 100.0);
    f.push_back(std::tanh(macdHist * 10.0));
    f.push_back(sentiment);
    f.push_back(std::tanh(garchVol * 50.0));
    
    // 2. Cycle Phase
    const double PI_VAL = 3.14159265358979323846;
    double phase = (cyclePeriod > 0) ? std::cos(2.0 * PI_VAL * dayIndex / cyclePeriod) : 0.0;
    f.push_back(phase);

    // 3. Lagged Returns
    for (int lag : {1, 2, 3, 5, 10}) {
        if (returnHistory_.size() >= (size_t)lag) {
            f.push_back(returnHistory_[returnHistory_.size() - lag]);
        } else {
            f.push_back(0.0);
        }
    }

    // 4. Feature Interactions (Cross-products)
    if (f.size() >= 5) {
        f.push_back(f[0] * f[2]); // RSI * Sentiment
        f.push_back(f[1] * f[3]); // MACD * Volatility
        f.push_back(f[0] * f[3]); // RSI * Volatility
        f.push_back(f[2] * f[4]); // Sentiment * Cycle
        f.push_back(f[1] * f[4]); // MACD * Cycle
    }

    return f;
}

void MLPredictor::addReturn(double dailyReturn) {
    returnHistory_.push_back(dailyReturn);
    if (returnHistory_.size() > MAX_HISTORY) {
        returnHistory_.erase(returnHistory_.begin());
    }
}

void MLPredictor::reset() {
    std::fill(weights.begin(), weights.end(), 0.0);
    bias = 0.0;
    trainSamples_ = 0;
    runningMSE_ = 0.0;
    returnHistory_.clear();
}

std::vector<std::pair<int, double>> MLPredictor::getFeatureImportance() const {
    std::vector<std::pair<int, double>> importance;
    for (size_t i = 0; i < weights.size(); ++i) {
        importance.push_back({(int)i, std::abs(weights[i])});
    }
    std::sort(importance.begin(), importance.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return importance;
}

// ============= NeuralNetPredictor Implementation =============

NeuralNetPredictor::NeuralNetPredictor(int inputSize, int hiddenSize)
    : inputSize_(inputSize), hiddenSize_(hiddenSize) {
    initializeWeights();
}

void NeuralNetPredictor::initializeWeights() {
    std::default_random_engine generator;
    std::normal_distribution<double> distribution(0.0, 0.1);

    weightsInput_.resize(inputSize_, std::vector<double>(hiddenSize_));
    velocityInput_.resize(inputSize_, std::vector<double>(hiddenSize_, 0.0));
    for (int i = 0; i < inputSize_; ++i) {
        for (int j = 0; j < hiddenSize_; ++j) {
            weightsInput_[i][j] = distribution(generator);
        }
    }

    biasHidden_.resize(hiddenSize_, 0.0);
    velocityHidden_.resize(hiddenSize_, 0.0);

    weightsOutput_.resize(hiddenSize_);
    velocityOutput_.resize(hiddenSize_, 0.0);
    for (int i = 0; i < hiddenSize_; ++i) {
        weightsOutput_[i] = distribution(generator);
    }

    biasOutput_ = 0.0;
    velocityBiasOutput_ = 0.0;
    
    hiddenActivations_.resize(hiddenSize_);
}

double NeuralNetPredictor::predict(const std::vector<double>& features) {
    if (features.size() < (size_t)inputSize_) return 0.0;

    // Hidden layer
    for (int j = 0; j < hiddenSize_; ++j) {
        double sum = biasHidden_[j];
        for (int i = 0; i < inputSize_; ++i) {
            sum += features[i] * weightsInput_[i][j];
        }
        hiddenActivations_[j] = tanh_activation(sum);
    }

    // Output layer (linear)
    double output = biasOutput_;
    for (int j = 0; j < hiddenSize_; ++j) {
        output += hiddenActivations_[j] * weightsOutput_[j];
    }

    return output;
}

void NeuralNetPredictor::train(const std::vector<double>& features, double target) {
    if (features.size() < (size_t)inputSize_) return;

    // Forward pass
    double prediction = predict(features);
    double error = prediction - target;

    // Backward pass
    // Output layer gradients
    double deltaOutput = error; // Linear activation derivative is 1
    
    std::vector<double> deltaHidden(hiddenSize_);
    for (int j = 0; j < hiddenSize_; ++j) {
        deltaHidden[j] = deltaOutput * weightsOutput_[j] * tanh_derivative(hiddenActivations_[j]);
    }

    // Update weights with momentum and L2
    for (int j = 0; j < hiddenSize_; ++j) {
        // Output weights
        double gradOutput = deltaOutput * hiddenActivations_[j] + regularization_ * weightsOutput_[j];
        velocityOutput_[j] = momentum_ * velocityOutput_[j] - learningRate_ * gradOutput;
        weightsOutput_[j] += velocityOutput_[j];

        // Hidden weights & biases
        for (int i = 0; i < inputSize_; ++i) {
            double gradInput = deltaHidden[j] * features[i] + regularization_ * weightsInput_[i][j];
            velocityInput_[i][j] = momentum_ * velocityInput_[i][j] - learningRate_ * gradInput;
            weightsInput_[i][j] += velocityInput_[i][j];
        }
        
        velocityHidden_[j] = momentum_ * velocityHidden_[j] - learningRate_ * deltaHidden[j];
        biasHidden_[j] += velocityHidden_[j];
    }

    velocityBiasOutput_ = momentum_ * velocityBiasOutput_ - learningRate_ * deltaOutput;
    biasOutput_ += velocityBiasOutput_;

    runningMSE_ += error * error;
    trainSamples_++;
}

void NeuralNetPredictor::trainBatch(const std::vector<std::vector<double>>& features,
                                    const std::vector<double>& targets,
                                    int epochs,
                                    double validationSplit) {
    (void)validationSplit; // Simplified batch training
    for (int e = 0; e < epochs; ++e) {
        for (size_t i = 0; i < features.size(); ++i) {
            train(features[i], targets[i]);
        }
    }
}

void NeuralNetPredictor::reset() {
    initializeWeights();
    trainSamples_ = 0;
    runningMSE_ = 0.0;
}

// ============= RidgeRegression Implementation =============

void RidgeRegression::fit(const std::vector<std::vector<double>>& X,
                         const std::vector<double>& y) {
    if (X.empty() || X.size() != y.size()) return;

    int n = X.size();
    int p = X[0].size();

    // Add bias term (column of 1s)
    std::vector<std::vector<double>> X_bias(n, std::vector<double>(p + 1));
    for (int i = 0; i < n; ++i) {
        X_bias[i][0] = 1.0;
        for (int j = 0; j < p; ++j) {
            X_bias[i][j+1] = X[i][j];
        }
    }

    // Normal equation: w = (X'X + lambda*I)^-1 * X'y
    auto Xt = transpose(X_bias);
    auto XtX = matMul(Xt, X_bias);

    // Add regularization
    for (int i = 0; i < p + 1; ++i) {
        XtX[i][i] += lambda_;
    }

    auto XtX_inv = inverse(XtX);
    if (XtX_inv.empty()) return; // SVD fallback would be better

    auto XtY = std::vector<double>(p + 1, 0.0);
    for (int i = 0; i < p + 1; ++i) {
        for (int k = 0; k < n; ++k) {
            XtY[i] += Xt[i][k] * y[k];
        }
    }

    std::vector<double> w(p + 1, 0.0);
    for (int i = 0; i < p + 1; ++i) {
        for (int j = 0; j < p + 1; ++j) {
            w[i] += XtX_inv[i][j] * XtY[j];
        }
    }

    bias_ = w[0];
    weights_.assign(w.begin() + 1, w.end());
    isFitted_ = true;
}

double RidgeRegression::predict(const std::vector<double>& x) const {
    if (!isFitted_ || x.size() != weights_.size()) return 0.0;
    double res = bias_;
    for (size_t i = 0; i < x.size(); ++i) {
        res += x[i] * weights_[i];
    }
    return res;
}

std::vector<double> RidgeRegression::predictBatch(const std::vector<std::vector<double>>& X) const {
    std::vector<double> res;
    for (const auto& x : X) res.push_back(predict(x));
    return res;
}

std::vector<std::vector<double>> RidgeRegression::matMul(
    const std::vector<std::vector<double>>& A,
    const std::vector<std::vector<double>>& B) const {
    int ni = A.size();
    int nk = A[0].size();
    int nj = B[0].size();
    std::vector<std::vector<double>> C(ni, std::vector<double>(nj, 0.0));
    for (int i = 0; i < ni; ++i)
        for (int j = 0; j < nj; ++j)
            for (int k = 0; k < nk; ++k)
                C[i][j] += A[i][k] * B[k][j];
    return C;
}

std::vector<std::vector<double>> RidgeRegression::transpose(
    const std::vector<std::vector<double>>& A) const {
    int ni = A.size();
    int nj = A[0].size();
    std::vector<std::vector<double>> At(nj, std::vector<double>(ni));
    for (int i = 0; i < ni; ++i)
        for (int j = 0; j < nj; ++j)
            At[j][i] = A[i][j];
    return At;
}

std::vector<std::vector<double>> RidgeRegression::inverse(
    std::vector<std::vector<double>> A) const {
    int n = A.size();
    std::vector<std::vector<double>> inv(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) inv[i][i] = 1.0;

    for (int i = 0; i < n; ++i) {
        double pivot = A[i][i];
        if (std::abs(pivot) < 1e-10) return {}; // Singular
        for (int j = 0; j < n; ++j) {
            A[i][j] /= pivot;
            inv[i][j] /= pivot;
        }
        for (int k = 0; k < n; ++k) {
            if (k != i) {
                double factor = A[k][i];
                for (int j = 0; j < n; ++j) {
                    A[k][j] -= factor * A[i][j];
                    inv[k][j] -= factor * inv[i][j];
                }
            }
        }
    }
    return inv;
}

// ============= EnsemblePredictor Implementation =============

EnsemblePredictor::EnsemblePredictor() {}

void EnsemblePredictor::addSample(const std::vector<double>& features, double target) {
    trainingFeatures_.push_back(features);
    trainingTargets_.push_back(target);
    linearModel_.train(features, target);
    neuralModel_.train(features, target);
}

void EnsemblePredictor::train(int epochs) {
    if (trainingFeatures_.empty()) return;
    ridgeModel_.fit(trainingFeatures_, trainingTargets_);
    // Neural model already trained online, but could do batch here
    (void)epochs;
}

double EnsemblePredictor::predict(const std::vector<double>& features) {
    double p1 = linearModel_.predict(features);
    double p2 = neuralModel_.predict(features);
    double p3 = ridgeModel_.predict(features);
    return linearWeight_ * p1 + neuralWeight_ * p2 + ridgeWeight_ * p3;
}

void EnsemblePredictor::reset() {
    linearModel_.reset();
    neuralModel_.reset();
    trainingFeatures_.clear();
    trainingTargets_.clear();
}