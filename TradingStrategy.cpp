#include "TradingStrategy.h"
#include "TechnicalAnalysis.h"
#include "MLPredictor.h"
#include "BlackScholes.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <numeric>

// --- Helper: Calculate log returns ---
static std::vector<double> calculateLogReturns(const std::vector<double>& prices) {
    std::vector<double> returns;
    if (prices.size() < 2) return returns;
    for (size_t i = 1; i < prices.size(); ++i) {
        if (prices[i-1] > 0)
            returns.push_back(std::log(prices[i] / prices[i-1]));
    }
    return returns;
}

// --- Regime Detection (Markov-lite) ---
std::string detectMarketRegime(const std::vector<double>& prices) {
    if (prices.size() < 50) return "Unknown";
    
    // Simple classification based on SMA + Volatility
    double sma50 = 0.0, sma200 = 0.0;
    int n = prices.size();
    for(int i=n-50; i<n; ++i) sma50 += prices[i]; sma50 /= 50.0;
    
    // If enough data for SMA200
    if (n >= 200) {
        for(int i=n-200; i<n; ++i) sma200 += prices[i]; sma200 /= 200.0;
    } else {
        sma200 = sma50; // Fallback
    }

    double current = prices.back();
    std::vector<double> rets = calculateLogReturns(prices);
    double vol = computeGARCHVolatility(rets); // Daily vol

    if (vol > 0.03) return "HighVol"; // Crisis/Crash mode
    
    if (current > sma50 && sma50 > sma200) return "Bull";
    if (current < sma50 && sma50 < sma200) return "Bear";
    
    return "Sideways";
}

double blendSentiment(double stat_score, double sentiment, const std::string& regime) {
    // Adaptive Weights
    double sentimentWeight = 0.25;
    
    // In High Volatility, news drives price (panic/euphoria)
    if (regime == "HighVol") sentimentWeight = 0.50;
    else if (std::abs(sentiment) >= 0.8) sentimentWeight = 0.40; 
    
    double technicalWeight = 1.0 - sentimentWeight;
    return (stat_score * technicalWeight) + (sentiment * sentimentWeight);
}

// Kelly Criterion for Position Sizing
// p = Win Probability, b = Odds (Win/Loss ratio)
// Returns % of bankroll to wager
double calculateKelly(double winProb, double riskRewardRatio) {
    if (riskRewardRatio <= 0) return 0.0;
    return (winProb * (riskRewardRatio + 1) - 1) / riskRewardRatio;
}

Signal generateSignal(const std::string& symbol, 
                      const std::vector<Candle>& candles, 
                      double sentimentScore,
                      const Fundamentals& fund,
                      const OnChainData& onChain,
                      const SupportResistance& levels,
                      MLPredictor& mlModel) 
{
    Signal sig;
    sig.action = "hold";
    sig.entry = 0.0;
    sig.exit = 0.0;
    sig.confidence = 0.0;
    sig.mlForecast = 0.0;

    if (candles.empty()) return sig;

    std::vector<double> closes;
    for(const auto& c : candles) closes.push_back(c.close);

    double currentPrice = closes.back();
    sig.entry = currentPrice;
    
    // Detect Regime
    std::string regime = detectMarketRegime(closes);
    
    // --- 0. Stats & ML ---
    std::vector<double> returns = calculateLogReturns(closes);
    double garchVol = computeGARCHVolatility(returns);
    int cyclePeriod = detectCycle(closes);
    
    std::pair<double, double> macd = computeMACD(closes);
    double rsi = computeAdaptiveRSI(closes, 14); 
    
    std::vector<double> features = mlModel.extractFeatures(
        rsi, macd.first - macd.second, sentimentScore, garchVol, cyclePeriod, (int)closes.size()
    );
    double mlPredPct = mlModel.predict(features);
    sig.mlForecast = mlPredPct;

    // --- 1. Technical Score ---
    double stat_score = 0.0;

    // Forecasts
    double linearForecast = forecastPrice(closes, 30);
    double polyForecast = forecastPricePoly(closes, 30, 2);
    
    // Regime Filtering for Mean Reversion vs Trend
    double forecast = linearForecast;
    if (regime == "Sideways") {
        // Prefer Poly (Mean Reversion / Curvature)
        forecast = polyForecast; 
    } else {
        // Trend Following: Trust linear or Poly only if confirming trend
        if (regime == "Bull" && polyForecast > currentPrice) forecast = polyForecast;
        else if (regime == "Bear" && polyForecast < currentPrice) forecast = polyForecast;
    }

    double forecast_diff = (forecast - currentPrice) / currentPrice;
    stat_score += std::clamp(forecast_diff * 5.0, -0.5, 0.5);
    
    // Indicators
    ADXResult adx = computeADX(candles);
    BollingerBands bb = computeBollingerBands(closes);
    
    // Regime-aware Logic
    if (regime == "Bull") {
        // Ignore Overbought RSI/BB
        if (currentPrice < bb.lower) stat_score += 0.4; // Buy Dip strong
        if (rsi < 40) stat_score += 0.3;
    } else if (regime == "Bear") {
        // Ignore Oversold
        if (currentPrice > bb.upper) stat_score -= 0.4; // Sell Rip strong
        if (rsi > 60) stat_score -= 0.3;
    } else {
        // Sideways: Mean Reversion rules apply fully
        if (currentPrice < bb.lower) stat_score += 0.3;
        else if (currentPrice > bb.upper) stat_score -= 0.3;
    }

    // MACD
    double hist = macd.first - macd.second;
    stat_score += (hist > 0) ? 0.2 : -0.2;

    // --- 2. Fundamental & On-Chain ---
    if (fund.valid && fund.pe_ratio > 0.1) {
        if (fund.pe_ratio < 15.0) stat_score += 0.2; 
        else if (fund.pe_ratio > 50.0) stat_score -= 0.1; 
    }
    if (onChain.valid) {
        if (onChain.net_inflow > 0) stat_score += 0.15; 
        else if (onChain.net_inflow < 0) stat_score -= 0.15; 
    }

    // --- 3. ML Bias ---
    if (mlPredPct > 0.005) stat_score += 0.15; 
    else if (mlPredPct < -0.005) stat_score -= 0.15;
    
    // --- 4. Blend ---
    stat_score = std::clamp(stat_score, -1.0, 1.0);
    double blended_score = blendSentiment(stat_score, sentimentScore, regime);

    // Deep Analysis
    bool squeeze = false;
    PatternResult pattern = {"", 0.0};
    if (std::abs(blended_score) < 0.4) {
        pattern = detectCandlestickPattern(candles);
        if (pattern.score > 0 && currentPrice < levels.support * 1.05) blended_score += 0.3; 
        else if (pattern.score < 0 && currentPrice > levels.resistance * 0.95) blended_score -= 0.3; 
        
        squeeze = checkVolatilitySqueeze(closes);
        if (squeeze) {
            if (blended_score > 0) blended_score += 0.1;
            else if (blended_score < 0) blended_score -= 0.1;
        }
    }
    
    // GARCH Veto
    bool highRisk = (garchVol > 0.03);
    if (highRisk && blended_score > 0) blended_score *= 0.5; 

    // Support/Resistance Veto
    if (blended_score < -0.2 && currentPrice <= levels.support * 1.02) {
        sig.action = "hold";
        sig.reason = "Hold: Bearish Signal, but at Support";
        sig.confidence = 50.0;
        return sig;
    }

    // --- Actions ---
    std::vector<double> resistanceLevels = findLocalExtrema(closes, 90, true);
    std::vector<double> supportLevels = findLocalExtrema(closes, 90, false);
    
    // Fallback if no extrema found (e.g. strong linear trend or insufficient data)
    if (resistanceLevels.empty()) {
        resistanceLevels.push_back(levels.resistance > currentPrice ? levels.resistance : currentPrice * 1.05);
    }
    if (supportLevels.empty()) {
        supportLevels.push_back(levels.support < currentPrice ? levels.support : currentPrice * 0.95);
    }

    auto filterTargets = [&](const std::vector<double>& raw, bool above) {
        std::vector<double> filtered;
        for(double t : raw) {
            if (above && t > currentPrice * 1.005) filtered.push_back(t); // 0.5% buffer
            if (!above && t < currentPrice * 0.995) filtered.push_back(t);
        }
        if (filtered.empty()) {
             double atr = computeATR(candles, 14); 
             if (atr == 0) atr = currentPrice * 0.02; // Fallback 2%
             filtered.push_back(above ? currentPrice + 2*atr : currentPrice - 2*atr);
        }
        if (above) std::sort(filtered.begin(), filtered.end()); 
        else std::sort(filtered.begin(), filtered.end(), std::greater<double>());
        return filtered;
    };

    if (blended_score > 0.25) {
        sig.action = "buy";
        sig.targets = filterTargets(resistanceLevels, true);
        sig.exit = sig.targets[0]; 
        
        double normalized = 50.0 + ((blended_score - 0.25) / 0.75) * 50.0;
        sig.confidence = std::clamp(normalized, 50.0, 100.0);
        
        sig.reason = "Buy (Score " + std::to_string(blended_score) + ") [" + regime + " Regime]";
        if (highRisk) sig.reason += " [High Risk]";
        
        double winProb = 0.55 + blended_score * 0.20; 
        double riskReward = (sig.exit - sig.entry) / (sig.entry - levels.support);
        if (sig.entry <= levels.support) riskReward = 2.0; 
        
        double kelly = calculateKelly(winProb, riskReward);
        if (kelly > 0) sig.reason += " [Size: " + std::to_string((int)(kelly * 100)) + "%]";
        
    } else if (blended_score < -0.25) {
        sig.action = "sell";
        sig.targets = filterTargets(supportLevels, false);
        sig.exit = sig.targets[0]; 

        double absScore = std::abs(blended_score);
        double normalized = 50.0 + ((absScore - 0.25) / 0.75) * 50.0;
        sig.confidence = std::clamp(normalized, 50.0, 100.0);
        
        sig.reason = "Sell (Score " + std::to_string(blended_score) + ") [" + regime + " Regime]";
        
    } else {
        sig.action = "hold";
        
        double absScore = std::abs(blended_score);
        double normalized = 50.0 + ((0.25 - absScore) / 0.25) * 50.0;
        sig.confidence = std::clamp(normalized, 50.0, 100.0);
        
        sig.reason = "Hold [" + regime + "]";
        
        auto buyTargets = filterTargets(supportLevels, false); 
        sig.prospectiveBuy = buyTargets[0];
        
        auto sellTargets = filterTargets(resistanceLevels, true); 
        sig.prospectiveSell = sellTargets[0];
    }

    // --- Options (Black-Scholes) ---
    if (std::abs(blended_score) > 0.4) {
        OptionSignal opt;
        opt.period_days = 45; 
        double T = 45.0 / 365.0;
        double r = 0.04; 
        double sigma = garchVol * std::sqrt(252.0); 
        
        if (blended_score > 0) {
            opt.type = "Call";
            opt.strike = currentPrice * 1.05; 
            double price = BlackScholes::callPrice(currentPrice, opt.strike, T, r, sigma);
            sig.reason += " [Call $" + std::to_string(opt.strike) + " @ $" + std::to_string(price) + "]";
        } else {
            opt.type = "Put";
            opt.strike = currentPrice * 0.95; 
            double price = BlackScholes::putPrice(currentPrice, opt.strike, T, r, sigma);
             sig.reason += " [Put $" + std::to_string(opt.strike) + " @ $" + std::to_string(price) + "]";
        }
        sig.option = opt;
    }

    return sig;
}