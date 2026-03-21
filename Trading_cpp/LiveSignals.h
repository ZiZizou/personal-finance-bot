#pragma once
#include "MarketData.h"
#include "TimeUtils.h"
#include "MarketClock.h"
#include "BarSeries.h"
#include "Providers.h"
#include "ThreadPool.h"
#include "SentimentService.h"
#include "RiskManagement.h"
#include "IStrategy.h"
#include "TechnicalAnalysis.h"
#include "Config.h"
#include "TelegramNotifier.h"
#include "TradingStrategy.h"
#include "MLPredictor.h"
#include "RegimeDetector.h"
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <memory>
#include <atomic>

// Live signal output row
struct LiveSignalRow {
    std::string timestamp;      // ET format
    std::string symbol;
    double lastClose;
    std::string regime;
    std::string action;         // BUY/SELL/HOLD
    double strength;
    double confidence;
    double limitPrice;
    double stopLoss;
    double takeProfit;
    std::string targets;        // Comma-separated targets
    double sentiment;
    std::string reason;
};

// Configuration for live signals mode
struct LiveSignalsConfig {
    int barSizeMinutes = 60;                   // Hourly bars
    int warmupDays = 60;                       // History for warmup
    std::string outputFile = "live_signals.csv";
    bool includeNews = true;
    bool includeSentiment = true;
    int numWorkers = 4;                        // Thread pool size
    int settleDelaySeconds = 3;                // Delay after bar close
};

// Live signals runner
class LiveSignalsRunner {
public:
    explicit LiveSignalsRunner(const LiveSignalsConfig& config);
    ~LiveSignalsRunner();

    // Load tickers from CSV file
    void loadTickers(const std::string& tickerFile);

    // Add a single ticker
    void addTicker(const std::string& symbol, const std::string& type = "stock");

    // Initialize (load history, setup providers)
    void initialize();

    // Run one iteration (process current bar for all symbols)
    void processBar();

    // Run continuous loop (call from main)
    void run();

    // Stop the loop
    void stop();

    // Check if running
    bool isRunning() const { return running_; }

    // Send status update (for scheduled mode)
    void sendStatus();

private:
    LiveSignalsConfig config_;
    std::atomic<bool> running_{false};

    // Tickers: symbol -> type
    std::vector<std::pair<std::string, std::string>> tickers_;

    // Bar series per symbol
    std::map<std::string, BarSeries> seriesMap_;

    // Providers
    std::unique_ptr<YahooPriceProvider> priceProvider_;
    std::unique_ptr<CachedSentimentProvider> sentimentProvider_;

    // Thread pool
    std::unique_ptr<ThreadPool> pool_;

    // Telegram notifier
    std::unique_ptr<TelegramNotifier> telegramNotifier_;

    // Strategy
    std::unique_ptr<IStrategy> strategy_;

    // Regime detector (HMM-based)
    RegimeDetection::RegimeDetector regimeDetector_;
    bool regimeDetectorTrained_ = false;

    // ML Model for signal generation
    MLPredictor mlModel_;

    // Python Signal Provider for dynamic ONNX models
    std::unique_ptr<PythonSignalProvider> pythonProvider_;

    // Dynamic ticker selection
    std::vector<std::string> selectedTickers_;
    bool usePythonSignals_ = false;
    int maxPythonModels_ = 7;

    // Helper methods for ticker selection
    void selectHotTickers();
    double calculateVolatility(const std::string& symbol);
    double getNewsSentiment(const std::string& symbol);

    // Output file
    std::ofstream outputFile_;
    bool headerWritten_ = false;

    // Helper methods
    void warmupSymbol(const std::string& symbol, const std::string& type);
    LiveSignalRow processSymbol(const std::string& symbol);
    void writeRow(const LiveSignalRow& row);
    void writeHeader();
    std::string detectRegime(const std::vector<double>& prices);
    void sendPeriodicStatus();
    VIXData fetchVIXData();

    // Generate VIX buy/hold/sell signal
    std::pair<std::string, std::string> generateVIXSignal(const VIXData& vix);
};

// Entry point for live signals mode
void runLiveSignals(const LiveSignalsConfig& config);
