#pragma once
#include "MarketData.h"
#include <vector>
#include <optional>

// BarSeries: Container for candles with append-only ordering guarantee
// Prevents duplicate or out-of-order bar processing
class BarSeries {
public:
    BarSeries() = default;

    // Initialize with historical data
    explicit BarSeries(const std::vector<Candle>& history) : bars_(history) {
        // Ensure bars are sorted by timestamp
        sortBars();
    }

    // Try to append a new bar. Returns true if appended, false if rejected (duplicate/out-of-order)
    bool tryAppend(const Candle& c) {
        if (!bars_.empty() && c.ts <= bars_.back().ts) {
            return false;  // Reject: timestamp not newer than last bar
        }
        bars_.push_back(c);
        return true;
    }

    // Get all bars (read-only)
    const std::vector<Candle>& bars() const { return bars_; }

    // Get number of bars
    size_t size() const { return bars_.size(); }

    // Check if empty
    bool empty() const { return bars_.empty(); }

    // Get last bar (if any)
    std::optional<Candle> lastBar() const {
        if (bars_.empty()) return std::nullopt;
        return bars_.back();
    }

    // Get bar at index
    const Candle& at(size_t idx) const { return bars_.at(idx); }

    // Get last N bars
    std::vector<Candle> lastN(size_t n) const {
        if (n >= bars_.size()) return bars_;
        return std::vector<Candle>(bars_.end() - n, bars_.end());
    }

    // Get timestamp of last bar
    std::optional<TimePoint> lastTimestamp() const {
        if (bars_.empty()) return std::nullopt;
        return bars_.back().ts;
    }

    // Check if we have a bar with this timestamp
    bool hasTimestamp(TimePoint ts) const {
        for (const auto& bar : bars_) {
            if (bar.ts == ts) return true;
        }
        return false;
    }

    // Clear all bars
    void clear() { bars_.clear(); }

    // Keep only the last N bars (for memory management in live trading)
    void trimToLast(size_t n) {
        if (bars_.size() > n) {
            bars_.erase(bars_.begin(), bars_.end() - n);
        }
    }

    // Extract close prices for indicator calculations
    std::vector<double> closePrices() const {
        std::vector<double> closes;
        closes.reserve(bars_.size());
        for (const auto& bar : bars_) {
            closes.push_back(bar.close);
        }
        return closes;
    }

private:
    std::vector<Candle> bars_;

    void sortBars() {
        std::sort(bars_.begin(), bars_.end(),
            [](const Candle& a, const Candle& b) { return a.ts < b.ts; });
    }
};
