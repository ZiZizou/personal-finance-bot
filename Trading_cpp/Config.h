#pragma once
#include <string>
#include <map>
#include <mutex>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "Result.h"

// Configuration Manager - Singleton pattern
// Loads API keys from environment variables and .env file
class Config {
private:
    std::map<std::string, std::string> config_;
    mutable std::mutex mutex_;
    bool initialized_ = false;

    // Private constructor for singleton
    Config() = default;

    // Load .env file into a map
    static std::map<std::string, std::string> loadEnvFile() {
        std::map<std::string, std::string> envMap;

        // Try to find .env in current directory and executable directory
        std::vector<std::string> envPaths;
        envPaths.push_back(".env");
        envPaths.push_back("../.env");
        envPaths.push_back("../../.env");
        envPaths.push_back("./.env");

        // Also check the directory where the executable is located
        try {
            std::string exePath = std::filesystem::current_path().string();
            envPaths.push_back(exePath + "/.env");
        } catch (...) {}

        for (const auto& path : envPaths) {
            if (std::filesystem::exists(path)) {
                std::ifstream file(path);
                if (file.is_open()) {
                    std::string line;
                    while (std::getline(file, line)) {
                        // Skip empty lines and comments
                        if (line.empty() || line[0] == '#') continue;

                        // Parse KEY=VALUE
                        size_t eqPos = line.find('=');
                        if (eqPos != std::string::npos) {
                            std::string key = line.substr(0, eqPos);
                            std::string value = line.substr(eqPos + 1);

                            // Trim whitespace
                            key.erase(0, key.find_first_not_of(" \t"));
                            key.erase(key.find_last_not_of(" \t") + 1);
                            value.erase(0, value.find_first_not_of(" \t"));
                            value.erase(value.find_last_not_of(" \t") + 1);

                            // Remove quotes if present
                            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                                value = value.substr(1, value.size() - 2);
                            }
                            if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
                                value = value.substr(1, value.size() - 2);
                            }

                            envMap[key] = value;
                        }
                    }
                    break;
                }
            }
        }
        return envMap;
    }

    // Static map to cache .env file contents
    static std::map<std::string, std::string>& getEnvCache() {
        static std::map<std::string, std::string> cache = loadEnvFile();
        return cache;
    }

    // Helper to get environment variable (checks .env file first, then system env)
    static std::string getEnv(const std::string& key) {
        // First check .env file cache
        auto& cache = getEnvCache();
        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second;
        }

        // Then check system environment variable
        char* val = std::getenv(key.c_str());
        return val ? std::string(val) : "";
    }

public:
    // Get environment variable (checks .env file first, then system env)
    static std::string getEnvVar(const std::string& key) {
        return getEnv(key);
    }

    // Delete copy/move constructors
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    Config(Config&&) = delete;
    Config& operator=(Config&&) = delete;

    // Get singleton instance
    static Config& getInstance() {
        static Config instance;
        return instance;
    }

    // Initialize configuration from environment
    Result<void> initialize() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (initialized_) {
            return Result<void>::ok();
        }

        // Required API keys
        struct KeyMapping {
            std::string envVar;
            std::string configKey;
            bool required;
        };

        std::vector<KeyMapping> keys = {
            {"NEWS_KEY", "NEWSAPI", false},          // NewsAPI for news headlines
            {"ALPHA_VANTAGE_KEY", "ALPHAVANTAGE", false}, // Alpha Vantage (optional backup)
            {"STOCK_TELEGRAM_BOT_TOKEN", "TELEGRAM_BOT_TOKEN", false}, // Telegram bot token
            {"STOCK_TELEGRAM_CHAT_ID", "TELEGRAM_CHAT_ID", false}, // Telegram chat ID
        };

        std::vector<std::string> missingRequired;

        for (const auto& key : keys) {
            std::string value = getEnv(key.envVar);
            if (!value.empty()) {
                config_[key.configKey] = value;
            } else if (key.required) {
                missingRequired.push_back(key.envVar);
            }
        }

        if (!missingRequired.empty()) {
            std::string msg = "Missing required environment variables: ";
            for (size_t i = 0; i < missingRequired.size(); ++i) {
                if (i > 0) msg += ", ";
                msg += missingRequired[i];
            }
            return Result<void>::err(Error::validation(msg));
        }

        // Optional configuration
        std::string logLevel = getEnv("LOG_LEVEL");
        if (!logLevel.empty()) {
            config_["LOG_LEVEL"] = logLevel;
        }

        std::string cacheDir = getEnv("CACHE_DIR");
        if (!cacheDir.empty()) {
            config_["CACHE_DIR"] = cacheDir;
        } else {
            config_["CACHE_DIR"] = ".cache";
        }

        std::string dataDir = getEnv("DATA_DIR");
        if (!dataDir.empty()) {
            config_["DATA_DIR"] = dataDir;
        } else {
            config_["DATA_DIR"] = ".";
        }

        // ONNX model configuration
        std::string onnxModelPath = getEnv("ONNX_MODEL_PATH");
        if (!onnxModelPath.empty()) {
            config_["ONNX_MODEL_PATH"] = onnxModelPath;
        } else {
            config_["ONNX_MODEL_PATH"] = "models/stock_predictor.onnx";
        }

        // Use ONNX model instead of native (disabled by default)
        std::string useOnnx = getEnv("USE_ONNX_MODEL");
        if (!useOnnx.empty()) {
            config_["USE_ONNX_MODEL"] = useOnnx;
        } else {
            config_["USE_ONNX_MODEL"] = "false";
        }

        // Python service integration
        std::string pythonServiceUrl = getEnv("PYTHON_SERVICE_URL");
        if (!pythonServiceUrl.empty()) {
            config_["PYTHON_SERVICE_URL"] = pythonServiceUrl;
        } else {
            config_["PYTHON_SERVICE_URL"] = "http://localhost:8000";
        }

        std::string usePythonService = getEnv("USE_PYTHON_SERVICE");
        if (!usePythonService.empty()) {
            config_["USE_PYTHON_SERVICE"] = usePythonService;
        } else {
            config_["USE_PYTHON_SERVICE"] = "false";
        }

        initialized_ = true;
        return Result<void>::ok();
    }

    // Check if initialized
    bool isInitialized() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_;
    }

    // Get API key (returns empty if not found)
    std::string getApiKey(const std::string& service) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = config_.find(service);
        return it != config_.end() ? it->second : "";
    }

    // Get API key with Result (for explicit error handling)
    Result<std::string> getApiKeyResult(const std::string& service) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = config_.find(service);
        if (it != config_.end() && !it->second.empty()) {
            return Result<std::string>::ok(it->second);
        }
        return Result<std::string>::err(Error::notFound("API key for " + service));
    }

    // Check if API key exists
    bool hasApiKey(const std::string& service) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = config_.find(service);
        return it != config_.end() && !it->second.empty();
    }

    // Get configuration value
    std::string get(const std::string& key, const std::string& defaultValue = "") const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = config_.find(key);
        return it != config_.end() ? it->second : defaultValue;
    }

    // Set configuration value (runtime override)
    void set(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_[key] = value;
    }

    // Validate configuration (no required keys currently - all APIs are optional)
    Result<void> validate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        // All API keys are optional - Yahoo Finance works without keys
        // NEWS_KEY and ALPHA_VANTAGE_KEY enhance functionality but are not required
        return Result<void>::ok();
    }

    // Get log level
    std::string getLogLevel() const {
        return get("LOG_LEVEL", "INFO");
    }

    // Get cache directory
    std::string getCacheDir() const {
        return get("CACHE_DIR", ".cache");
    }

    // Get data directory
    std::string getDataDir() const {
        return get("DATA_DIR", ".");
    }

    // Get ONNX model path
    std::string getONNXModelPath() const {
        return get("ONNX_MODEL_PATH", "models/stock_predictor.onnx");
    }

    // Get whether to use ONNX model
    bool getUseONNXModel() const {
        return getBool("USE_ONNX_MODEL", false);
    }

    // Get Python service URL
    std::string getPythonServiceUrl() const {
        return get("PYTHON_SERVICE_URL", "http://localhost:8000");
    }

    // Get whether to use Python service
    bool getUsePythonService() const {
        return getBool("USE_PYTHON_SERVICE", false);
    }

    // Get whether to use Python signals (dynamic model system)
    bool getUsePythonSignals() const {
        return getBool("USE_PYTHON_SIGNALS", false);
    }

    // Get max number of Python models (hot tickers)
    int getMaxPythonModels() const {
        return getInt("MAX_PYTHON_MODELS", 7);
    }

    // Get Python signals API URL
    std::string getPythonSignalsUrl() const {
        return get("PYTHON_SIGNALS_URL", "http://localhost:8000");
    }

    // Get string from environment or config
    std::string getString(const std::string& key, const std::string& defaultValue = "") const {
        // First check .env file and environment (using our helper that checks both)
        std::string val = getEnv(key);
        if (!val.empty()) return val;
        // Then check config map
        return get(key, defaultValue);
    }

    // Get int from environment or config
    int getInt(const std::string& key, int defaultValue = 0) const {
        std::string val = getString(key);
        if (val.empty()) return defaultValue;
        try {
            return std::stoi(val);
        } catch (...) {
            return defaultValue;
        }
    }

    // Get bool from environment or config
    bool getBool(const std::string& key, bool defaultValue = false) const {
        std::string val = getString(key);
        if (val.empty()) return defaultValue;
        return val == "true" || val == "1" || val == "yes" || val == "TRUE";
    }

    // Get double from environment or config
    double getDouble(const std::string& key, double defaultValue = 0.0) const {
        std::string val = getString(key);
        if (val.empty()) return defaultValue;
        try {
            return std::stod(val);
        } catch (...) {
            return defaultValue;
        }
    }

    // Print configuration (masked API keys)
    void printConfig() const {
        std::lock_guard<std::mutex> lock(mutex_);

        std::cout << "=== Configuration ===" << std::endl;
        for (const auto& [key, value] : config_) {
            if (key.find("KEY") != std::string::npos ||
                key.find("API") != std::string::npos ||
                key.find("SECRET") != std::string::npos) {
                // Mask sensitive values
                std::string masked = value.length() > 4 ?
                    value.substr(0, 2) + "****" + value.substr(value.length() - 2) :
                    "****";
                std::cout << "  " << key << ": " << masked << std::endl;
            } else {
                std::cout << "  " << key << ": " << value << std::endl;
            }
        }
        std::cout << "====================" << std::endl;
    }
};

// Helper function for quick initialization
inline Result<void> initializeConfig() {
    return Config::getInstance().initialize();
}

// Helper function to get API key (throws on missing required key)
inline std::string getApiKey(const std::string& service) {
    return Config::getInstance().getApiKey(service);
}

// Environment variable helper
class Environment {
public:
    static std::string get(const std::string& key, const std::string& defaultValue = "") {
        // Use Config's getEnv which checks .env file first, then system env
        std::string val = Config::getInstance().getEnvVar(key);
        return val.empty() ? defaultValue : val;
    }

    static int getInt(const std::string& key, int defaultValue = 0) {
        std::string val = get(key);
        if (val.empty()) return defaultValue;
        try {
            return std::stoi(val);
        } catch (...) {
            return defaultValue;
        }
    }

    static float getFloat(const std::string& key, float defaultValue = 0.0f) {
        std::string val = get(key);
        if (val.empty()) return defaultValue;
        try {
            return std::stof(val);
        } catch (...) {
            return defaultValue;
        }
    }

    static bool getBool(const std::string& key, bool defaultValue = false) {
        std::string val = get(key);
        if (val.empty()) return defaultValue;
        return val == "true" || val == "1" || val == "yes" || val == "TRUE";
    }
};
