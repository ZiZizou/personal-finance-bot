#pragma once
#include <vector>
#include <string>
#include <map>
#include "HiddenMarkovModel.h"

// Market Regime Detection using Hidden Markov Model
namespace RegimeDetection {

// Market regime types
enum class MarketRegime {
    BullLowVol = 0,    // Uptrend with low volatility
    BullHighVol = 1,   // Uptrend with high volatility
    BearLowVol = 2,    // Downtrend with low volatility
    BearHighVol = 3,   // Downtrend with high volatility
    Sideways = 4,      // Range-bound market
    Unknown = -1
};

// Regime information
struct RegimeInfo {
    MarketRegime regime;
    std::string name;
    double probability;
    std::string description;
};

// Feature extractor for regime detection
class RegimeFeatureExtractor {
public:
    // Extract features from price history for regime detection
    // Returns: {return_5d, volatility_20d, volume_ratio}
    static std::vector<double> extractFeatures(const std::vector<double>& closes,
                                                const std::vector<int64_t>& volumes,
                                                int lookback = 20) {
        std::vector<double> features(2, 0.0);  // 2 features by default

        if (closes.size() < lookback + 1) {
            return features;
        }

        // Feature 1: 5-day return (direction)
        // Normalized to reasonable range (-1 to 1 for -10% to +10%)
        double return5d = (closes.back() - closes[closes.size() - 5]) / closes[closes.size() - 5];
        features[0] = return5d * 10.0;  // Scale to roughly -1 to 1

        // Feature 2: 20-day realized volatility
        // Calculate daily returns
        std::vector<double> returns;
        for (size_t i = closes.size() - lookback; i < closes.size() - 1; ++i) {
            double r = (closes[i + 1] - closes[i]) / closes[i];
            returns.push_back(r);
        }

        if (!returns.empty()) {
            // Calculate standard deviation
            double mean = 0.0;
            for (double r : returns) mean += r;
            mean /= returns.size();

            double var = 0.0;
            for (double r : returns) var += (r - mean) * (r - mean);
            double stdDev = std::sqrt(var / returns.size());

            // Annualize (roughly) and scale to reasonable range
            features[1] = stdDev * std::sqrt(252.0) * 10.0;  // Scale to 0-2 range typically
        }

        return features;
    }

    // Extract features with volume
    static std::vector<double> extractFeaturesWithVolume(const std::vector<double>& closes,
                                                           const std::vector<int64_t>& volumes,
                                                           int lookback = 20) {
        std::vector<double> features = extractFeatures(closes, volumes, lookback);

        if (volumes.size() >= lookback && !volumes.empty()) {
            // Feature 3: Volume ratio (current vs average)
            double recentVol = 0.0;
            for (size_t i = volumes.size() - 5; i < volumes.size(); ++i) {
                recentVol += volumes[i];
            }
            recentVol /= 5.0;

            double avgVol = 0.0;
            for (size_t i = volumes.size() - lookback; i < volumes.size(); ++i) {
                avgVol += volumes[i];
            }
            avgVol /= lookback;

            double volumeRatio = avgVol > 0 ? recentVol / avgVol : 1.0;
            features.push_back(volumeRatio);
        }

        return features;
    }
};

// Market Regime Detector
class RegimeDetector {
private:
    HiddenMarkovModel hmm_;
    bool isTrained_ = false;
    int lookback_;

    // State names for logging
    static const std::map<int, std::string> stateNames_;

public:
    RegimeDetector(int numStates = 5, int lookback = 60)
        : hmm_(numStates, 100, 1e-4), lookback_(lookback) {
    }

    // Train the regime detector on historical data
    // Returns true if training successful
    bool train(const std::vector<double>& closes,
               const std::vector<int64_t>& volumes) {
        if (closes.size() < 100) {
            return false;
        }

        // Extract features for each time point
        std::vector<std::vector<double>> features;

        for (size_t i = lookback_; i < closes.size(); ++i) {
            // Create lookback window
            std::vector<double> windowCloses(closes.begin(), closes.begin() + i + 1);
            std::vector<int64_t> windowVols;

            if (!volumes.empty()) {
                windowVols = std::vector<int64_t>(volumes.begin(), volumes.begin() + i + 1);
            }

            auto feat = RegimeFeatureExtractor::extractFeaturesWithVolume(
                windowCloses, windowVols, lookback_);

            if (feat.size() >= 2) {
                features.push_back(feat);
            }
        }

        if (features.size() < 50) {
            return false;
        }

        // Train HMM
        bool success = hmm_.train(features);
        isTrained_ = success;

        return success;
    }

    // Train using pre-computed features
    bool train(const std::vector<std::vector<double>>& features) {
        bool success = hmm_.train(features);
        isTrained_ = success;
        return success;
    }

    // Detect current regime
    RegimeInfo detectCurrentRegime(const std::vector<double>& closes,
                                    const std::vector<int64_t>& volumes) {
        RegimeInfo info;
        info.regime = MarketRegime::Unknown;
        info.name = "Unknown";
        info.probability = 0.0;
        info.description = "Not enough data";

        if (!isTrained_ || closes.size() < (size_t)lookback_) {
            return info;
        }

        // Extract current features
        auto features = RegimeFeatureExtractor::extractFeaturesWithVolume(
            closes, volumes, lookback_);

        if (features.size() < 2) {
            return info;
        }

        // Get state probabilities
        auto probs = hmm_.getStateProbabilities(features);

        // Find most likely state
        double maxProb = 0.0;
        int bestState = 0;
        for (int i = 0; i < (int)probs.size(); ++i) {
            if (probs[i] > maxProb) {
                maxProb = probs[i];
                bestState = i;
            }
        }

        // Map HMM state to market regime
        info.regime = mapStateToRegime(bestState, features);
        info.name = getRegimeName(info.regime);
        info.probability = maxProb;
        info.description = getRegimeDescription(info.regime, features);

        return info;
    }

    // Detect regime with pre-computed features
    RegimeInfo detectRegime(const std::vector<double>& features) {
        RegimeInfo info;
        info.regime = MarketRegime::Unknown;
        info.name = "Unknown";
        info.probability = 0.0;
        info.description = "Not trained";

        if (!isTrained_ || features.size() < 2) {
            return info;
        }

        // Get state probabilities
        auto probs = hmm_.getStateProbabilities(features);

        // Find most likely state
        double maxProb = 0.0;
        int bestState = 0;
        for (int i = 0; i < (int)probs.size(); ++i) {
            if (probs[i] > maxProb) {
                maxProb = probs[i];
                bestState = i;
            }
        }

        info.regime = mapStateToRegime(bestState, features);
        info.name = getRegimeName(info.regime);
        info.probability = maxProb;
        info.description = getRegimeDescription(info.regime, features);

        return info;
    }

    // Get state probabilities
    std::vector<double> getStateProbabilities(const std::vector<double>& features) const {
        if (!isTrained_) {
            return std::vector<double>();
        }
        return hmm_.getStateProbabilities(features);
    }

    // Get recommended strategy parameters based on regime
    struct RegimeRecommendations {
        std::string strategyType;   // "trend", "mean_reversion", "neutral"
        double positionSize;       // 0.0 to 1.0
        double stopLossMultiplier;  // ATR multiplier
        double takeProfitMultiplier; // ATR multiplier
    };

    RegimeRecommendations getRecommendations(MarketRegime regime) const {
        RegimeRecommendations rec;

        switch (regime) {
            case MarketRegime::BullLowVol:
                rec.strategyType = "trend_following";
                rec.positionSize = 1.5;
                rec.stopLossMultiplier = 2.0;
                rec.takeProfitMultiplier = 3.0;
                break;
            case MarketRegime::BullHighVol:
                rec.strategyType = "trend_following";
                rec.positionSize = 0.75;
                rec.stopLossMultiplier = 2.5;
                rec.takeProfitMultiplier = 2.0;
                break;
            case MarketRegime::BearLowVol:
                rec.strategyType = "mean_reversion";
                rec.positionSize = 0.5;
                rec.stopLossMultiplier = 1.5;
                rec.takeProfitMultiplier = 2.0;
                break;
            case MarketRegime::BearHighVol:
                rec.strategyType = "mean_reversion";
                rec.positionSize = 0.25;
                rec.stopLossMultiplier = 1.5;
                rec.takeProfitMultiplier = 1.5;
                break;
            case MarketRegime::Sideways:
                rec.strategyType = "mean_reversion";
                rec.positionSize = 1.0;
                rec.stopLossMultiplier = 2.0;
                rec.takeProfitMultiplier = 2.0;
                break;
            default:
                rec.strategyType = "neutral";
                rec.positionSize = 0.5;
                rec.stopLossMultiplier = 2.0;
                rec.takeProfitMultiplier = 2.0;
                break;
        }

        return rec;
    }

    bool isTrained() const { return isTrained_; }

private:
    MarketRegime mapStateToRegime(int state, const std::vector<double>& features) {
        if (state < 0 || state >= (int)hmm_.getNumStates()) {
            return MarketRegime::Unknown;
        }

        // If we have enough states, map based on HMM state
        // Otherwise, use feature values to determine regime
        if (hmm_.getNumStates() >= 5) {
            // Direct mapping based on trained states
            // This is a simplification - in practice, you'd analyze the learned parameters
            return static_cast<MarketRegime>(state);
        }

        // Fallback: Use feature-based classification
        double return5d = features.size() > 0 ? features[0] / 10.0 : 0.0;
        double volatility = features.size() > 1 ? features[1] / 10.0 : 0.15;

        bool isBullish = return5d > 0;
        bool isHighVol = volatility > 0.15;

        if (isBullish && !isHighVol) return MarketRegime::BullLowVol;
        if (isBullish && isHighVol) return MarketRegime::BullHighVol;
        if (!isBullish && !isHighVol) return MarketRegime::BearLowVol;
        if (!isBullish && isHighVol) return MarketRegime::BearHighVol;
        return MarketRegime::Sideways;
    }

    std::string getRegimeName(MarketRegime regime) const {
        switch (regime) {
            case MarketRegime::BullLowVol: return "Bull Low Vol";
            case MarketRegime::BullHighVol: return "Bull High Vol";
            case MarketRegime::BearLowVol: return "Bear Low Vol";
            case MarketRegime::BearHighVol: return "Bear High Vol";
            case MarketRegime::Sideways: return "Sideways";
            default: return "Unknown";
        }
    }

    std::string getRegimeDescription(MarketRegime regime,
                                      const std::vector<double>& features) const {
        std::string desc = getRegimeName(regime);

        if (features.size() >= 2) {
            double ret = features[0] * 10.0;  // Unscale
            double vol = features[1] / 10.0;
            desc += " (Return: " + std::to_string((int)(ret * 100)) + "%, Vol: " +
                   std::to_string((int)(vol * 100)) + "%)";
        }

        return desc;
    }
};

// State name mapping
inline const std::map<int, std::string> RegimeDetector::stateNames_ = {
    {0, "Bull Low Vol"},
    {1, "Bull High Vol"},
    {2, "Bear Low Vol"},
    {3, "Bear High Vol"},
    {4, "Sideways"}
};

} // namespace RegimeDetection
