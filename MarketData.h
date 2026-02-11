#pragma once
#include <string>
#include <vector>
#include <chrono>
#include "TimeUtils.h"

struct Candle {
    TimePoint ts;       // Timestamp as chrono time_point
    std::string date;   // Kept for backward compatibility during transition
    double open;
    double high;
    double low;
    double close;
    int64_t volume;
};

struct Fundamentals {
    double pe_ratio;
    double market_cap;
    double fifty_day_avg;
    bool valid;
};

struct OnChainData {
    double net_inflow;      // Exchange Net Flow
    double large_tx_count;  // Whale transactions
    bool valid;
};

// Data Fetching Interface
std::vector<Candle> fetchCandles(const std::string& symbol, const std::string& type);
Fundamentals fetchFundamentals(const std::string& symbol, const std::string& type);
OnChainData fetchOnChainData(const std::string& symbol);
