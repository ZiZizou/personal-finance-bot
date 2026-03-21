#include "SentimentService.h"
#include "NewsManager.h"
#include <future>
#include <iostream>

// SentimentService implementation

SentimentService::SentimentService(const Config& config)
    : config_(config), inferencePool_(std::make_unique<ThreadPool>(1)) {
    // Single-threaded pool for inference to avoid model contention
}

SentimentService::~SentimentService() = default;

Result<double> SentimentService::analyzeWithTimeout(
    const std::vector<std::string>& headlines,
    const std::string& symbol
) {
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sentimentCache_.find(symbol);
        if (it != sentimentCache_.end() && !it->second.isExpired(config_.sentimentCacheTTL)) {
            return Result<double>::ok(it->second.value);
        }
    }

    // If no headlines, return neutral
    if (headlines.empty()) {
        return Result<double>::ok(config_.neutralSentiment);
    }

    // Submit analysis to inference thread
    auto future = inferencePool_->submit([this, headlines]() {
        return doAnalysis(headlines);
    });

    // Wait with timeout
    auto status = future.wait_for(config_.inferenceTimeout);

    if (status == std::future_status::ready) {
        double sentiment = future.get();

        // Update cache
        updateCache(symbol, sentiment);

        return Result<double>::ok(sentiment);
    } else {
        // Timeout - return cached value or neutral
        auto cached = getCachedSentiment(symbol);
        if (cached) {
            return Result<double>::ok(*cached);
        }
        return Result<double>::ok(config_.neutralSentiment);
    }
}

std::optional<double> SentimentService::getCachedSentiment(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sentimentCache_.find(symbol);
    if (it != sentimentCache_.end()) {
        // Return even if expired (as fallback)
        return it->second.value;
    }
    return std::nullopt;
}

void SentimentService::updateCache(const std::string& symbol, double sentiment) {
    std::lock_guard<std::mutex> lock(mutex_);
    sentimentCache_[symbol] = CacheEntry<double>{sentiment, std::chrono::steady_clock::now()};
}

void SentimentService::clearCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    sentimentCache_.clear();
}

double SentimentService::doAnalysis(const std::vector<std::string>& headlines) {
    try {
        auto& analyzer = SentimentAnalyzer::getInstance();
        return (double)analyzer.analyze(headlines);
    } catch (const std::exception& e) {
        std::cerr << "Sentiment analysis error: " << e.what() << std::endl;
        return 0.0;  // Neutral on error
    }
}

// NewsCache implementation

NewsCache::NewsCache(std::chrono::seconds ttl)
    : ttl_(ttl) {}

std::optional<std::vector<std::string>> NewsCache::get(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(symbol);
    if (it != cache_.end() && !it->second.isExpired(ttl_)) {
        return it->second.value;
    }
    return std::nullopt;
}

void NewsCache::put(const std::string& symbol, const std::vector<std::string>& headlines) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_[symbol] = CacheEntry<std::vector<std::string>>{headlines, std::chrono::steady_clock::now()};
}

bool NewsCache::isValid(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(symbol);
    return it != cache_.end() && !it->second.isExpired(ttl_);
}

void NewsCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

// CachedSentimentProvider implementation

CachedSentimentProvider::CachedSentimentProvider()
    : newsCache_(std::chrono::seconds(900)) {}

Result<double> CachedSentimentProvider::getSentiment(const std::string& symbol, bool forceRefresh) {
    // Get headlines (from cache or fetch)
    std::vector<std::string> headlines;

    if (!forceRefresh && newsCache_.isValid(symbol)) {
        auto cached = newsCache_.get(symbol);
        if (cached) {
            headlines = *cached;
        }
    }

    if (headlines.empty()) {
        headlines = fetchNews(symbol);
        if (!headlines.empty()) {
            newsCache_.put(symbol, headlines);
        }
    }

    // Analyze sentiment
    return sentimentService_.analyzeWithTimeout(headlines, symbol);
}

void CachedSentimentProvider::setInferenceTimeout(std::chrono::milliseconds timeout) {
    // Would need to recreate service to change this
    // For now, this is set at construction
}

void CachedSentimentProvider::setNewsTTL(std::chrono::seconds ttl) {
    newsCache_.setTTL(ttl);
}

void CachedSentimentProvider::setSentimentTTL(std::chrono::seconds ttl) {
    sentimentTTL_ = ttl;
}

std::vector<std::string> CachedSentimentProvider::fetchNews(const std::string& symbol) {
    try {
        return NewsManager::fetchNews(symbol);
    } catch (...) {
        return {};
    }
}
