#pragma once
#include <string>

struct Candle {
    std::string date;
    float open;
    float high;
    float low;
    float close;
    long long volume; // Added Volume
};

struct Fundamentals {
    float pe_ratio;      // Price-to-Earnings (0 if N/A)
    float market_cap;    // Market Cap
    float fifty_day_avg; // 50 Day Moving Average (from Yahoo)
    bool valid;          // True if fetch succeeded
};