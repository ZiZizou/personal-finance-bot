#include "TradingStrategy.h"
#include "TechnicalAnalysis.h"
#include "MLPredictor.h"
#include "BlackScholes.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <numeric>

extern std::vector<float> calculateLogReturns(const std::vector<float>& prices);
extern float computeGARCHVolatility(const std::vector<float>& returns);
extern float computeAdaptiveRSI(const std::vector<float>& prices, int basePeriod);

// --- Regime Detection (Markov-lite) ---
std::string detectMarketRegime(const std::vector<float>& prices) {
    if (prices.size() < 50) return "Unknown";
    
    // Simple classification based on SMA + Volatility
    float sma50 = 0.0f, sma200 = 0.0f;
    int n = prices.size();
    for(int i=n-50; i<n; ++i) sma50 += prices[i]; sma50 /= 50.0f;
    
    // If enough data for SMA200
    if (n >= 200) {
        for(int i=n-200; i<n; ++i) sma200 += prices[i]; sma200 /= 200.0f;
    } else {
        sma200 = sma50; // Fallback
    }

    float current = prices.back();
    std::vector<float> rets = calculateLogReturns(prices);
    float vol = computeGARCHVolatility(rets); // Daily vol

    if (vol > 0.03f) return "HighVol"; // Crisis/Crash mode
    
    if (current > sma50 && sma50 > sma200) return "Bull";
    if (current < sma50 && sma50 < sma200) return "Bear";
    
    return "Sideways";
}

float blendSentiment(float stat_score, float sentiment, const std::string& regime) {
    // Adaptive Weights
    float sentimentWeight = 0.25f;
    
    // In High Volatility, news drives price (panic/euphoria)
    if (regime == "HighVol") sentimentWeight = 0.50f;
    else if (std::abs(sentiment) >= 0.8f) sentimentWeight = 0.40f; 
    
    float technicalWeight = 1.0f - sentimentWeight;
    return (stat_score * technicalWeight) + (sentiment * sentimentWeight);
}

// Kelly Criterion for Position Sizing
// p = Win Probability, b = Odds (Win/Loss ratio)
// Returns % of bankroll to wager
float calculateKelly(float winProb, float riskRewardRatio) {
    if (riskRewardRatio <= 0) return 0.0f;
    return (winProb * (riskRewardRatio + 1) - 1) / riskRewardRatio;
}

Signal generateSignal(const std::string& symbol, 
                      const std::vector<Candle>& candles, 
                      float sentimentScore,
                      const Fundamentals& fund,
                      const OnChainData& onChain,
                      const SupportResistance& levels,
                      MLPredictor& mlModel) 
{
    Signal sig;
    sig.action = "hold";
    sig.entry = 0.0f;
    sig.exit = 0.0f;
    sig.confidence = 0.0f;
    sig.mlForecast = 0.0f;

    if (candles.empty()) return sig;

    std::vector<float> closes;
    for(const auto& c : candles) closes.push_back(c.close);

    float currentPrice = closes.back();
    sig.entry = currentPrice;
    
    // Detect Regime
    std::string regime = detectMarketRegime(closes);
    
    // --- 0. Stats & ML ---
    std::vector<float> returns = calculateLogReturns(closes);
    float garchVol = computeGARCHVolatility(returns);
    int cyclePeriod = detectCycle(closes);
    
    std::pair<float, float> macd = computeMACD(closes);
    float rsi = computeAdaptiveRSI(closes, 14); 
    
    std::vector<float> features = mlModel.extractFeatures(
        rsi, macd.first - macd.second, sentimentScore, garchVol, cyclePeriod, (int)closes.size()
    );
    float mlPredPct = mlModel.predict(features);
    sig.mlForecast = mlPredPct;

    // --- 1. Technical Score ---
    float stat_score = 0.0f;

    // Forecasts
    float linearForecast = forecastPrice(closes, 30);
    float polyForecast = forecastPricePoly(closes, 30, 2);
    
    // Regime Filtering for Mean Reversion vs Trend
    float forecast = linearForecast;
    if (regime == "Sideways") {
        // Prefer Poly (Mean Reversion / Curvature)
        forecast = polyForecast; 
    } else {
        // Trend Following: Trust linear or Poly only if confirming trend
        if (regime == "Bull" && polyForecast > currentPrice) forecast = polyForecast;
        else if (regime == "Bear" && polyForecast < currentPrice) forecast = polyForecast;
    }

    float forecast_diff = (forecast - currentPrice) / currentPrice;
    stat_score += std::clamp(forecast_diff * 5.0f, -0.5f, 0.5f);
    
    // Indicators
    ADXResult adx = computeADX(candles);
    BollingerBands bb = computeBollingerBands(closes);
    
    // Regime-aware Logic
    if (regime == "Bull") {
        // Ignore Overbought RSI/BB
        if (currentPrice < bb.lower) stat_score += 0.4f; // Buy Dip strong
        if (rsi < 40) stat_score += 0.3f;
    } else if (regime == "Bear") {
        // Ignore Oversold
        if (currentPrice > bb.upper) stat_score -= 0.4f; // Sell Rip strong
        if (rsi > 60) stat_score -= 0.3f;
    } else {
        // Sideways: Mean Reversion rules apply fully
        if (currentPrice < bb.lower) stat_score += 0.3f;
        else if (currentPrice > bb.upper) stat_score -= 0.3f;
    }

    // MACD
    float hist = macd.first - macd.second;
    stat_score += (hist > 0) ? 0.2f : -0.2f;

    // --- 2. Fundamental & On-Chain ---
    if (fund.valid && fund.pe_ratio > 0.1f) {
        if (fund.pe_ratio < 15.0f) stat_score += 0.2f; 
        else if (fund.pe_ratio > 50.0f) stat_score -= 0.1f; 
    }
    if (onChain.valid) {
        if (onChain.net_inflow > 0) stat_score += 0.15f; 
        else if (onChain.net_inflow < 0) stat_score -= 0.15f; 
    }

    // --- 3. ML Bias ---
    if (mlPredPct > 0.005f) stat_score += 0.15f; 
    else if (mlPredPct < -0.005f) stat_score -= 0.15f;
    
    // --- 4. Blend ---
    stat_score = std::clamp(stat_score, -1.0f, 1.0f);
    float blended_score = blendSentiment(stat_score, sentimentScore, regime);

    // Deep Analysis
    bool squeeze = false;
    PatternResult pattern = {"", 0.0f};
    if (std::abs(blended_score) < 0.4f) {
        pattern = detectCandlestickPattern(candles);
        if (pattern.score > 0 && currentPrice < levels.support * 1.05f) blended_score += 0.3f; 
        else if (pattern.score < 0 && currentPrice > levels.resistance * 0.95f) blended_score -= 0.3f; 
        
        squeeze = checkVolatilitySqueeze(closes);
        if (squeeze) {
            if (blended_score > 0) blended_score += 0.1f;
            else if (blended_score < 0) blended_score -= 0.1f;
        }
    }
    
    // GARCH Veto
    bool highRisk = (garchVol > 0.03f);
    if (highRisk && blended_score > 0) blended_score *= 0.5f; 

    // Support/Resistance Veto
    if (blended_score < -0.2f && currentPrice <= levels.support * 1.02f) {
        sig.action = "hold";
        sig.reason = "Hold: Bearish Signal, but at Support";
        sig.confidence = 50.0f;
        return sig;
    }

    // --- Actions ---
    std::vector<float> resistanceLevels = findLocalExtrema(closes, 90, true);
    std::vector<float> supportLevels = findLocalExtrema(closes, 90, false);
    
    // Fallback if no extrema found (e.g. strong linear trend or insufficient data)
    // Use levels.resistance/support or simple percentages
    if (resistanceLevels.empty()) {
        resistanceLevels.push_back(levels.resistance > currentPrice ? levels.resistance : currentPrice * 1.05f);
    }
    if (supportLevels.empty()) {
        supportLevels.push_back(levels.support < currentPrice ? levels.support : currentPrice * 0.95f);
    }

    auto filterTargets = [&](const std::vector<float>& raw, bool above) {
        std::vector<float> filtered;
        for(float t : raw) {
            if (above && t > currentPrice * 1.005f) filtered.push_back(t); // 0.5% buffer
            if (!above && t < currentPrice * 0.995f) filtered.push_back(t);
        }
        if (filtered.empty()) {
             // Force at least one target based on volatility/ATR or fixed %
             float atr = computeATR(candles, 14); 
             if (atr == 0) atr = currentPrice * 0.02f; // Fallback 2%
             filtered.push_back(above ? currentPrice + 2*atr : currentPrice - 2*atr);
        }
        if (above) std::sort(filtered.begin(), filtered.end()); 
        else std::sort(filtered.begin(), filtered.end(), std::greater<float>());
        return filtered;
    };

    if (blended_score > 0.25f) {
        sig.action = "buy";
        sig.targets = filterTargets(resistanceLevels, true);
        sig.exit = sig.targets[0]; 
        
        sig.confidence = std::abs(blended_score) * 100.0f;
        sig.reason = "Buy (Score " + std::to_string(blended_score) + ") [" + regime + " Regime]";
        if (highRisk) sig.reason += " [High Risk]";
        
        // Kelly
        float winProb = 0.55f + (sig.confidence / 100.0f) * 0.20f; 
        float riskReward = (sig.exit - sig.entry) / (sig.entry - levels.support); // Target / Stop
        // Safety: ensure positive risk reward calc
        if (sig.entry <= levels.support) riskReward = 2.0f; // Default if price at/below support
        
        float kelly = calculateKelly(winProb, riskReward);
        if (kelly > 0) sig.reason += " [Size: " + std::to_string((int)(kelly * 100)) + "%]";
        
    } else if (blended_score < -0.25f) {
        sig.action = "sell";
        sig.targets = filterTargets(supportLevels, false);
        sig.exit = sig.targets[0]; 

        sig.confidence = std::abs(blended_score) * 100.0f;
        sig.reason = "Sell (Score " + std::to_string(blended_score) + ") [" + regime + " Regime]";
        
    } else {
        sig.action = "hold";
        sig.confidence = (1.0f - std::abs(blended_score)) * 100.0f;
        sig.reason = "Hold [" + regime + "]";
        
        auto buyTargets = filterTargets(supportLevels, false); 
        sig.prospectiveBuy = buyTargets[0];
        
        auto sellTargets = filterTargets(resistanceLevels, true); 
        sig.prospectiveSell = sellTargets[0];
    }

    // --- Options (Black-Scholes) ---
    if (std::abs(blended_score) > 0.4f) {
        OptionSignal opt;
        opt.period_days = 45; 
        float T = 45.0f / 365.0f;
        float r = 0.04f; // 4% risk free
        float sigma = garchVol * std::sqrt(252.0f); // Annualized Vol
        
        // Find efficient strike (Delta ~ 0.30 for OTM shots)
        // Iterative search or simple heuristic
        
        if (blended_score > 0) {
            opt.type = "Call";
            opt.strike = currentPrice * 1.05f; 
            float price = BlackScholes::callPrice(currentPrice, opt.strike, T, r, sigma);
            sig.reason += " [Call $" + std::to_string(opt.strike) + " @ $" + std::to_string(price) + "]";
        } else {
            opt.type = "Put";
            opt.strike = currentPrice * 0.95f; 
            float price = BlackScholes::putPrice(currentPrice, opt.strike, T, r, sigma);
             sig.reason += " [Put $" + std::to_string(opt.strike) + " @ $" + std::to_string(price) + "]";
        }
        sig.option = opt;
    }

    return sig;
}

