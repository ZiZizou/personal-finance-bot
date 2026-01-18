#include "MarketData.h"
#include "NetworkUtils.h"
#include "json.hpp"
#include <iostream>
#include <algorithm>
#include <map>

using json = nlohmann::json;

// --- Helper Functions ---

std::string getCoinApiKey() {
    return NetworkUtils::getApiKey("COINAPI");
}

std::string formatSymbol(const std::string& symbol, const std::string& type) {
    std::string s = symbol;
    if (type == "crypto") {
        // Yahoo uses "BTC-USD" format for crypto, which matches our input usually.
        // If input is "bitcoin" (CoinGecko style), we might need mapping, 
        // but assuming input "BTC-USD" for Yahoo is correct.
        // Ensure it's uppercase
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        if (s.find("-USD") == std::string::npos && s.length() <= 5) {
            s += "-USD";
        }
    }
    return s;
}

// --- Implementation ---

std::vector<Candle> fetchCandles(const std::string& symbol, const std::string& type) {
    std::vector<Candle> candles;
    std::string ySymbol = formatSymbol(symbol, type);
    
    // Yahoo Chart API v8
    std::string url = "https://query1.finance.yahoo.com/v8/finance/chart/" + ySymbol + "?interval=1d&range=2y";

    std::string response = NetworkUtils::fetchData(url);
    if (response.empty()) return candles;

    try {
        json data = json::parse(response);
        if (data.contains("chart") && data["chart"].contains("result")) {
            auto result = data["chart"]["result"];
            if (!result.is_null() && !result.empty()) {
                auto quoteData = result[0];
                if (quoteData.contains("timestamp") && quoteData.contains("indicators")) {
                    auto timestamps = quoteData["timestamp"];
                    auto indicators = quoteData["indicators"]["quote"][0];
                    
                    if (timestamps.is_array() && indicators.contains("close")) {
                        size_t len = timestamps.size();
                        auto closes = indicators["close"];
                        auto opens = indicators.contains("open") ? indicators["open"] : closes;
                        auto highs = indicators.contains("high") ? indicators["high"] : closes;
                        auto lows = indicators.contains("low") ? indicators["low"] : closes;
                        auto volumes = indicators.contains("volume") ? indicators["volume"] : json::array();

                        for (size_t i = 0; i < len; ++i) {
                            if (closes[i].is_null()) continue;

                            Candle c;
                            c.date = std::to_string(timestamps[i].get<long long>());
                            c.close = closes[i].get<float>();
                            c.open = !opens[i].is_null() ? opens[i].get<float>() : c.close;
                            c.high = !highs[i].is_null() ? highs[i].get<float>() : c.close;
                            c.low = !lows[i].is_null() ? lows[i].get<float>() : c.close;
                            
                            if (!volumes.empty() && i < volumes.size() && !volumes[i].is_null()) {
                                c.volume = volumes[i].get<long long>();
                            } else {
                                c.volume = 0;
                            }
                            
                            candles.push_back(c);
                        }
                    }
                }
            }
        }
    } catch (const json::parse_error& e) {
        std::cerr << "JSON Parse Error (Candles " << symbol << "): " << e.what() << std::endl;
    } catch (...) {
        // std::cerr << "Unknown Error parsing candles for " << symbol << std::endl;
    }
    return candles;
}

Fundamentals fetchFundamentals(const std::string& symbol, const std::string& type) {
    Fundamentals fund = {0.0f, 0.0f, 0.0f, false};
    std::string ySymbol = formatSymbol(symbol, type);
    
    // Yahoo Quote API v7
    std::string url = "https://query1.finance.yahoo.com/v7/finance/quote?symbols=" + ySymbol;
    
    std::string response = NetworkUtils::fetchData(url);
    if (response.empty()) return fund;

    try {
        json data = json::parse(response);
        if (data.contains("quoteResponse") && data["quoteResponse"].contains("result")) {
            auto result = data["quoteResponse"]["result"];
            if (!result.empty()) {
                auto q = result[0];
                fund.pe_ratio = q.value("trailingPE", 0.0f);
                fund.market_cap = q.value("marketCap", 0.0f);
                fund.fifty_day_avg = q.value("fiftyDayAverage", 0.0f);
                fund.valid = true;
            }
        }
    } catch (...) {}
    return fund;
}

OnChainData fetchOnChainData(const std::string& symbol) {
    OnChainData data = {0.0f, 0.0f, false};
    
    // CoinAPI (Optional)
    std::string key = getCoinApiKey();
    if (key == "DEMO" || key.empty()) return data;

    // Mapping for CoinAPI (BTC, ETH)
    std::string assetId = symbol;
    if (assetId.find("-USD") != std::string::npos) {
        assetId = assetId.substr(0, assetId.find("-USD"));
    }

    // Example Endpoint: Exchange Flows (Simulated endpoint for CoinAPI Free Tier constraints)
    // Real endpoint: /v1/metrics/symbol/...
    std::string url = "https://rest.coinapi.io/v1/assets/" + assetId + "?apikey=" + key; 
    
    // Note: CoinAPI Free Tier is very limited. This often returns generic info.
    // For specific on-chain metrics like "Net Inflow", you need paid tiers or specific glassnode APIs.
    // We will do a basic fetch to validate the key works.
    
    std::vector<std::string> headers = { "X-CoinAPI-Key: " + key };
    std::string response = NetworkUtils::fetchData(url, 300, headers);
    
    if (!response.empty()) {
        try {
            json j = json::parse(response);
            if (j.is_array() && !j.empty()) {
                 data.valid = true;
                 // Mocking flow data based on volume trend if real API doesn't provide it freely
                 // Real implementation would parse "volume_1day_usd" etc.
                 data.net_inflow = 0.0f; // Placeholder
            }
        } catch (...) {}
    }
    
    return data;
}