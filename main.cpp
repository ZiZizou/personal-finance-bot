#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <numeric>
#include <algorithm>
#include <map>

#include "json.hpp"
using json = nlohmann::json;

#include "MarketData.h"
#include "TradingStrategy.h"
#include "TechnicalAnalysis.h"
#include "SentimentAnalyzer.h"
#include "NewsManager.h"

#define ENABLE_CURL

#ifdef ENABLE_CURL
#include <curl/curl.h>
#endif

// --- Fetchers ---

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

std::string fetchData(const std::string& url) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

struct Ticker { std::string symbol; std::string type; };

std::vector<Ticker> readTickers(const std::string& filePath) {
    std::vector<Ticker> tickers;
    std::ifstream file(filePath);
    if (!file.is_open()) return tickers;
    std::string line;
    if (std::getline(file, line)) {} 
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string symbol, type;
        if (std::getline(ss, symbol, ',') && std::getline(ss, type)) {
             size_t first = type.find_first_not_of(" \t\r\n");
             if (std::string::npos == first) type = "";
             else type = type.substr(first, type.find_last_not_of(" \t\r\n") - first + 1);
            tickers.push_back({symbol, type});
        }
    }
    return tickers;
}

// Fetch Price & Volume
std::vector<Candle> fetchCandles(const std::string& symbol, const std::string& type) {
    std::string url;
    if (type == "stock") {
        url = "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol + "?interval=1d&range=2y";
    } else {
        std::string id = symbol; 
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        if (id == "btc-usd") id = "bitcoin";
        url = "https://api.coingecko.com/api/v3/coins/" + id + "/market_chart?vs_currency=usd&days=365";
    }

    std::string response = fetchData(url);
    std::vector<Candle> sortedCandles;

    try {
        json data = json::parse(response);
        if (type == "stock") {
            if (data.contains("chart") && data["chart"].contains("result") && !data["chart"]["result"].is_null()) {
                auto result = data["chart"]["result"][0];
                if (result.contains("timestamp") && result.contains("indicators")) {
                    auto timestamps = result["timestamp"];
                    auto quote = result["indicators"]["quote"][0];
                    
                    size_t len = timestamps.size();
                    for (size_t i = 0; i < len; ++i) {
                        if (quote["close"][i].is_null()) continue;
                        Candle c;
                        c.date = std::to_string(timestamps[i].get<long long>());
                        c.close = quote["close"][i].get<float>();
                        // Volume parsing
                        if (quote.contains("volume") && !quote["volume"][i].is_null()) 
                            c.volume = quote["volume"][i].get<long long>();
                        else 
                            c.volume = 0;
                            
                        // OHLC or fallback
                        c.open = quote.contains("open") && !quote["open"][i].is_null() ? quote["open"][i].get<float>() : c.close;
                        c.high = quote.contains("high") && !quote["high"][i].is_null() ? quote["high"][i].get<float>() : c.close;
                        c.low = quote.contains("low") && !quote["low"][i].is_null() ? quote["low"][i].get<float>() : c.close;
                        
                        sortedCandles.push_back(c);
                    }
                }
            }
        } else {
            // Crypto (CoinGecko Simple)
            if (data.contains("prices")) {
                for (const auto& item : data["prices"]) {
                    Candle c;
                    c.date = std::to_string(item[0].get<long long>());
                    c.close = item[1].get<float>();
                    c.open=c.close; c.high=c.close; c.low=c.close; c.volume=0; 
                    sortedCandles.push_back(c);
                }
                // Need total volumes? CoinGecko has "total_volumes" array separately.
                if (data.contains("total_volumes")) {
                    size_t i = 0;
                    for (const auto& vol : data["total_volumes"]) {
                        if (i < sortedCandles.size()) sortedCandles[i].volume = (long long)vol[1].get<double>();
                        i++;
                    }
                }
            }
        }
    } catch (...) {}
    return sortedCandles;
}

// Fetch Fundamentals (P/E)
Fundamentals fetchFundamentals(const std::string& symbol, const std::string& type) {
    Fundamentals fund = {0.0f, 0.0f, 0.0f, false};
    if (type != "stock") return fund; // Crypto usually N/A for P/E

    std::string url = "https://query1.finance.yahoo.com/v7/finance/quote?symbols=" + symbol;
    std::string response = fetchData(url);
    
    try {
        json data = json::parse(response);
        if (data.contains("quoteResponse") && data["quoteResponse"].contains("result")) {
            auto result = data["quoteResponse"]["result"];
            if (!result.empty()) {
                auto q = result[0];
                if (q.contains("trailingPE") && !q["trailingPE"].is_null()) 
                    fund.pe_ratio = q["trailingPE"].get<float>();
                
                if (q.contains("marketCap") && !q["marketCap"].is_null())
                    fund.market_cap = q["marketCap"].get<long long>(); // Cast huge number
                    
                if (q.contains("fiftyDayAverage") && !q["fiftyDayAverage"].is_null())
                    fund.fifty_day_avg = q["fiftyDayAverage"].get<float>();
                    
                fund.valid = true;
            }
        }
    } catch (...) {}
    return fund;
}

int main() {
    // SentimentAnalyzer::getInstance().setVerbose(true); // Debug if needed
    SentimentAnalyzer::getInstance().init("sentiment.gguf");
    
    std::vector<Ticker> tickers = readTickers("tickers.csv");
    std::cout << "Starting Trading Bot 2.0 (Volume + Value + Curves)..." << std::endl;

    for (const auto& t : tickers) {
        std::cout << "\nAnalyzing " << t.symbol << "..." << std::endl;
        
        // 1. Data
        std::vector<Candle> candles = fetchCandles(t.symbol, t.type);
        if (candles.size() < 60) { std::cout << "  Insufficient Data." << std::endl; continue; }
        
        Fundamentals fund = fetchFundamentals(t.symbol, t.type);
        std::vector<std::string> news = NewsManager::fetchNews(t.symbol);
        
        // 2. Analysis
        float sentiment = (!news.empty()) ? SentimentAnalyzer::getInstance().analyze(news) : 0.0f;
        
        // Compute Support/Resistance for Strategy
        std::vector<float> prices;
        for(auto& c : candles) prices.push_back(c.close);
        SupportResistance levels = identifyLevels(prices, 60); // 60 day pivot
        
        // 3. Generate Signal
        Signal sig = generateSignal(t.symbol, candles, sentiment, fund, levels);
        
        // 4. Output
        float current = prices.back();
        std::cout << "  Price: " << current;
        if (fund.valid) std::cout << " | P/E: " << fund.pe_ratio;
        std::cout << " | Sentiment: " << sentiment << std::endl;
        std::cout << "  Support: " << levels.support << " | Resistance: " << levels.resistance << std::endl;
        
        std::cout << "  -> ACTION: " << sig.action 
                  << " (Conf: " << sig.confidence << "%)" << std::endl;
                  
        if (sig.action != "hold") {
            std::cout << "     Targets: ";
            for (size_t i = 0; i < sig.targets.size() && i < 3; ++i) {
                std::cout << sig.targets[i] << (i == sig.targets.size() - 1 || i == 2 ? "" : ", ");
            }
            std::cout << std::endl;
        } else {
            std::cout << "     Prospective Buy: " << sig.prospectiveBuy << " | Prospective Sell: " << sig.prospectiveSell << std::endl;
        }
        
        if (sig.option) {
            std::cout << "  -> OPTION: " << sig.option->type 
                      << " Strike: " << sig.option->strike << std::endl;
        }
        std::cout << "     Reason: " << sig.reason << std::endl;
    }
    return 0;
}
