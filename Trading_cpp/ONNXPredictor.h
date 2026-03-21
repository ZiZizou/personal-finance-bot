#pragma once
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include "ONNXInference.h"

// ONNX-based Predictor that wraps ONNX Runtime inference
// Implements the same interface as MLPredictor for easy integration
class ONNXPredictor {
private:
    std::unique_ptr<ONNXInference> inference_;
    bool isLoaded_ = false;

    // Feature configuration (must match training)
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

    // Model metadata
    std::string modelPath_;
    int inferenceCount_ = 0;
    double sumPrediction_ = 0.0;

public:
    ONNXPredictor();
    explicit ONNXPredictor(const std::string& modelPath);

    // Load ONNX model from file
    bool loadModel(const std::string& modelPath);

    // Predict next % change (returns prediction)
    // Features must be size TOTAL_FEATURE_COUNT (15)
    double predict(const std::vector<double>& features) const;

    // Predict with float vector (for ONNX inference)
    float predictFloat(const std::vector<float>& features) const;

    // Helper to extract features from raw data (same as MLPredictor)
    std::vector<double> extractFeatures(double rsi, double macdHist, double sentiment,
                                       double garchVol, int cyclePeriod, int dayIndex);

    // Add return to history for lagged features
    void addReturn(double dailyReturn);

    // Training is done externally in Python - these are no-ops
    void train(const std::vector<double>& features, double target) {
        (void)features; (void)target;
        // No-op: ONNX models are pre-trained
    }

    void trainBatch(const std::vector<std::vector<double>>& features,
                    const std::vector<double>& targets,
                    double validationSplit = 0.2,
                    int epochs = 100) {
        (void)features; (void)targets; (void)validationSplit; (void)epochs;
        // No-op: ONNX models are pre-trained
    }

    // Get/set hyperparameters (for interface compatibility)
    double getLearningRate() const { return 0.0; }
    void setLearningRate(double lr) { (void)lr; }
    double getRegularization() const { return 0.0; }
    void setRegularization(double reg) { (void)reg; }

    // Get training statistics
    int getTrainSamples() const { return 0; }
    double getRunningMSE() const { return 0.0; }

    // Check if model is loaded
    bool isLoaded() const { return isLoaded_; }

    // Reset model (re-loads from file)
    void reset();

    // Get inference count
    int getInferenceCount() const { return inferenceCount_; }

    // Get average prediction
    double getAveragePrediction() const {
        return inferenceCount_ > 0 ? sumPrediction_ / inferenceCount_ : 0.0;
    }

    // Get feature importance (not available for ONNX models)
    std::vector<std::pair<int, double>> getFeatureImportance() const;

    // Get model info
    std::string getModelPath() const { return modelPath_; }
    std::string getDeviceType() const {
        return inference_ ? inference_->getDeviceType() : "None";
    }
};

// Implementation
inline ONNXPredictor::ONNXPredictor() : isLoaded_(false) {
    inference_ = std::make_unique<ONNXInference>();
}

inline ONNXPredictor::ONNXPredictor(const std::string& modelPath) : isLoaded_(false) {
    inference_ = std::make_unique<ONNXInference>();
    loadModel(modelPath);
}

inline bool ONNXPredictor::loadModel(const std::string& modelPath) {
    modelPath_ = modelPath;
    isLoaded_ = inference_->loadModel(modelPath);

    if (isLoaded_) {
        std::cout << "ONNXPredictor loaded successfully" << std::endl;
        std::cout << "  Model: " << modelPath << std::endl;
        std::cout << "  Input size: " << inference_->getInputSize() << std::endl;
        std::cout << "  Output size: " << inference_->getOutputSize() << std::endl;
        std::cout << "  Device: " << inference_->getDeviceType() << std::endl;
    } else {
        std::cerr << "Failed to load ONNX model: " << modelPath << std::endl;
    }

    return isLoaded_;
}

inline double ONNXPredictor::predict(const std::vector<double>& features) const {
    if (!isLoaded_) {
        std::cerr << "ONNXPredictor: Model not loaded" << std::endl;
        return 0.0;
    }

    if (static_cast<int>(features.size()) != TOTAL_FEATURE_COUNT) {
        std::cerr << "ONNXPredictor: Feature size mismatch. Expected "
                  << TOTAL_FEATURE_COUNT << ", got " << features.size() << std::endl;
        return 0.0;
    }

    // Convert to float vector for ONNX (explicit conversion)
    std::vector<float> floatFeatures;
    floatFeatures.reserve(features.size());
    for (const auto& f : features) {
        floatFeatures.push_back(static_cast<float>(f));
    }
    return predictFloat(floatFeatures);
}

inline float ONNXPredictor::predictFloat(const std::vector<float>& features) const {
    if (!isLoaded_) {
        return 0.0f;
    }

    auto result = inference_->predict(features);
    if (!result.empty()) {
        return result[0];
    }
    return 0.0f;
}

inline std::vector<double> ONNXPredictor::extractFeatures(double rsi, double macdHist,
                                                          double sentiment, double garchVol,
                                                          int cyclePeriod, int dayIndex) {
    std::vector<double> features;
    features.reserve(TOTAL_FEATURE_COUNT);

    // Base features (normalized)
    // RSI: 0-100 -> 0-1
    features.push_back(rsi / 100.0);

    // MACD Hist: typically -0.5 to 0.5, already normalized
    {
        double val = macdHist;
        if (val < -1.0) val = -1.0;
        if (val > 1.0) val = 1.0;
        features.push_back(val);
    }

    // Sentiment: -1 to 1 -> 0 to 1
    features.push_back((sentiment + 1.0) / 2.0);

    // GARCH Volatility: normalize (typical range 0.01-0.5)
    {
        double val = garchVol * 10.0;
        if (val > 1.0) val = 1.0;
        features.push_back(val);
    }

    // Cycle phase: cos of period
    if (cyclePeriod > 0) {
        features.push_back(cos(2.0 * 3.14159 * dayIndex / cyclePeriod));
    } else {
        features.push_back(0.0);
    }

    // Lagged returns
    size_t histSize = returnHistory_.size();
    size_t laggedFeatures = static_cast<size_t>(LAGGED_FEATURES);
    size_t numLagged = (histSize < laggedFeatures) ? histSize : laggedFeatures;
    for (size_t i = 0; i < laggedFeatures; ++i) {
        if (i < numLagged) {
            // Use recent returns, pad with 0 if not enough history
            size_t idx = returnHistory_.size() - numLagged + i;
            features.push_back(returnHistory_[idx]);
        } else {
            features.push_back(0.0);
        }
    }

    // Cross features (manually computed for feature expansion)
    // RSI * Sentiment
    features.push_back(features[0] * features[2]);
    // RSI * Volatility
    features.push_back(features[0] * features[3]);
    // MACD * Volatility
    features.push_back(features[1] * features[3]);
    // Lagged return 1 * Lagged return 2
    if (LAGGED_FEATURES >= 2) {
        features.push_back(features[5] * features[6]);
    } else {
        features.push_back(0.0);
    }
    // Sentiment * Lagged return 1
    if (LAGGED_FEATURES >= 1) {
        features.push_back(features[2] * features[5]);
    } else {
        features.push_back(0.0);
    }

    return features;
}

inline void ONNXPredictor::addReturn(double dailyReturn) {
    returnHistory_.push_back(dailyReturn);
    if (returnHistory_.size() > MAX_HISTORY) {
        returnHistory_.erase(returnHistory_.begin());
    }
}

inline void ONNXPredictor::reset() {
    if (!modelPath_.empty()) {
        loadModel(modelPath_);
    }
    returnHistory_.clear();
    inferenceCount_ = 0;
    sumPrediction_ = 0.0;
}

inline std::vector<std::pair<int, double>> ONNXPredictor::getFeatureImportance() const {
    // Feature importance not available for ONNX models
    // Return uniform importance
    std::vector<std::pair<int, double>> importance;
    for (int i = 0; i < TOTAL_FEATURE_COUNT; ++i) {
        importance.emplace_back(i, 1.0 / TOTAL_FEATURE_COUNT);
    }
    return importance;
}
