#pragma once
#include <string>
#include <vector>
#include <optional>
#include "MarketData.h"
#include "TechnicalAnalysis.h" // For SupportResistance struct

struct OptionSignal {
    std::string type; // "call" or "put"
    float strike;
    int period_days; // Expiry duration
};

struct Signal {
    std::string action; // "buy", "sell", "hold"
    float entry;
    float exit; // Primary target (kept for compatibility)
    std::vector<float> targets; // List of potential targets
    float confidence; // 0-100%
    std::string reason;
    std::optional<OptionSignal> option;
    
    // For Hold signals:
    float prospectiveBuy;
    float prospectiveSell;
};

// Updated Signature
Signal generateSignal(const std::string& symbol, 
                      const std::vector<Candle>& candles, 
                      float sentimentScore,
                      const Fundamentals& fund,
                      const SupportResistance& levels);