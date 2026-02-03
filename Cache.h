#pragma once
#include <map>
#include <list>
#include <mutex>
#include <chrono>
#include <optional>
#include <string>
#include <functional>
#include <thread>

// Thread-safe LRU Cache with TTL support
template<typename Key, typename Value>
class LRUCache {
private:
    struct CacheEntry {
        Value value;
        std::chrono::steady_clock::time_point expiry;
    };

    size_t capacity_;
    std::chrono::seconds ttl_;

    // LRU list: front = most recently used, back = least recently used
    std::list<Key> lruList_;

    // Map from key to (iterator in list, cached value)
    std::map<Key, std::pair<typename std::list<Key>::iterator, CacheEntry>> cacheMap_;

    mutable std::mutex mutex_;

    void evictExpired() {
        auto now = std::chrono::steady_clock::now();
        auto it = cacheMap_.begin();
        while (it != cacheMap_.end()) {
            if (it->second.second.expiry < now) {
                lruList_.erase(it->second.first);
                it = cacheMap_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void evictLRU() {
        while (cacheMap_.size() >= capacity_ && !lruList_.empty()) {
            const Key& lruKey = lruList_.back();
            cacheMap_.erase(lruKey);
            lruList_.pop_back();
        }
    }

public:
    // Constructor with capacity and TTL (time-to-live in seconds)
    LRUCache(size_t capacity = 100, std::chrono::seconds ttl = std::chrono::seconds(300))
        : capacity_(capacity), ttl_(ttl) {}

    // Get value from cache
    std::optional<Value> get(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = cacheMap_.find(key);
        if (it == cacheMap_.end()) {
            return std::nullopt;  // Not found
        }

        // Check if expired
        auto now = std::chrono::steady_clock::now();
        if (it->second.second.expiry < now) {
            // Expired, remove and return nullopt
            lruList_.erase(it->second.first);
            cacheMap_.erase(it);
            return std::nullopt;
        }

        // Move to front of LRU list (most recently used)
        lruList_.erase(it->second.first);
        lruList_.push_front(key);
        it->second.first = lruList_.begin();

        return it->second.second.value;
    }

    // Put value into cache
    void put(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::steady_clock::now();
        auto expiry = now + ttl_;

        auto it = cacheMap_.find(key);
        if (it != cacheMap_.end()) {
            // Update existing entry
            it->second.second.value = value;
            it->second.second.expiry = expiry;

            // Move to front
            lruList_.erase(it->second.first);
            lruList_.push_front(key);
            it->second.first = lruList_.begin();
        } else {
            // Evict if needed
            evictExpired();
            evictLRU();

            // Insert new entry
            lruList_.push_front(key);
            CacheEntry entry{value, expiry};
            cacheMap_[key] = {lruList_.begin(), entry};
        }
    }

    // Put with custom TTL
    void put(const Key& key, const Value& value, std::chrono::seconds customTTL) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::steady_clock::now();
        auto expiry = now + customTTL;

        auto it = cacheMap_.find(key);
        if (it != cacheMap_.end()) {
            it->second.second.value = value;
            it->second.second.expiry = expiry;
            lruList_.erase(it->second.first);
            lruList_.push_front(key);
            it->second.first = lruList_.begin();
        } else {
            evictExpired();
            evictLRU();
            lruList_.push_front(key);
            CacheEntry entry{value, expiry};
            cacheMap_[key] = {lruList_.begin(), entry};
        }
    }

    // Check if key exists and is not expired
    bool contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = cacheMap_.find(key);
        if (it == cacheMap_.end()) {
            return false;
        }

        auto now = std::chrono::steady_clock::now();
        return it->second.second.expiry >= now;
    }

    // Remove a specific key
    bool remove(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = cacheMap_.find(key);
        if (it != cacheMap_.end()) {
            lruList_.erase(it->second.first);
            cacheMap_.erase(it);
            return true;
        }
        return false;
    }

    // Clear all entries
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cacheMap_.clear();
        lruList_.clear();
    }

    // Get current size
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cacheMap_.size();
    }

    // Get capacity
    size_t capacity() const {
        return capacity_;
    }

    // Set new capacity (may trigger evictions)
    void setCapacity(size_t newCapacity) {
        std::lock_guard<std::mutex> lock(mutex_);
        capacity_ = newCapacity;
        evictLRU();
    }

    // Set new TTL (affects new entries only)
    void setTTL(std::chrono::seconds newTTL) {
        std::lock_guard<std::mutex> lock(mutex_);
        ttl_ = newTTL;
    }

    // Get or compute: if key exists return cached value, otherwise compute and cache
    Value getOrCompute(const Key& key, std::function<Value()> compute) {
        auto cached = get(key);
        if (cached) {
            return *cached;
        }

        Value value = compute();
        put(key, value);
        return value;
    }

    // Clean up expired entries
    void cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        evictExpired();
    }

    // Get statistics
    struct Stats {
        size_t size;
        size_t capacity;
        std::chrono::seconds ttl;
    };

    Stats getStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return Stats{cacheMap_.size(), capacity_, ttl_};
    }
};

// Specialized string cache for API responses
class StringCache : public LRUCache<std::string, std::string> {
public:
    StringCache(size_t capacity = 100, int ttlSeconds = 300)
        : LRUCache(capacity, std::chrono::seconds(ttlSeconds)) {}

    // Hash URL for consistent keys
    static std::string hashKey(const std::string& url) {
        std::hash<std::string> hasher;
        return std::to_string(hasher(url));
    }
};

// Rate limiter for API calls
class RateLimiter {
private:
    struct DomainState {
        std::chrono::steady_clock::time_point lastRequest;
        int requestCount;
        std::chrono::steady_clock::time_point windowStart;
    };

    std::map<std::string, DomainState> domainStates_;
    int maxRequestsPerWindow_;
    std::chrono::seconds windowDuration_;
    std::chrono::milliseconds minInterval_;
    mutable std::mutex mutex_;

public:
    RateLimiter(int maxRequestsPerWindow = 60,
                std::chrono::seconds windowDuration = std::chrono::seconds(60),
                std::chrono::milliseconds minInterval = std::chrono::milliseconds(100))
        : maxRequestsPerWindow_(maxRequestsPerWindow),
          windowDuration_(windowDuration),
          minInterval_(minInterval) {}

    // Check if request is allowed (and record it)
    bool allowRequest(const std::string& domain) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::steady_clock::now();

        auto& state = domainStates_[domain];

        // Check minimum interval
        if (state.lastRequest.time_since_epoch().count() > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - state.lastRequest);
            if (elapsed < minInterval_) {
                return false;
            }
        }

        // Reset window if expired
        if (state.windowStart.time_since_epoch().count() == 0 ||
            now - state.windowStart > windowDuration_) {
            state.windowStart = now;
            state.requestCount = 0;
        }

        // Check rate limit
        if (state.requestCount >= maxRequestsPerWindow_) {
            return false;
        }

        // Record request
        state.lastRequest = now;
        state.requestCount++;

        return true;
    }

    // Wait until request is allowed
    void waitForAllowance(const std::string& domain) {
        while (!allowRequest(domain)) {
            std::this_thread::sleep_for(minInterval_);
        }
    }

    // Get wait time until next request is allowed (in milliseconds)
    std::chrono::milliseconds getWaitTime(const std::string& domain) const {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = domainStates_.find(domain);
        if (it == domainStates_.end()) {
            return std::chrono::milliseconds(0);
        }

        auto now = std::chrono::steady_clock::now();
        const auto& state = it->second;

        // Check if we need to wait for window reset
        if (state.requestCount >= maxRequestsPerWindow_) {
            auto windowEnd = state.windowStart + windowDuration_;
            if (windowEnd > now) {
                return std::chrono::duration_cast<std::chrono::milliseconds>(windowEnd - now);
            }
        }

        // Check minimum interval
        auto timeSinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - state.lastRequest);
        if (timeSinceLast < minInterval_) {
            return minInterval_ - timeSinceLast;
        }

        return std::chrono::milliseconds(0);
    }

    // Reset state for a domain
    void reset(const std::string& domain) {
        std::lock_guard<std::mutex> lock(mutex_);
        domainStates_.erase(domain);
    }

    // Reset all
    void resetAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        domainStates_.clear();
    }
};
