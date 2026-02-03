#include "MLPredictor.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <random>

// ============= MLPredictor Implementation =============

MLPredictor::MLPredictor() {
    weights.resize(TOTAL_FEATURE_COUNT, 0.0f);
    bias = 0.0f;
    learningRate = 0.01f;
    regularization = 0.001f;  // L2 regularization
}

void MLPredictor::train(const std::vector<float>& features, float target) {
    if (features.size() < BASE_FEATURE_COUNT) return;

    // Pad features if needed
    std::vector<float> paddedFeatures = features;
    while (paddedFeatures.size() < TOTAL_FEATURE_COUNT) {
        paddedFeatures.push_back(0.0f);
    }

    // Forward pass
    float prediction = predict(paddedFeatures);

    // Error
    float error = prediction - target;

    // Update MSE tracking
    runningMSE_ += error * error;
    trainSamples_++;

    // Backward pass with L2 regularization
    // dL/dw = 2 * error * feature + 2 * lambda * weight
    for (size_t i = 0; i < TOTAL_FEATURE_COUNT && i < paddedFeatures.size(); ++i) {
        float gradient = error * paddedFeatures[i] + regularization * weights[i];
        weights[i] -= learningRate * gradient;
    }
    bias -= learningRate * error;
}

void MLPredictor::trainBatch(const std::vector<std::vector<float>>& features,
                             const std::vector<float>& targets,
                             float validationSplit,
                             int epochs) {
    if (features.empty() || features.size() != targets.size()) return;

    // Split data
    size_t n = features.size();
    size_t valSize = (size_t)(n * validationSplit);
    size_t trainSize = n - valSize;

    // Shuffle indices
    std::vector<size_t> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(indices.begin(), indices.end(), g);

    float prevValMSE = 1e10f;
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
        float valMSE = 0.0f;
        for (size_t i = trainSize; i < n; ++i) {
            size_t idx = indices[i];
            float pred = predict(features[idx]);
            float err = pred - targets[idx];
            valMSE += err * err;
        }
        valMSE /= valSize;

        // Early stopping
        if (valMSE < prevValMSE - 1e-6f) {
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

float MLPredictor::predict(const std::vector<float>& features) const {
    if (features.size() < BASE_FEATURE_COUNT) return 0.0f;

    float sum = bias;
    for (size_t i = 0; i < TOTAL_FEATURE_COUNT && i < features.size(); ++i) {
        sum += weights[i] * features[i];
    }
    return sum;
}

std::vector<float> MLPredictor::extractFeatures(float rsi, float macdHist, float sentiment,
                                                float garchVol, int cyclePeriod, int dayIndex) {
    std::vector<float> f(TOTAL_FEATURE_COUNT, 0.0f);

    // 1. RSI (Scaled to -1..1)
    f[0] = (rsi - 50.0f) / 50.0f;

    // 2. MACD Hist (Tanh to squash)
    f[1] = std::tanh(macdHist);

    // 3. Sentiment (-1..1)
    f[2] = sentiment;

    // 4. GARCH Volatility (Scaled)
    f[3] = std::tanh(garchVol * 10.0f);

    // 5. Cycle Phase (Cosine of time in cycle)
    if (cyclePeriod > 0) {
        float angle = 2.0f * 3.14159f * (float)dayIndex / (float)cyclePeriod;
        f[4] = std::cos(angle);
    }

    // 6-10. Lagged returns (from history)
    const int lagDays[] = {1, 2, 3, 5, 10};
    for (int i = 0; i < LAGGED_FEATURES; ++i) {
        int lag = lagDays[i];
        if (returnHistory_.size() >= (size_t)lag) {
            f[BASE_FEATURE_COUNT + i] = returnHistory_[returnHistory_.size() - lag];
        }
    }

    // 11-15. Cross-product features
    f[BASE_FEATURE_COUNT + LAGGED_FEATURES] = f[0] * f[1];      // RSI * MACD
    f[BASE_FEATURE_COUNT + LAGGED_FEATURES + 1] = f[0] * f[2];  // RSI * Sentiment
    f[BASE_FEATURE_COUNT + LAGGED_FEATURES + 2] = f[1] * f[3];  // MACD * Vol
    f[BASE_FEATURE_COUNT + LAGGED_FEATURES + 3] = f[2] * f[3];  // Sentiment * Vol
    f[BASE_FEATURE_COUNT + LAGGED_FEATURES + 4] = f[0] * f[0];  // RSI squared

    return f;
}

void MLPredictor::addReturn(float dailyReturn) {
    returnHistory_.push_back(dailyReturn);
    if (returnHistory_.size() > MAX_HISTORY) {
        returnHistory_.erase(returnHistory_.begin());
    }
}

void MLPredictor::reset() {
    std::fill(weights.begin(), weights.end(), 0.0f);
    bias = 0.0f;
    trainSamples_ = 0;
    runningMSE_ = 0.0f;
    returnHistory_.clear();
}

std::vector<std::pair<int, float>> MLPredictor::getFeatureImportance() const {
    std::vector<std::pair<int, float>> importance;
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
    std::random_device rd;
    std::mt19937 gen(rd());

    // Xavier initialization
    float stdInput = std::sqrt(2.0f / (inputSize_ + hiddenSize_));
    float stdHidden = std::sqrt(2.0f / (hiddenSize_ + 1));

    std::normal_distribution<float> distInput(0.0f, stdInput);
    std::normal_distribution<float> distHidden(0.0f, stdHidden);

    weightsInput_.resize(inputSize_, std::vector<float>(hiddenSize_));
    for (int i = 0; i < inputSize_; ++i) {
        for (int j = 0; j < hiddenSize_; ++j) {
            weightsInput_[i][j] = distInput(gen);
        }
    }

    biasHidden_.resize(hiddenSize_, 0.0f);
    weightsOutput_.resize(hiddenSize_);
    for (int i = 0; i < hiddenSize_; ++i) {
        weightsOutput_[i] = distHidden(gen);
    }
    biasOutput_ = 0.0f;

    hiddenActivations_.resize(hiddenSize_);

    // Initialize momentum terms
    velocityInput_.resize(inputSize_, std::vector<float>(hiddenSize_, 0.0f));
    velocityHidden_.resize(hiddenSize_, 0.0f);
    velocityOutput_.resize(hiddenSize_, 0.0f);
}

float NeuralNetPredictor::predict(const std::vector<float>& features) {
    if ((int)features.size() < inputSize_) return 0.0f;

    // Hidden layer
    for (int j = 0; j < hiddenSize_; ++j) {
        float sum = biasHidden_[j];
        for (int i = 0; i < inputSize_; ++i) {
            sum += features[i] * weightsInput_[i][j];
        }
        hiddenActivations_[j] = tanh_activation(sum);
    }

    // Output layer (linear)
    float output = biasOutput_;
    for (int j = 0; j < hiddenSize_; ++j) {
        output += hiddenActivations_[j] * weightsOutput_[j];
    }

    return output;
}

void NeuralNetPredictor::train(const std::vector<float>& features, float target) {
    if ((int)features.size() < inputSize_) return;

    // Forward pass (stores activations)
    float prediction = predict(features);

    // Error
    float error = prediction - target;

    // Update stats
    runningMSE_ += error * error;
    trainSamples_++;

    // Backpropagation

    // Output layer gradient
    float dOutput = error;  // Linear activation derivative = 1

    // Hidden layer gradients
    std::vector<float> dHidden(hiddenSize_);
    for (int j = 0; j < hiddenSize_; ++j) {
        dHidden[j] = dOutput * weightsOutput_[j] * tanh_derivative(hiddenActivations_[j]);
    }

    // Update output weights with momentum
    for (int j = 0; j < hiddenSize_; ++j) {
        float grad = dOutput * hiddenActivations_[j] + regularization_ * weightsOutput_[j];
        velocityOutput_[j] = momentum_ * velocityOutput_[j] - learningRate_ * grad;
        weightsOutput_[j] += velocityOutput_[j];
    }
    velocityBiasOutput_ = momentum_ * velocityBiasOutput_ - learningRate_ * dOutput;
    biasOutput_ += velocityBiasOutput_;

    // Update input-hidden weights with momentum
    for (int i = 0; i < inputSize_; ++i) {
        for (int j = 0; j < hiddenSize_; ++j) {
            float grad = dHidden[j] * features[i] + regularization_ * weightsInput_[i][j];
            velocityInput_[i][j] = momentum_ * velocityInput_[i][j] - learningRate_ * grad;
            weightsInput_[i][j] += velocityInput_[i][j];
        }
    }

    for (int j = 0; j < hiddenSize_; ++j) {
        velocityHidden_[j] = momentum_ * velocityHidden_[j] - learningRate_ * dHidden[j];
        biasHidden_[j] += velocityHidden_[j];
    }
}

void NeuralNetPredictor::trainBatch(const std::vector<std::vector<float>>& features,
                                    const std::vector<float>& targets,
                                    int epochs,
                                    float validationSplit) {
    if (features.empty()) return;

    size_t n = features.size();
    size_t valSize = (size_t)(n * validationSplit);
    size_t trainSize = n - valSize;

    std::vector<size_t> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
    std::random_device rd;
    std::mt19937 g(rd());

    for (int epoch = 0; epoch < epochs; ++epoch) {
        std::shuffle(indices.begin(), indices.begin() + trainSize, g);

        for (size_t i = 0; i < trainSize; ++i) {
            train(features[indices[i]], targets[indices[i]]);
        }
    }
}

void NeuralNetPredictor::reset() {
    initializeWeights();
    trainSamples_ = 0;
    runningMSE_ = 0.0f;
}

// ============= RidgeRegression Implementation =============

void RidgeRegression::fit(const std::vector<std::vector<float>>& X,
                          const std::vector<float>& y) {
    if (X.empty() || y.empty() || X.size() != y.size()) return;

    int n = X.size();
    int p = X[0].size();

    // Add bias column (column of 1s)
    std::vector<std::vector<float>> Xb(n, std::vector<float>(p + 1));
    for (int i = 0; i < n; ++i) {
        Xb[i][0] = 1.0f;  // Bias term
        for (int j = 0; j < p; ++j) {
            Xb[i][j + 1] = X[i][j];
        }
    }

    // X'X
    auto Xt = transpose(Xb);
    auto XtX = matMul(Xt, Xb);

    // Add regularization: X'X + lambda*I
    for (int i = 1; i < p + 1; ++i) {  // Don't regularize bias
        XtX[i][i] += lambda_;
    }

    // X'y
    std::vector<float> Xty(p + 1, 0.0f);
    for (int j = 0; j < p + 1; ++j) {
        for (int i = 0; i < n; ++i) {
            Xty[j] += Xt[j][i] * y[i];
        }
    }

    // Solve (X'X + lambda*I)^-1 * X'y
    auto XtXinv = inverse(XtX);

    weights_.resize(p);
    for (int j = 0; j < p + 1; ++j) {
        float sum = 0.0f;
        for (int k = 0; k < p + 1; ++k) {
            sum += XtXinv[j][k] * Xty[k];
        }
        if (j == 0) {
            bias_ = sum;
        } else {
            weights_[j - 1] = sum;
        }
    }

    isFitted_ = true;
}

float RidgeRegression::predict(const std::vector<float>& x) const {
    if (!isFitted_ || x.size() != weights_.size()) return 0.0f;

    float sum = bias_;
    for (size_t i = 0; i < weights_.size(); ++i) {
        sum += weights_[i] * x[i];
    }
    return sum;
}

std::vector<float> RidgeRegression::predictBatch(const std::vector<std::vector<float>>& X) const {
    std::vector<float> predictions;
    for (const auto& x : X) {
        predictions.push_back(predict(x));
    }
    return predictions;
}

std::vector<std::vector<float>> RidgeRegression::matMul(
    const std::vector<std::vector<float>>& A,
    const std::vector<std::vector<float>>& B) const {

    int m = A.size();
    int n = B[0].size();
    int k = A[0].size();

    std::vector<std::vector<float>> C(m, std::vector<float>(n, 0.0f));
    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < n; ++j) {
            for (int l = 0; l < k; ++l) {
                C[i][j] += A[i][l] * B[l][j];
            }
        }
    }
    return C;
}

std::vector<std::vector<float>> RidgeRegression::transpose(
    const std::vector<std::vector<float>>& A) const {

    int m = A.size();
    int n = A[0].size();

    std::vector<std::vector<float>> T(n, std::vector<float>(m));
    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < n; ++j) {
            T[j][i] = A[i][j];
        }
    }
    return T;
}

std::vector<std::vector<float>> RidgeRegression::inverse(
    std::vector<std::vector<float>> A) const {

    int n = A.size();
    std::vector<std::vector<float>> I(n, std::vector<float>(n, 0.0f));
    for (int i = 0; i < n; ++i) I[i][i] = 1.0f;

    // Gauss-Jordan elimination
    for (int i = 0; i < n; ++i) {
        // Find pivot
        int maxRow = i;
        for (int k = i + 1; k < n; ++k) {
            if (std::abs(A[k][i]) > std::abs(A[maxRow][i])) {
                maxRow = k;
            }
        }
        std::swap(A[i], A[maxRow]);
        std::swap(I[i], I[maxRow]);

        if (std::abs(A[i][i]) < 1e-10f) continue;

        // Scale pivot row
        float pivot = A[i][i];
        for (int j = 0; j < n; ++j) {
            A[i][j] /= pivot;
            I[i][j] /= pivot;
        }

        // Eliminate column
        for (int k = 0; k < n; ++k) {
            if (k != i) {
                float factor = A[k][i];
                for (int j = 0; j < n; ++j) {
                    A[k][j] -= factor * A[i][j];
                    I[k][j] -= factor * I[i][j];
                }
            }
        }
    }

    return I;
}

// ============= EnsemblePredictor Implementation =============

EnsemblePredictor::EnsemblePredictor() : neuralModel_(15, 8) {}

void EnsemblePredictor::addSample(const std::vector<float>& features, float target) {
    trainingFeatures_.push_back(features);
    trainingTargets_.push_back(target);
}

void EnsemblePredictor::train(int epochs) {
    if (trainingFeatures_.empty()) return;

    // Train linear model online
    for (size_t i = 0; i < trainingFeatures_.size(); ++i) {
        linearModel_.train(trainingFeatures_[i], trainingTargets_[i]);
    }

    // Train neural network batch
    neuralModel_.trainBatch(trainingFeatures_, trainingTargets_, epochs);

    // Train ridge regression
    ridgeModel_.fit(trainingFeatures_, trainingTargets_);
}

float EnsemblePredictor::predict(const std::vector<float>& features) {
    float linearPred = linearModel_.predict(features);
    float neuralPred = neuralModel_.predict(features);
    float ridgePred = ridgeModel_.predict(features);

    return linearWeight_ * linearPred +
           neuralWeight_ * neuralPred +
           ridgeWeight_ * ridgePred;
}

void EnsemblePredictor::reset() {
    linearModel_.reset();
    neuralModel_.reset();
    ridgeModel_ = RidgeRegression(0.1f);
    trainingFeatures_.clear();
    trainingTargets_.clear();
}
