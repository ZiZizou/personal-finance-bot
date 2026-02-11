#pragma once
#include <string>
#include <vector>
#include <optional>
#include "MarketData.h"
#include "TechnicalAnalysis.h" // For SupportResistance struct
#include "MLPredictor.h"      // New ML integration

struct OptionSignal {
    std::string type; // "call" or "put"
    double strike;
    int period_days; // Expiry duration
};

struct Signal {
    std::string action; // "buy", "sell", "hold"
    double entry;
    double exit; // Primary target (kept for compatibility)
    std::vector<double> targets; // List of potential targets
    double confidence; // 0-100%
    std::string reason;
    std::optional<OptionSignal> option;

    // For Hold signals:
    double prospectiveBuy;
    double prospectiveSell;

    // ML Prediction debug
    double mlForecast;
};

// Regime Detection
// Returns: "Bull", "Bear", "Sideways", "HighVol"
std::string detectMarketRegime(const std::vector<double>& prices);

// Updated Signature
Signal generateSignal(const std::string& symbol,
                      const std::vector<Candle>& candles,
                      double sentimentScore,
                      const Fundamentals& fund,
                      const OnChainData& onChain,
                      const SupportResistance& levels,
                      MLPredictor& mlModel); // Pass by ref to allow state update if we were training inline (though we likely train before)