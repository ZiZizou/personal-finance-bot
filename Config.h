#pragma once
#include <string>
#include <map>
#include <mutex>
#include <cstdlib>
#include <stdexcept>
#include "Result.h"

// Configuration Manager - Singleton pattern
// Loads API keys from environment variables only (no hardcoded fallbacks for security)
class Config {
private:
    std::map<std::string, std::string> config_;
    mutable std::mutex mutex_;
    bool initialized_ = false;

    // Private constructor for singleton
    Config() = default;

    // Helper to get environment variable
    static std::string getEnv(const std::string& key) {
        char* val = std::getenv(key.c_str());
        return val ? std::string(val) : "";
    }

public:
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
        char* val = std::getenv(key.c_str());
        return val ? std::string(val) : defaultValue;
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
