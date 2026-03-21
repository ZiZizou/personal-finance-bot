#pragma once
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <fstream>

// Try to include ONNX Runtime headers
#ifdef USE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

// Helper to convert string to wstring for Windows
#ifdef _WIN32
#include <windows.h>
inline std::wstring to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// Helper to load ONNX DLL from vcpkg path explicitly
inline void loadONNXRuntimeDLL() {
    std::string dllPath = "C:\\Users\\Atharva\\vcpkg\\installed\\x64-windows\\bin\\onnxruntime.dll";
    HMODULE h = LoadLibraryA(dllPath.c_str());
    if (h) {
        std::cout << "Loaded ONNX Runtime from: " << dllPath << std::endl;
    } else {
        std::cerr << "Failed to load ONNX Runtime from: " << dllPath << std::endl;
    }
}
#else
// For non-Windows, use simple conversion (assuming ASCII or UTF-8)
inline std::wstring to_wstring(const std::string& str) {
    return std::wstring(str.begin(), str.end());
}
#endif

// ONNX Runtime inference wrapper with OpenVINO support for Intel Arc GPU
class ONNXInference {
private:
#ifdef USE_ONNXRUNTIME
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    std::unique_ptr<Ort::SessionOptions> sessionOptions_;
    
    // Store strings to avoid dangling pointers
    std::vector<std::string> inputNodeNames_;
    std::vector<std::string> outputNodeNames_;
    
    // Pointers for ONNX Runtime API
    std::vector<const char*> inputNames_;
    std::vector<const char*> outputNames_;
    
    std::vector<int64_t> inputShape_;
    std::vector<int64_t> outputShape_;
#endif

    bool isLoaded_ = false;
    std::string modelPath_;
    std::string deviceType_;  // "GPU", "CPU"
    int inputSize_ = 0;
    int outputSize_ = 0;

public:
    ONNXInference();
    ~ONNXInference();

    // Load ONNX model from file
    // Returns true if successful, false otherwise
    bool loadModel(const std::string& modelPath);

    // Check if model is loaded
    bool isLoaded() const { return isLoaded_; }

    // Run inference on input features
    // Input: feature vector of size inputSize_
    // Output: prediction vector of size outputSize_
    std::vector<float> predict(const std::vector<float>& features);

    // Run inference with raw float pointer
    float* predictRaw(float* features, int featureSize, float* output, int outputSize);

    // Get model input size
    int getInputSize() const { return inputSize_; }

    // Get model output size
    int getOutputSize() const { return outputSize_; }

    // Get device type
    std::string getDeviceType() const { return deviceType_; }

    // Get model path
    std::string getModelPath() const { return modelPath_; }

    // Check if GPU is available
    static bool isGPUAvailable();

    // Get available execution providers
    static std::vector<std::string> getAvailableProviders();
};

// Inline implementation for header-only mode (when ONNX Runtime is not available)
#ifndef USE_ONNXRUNTIME

inline ONNXInference::ONNXInference() : isLoaded_(false), deviceType_("None") {}

inline ONNXInference::~ONNXInference() {}

inline bool ONNXInference::loadModel(const std::string& modelPath) {
    std::cerr << "ONNX Runtime not available. Please compile with USE_ONNXRUNTIME defined." << std::endl;
    return false;
}

inline std::vector<float> ONNXInference::predict(const std::vector<float>& features) {
    return std::vector<float>();
}

inline float* ONNXInference::predictRaw(float* features, int featureSize, float* output, int outputSize) {
    return nullptr;
}

inline bool ONNXInference::isGPUAvailable() {
    return false;
}

inline std::vector<std::string> ONNXInference::getAvailableProviders() {
    return std::vector<std::string>();
}

#else // USE_ONNXRUNTIME defined

// Implementation with ONNX Runtime
inline ONNXInference::ONNXInference()
    : isLoaded_(false), deviceType_("CPU") {
    // Note: Environment is created lazily in loadModel() to avoid DLL loading when not needed
}

inline ONNXInference::~ONNXInference() {
    // Ort::Session and other resources will be automatically freed
}

inline bool ONNXInference::loadModel(const std::string& modelPath) {
    try {
        modelPath_ = modelPath;

        // Reset previous state
        inputNodeNames_.clear();
        outputNodeNames_.clear();
        inputNames_.clear();
        outputNames_.clear();

        // Check if file exists
        std::ifstream file(modelPath);
        if (!file.good()) {
            std::cerr << "ONNX model file not found: " << modelPath << std::endl;
            return false;
        }
        file.close();

        // Load ONNX Runtime DLL explicitly from vcpkg path
        loadONNXRuntimeDLL();

        // Create environment (only when actually loading a model)
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "TradingBotONNX");

        // Create session options
        sessionOptions_ = std::make_unique<Ort::SessionOptions>();

        // Set optimization level
        sessionOptions_->SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

        // Get available providers and log them
        std::vector<std::string> providers = getAvailableProviders();
        std::cout << "Available ONNX providers: ";
        for (const auto& p : providers) {
            std::cout << p << " ";
        }
        std::cout << std::endl;

        // Use CPU by default (OpenVINO/CUDA require specific build configurations)
        // The ONNX Runtime version in vcpkg may not have GPU support compiled in
        deviceType_ = "CPU";
        std::cout << "Using CPU for ONNX inference" << std::endl;

        // Create session (use wide string for Windows)
        std::wstring wmodelPath = to_wstring(modelPath);
        session_ = std::make_unique<Ort::Session>(*env_, wmodelPath.c_str(), *sessionOptions_);

        // Get input/output names and shapes
        Ort::AllocatorWithDefaultOptions allocator;

        // Get input info
        size_t numInputNodes = session_->GetInputCount();
        if (numInputNodes == 0) {
            std::cerr << "ONNX model has no inputs" << std::endl;
            return false;
        }

        // Get first input name and shape
        // Version-agnostic way to handle name retrieval (ORT 1.12+ uses AllocatedStringPtr)
#if ORT_API_VERSION >= 12
        auto inputNameAllocated = session_->GetInputNameAllocated(0, allocator);
        inputNodeNames_.push_back(inputNameAllocated.get());
#else
        char* inputNameRaw = session_->GetInputName(0, allocator);
        inputNodeNames_.push_back(inputNameRaw);
        allocator.Free(inputNameRaw);
#endif
        inputNames_.push_back(inputNodeNames_.back().c_str());

        auto inputShape = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        inputShape_ = inputShape;

        // Calculate input size
        inputSize_ = 1;
        for (auto dim : inputShape) {
            if (dim > 0) {
                inputSize_ *= static_cast<int>(dim);
            }
        }

        // Get output info
        size_t numOutputNodes = session_->GetOutputCount();
        if (numOutputNodes == 0) {
            std::cerr << "ONNX model has no outputs" << std::endl;
            return false;
        }

        // Get first output name and shape
#if ORT_API_VERSION >= 12
        auto outputNameAllocated = session_->GetOutputNameAllocated(0, allocator);
        outputNodeNames_.push_back(outputNameAllocated.get());
#else
        char* outputNameRaw = session_->GetOutputName(0, allocator);
        outputNodeNames_.push_back(outputNameRaw);
        allocator.Free(outputNameRaw);
#endif
        outputNames_.push_back(outputNodeNames_.back().c_str());

        auto outputShape = session_->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        outputShape_ = outputShape;

        // Calculate output size
        outputSize_ = 1;
        for (auto dim : outputShape) {
            if (dim > 0) {
                outputSize_ *= static_cast<int>(dim);
            }
        }

        isLoaded_ = true;
        std::cout << "ONNX model loaded successfully from: " << modelPath << std::endl;
        std::cout << "  Input size: " << inputSize_ << ", Output size: " << outputSize_ << std::endl;
        std::cout << "  Device: " << deviceType_ << std::endl;

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Error loading ONNX model: " << e.what() << std::endl;
        return false;
    }
}

inline std::vector<float> ONNXInference::predict(const std::vector<float>& features) {
    if (!isLoaded_) {
        std::cerr << "ONNX model not loaded" << std::endl;
        return std::vector<float>();
    }

    if (static_cast<int>(features.size()) != inputSize_) {
        std::cerr << "Input size mismatch. Expected: " << inputSize_
                  << ", Got: " << features.size() << std::endl;
        return std::vector<float>();
    }

    try {
        // Create input tensor
        std::vector<int64_t> inputShape = inputShape_;
        // Handle dynamic shapes (set to 1 for batch dimension if -1)
        for (auto& dim : inputShape) {
            if (dim < 0) dim = 1;
        }

        auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo,
            const_cast<float*>(features.data()),
            features.size(),
            inputShape.data(),
            inputShape.size());

        // Run inference
        auto outputTensors = session_->Run(
            Ort::RunOptions{nullptr},
            inputNames_.data(),
            &inputTensor,
            1,
            outputNames_.data(),
            1);

        // Get output
        float* outputData = outputTensors[0].GetTensorMutableData<float>();
        std::vector<float> result(outputData, outputData + outputSize_);

        return result;

    } catch (const std::exception& e) {
        std::cerr << "Error running ONNX inference: " << e.what() << std::endl;
        return std::vector<float>();
    }
}

inline float* ONNXInference::predictRaw(float* features, int featureSize, float* output, int outputSize) {
    if (!isLoaded_) {
        std::cerr << "ONNX model not loaded" << std::endl;
        return nullptr;
    }

    if (featureSize != inputSize_ || outputSize != outputSize_) {
        std::cerr << "Size mismatch. Input: " << featureSize
                  << " (expected " << inputSize_ << "), Output: " << outputSize_
                  << " (expected " << outputSize_ << ")" << std::endl;
        return nullptr;
    }

    try {
        std::vector<int64_t> inputShape = inputShape_;
        for (auto& dim : inputShape) {
            if (dim < 0) dim = 1;
        }

        auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo,
            features,
            featureSize,
            inputShape.data(),
            inputShape.size());

        auto outputTensors = session_->Run(
            Ort::RunOptions{nullptr},
            inputNames_.data(),
            &inputTensor,
            1,
            outputNames_.data(),
            1);

        float* outputData = outputTensors[0].GetTensorMutableData<float>();
        std::memcpy(output, outputData, outputSize_ * sizeof(float));

        return output;

    } catch (const std::exception& e) {
        std::cerr << "Error running ONNX inference: " << e.what() << std::endl;
        return nullptr;
    }
}

inline bool ONNXInference::isGPUAvailable() {
    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "GPUMonitor");
        Ort::SessionOptions sessOpts;
        sessOpts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

        // Try CUDA provider
        std::vector<std::string> providers = getAvailableProviders();
        for (const auto& p : providers) {
            if (p == "CUDA" || p == "GPU") {
                return true;
            }
        }
        return false;
    } catch (...) {
        return false;
    }
}

inline std::vector<std::string> ONNXInference::getAvailableProviders() {
    std::vector<std::string> providers;

    try {
        // Get available execution providers from ONNX Runtime
        const auto& providerList = Ort::GetAvailableProviders();
        for (const auto& provider : providerList) {
            providers.push_back(std::string(provider));
        }
    } catch (...) {
        // If we can't get providers, return empty
    }

    return providers;
}

#endif // USE_ONNXRUNTIME
