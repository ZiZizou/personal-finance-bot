#pragma once
#include "FinancialSentiment.h"
#include "Result.h"
#include "ThreadPool.h"
#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <map>
#include <optional>

// Cache entry for sentiment and news
template<typename T>
struct CacheEntry {
    T value;
    std::chrono::steady_clock::time_point timestamp;

    bool isExpired(std::chrono::seconds ttl) const {
        auto now = std::chrono::steady_clock::now();
        return (now - timestamp) > ttl;
    }
};

// SentimentService configuration
struct SentimentServiceConfig {
    std::chrono::milliseconds inferenceTimeout{300};  // 300ms timeout
    std::chrono::seconds sentimentCacheTTL{1800};     // 30 min cache
    std::chrono::seconds newsCacheTTL{900};           // 15 min cache
    double neutralSentiment = 0.0;                     // Default when timeout
};

// SentimentService: Provides time-budgeted sentiment analysis with caching
class SentimentService {
public:
    using Config = SentimentServiceConfig;

    explicit SentimentService(const Config& config = Config());
    ~SentimentService();

    // Analyze headlines with timeout
    // Returns cached value or neutral if inference times out
    Result<double> analyzeWithTimeout(const std::vector<std::string>& headlines, const std::string& symbol);

    // Get cached sentiment (if available and not expired)
    std::optional<double> getCachedSentiment(const std::string& symbol) const;

    // Force cache update
    void updateCache(const std::string& symbol, double sentiment);

    // Clear cache
    void clearCache();

    // Get config
    const Config& getConfig() const { return config_; }

private:
    Config config_;
    mutable std::mutex mutex_;
    std::unique_ptr<ThreadPool> inferencePool_;  // Single thread for inference

    // Cache: symbol -> sentiment score
    std::map<std::string, CacheEntry<double>> sentimentCache_;

    // Internal analysis method
    double doAnalysis(const std::vector<std::string>& headlines);
};

// NewsCache: Caches news headlines per symbol
class NewsCache {
public:
    explicit NewsCache(std::chrono::seconds ttl = std::chrono::seconds(900));

    // Get cached headlines (if not expired)
    std::optional<std::vector<std::string>> get(const std::string& symbol) const;

    // Update cache
    void put(const std::string& symbol, const std::vector<std::string>& headlines);

    // Check if cache is valid
    bool isValid(const std::string& symbol) const;

    // Clear cache
    void clear();

    // Set TTL
    void setTTL(std::chrono::seconds ttl) { ttl_ = ttl; }

private:
    std::chrono::seconds ttl_;
    mutable std::mutex mutex_;
    std::map<std::string, CacheEntry<std::vector<std::string>>> cache_;
};

// Combined news + sentiment fetcher with caching
class CachedSentimentProvider {
public:
    CachedSentimentProvider();

    // Get sentiment for a symbol (fetches news if needed, caches both)
    Result<double> getSentiment(const std::string& symbol, bool forceRefresh = false);

    // Configure timeouts
    void setInferenceTimeout(std::chrono::milliseconds timeout);
    void setNewsTTL(std::chrono::seconds ttl);
    void setSentimentTTL(std::chrono::seconds ttl);

private:
    SentimentService sentimentService_;
    NewsCache newsCache_;
    std::chrono::seconds sentimentTTL_{1800};

    // Fetch news headlines for a symbol
    std::vector<std::string> fetchNews(const std::string& symbol);
};
