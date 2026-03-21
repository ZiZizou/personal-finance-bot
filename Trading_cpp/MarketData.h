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
    // Existing basic fields
    double pe_ratio;
    double market_cap;
    double fifty_day_avg;

    // Valuation metrics
    double forward_pe;
    double peg_ratio;
    double price_to_book;
    double price_to_sales;
    double enterprise_to_revenue;
    double enterprise_to_ebitda;

    // Financial Health
    double debt_to_equity;
    double current_ratio;
    double quick_ratio;
    double total_cash;
    double total_debt;
    double free_cashflow;

    // Growth metrics
    double earnings_growth;
    double revenue_growth;
    double eps;
    double revenue_per_share;

    // Margins
    double gross_margin;
    double operating_margin;
    double profit_margin;

    // Price/Volume metrics
    double fifty_two_week_high;
    double fifty_two_week_low;
    double two_hundred_day_avg;
    double beta;
    double avg_volume;
    double avg_volume_10day;

    // Dividends
    double dividend_yield;
    double dividend_rate;
    double payout_ratio;

    // Company Info
    std::string sector;
    std::string industry;
    std::string market_state;

    // Analyst & Sentiment
    double analyst_rating;  // 1-5 scale (1=strong buy, 5=strong sell)
    double short_percent_float;
    double short_ratio;
    double institutional_ownership_pct;

    // Earnings
    double earnings_estimate_avg;
    double earnings_estimate_low;
    double earnings_estimate_high;
    std::string earnings_date;
    double earnings_surprise_pct;
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
