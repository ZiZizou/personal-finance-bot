#pragma once
#include "Broker.h"
#include "Result.h"
#include "TimeUtils.h"
#include <map>
#include <mutex>
#include <chrono>
#include <thread>

// StateStore: Persistent storage for order keys and state
// Initial implementation uses in-memory storage with JSON file backup
class StateStore {
public:
    explicit StateStore(const std::string& filePath = "state.json");
    ~StateStore();

    // Order key management
    bool hasOrderKey(const std::string& key) const;
    void putOrderKey(const std::string& key, const std::string& orderId);
    std::string getOrderIdByKey(const std::string& key) const;

    // Last processed bar timestamp
    void putLastBarTs(const std::string& symbol, TimePoint ts);
    std::optional<TimePoint> getLastBarTs(const std::string& symbol) const;

    // Persist to disk
    void save();
    void load();

private:
    std::string filePath_;
    mutable std::mutex mutex_;
    std::map<std::string, std::string> orderKeys_;  // key -> orderId
    std::map<std::string, int64_t> lastBarTs_;       // symbol -> unix timestamp
};

// OrderManager: Handles order submission with idempotency and retry logic
class OrderManager {
public:
    explicit OrderManager(IBroker& broker, StateStore& store);

    // Submit an order with idempotency key
    // If key already exists, returns the existing order ID
    Result<std::string> submitOnce(const std::string& key, const OrderRequest& req);

    // Retry configuration
    void setMaxRetries(int maxRetries) { maxRetries_ = maxRetries; }
    void setInitialBackoffMs(int ms) { initialBackoffMs_ = ms; }

private:
    IBroker& broker_;
    StateStore& store_;
    std::mutex mutex_;

    int maxRetries_ = 3;
    int initialBackoffMs_ = 100;

    bool isRetryableError(const Error& error) const;
    int calculateBackoff(int attempt) const;
};

// Generate a unique order key based on symbol and bar timestamp
inline std::string generateOrderKey(const std::string& symbol, TimePoint barTs) {
    return symbol + "_" + std::to_string(TimeUtils::toUnixSeconds(barTs));
}
