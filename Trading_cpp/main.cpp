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
#include "FinancialSentiment.h"
#include "NewsManager.h"
#include "NetworkUtils.h"
#include "Backtester.h"
#include "Logger.h"
#include "ReportGenerator.h"
#include "BlackScholes.h"
#include "Config.h"
#include "Result.h"
#include "MLPredictor.h"

// New enhanced components
#include "BacktestConfig.h"
#include "IStrategy.h"
#include "Strategies/MeanReversionStrategy.h"
#include "Strategies/TrendFollowingStrategy.h"
#include "Strategies/MLStrategy.h"
#include "Strategies/EnsembleStrategy.h"
#include "WalkForward.h"
#include "VolatilityModels.h"
#include "LiveSignals.h"
#include "MarketClock.h"

// ONNX Runtime (optional)
#ifdef USE_ONNXRUNTIME
#include "ONNXPredictor.h"
#endif

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

    // Use EnsembleStrategy (MeanReversion + TrendFollowing) for enhanced backtesting
    std::vector<std::unique_ptr<IStrategy>> ensemble;
    ensemble.push_back(std::make_unique<MeanReversionStrategy>());
    ensemble.push_back(std::make_unique<TrendFollowingStrategy>());
    EnsembleStrategy strategy(std::move(ensemble), 0.5);
    BacktestResult bt = backtester.run(candles, strategy);

    // Also run legacy static method for comparison (optional)
    // BacktestResult btLegacy = Backtester::run(candles);

    Fundamentals fund = fetchFundamentals(t.symbol, t.type);
    OnChainData onChain = {0.0, 0.0, false};
    if (t.type != "stock") {
        onChain = fetchOnChainData(t.symbol);
    }

    std::vector<std::string> news = NewsManager::fetchNews(t.symbol);
    double sentiment = (!news.empty()) ? (double)SentimentAnalyzer::getInstance().analyze(news) : 0.0;

    std::vector<double> prices;
    for(auto& c : candles) prices.push_back(c.close);
    SupportResistance levels = identifyLevels(prices, 60);

    // Default VIX data (neutral) for main.cpp - live mode uses real VIX
    VIXData vix;
    vix.current = 20.0;
    vix.sma20 = 20.0;
    vix.trend = 0.0;

    Signal sig = generateSignal(t.symbol, candles, sentiment, fund, onChain, levels, mlModelCopy, vix);

    // Greeks Calculation for Options
    if (sig.option) {
        double T = (double)sig.option->period_days / 365.0;
        double sigma = 0.30;
        Greeks g = BlackScholes::calculateGreeks(prices.back(), sig.option->strike, T, 0.04, sigma, sig.option->type == "Call");

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

    // Test Ensemble Strategy
    std::vector<std::unique_ptr<IStrategy>> ensChildren;
    ensChildren.push_back(std::make_unique<MeanReversionStrategy>());
    ensChildren.push_back(std::make_unique<TrendFollowingStrategy>());
    EnsembleStrategy ensStrategy(std::move(ensChildren), 0.5);

    Logger::getInstance().log("Testing Ensemble Strategy...");
    BacktestResult ensResult = backtester.run(candles, ensStrategy);

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

    ss.str("");
    ss << "Ensemble - Return: " << (ensResult.totalReturn * 100) << "%, "
       << "Sharpe: " << ensResult.sharpeRatio << ", "
       << "MaxDD: " << (ensResult.maxDrawdown * 100) << "%, "
       << "Win Rate: " << (ensResult.winRate * 100) << "%";
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

    // Check execution mode
    std::string mode = Config::getInstance().getString("MODE", "backtest");
    std::cout << "[DEBUG] BOT MODE: " << mode << std::endl;

    std::string tgToken = Config::getInstance().getString("STOCK_TELEGRAM_BOT_TOKEN", "");
    std::string tgChatId = Config::getInstance().getString("STOCK_TELEGRAM_CHAT_ID", "");
    std::cout << "[DEBUG] Telegram Token set: " << (tgToken.empty() ? "NO" : "YES") << std::endl;
    std::cout << "[DEBUG] Telegram Chat ID set: " << (tgChatId.empty() ? "NO" : "YES") << std::endl;

    if (mode == "live_signals") {
        // DEPRECATED: This mode reads from tickers.csv which is no longer the source of truth.
        // Trading-cpp should be invoked by Python API with specific tickers to analyze.
        // This mode will be removed in a future version.
        Logger::getInstance().log("DEPRECATED: live_signals mode is deprecated.");
        Logger::getInstance().log("Use Python API to manage tickers and invoke C++ for analysis.");

        Logger::getInstance().log("Starting in LIVE SIGNALS mode (deprecated)...");

        // Configure live signals from environment
        LiveSignalsConfig liveConfig;
        liveConfig.barSizeMinutes = Config::getInstance().getInt("BAR_SIZE_MINUTES", 60);
        liveConfig.warmupDays = Config::getInstance().getInt("WARMUP_DAYS", 60);
        liveConfig.outputFile = Config::getInstance().getString("LIVE_OUTPUT_CSV", "live_signals.csv");
        liveConfig.includeNews = Config::getInstance().getBool("LIVE_INCLUDE_NEWS", true);
        liveConfig.includeSentiment = Config::getInstance().getBool("LIVE_INCLUDE_SENTIMENT", true);
        liveConfig.numWorkers = Config::getInstance().getInt("NUM_WORKERS", 4);
        liveConfig.settleDelaySeconds = Config::getInstance().getInt("SETTLE_DELAY_SECONDS", 3);

        // Initialize sentiment analyzer for live mode
        SentimentAnalyzer::getInstance().init();

        // Run live signals mode (blocks until stopped)
        runLiveSignals(liveConfig);

        return 0;
    }

    // DEPRECATED: Scheduled mode - runs once and exits - ideal for Windows Task Scheduler
    // DEPRECATED: This mode reads from tickers.csv directly. It should be invoked by Python API instead.
    // DEPRECATED: The Python scheduler (scheduler.py) handles hourly signal generation.
    // DEPRECATED: C++ should only perform ONNX inference when called by Python with specific tickers.
    if (mode == "scheduled") {
        // Check if market is open (weekdays 8AM-6PM ET)
        auto nowET = MarketClock::nowET();
        if (!MarketClock::isMarketOpen(nowET)) {
            int dow = TimeUtils::getDayOfWeek(nowET);
            int hour = TimeUtils::getHourET(nowET);
            std::string dayName = (dow == 0) ? "Sunday" : (dow == 6) ? "Saturday" : "weekday";
            std::string status = (dow == 0 || dow == 6) ? "Weekend" : "Outside market hours";
            Logger::getInstance().log("Skipping scheduled run: " + status + " (" + dayName + " " + std::to_string(hour) + " ET)");
            std::cout << "Market closed. Skipping scheduled run." << std::endl;
            return 0;
        }

        Logger::getInstance().log("DEPRECATED: scheduled mode - C++ should be invoked by Python API");
        Logger::getInstance().log("Starting in SCHEDULED mode (deprecated - use Python scheduler instead)...");

        // Configure live signals from environment
        LiveSignalsConfig liveConfig;
        liveConfig.barSizeMinutes = Config::getInstance().getInt("BAR_SIZE_MINUTES", 60);
        liveConfig.warmupDays = Config::getInstance().getInt("WARMUP_DAYS", 60);
        liveConfig.outputFile = Config::getInstance().getString("LIVE_OUTPUT_CSV", "live_signals.csv");
        liveConfig.includeNews = Config::getInstance().getBool("LIVE_INCLUDE_NEWS", true);
        liveConfig.includeSentiment = Config::getInstance().getBool("LIVE_INCLUDE_SENTIMENT", true);
        liveConfig.numWorkers = Config::getInstance().getInt("NUM_WORKERS", 4);
        liveConfig.settleDelaySeconds = Config::getInstance().getInt("SETTLE_DELAY_SECONDS", 3);

        // Initialize sentiment analyzer
        SentimentAnalyzer::getInstance().init();

        // Create runner and run once
        LiveSignalsRunner runner(liveConfig);
        runner.loadTickers("tickers.csv");  // DEPRECATED: Use Python API instead
        runner.initialize();

        // Run once and send signals
        runner.processBar();

        // Send completion status
        runner.sendStatus();

        Logger::getInstance().log("Scheduled run complete. Exiting...");
        return 0;
    }

    // Set API keys from config (all optional - Yahoo Finance works without keys)
    // NEWS_KEY: Enables NewsAPI for better news headlines
    // ALPHA_VANTAGE_KEY: Optional backup data source
    NetworkUtils::setApiKey("NEWSAPI", Config::getInstance().getApiKey("NEWSAPI"));
    NetworkUtils::setApiKey("ALPHAVANTAGE", Config::getInstance().getApiKey("ALPHAVANTAGE"));

    // Initialize sentiment analyzer
    SentimentAnalyzer::getInstance().init();

    // DEPRECATED: Tickers should come from Python API (portfolio.json + selected_tickers.txt)
    // This fallback reads tickers.csv for backwards compatibility only.
    std::vector<Ticker> tickers = readTickers("tickers.csv");
    if (tickers.empty()) {
        Logger::getInstance().log("Warning: No tickers loaded from tickers.csv");
        Logger::getInstance().log("Trading-cpp should receive tickers from Python API");
    }
    MLPredictor mlModel;

    // Initialize ONNX predictor if enabled
#ifdef USE_ONNXRUNTIME
    std::unique_ptr<ONNXPredictor> onnxPredictor;
    if (Config::getInstance().getUseONNXModel()) {
        std::string onnxPath = Config::getInstance().getONNXModelPath();
        Logger::getInstance().log("Loading ONNX model from: " + onnxPath);
        onnxPredictor = std::make_unique<ONNXPredictor>(onnxPath);
        if (onnxPredictor->isLoaded()) {
            Logger::getInstance().log("ONNX model loaded successfully on: " + onnxPredictor->getDeviceType());
        } else {
            Logger::getInstance().log("Failed to load ONNX model, falling back to native MLPredictor");
        }
    } else {
        Logger::getInstance().log("Using native MLPredictor (set USE_ONNX_MODEL=true to enable ONNX)");
    }
#else
    Logger::getInstance().log("ONNX Runtime not compiled in. Set USE_ONNXRUNTIME to enable.");
#endif

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
