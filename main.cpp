#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <future>
#include <mutex>
#include <cstdlib> // For getenv

#include "json.hpp"
#include "MarketData.h"
#include "TradingStrategy.h"
#include "TechnicalAnalysis.h"
#include "SentimentAnalyzer.h"
#include "NewsManager.h"
#include "NetworkUtils.h"
#include "Backtester.h"
#include "Logger.h"
#include "ReportGenerator.h"
#include "BlackScholes.h" // Needed for Greeks logic injection if not already in strategy

using json = nlohmann::json;

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

// Thread-safe collection of results
std::mutex resultsMutex;
std::vector<AnalysisResult> globalResults;

// Function to process a single ticker (for async)
void processTicker(Ticker t, MLPredictor mlModelCopy) { // Pass ML by value or protect? ML predict is const-ish usually
    // Note: MLPredictor::predict is const, but extractFeatures is const.
    // We instantiate a local predictor or use a shared one if thread-safe.
    // The ML model in our code has state (weights).
    // For simplicity in this step, we pass a copy or assume pre-trained.
    
    Logger::getInstance().log("Analyzing " + t.symbol + "...");

    std::vector<Candle> candles = fetchCandles(t.symbol, t.type);
    if (candles.size() < 60) { 
        Logger::getInstance().log("Insufficient Data for " + t.symbol);
        return; 
    }
    
    // Backtest
    BacktestResult bt = Backtester::run(candles);
    
    Fundamentals fund = fetchFundamentals(t.symbol, t.type);
    OnChainData onChain = {0.0f, 0.0f, false};
    if (t.type != "stock") {
        onChain = fetchOnChainData(t.symbol);
    }

    std::vector<std::string> news = NewsManager::fetchNews(t.symbol);
    float sentiment = (!news.empty()) ? SentimentAnalyzer::getInstance().analyze(news) : 0.0f;
    
    std::vector<float> prices;
    for(auto& c : candles) prices.push_back(c.close);
    SupportResistance levels = identifyLevels(prices, 60); 
    
    Signal sig = generateSignal(t.symbol, candles, sentiment, fund, onChain, levels, mlModelCopy);
    
    // Greeks Calculation for Options
    if (sig.option) {
        float T = (float)sig.option->period_days / 365.0f;
        // Approx Vol from GARCH or hist
        // Re-calculating vol here for display (inefficient but distinct from strategy internals)
        float sigma = 0.30f; // Default if not passed
        // For accurate reporting, ideally strategy returns the Vol used. 
        // Let's rely on BlackScholes IV or just calculate Greeks on assumption
        Greeks g = BlackScholes::calculateGreeks(prices.back(), sig.option->strike, T, 0.04f, sigma, sig.option->type == "Call");
        
        std::stringstream ss;
        ss << sig.reason << " [Delta: " << g.delta << ", Theta: " << g.theta << "]";
        sig.reason = ss.str();
    }

    AnalysisResult res;
    res.symbol = t.symbol;
    res.price = prices.back();
    res.sentiment = sentiment;
    res.action = sig.action;
    res.confidence = sig.confidence;
    res.reason = sig.reason;
    res.backtest = bt;
    res.signal = sig;
    res.history = candles;

    {
        std::lock_guard<std::mutex> lock(resultsMutex);
        globalResults.push_back(res);
    }
}

std::string getEnvVar(const std::string& key, const std::string& defaultVal) {
    char* val = std::getenv(key.c_str());
    return val ? std::string(val) : defaultVal;
}

int main() {
    Logger::getInstance().log("Starting Trading Bot 4.0 (Parallel + Reporting)...");
    
    // 0. Init API Keys from Env or Default
    NetworkUtils::setApiKey("FMP", getEnvVar("FMP_KEY", "demo")); 
    NetworkUtils::setApiKey("COINAPI", getEnvVar("COIN_KEY", "DEMO_KEY")); 
    NetworkUtils::setApiKey("NEWSAPI", getEnvVar("NEWS_KEY", "DEMO_KEY"));
    NetworkUtils::setApiKey("HF", getEnvVar("HF_KEY", "DEMO_KEY")); // HuggingFace

    SentimentAnalyzer::getInstance().init("sentiment.gguf");
    
    std::vector<Ticker> tickers = readTickers("tickers.csv");
    MLPredictor mlModel; // Initial model

    // 1. Parallel Execution
    std::vector<std::future<void>> futures;
    for (const auto& t : tickers) {
        // Launch async
        futures.push_back(std::async(std::launch::async, processTicker, t, mlModel));
    }

    // Wait for all
    for (auto& f : futures) {
        f.wait();
    }
    
    Logger::getInstance().log("Analysis Complete. Generating Reports...");

    // 2. Reporting
    ReportGenerator::generateCSV(globalResults, "report.csv");
    ReportGenerator::generateHTML(globalResults, "report.html");
    
    Logger::getInstance().log("Reports saved to report.csv and report.html");
    std::cout << "\nDisclaimer: Not financial advice. Past performance is not indicative of future results.\n";

    return 0;
}
