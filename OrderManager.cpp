#include "OrderManager.h"
#include "json.hpp"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

// StateStore implementation

StateStore::StateStore(const std::string& filePath)
    : filePath_(filePath) {
    load();
}

StateStore::~StateStore() {
    save();
}

bool StateStore::hasOrderKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return orderKeys_.find(key) != orderKeys_.end();
}

void StateStore::putOrderKey(const std::string& key, const std::string& orderId) {
    std::lock_guard<std::mutex> lock(mutex_);
    orderKeys_[key] = orderId;
    // Auto-save after important updates
    save();
}

std::string StateStore::getOrderIdByKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = orderKeys_.find(key);
    if (it != orderKeys_.end()) {
        return it->second;
    }
    return "";
}

void StateStore::putLastBarTs(const std::string& symbol, TimePoint ts) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastBarTs_[symbol] = TimeUtils::toUnixSeconds(ts);
}

std::optional<TimePoint> StateStore::getLastBarTs(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = lastBarTs_.find(symbol);
    if (it != lastBarTs_.end()) {
        return TimeUtils::fromUnixSeconds(it->second);
    }
    return std::nullopt;
}

void StateStore::save() {
    // Note: mutex already held by caller or called at destruction
    try {
        json j;
        j["orderKeys"] = orderKeys_;
        j["lastBarTs"] = lastBarTs_;

        // Write to temp file first, then rename (atomic on most systems)
        std::string tempPath = filePath_ + ".tmp";
        std::ofstream file(tempPath);
        if (file.is_open()) {
            file << j.dump(2);
            file.close();

            // Rename temp to actual file
            std::rename(tempPath.c_str(), filePath_.c_str());
        }
    } catch (const std::exception& e) {
        std::cerr << "Error saving state: " << e.what() << std::endl;
    }
}

void StateStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::ifstream file(filePath_);
        if (file.is_open()) {
            json j = json::parse(file);
            if (j.contains("orderKeys")) {
                orderKeys_ = j["orderKeys"].get<std::map<std::string, std::string>>();
            }
            if (j.contains("lastBarTs")) {
                lastBarTs_ = j["lastBarTs"].get<std::map<std::string, int64_t>>();
            }
        }
    } catch (const std::exception& e) {
        // File might not exist on first run - that's OK
        // std::cerr << "Error loading state: " << e.what() << std::endl;
    }
}

// OrderManager implementation

OrderManager::OrderManager(IBroker& broker, StateStore& store)
    : broker_(broker), store_(store) {}

Result<std::string> OrderManager::submitOnce(const std::string& key, const OrderRequest& req) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if we already have this order
    if (store_.hasOrderKey(key)) {
        return Result<std::string>::ok(store_.getOrderIdByKey(key));
    }

    // Build request with client order ID
    OrderRequest request = req;
    request.clientOrderId = key;

    // Retry loop with exponential backoff
    for (int attempt = 0; attempt < maxRetries_; ++attempt) {
        auto result = broker_.placeOrder(request);

        if (result.isOk()) {
            std::string orderId = result.value();
            store_.putOrderKey(key, orderId);
            return Result<std::string>::ok(orderId);
        }

        // Check if error is retryable
        if (!isRetryableError(result.error())) {
            return result;  // Non-retryable error
        }

        // Backoff before retry
        if (attempt < maxRetries_ - 1) {
            int backoffMs = calculateBackoff(attempt);
            std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
        }
    }

    return Result<std::string>::err(Error::internal("Max retries exceeded"));
}

bool OrderManager::isRetryableError(const Error& error) const {
    // Network errors and rate limit errors are retryable
    return error.code == Error::NetworkError ||
           error.code == Error::RateLimitError ||
           error.code == Error::TimeoutError;
}

int OrderManager::calculateBackoff(int attempt) const {
    // Exponential backoff: initial * 2^attempt
    return initialBackoffMs_ * (1 << attempt);
}
