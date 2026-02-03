#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <future>
#include <mutex>
#include <thread>

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
#include "BlackScholes.h"
#include "Config.h"
#include "Result.h"

// New enhanced components
#include "BacktestConfig.h"
#include "IStrategy.h"
#include "Strategies/MeanReversionStrategy.h"
#include "Strategies/TrendFollowingStrategy.h"
#include "Strategies/MLStrategy.h"
#include "WalkForward.h"
#include "VolatilityModels.h"

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
void processTicker(Ticker t, MLPredictor mlModelCopy) {
    Logger::getInstance().log("Analyzing " + t.symbol + "...");

    std::vector<Candle> candles = fetchCandles(t.symbol, t.type);
    if (candles.size() < 60) {
        Logger::getInstance().log("Insufficient Data for " + t.symbol);
        return;
    }

    // Enhanced backtest with new configuration
    BacktestConfig btConfig = BacktestConfig::realisticConfig();
    Backtester backtester(btConfig);

    // Use MeanReversionStrategy for enhanced backtesting
    MeanReversionStrategy strategy;
    BacktestResult bt = backtester.run(candles, strategy);

    // Also run legacy static method for comparison (optional)
    // BacktestResult btLegacy = Backtester::run(candles);

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
        float sigma = 0.30f;
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

// Run enhanced backtest with walk-forward analysis
void runEnhancedBacktest(const std::string& symbol, const std::vector<Candle>& candles) {
    Logger::getInstance().log("Running enhanced backtest for " + symbol + "...");

    // Configure backtester with realistic settings
    BacktestConfig config = BacktestConfig::realisticConfig();
    config.risk.enableStopLoss = true;
    config.risk.stopLossPercent = 0.02f;
    config.risk.enableTrailingStop = true;
    config.risk.trailingStopPercent = 0.015f;

    Backtester backtester(config);

    // Test multiple strategies
    MeanReversionStrategy mrStrategy;
    TrendFollowingStrategy tfStrategy;

    Logger::getInstance().log("Testing Mean Reversion Strategy...");
    BacktestResult mrResult = backtester.run(candles, mrStrategy);

    Logger::getInstance().log("Testing Trend Following Strategy...");
    BacktestResult tfResult = backtester.run(candles, tfStrategy);

    // Log results
    std::stringstream ss;
    ss << "Mean Reversion - Return: " << (mrResult.totalReturn * 100) << "%, "
       << "Sharpe: " << mrResult.sharpeRatio << ", "
       << "MaxDD: " << (mrResult.maxDrawdown * 100) << "%, "
       << "Win Rate: " << (mrResult.winRate * 100) << "%";
    Logger::getInstance().log(ss.str());

    ss.str("");
    ss << "Trend Following - Return: " << (tfResult.totalReturn * 100) << "%, "
       << "Sharpe: " << tfResult.sharpeRatio << ", "
       << "MaxDD: " << (tfResult.maxDrawdown * 100) << "%, "
       << "Win Rate: " << (tfResult.winRate * 100) << "%";
    Logger::getInstance().log(ss.str());

    // Walk-forward analysis
    if (candles.size() > 500) {
        Logger::getInstance().log("Running walk-forward analysis...");

        WalkForwardOptimizer::Config wfConfig;
        wfConfig.numWindows = 5;
        wfConfig.trainRatio = 0.7f;
        wfConfig.optimizationTarget = "sharpe";

        WalkForwardOptimizer wfOptimizer(wfConfig, config);

        std::vector<ParameterGrid> paramGrids = {
            {"rsiBuyThreshold", 25.0f, 35.0f, 5.0f},
            {"rsiSellThreshold", 65.0f, 75.0f, 5.0f}
        };

        WalkForwardResult wfResult = wfOptimizer.run(mrStrategy, candles, paramGrids);

        ss.str("");
        ss << "Walk-Forward Efficiency: " << wfResult.walkForwardEfficiency
           << ", Robustness: " << wfResult.robustnessScore
           << ", Is Robust: " << (wfResult.isRobust() ? "Yes" : "No");
        Logger::getInstance().log(ss.str());
    }
}

int main() {
    Logger::getInstance().log("Starting Trading Bot 5.0 (Enhanced Backtesting + Analytics)...");

    // 0. Initialize configuration from environment
    auto configResult = Config::getInstance().initialize();
    if (configResult.isError()) {
        Logger::getInstance().log("Warning: " + configResult.error().message);
    }

    // Set API keys from config (all optional - Yahoo Finance works without keys)
    // NEWS_KEY: Enables NewsAPI for better news headlines
    // ALPHA_VANTAGE_KEY: Optional backup data source
    NetworkUtils::setApiKey("NEWSAPI", Config::getInstance().getApiKey("NEWSAPI"));
    NetworkUtils::setApiKey("ALPHAVANTAGE", Config::getInstance().getApiKey("ALPHAVANTAGE"));

    // Initialize sentiment analyzer
    SentimentAnalyzer::getInstance().init("sentiment.gguf");

    std::vector<Ticker> tickers = readTickers("tickers.csv");
    MLPredictor mlModel;

    // 1. Parallel Execution (Throttled in batches of 4)
    const size_t batchSize = 4;
    for (size_t i = 0; i < tickers.size(); i += batchSize) {
        std::vector<std::future<void>> batchFutures;
        for (size_t j = i; j < i + batchSize && j < tickers.size(); ++j) {
            batchFutures.push_back(std::async(std::launch::async, processTicker, tickers[j], mlModel));
        }
        
        // Wait for current batch to complete before starting next
        for (auto& f : batchFutures) {
            f.wait();
        }
        
        // Brief polite delay between batches
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    Logger::getInstance().log("Analysis Complete. Generating Reports...");

    // 2. Run enhanced backtest for first ticker with sufficient data
    for (const auto& result : globalResults) {
        if (result.history.size() > 200) {
            runEnhancedBacktest(result.symbol, result.history);
            break;  // Just run for first suitable ticker as demo
        }
    }

    // 3. Reporting
    ReportGenerator::generateCSV(globalResults, "report.csv");
    ReportGenerator::generateHTML(globalResults, "report.html");

    Logger::getInstance().log("Reports saved to report.csv and report.html");

    // Summary of enhanced metrics
    Logger::getInstance().log("\n=== Enhanced Backtest Summary ===");
    for (const auto& result : globalResults) {
        if (result.backtest.trades > 0) {
            std::stringstream ss;
            ss << result.symbol << ": "
               << "Return=" << (result.backtest.totalReturn * 100) << "%, "
               << "Sharpe=" << result.backtest.sharpeRatio << ", "
               << "Sortino=" << result.backtest.sortinoRatio << ", "
               << "MaxDD=" << (result.backtest.maxDrawdown * 100) << "%, "
               << "WinRate=" << (result.backtest.winRate * 100) << "%, "
               << "ProfitFactor=" << result.backtest.profitFactor << ", "
               << "Trades=" << result.backtest.trades;
            Logger::getInstance().log(ss.str());
        }
    }

    std::cout << "\nDisclaimer: Not financial advice. Past performance is not indicative of future results.\n";

    return 0;
}
