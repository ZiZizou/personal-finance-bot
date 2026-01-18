#pragma once
#include <string>
#include <vector>

struct Candle {
    std::string date;
    float open;
    float high;
    float low;
    float close;
    long long volume;
};

struct Fundamentals {
    float pe_ratio;      
    float market_cap;    
    float fifty_day_avg; 
    bool valid;          
};

struct OnChainData {
    float net_inflow;      // Exchange Net Flow
    float large_tx_count;  // Whale transactions
    bool valid;
};

// Data Fetching Interface
std::vector<Candle> fetchCandles(const std::string& symbol, const std::string& type);
Fundamentals fetchFundamentals(const std::string& symbol, const std::string& type);
OnChainData fetchOnChainData(const std::string& symbol);
