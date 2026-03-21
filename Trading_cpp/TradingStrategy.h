#pragma once
#include <string>
#include <vector>
#include <optional>
#include "MarketData.h"
#include "TechnicalAnalysis.h" // For SupportResistance struct
#include "MLPredictor.h"      // New ML integration
#include "FundamentalScorer.h" // For comprehensive fundamental scoring

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
    double stopLoss; // Stop loss price
    double takeProfit; // Take profit price
    std::string reason;
    std::optional<OptionSignal> option;

    // For Hold signals:
    double prospectiveBuy;
    double prospectiveSell;

    // ML Prediction debug
    double mlForecast;
};

// VIX Data for volatility analysis
struct VIXData {
    double current;  // Current VIX value
    double sma20;    // 20-day simple moving average
    double trend;    // VIX momentum (current - sma20), positive = rising
};

// Regime Detection
// Returns: "Bull", "Bear", "Sideways", "HighVol"
std::string detectMarketRegime(const std::vector<double>& prices);

// Updated Signature with VIX
Signal generateSignal(const std::string& symbol,
                      const std::vector<Candle>& candles,
                      double sentimentScore,
                      const Fundamentals& fund,
                      const OnChainData& onChain,
                      const SupportResistance& levels,
                      MLPredictor& mlModel,
                      const VIXData& vix);