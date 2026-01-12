#include "TradingStrategy.h"
#include "TechnicalAnalysis.h"
#include <cmath>
#include <iostream>
#include <algorithm>

float blendSentiment(float stat_score, float sentiment) {
    return (stat_score * 0.7f) + (sentiment * 0.3f);
}

Signal generateSignal(const std::string& symbol, 
                      const std::vector<Candle>& candles, 
                      float sentimentScore,
                      const Fundamentals& fund,
                      const SupportResistance& levels) 
{
    Signal sig;
    sig.action = "hold";
    sig.entry = 0.0f;
    sig.exit = 0.0f;
    sig.confidence = 0.0f;

    if (candles.empty()) return sig;

    std::vector<float> closes;
    for(const auto& c : candles) closes.push_back(c.close);

    float currentPrice = closes.back();
    sig.entry = currentPrice;

    // --- 1. Technical Score (-1.0 to 1.0) ---
    float stat_score = 0.0f;

    // A. Forecast (Poly vs Linear)
    float linearForecast = forecastPrice(closes, 30);
    float polyForecast = forecastPricePoly(closes, 30, 2);
    
    float forecast = linearForecast;
    bool reversalDetected = false;
    
    // Poly reversal logic
    if (currentPrice < levels.support * 1.05f && polyForecast > currentPrice && linearForecast < currentPrice) {
        forecast = polyForecast; 
        reversalDetected = true;
    }

    float forecast_diff = (forecast - currentPrice) / currentPrice;
    float forecast_contrib = std::clamp(forecast_diff * 5.0f, -0.5f, 0.5f);
    stat_score += forecast_contrib;
    
    // B. Advanced Indicators (ADX + BB + RSI)
    ADXResult adx = computeADX(candles);
    BollingerBands bb = computeBollingerBands(closes);
    float rsi = computeRSI(closes);

    bool trending = (adx.adx > 25.0f);
    
    // RSI Logic (Filtered by Trend)
    float rsi_contrib = 0.0f;
    if (trending) {
        // In strong trend, RSI overbought/oversold is often a continuation signal, or at least not a reversal
        if (adx.plusDI > adx.minusDI) {
            // Uptrend
            if (rsi > 70) rsi_contrib = 0.0f; // Ignore Overbought, keep riding
            else if (rsi < 40) rsi_contrib = 0.4f; // Buy the dip in uptrend
        } else {
            // Downtrend
            if (rsi < 30) rsi_contrib = 0.0f; // Ignore Oversold, keep falling
            else if (rsi > 60) rsi_contrib = -0.4f; // Sell the rip in downtrend
        }
    } else {
        // Ranging Market -> RSI works normally
        rsi_contrib = (50.0f - rsi) / 100.0f; 
    }
    stat_score += rsi_contrib;

    // Bollinger Bands Logic
    if (bb.middle > 0) {
        if (currentPrice < bb.lower) {
            stat_score += 0.3f; // Strong mean reversion buy
        } else if (currentPrice > bb.upper) {
            stat_score -= 0.3f; // Strong mean reversion sell
        }
    }

    // C. MACD
    std::pair<float, float> macd = computeMACD(closes);
    float hist = macd.first - macd.second;
    stat_score += (hist > 0) ? 0.2f : -0.2f;

    // --- 2. Fundamental Score Adjustment ---
    // If P/E is valid and low, it boosts the score (Value Buy)
    if (fund.valid && fund.pe_ratio > 0.1f) {
        if (fund.pe_ratio < 15.0f) {
            stat_score += 0.2f; // Undervalued boost
        } else if (fund.pe_ratio > 50.0f) {
            stat_score -= 0.1f; // Overvalued drag
        }
    }

    // --- 3. Volume Logic (OBV Divergence - Simplified) ---
    // If we can't easily calc divergence without more history processing, 
    // we just check if recent volume is high on up days?
    // Let's simpler: If Poly Forecast is Up AND we are at Support, double the reversal bonus.
    if (reversalDetected) stat_score += 0.2f;

    // Clamp
    stat_score = std::clamp(stat_score, -1.0f, 1.0f);

    // --- 4. Blend with Sentiment ---
    float blended_score = blendSentiment(stat_score, sentimentScore);

    // --- 4b. Deep Analysis (Low Confidence Boost) ---
    // If we are in the "Hold" or weak "Buy/Sell" zone, look closer.
    bool squeeze = false;
    PatternResult pattern = {"", 0.0f};

    if (std::abs(blended_score) < 0.4f) {
        // 1. Check Patterns
        pattern = detectCandlestickPattern(candles);
        
        // Contextual Boost: Pattern must match location
        if (pattern.score > 0 && currentPrice < levels.support * 1.05f) {
            blended_score += 0.3f; // Bullish pattern at support -> Strong Buy Signal
        } else if (pattern.score < 0 && currentPrice > levels.resistance * 0.95f) {
            blended_score -= 0.3f; // Bearish pattern at resistance -> Strong Sell Signal
        } else if (pattern.name == "Doji") {
            // Doji at support/resistance often means reversal
            if (currentPrice < levels.support * 1.05f) blended_score += 0.15f;
            if (currentPrice > levels.resistance * 0.95f) blended_score -= 0.15f;
        }

        // 2. Check Volatility Squeeze
        squeeze = checkVolatilitySqueeze(closes);
        if (squeeze) {
            // Squeeze means big move coming. 
            // If we have a slight bias, amplify it.
            if (blended_score > 0) blended_score += 0.1f;
            else if (blended_score < 0) blended_score -= 0.1f;
        }
    }

    // --- 5. Support/Resistance Logic (The Veto) ---
    // If Selling, but Price is at Support -> HOLD
    if (blended_score < -0.2f && currentPrice <= levels.support * 1.02f) {
        sig.action = "hold";
        sig.reason = "Hold: Bearish Signal, but Price is at Major Support ($" + std::to_string(levels.support) + ")";
        sig.confidence = 50.0f;
        return sig;
    }

    // --- 6. Determine Action ---
    float atr = computeATR(candles);
    
    // Calculate Targets based on potential action
    // If Buy, we look for Resistance (Maxima)
    // If Sell, we look for Support (Minima)
    std::vector<float> resistanceLevels = findLocalExtrema(closes, 90, true);
    std::vector<float> supportLevels = findLocalExtrema(closes, 90, false);
    
    // Filter targets that are useful (e.g. above price for buy, below for sell)
    auto filterTargets = [&](const std::vector<float>& raw, bool above) {
        std::vector<float> filtered;
        for(float t : raw) {
            if (above && t > currentPrice * 1.01f) filtered.push_back(t);
            if (!above && t < currentPrice * 0.99f) filtered.push_back(t);
        }
        if (above) std::sort(filtered.begin(), filtered.end()); // Ascending for Buy targets
        else std::sort(filtered.begin(), filtered.end(), std::greater<float>()); // Descending for Sell targets
        return filtered;
    };

    if (blended_score > 0.25f) {
        sig.action = "buy";
        sig.targets = filterTargets(resistanceLevels, true);
        
        // Fallback target if no local maxima found above
        if (sig.targets.empty()) sig.targets.push_back(levels.resistance);
        
        sig.exit = sig.targets[0]; // Nearest target
        
        sig.confidence = std::abs(blended_score) * 100.0f;
        sig.reason = "Buy (Score " + std::to_string(blended_score) + ")";
        if (reversalDetected) sig.reason += " [Reversal Pattern]";
        if (fund.valid && fund.pe_ratio < 15) sig.reason += " [Undervalued P/E]";
        if (currentPrice < bb.lower) sig.reason += " [BB Oversold]";
        if (trending && adx.plusDI > adx.minusDI) sig.reason += " [Strong Uptrend]";
        if (!pattern.name.empty() && pattern.score > 0) sig.reason += " [" + pattern.name + "]";
        if (squeeze) sig.reason += " [Vol Squeeze]";
        
    } else if (blended_score < -0.25f) {
        sig.action = "sell";
        sig.targets = filterTargets(supportLevels, false);

        // Fallback target
        if (sig.targets.empty()) sig.targets.push_back(levels.support);

        sig.exit = sig.targets[0]; // Nearest target

        sig.confidence = std::abs(blended_score) * 100.0f;
        sig.reason = "Sell (Score " + std::to_string(blended_score) + ")";
        if (currentPrice > bb.upper) sig.reason += " [BB Overbought]";
        if (trending && adx.minusDI > adx.plusDI) sig.reason += " [Strong Downtrend]";
        if (!pattern.name.empty() && pattern.score < 0) sig.reason += " [" + pattern.name + "]";
        if (squeeze) sig.reason += " [Vol Squeeze]";
    } else {
        sig.action = "hold";
        sig.confidence = (1.0f - std::abs(blended_score)) * 100.0f;
        sig.reason = "Hold: Neutral Score (" + std::to_string(blended_score) + ")";
        if (trending) sig.reason += " [Trending]"; else sig.reason += " [Ranging]";
        if (squeeze) sig.reason += " [Watch for Breakout (Squeeze)]";
        if (!pattern.name.empty()) sig.reason += " [" + pattern.name + " Detected]";
        
        // Populate prospective levels for Hold
        auto buyTargets = filterTargets(supportLevels, false); // Returns descending (nearest first)
        if (!buyTargets.empty()) sig.prospectiveBuy = buyTargets[0];
        else sig.prospectiveBuy = levels.support;
        
        auto sellTargets = filterTargets(resistanceLevels, true); // Returns ascending (nearest first)
        if (!sellTargets.empty()) sig.prospectiveSell = sellTargets[0];
        else sig.prospectiveSell = levels.resistance;
    }

    // --- 7. Options ---
    if (std::abs(blended_score) > 0.4f) {
        OptionSignal opt;
        opt.period_days = 45; 
        if (blended_score > 0) {
            opt.type = "Call";
            opt.strike = currentPrice * 1.05f; 
        } else {
            opt.type = "Put";
            opt.strike = currentPrice * 0.95f; 
        }
        sig.option = opt;
    }

    return sig;
}
