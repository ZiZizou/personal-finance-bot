#include "LiveSignals.h"
#include "Strategies/MeanReversionStrategy.h"
#include "Strategies/RegimeSwitchingStrategy.h"
#include "TradingStrategy.h"
#include "TechnicalAnalysis.h"
#include "RegimeDetector.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>

LiveSignalsRunner::LiveSignalsRunner(const LiveSignalsConfig& config)
    : config_(config) {
    priceProvider_ = std::make_unique<YahooPriceProvider>();
    sentimentProvider_ = std::make_unique<CachedSentimentProvider>();
    pool_ = std::make_unique<ThreadPool>(config.numWorkers);

    // Use a placeholder strategy - we'll use TradingStrategy::generateSignal directly
    strategy_ = std::make_unique<MeanReversionStrategy>();

    std::string token = Environment::get("STOCK_TELEGRAM_BOT_TOKEN");
    std::string chatId = Environment::get("STOCK_TELEGRAM_CHAT_ID");
    telegramNotifier_ = std::make_unique<TelegramNotifier>(token, chatId);

    // Initialize Python signal provider if enabled
    Config& cfg = Config::instance();
    usePythonSignals_ = cfg.getUsePythonSignals();
    maxPythonModels_ = cfg.getMaxPythonModels();

    if (usePythonSignals_) {
        std::string apiUrl = cfg.getPythonSignalsUrl();
        pythonProvider_ = std::make_unique<PythonSignalProvider>(apiUrl);
        std::cout << "[PythonSignals] Enabled - URL: " << apiUrl << ", Max models: " << maxPythonModels_ << std::endl;
    }
}

LiveSignalsRunner::~LiveSignalsRunner() {
    stop();
    if (outputFile_.is_open()) {
        outputFile_.close();
    }
}

void LiveSignalsRunner::sendStatus() {
    sendPeriodicStatus();
}

void LiveSignalsRunner::loadTickers(const std::string& tickerFile) {
    std::ifstream file(tickerFile);
    if (!file.is_open()) {
        std::cerr << "Failed to open ticker file: " << tickerFile << std::endl;
        return;
    }

    std::string line;
    // Skip header if exists
    std::getline(file, line);

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        // Parse: symbol,type
        size_t commaPos = line.find(',');
        std::string symbol, type;

        if (commaPos != std::string::npos) {
            symbol = line.substr(0, commaPos);
            type = line.substr(commaPos + 1);
        } else {
            symbol = line;
            type = "stock";
        }

        // Trim whitespace
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(symbol);
        trim(type);

        if (!symbol.empty()) {
            addTicker(symbol, type);
        }
    }

    std::cout << "Loaded " << tickers_.size() << " tickers" << std::endl;
}

void LiveSignalsRunner::addTicker(const std::string& symbol, const std::string& type) {
    tickers_.emplace_back(symbol, type);
}

void LiveSignalsRunner::initialize() {
    // Open output file
    outputFile_.open(config_.outputFile, std::ios::app);

    // Write header if file is empty
    outputFile_.seekp(0, std::ios::end);
    if (outputFile_.tellp() == 0) {
        writeHeader();
    }

    if (telegramNotifier_ && telegramNotifier_->isEnabled()) {
        std::cout << "[DEBUG] Telegram notifier is initialized and ENABLED." << std::endl;
        auto res = telegramNotifier_->sendStatusMessage("🚀 Trading Bot started in Live Signals mode");
        if (res.isError()) {
            std::cerr << "[DEBUG] Initial Telegram message failed: " << res.error().message << std::endl;
        }
    } else {
        std::cout << "[DEBUG] Telegram notifier is DISABLED (missing token or chat ID)." << std::endl;
    }

    // Warmup each symbol
    std::cout << "Warming up history for " << tickers_.size() << " symbols..." << std::endl;

    for (const auto& [symbol, type] : tickers_) {
        warmupSymbol(symbol, type);
    }

    std::cout << "Warmup complete" << std::endl;

    // Train HMM-based regime detector using combined price data
    std::cout << "Training regime detector..." << std::endl;
    std::vector<double> combinedPrices;
    std::vector<int64_t> combinedVolumes;

    for (const auto& [symbol, type] : tickers_) {
        auto it = seriesMap_.find(symbol);
        if (it != seriesMap_.end()) {
            const auto& bars = it->second.bars();
            for (const auto& bar : bars) {
                if (combinedPrices.size() < 5000) {  // Limit training data
                    combinedPrices.push_back(bar.close);
                    combinedVolumes.push_back(bar.volume);
                }
            }
        }
    }

    if (combinedPrices.size() >= 200) {
        regimeDetectorTrained_ = regimeDetector_.train(combinedPrices, combinedVolumes);
        if (regimeDetectorTrained_) {
            std::cout << "Regime detector trained successfully" << std::endl;
        } else {
            std::cout << "Regime detector training failed, using fallback" << std::endl;
        }
    } else {
        std::cout << "Insufficient data for regime detector training" << std::endl;
    }

    // Select hot tickers for Python models if enabled
    if (usePythonSignals_) {
        selectHotTickers();
    }
}

void LiveSignalsRunner::warmupSymbol(const std::string& symbol, const std::string& type) {
    auto barSize = std::chrono::minutes(config_.barSizeMinutes);
    auto now = TimeUtils::now();
    auto start = now - std::chrono::hours(24 * config_.warmupDays);

    auto result = priceProvider_->getHistory(symbol, barSize, start, now);
    if (result.isOk()) {
        BarSeries series(result.value());
        seriesMap_[symbol] = series;
        std::cout << "  " << symbol << ": " << series.size() << " bars loaded" << std::endl;
    } else {
        std::cerr << "  " << symbol << ": Failed to load history" << std::endl;
    }
}

void LiveSignalsRunner::processBar() {
    std::vector<std::future<LiveSignalRow>> futures;

    // Submit tasks for each symbol
    for (const auto& [symbol, type] : tickers_) {
        futures.push_back(pool_->submit([this, symbol]() {
            return processSymbol(symbol);
        }));
    }

    // Collect results and write
    std::vector<LiveSignalRow> rows;
    for (auto& fut : futures) {
        try {
            auto row = fut.get();
            writeRow(row);
            rows.push_back(std::move(row));
        } catch (const std::exception& e) {
            std::cerr << "Error processing: " << e.what() << std::endl;
        }
    }

    // Flush output
    outputFile_.flush();

    // Fetch VIX data and generate signal for Telegram
    VIXData vix = fetchVIXData();
    auto vixSignal = generateVIXSignal(vix);

    // Log VIX signal
    std::cout << "  [VIX Signal] " << vixSignal.first << " - " << vixSignal.second << std::endl;

    // Send Telegram notification with VIX signal
    if (telegramNotifier_ && telegramNotifier_->isEnabled() && !rows.empty()) {
        auto result = telegramNotifier_->notify(rows, config_.barSizeMinutes, vixSignal, vix.current);
        if (result.isError()) {
            std::cerr << "[Telegram] " << result.error().message << std::endl;
        }
    }
}

void LiveSignalsRunner::sendPeriodicStatus() {
    if (!telegramNotifier_ || !telegramNotifier_->isEnabled()) {
        return;
    }

    // Get current market status
    auto now = MarketClock::nowET();
    bool isOpen = MarketClock::isMarketOpen(now);

    std::ostringstream ss;
    ss << "<b>📊 Status Update " << TimeUtils::formatTimeET(now) << "</b>\n\n";

    if (isOpen) {
        ss << "🟢 Market Open (8AM-6PM ET)\n";
    } else {
        ss << "🔴 Market Closed\n";
    }

    ss << "Monitoring " << tickers_.size() << " symbols\n";
    ss << "Bar size: " << config_.barSizeMinutes << " min";

    auto result = telegramNotifier_->sendStatusMessage(ss.str());
    if (result.isError()) {
        std::cerr << "[Telegram periodic] " << result.error().message << std::endl;
    } else {
        std::cout << "[Telegram] Periodic status sent" << std::endl;
    }
}

LiveSignalRow LiveSignalsRunner::processSymbol(const std::string& symbol) {
    LiveSignalRow row;
    row.symbol = symbol;
    row.action = "HOLD";
    row.strength = 0.0;
    row.confidence = 0.0;
    row.sentiment = 0.0;

    auto& series = seriesMap_[symbol];

    // Fetch recent bars
    auto barSize = std::chrono::minutes(config_.barSizeMinutes);
    auto recentResult = priceProvider_->getRecentBars(symbol, barSize, 5);

    if (recentResult.isError()) {
        row.reason = "Failed to fetch bars: " + recentResult.error().details;
        return row;
    }

    auto recentBars = recentResult.value();
    auto now = TimeUtils::now();

    // Find last completed bar
    auto completedBar = findLastCompletedBar(recentBars, now, barSize);
    if (!completedBar) {
        row.reason = "No completed bar";
        return row;
    }

    // Try to append the new bar (only if not already processed)
    bool isNewBar = series.tryAppend(*completedBar);

    // Set timestamp
    row.timestamp = TimeUtils::formatTimeET(completedBar->ts);
    row.lastClose = completedBar->close;

    // Get sentiment (if enabled)
    if (config_.includeSentiment) {
        auto sentimentResult = sentimentProvider_->getSentiment(symbol);
        if (sentimentResult.isOk()) {
            row.sentiment = sentimentResult.value();
        }
    }

    // Check if this ticker is selected for Python signals
    bool isSelectedTicker = std::find(selectedTickers_.begin(), selectedTickers_.end(), symbol) != selectedTickers_.end();

    // Try Python signal provider first for selected tickers
    Signal sig;
    bool usedPythonSignal = false;

    if (usePythonSignals_ && pythonProvider_ && isSelectedTicker) {
        auto pythonResult = pythonProvider_->getSignal(symbol);
        if (pythonResult.isOk()) {
            PythonSignal psig = pythonResult.value();

            // Convert Python signal to our signal format
            sig.action = psig.signal;
            sig.confidence = psig.confidence;
            sig.reason = "Python ONNX: " + psig.source;

            // Set entry/exit prices
            sig.entry = psig.price;
            sig.stopLoss = psig.price * 0.95; // Default 5% stop loss
            sig.takeProfit = psig.price * 1.10; // Default 10% take profit

            // Extract price from signal if available
            if (psig.price > 0) {
                row.lastClose = psig.price;
            }

            usedPythonSignal = true;
            std::cout << "[PythonSignals] " << symbol << ": " << psig.signal
                      << " (confidence: " << psig.confidence << ")" << std::endl;
        }
    }

    // Use native C++ signal generation if Python failed or not selected
    if (!usedPythonSignal) {
        // Extract close prices
        auto closes = series.closePrices();

        // Fetch fundamentals for signal generation
        // Use tickers list to find the type for this symbol
        std::string type = "stock";
        for (const auto& t : tickers_) {
            if (t.first == symbol) {
                type = t.second;
                break;
            }
        }
        Fundamentals fund = fetchFundamentals(symbol, type);
        OnChainData onChain = {0.0, 0.0, false};
        if (type != "stock") {
            onChain = fetchOnChainData(symbol);
        }

        auto levels = identifyLevels(closes, 60);

        // Fetch VIX data for volatility analysis
        VIXData vix = fetchVIXData();

        sig = generateSignal(symbol, series.bars(), row.sentiment, fund, onChain, levels, mlModel_, vix);
    }

    // Map signal to output
    if (sig.action == "buy") {
        row.action = "BUY";
    } else if (sig.action == "sell") {
        row.action = "SELL";
    } else {
        row.action = "HOLD";
    }

    row.strength = std::abs(sig.confidence);
    row.confidence = sig.confidence;
    row.reason = sig.reason;
    row.limitPrice = sig.entry;
    row.stopLoss = sig.stopLoss;
    row.takeProfit = sig.takeProfit;

    // Detect regime
    auto closes = series.closePrices();
    row.regime = detectRegime(closes);

    // Build targets string
    std::vector<double> extrema = findLocalExtrema(closes, 60, true);
    std::ostringstream targetsStream;
    for (size_t i = 0; i < extrema.size() && i < 3; ++i) {
        if (i > 0) targetsStream << ";";
        targetsStream << std::fixed << std::setprecision(2) << extrema[i];
    }
    row.targets = targetsStream.str();

    return row;
}

void LiveSignalsRunner::writeHeader() {
    outputFile_ << "timestamp,symbol,lastClose,regime,action,strength,confidence,"
                << "limitPrice,stopLoss,takeProfit,targets,sentiment,reason" << std::endl;
    headerWritten_ = true;
}

void LiveSignalsRunner::writeRow(const LiveSignalRow& row) {
    outputFile_ << row.timestamp << ","
                << row.symbol << ","
                << std::fixed << std::setprecision(2) << row.lastClose << ","
                << row.regime << ","
                << row.action << ","
                << std::setprecision(3) << row.strength << ","
                << row.confidence << ","
                << std::setprecision(2) << row.limitPrice << ","
                << row.stopLoss << ","
                << row.takeProfit << ","
                << "\"" << row.targets << "\","
                << std::setprecision(3) << row.sentiment << ","
                << "\"" << row.reason << "\""
                << std::endl;

    // Also log to console
    std::cout << "[" << row.timestamp << "] " << row.symbol
              << " " << row.action << " @ " << row.lastClose
              << " (strength: " << row.strength << ")" << std::endl;
}

std::string LiveSignalsRunner::detectRegime(const std::vector<double>& prices) {
    // Try to use HMM-based regime detector if trained
    if (regimeDetectorTrained_ && prices.size() >= 100) {
        // Need volumes for full feature extraction
        std::vector<int64_t> emptyVols;
        auto info = regimeDetector_.detectCurrentRegime(prices, emptyVols);
        if (info.regime != RegimeDetection::MarketRegime::Unknown) {
            return info.name;
        }
    }

    // Fallback to simple regime detection
    return detectMarketRegime(prices);
}

VIXData LiveSignalsRunner::fetchVIXData() {
    VIXData vix;
    vix.current = 0.0;
    vix.sma20 = 0.0;
    vix.trend = 0.0;

    // Fetch VIX data using "^VIX" symbol
    const std::string vixSymbol = "^VIX";
    auto barSize = std::chrono::minutes(config_.barSizeMinutes);
    auto now = TimeUtils::now();
    auto start = now - std::chrono::hours(24 * 30); // 30 days of data for SMA20

    auto result = priceProvider_->getHistory(vixSymbol, barSize, start, now);
    if (result.isError()) {
        std::cerr << "  [VIX] Failed to fetch VIX data: " << result.error().details << std::endl;
        // Return default VIX (neutral) if fetch fails
        vix.current = 20.0; // Default to mid-range
        vix.sma20 = 20.0;
        vix.trend = 0.0;
        return vix;
    }

    auto bars = result.value();
    if (bars.empty()) {
        std::cerr << "  [VIX] No VIX bars returned" << std::endl;
        vix.current = 20.0;
        vix.sma20 = 20.0;
        vix.trend = 0.0;
        return vix;
    }

    // Extract close prices
    std::vector<double> vixCloses;
    for (const auto& bar : bars) {
        vixCloses.push_back(bar.close);
    }

    // Current VIX value
    vix.current = vixCloses.back();

    // Compute 20-day SMA manually
    if (vixCloses.size() >= 20) {
        double sum = 0.0;
        for (size_t i = vixCloses.size() - 20; i < vixCloses.size(); ++i) {
            sum += vixCloses[i];
        }
        vix.sma20 = sum / 20.0;
    } else {
        vix.sma20 = vix.current;
    }

    // VIX trend (momentum)
    vix.trend = vix.current - vix.sma20;

    std::cout << "  [VIX] Current: " << vix.current << ", SMA20: " << vix.sma20
              << ", Trend: " << (vix.trend > 0 ? "+" : "") << vix.trend << std::endl;

    return vix;
}

std::pair<std::string, std::string> LiveSignalsRunner::generateVIXSignal(const VIXData& vix) {
    // VIX signal logic:
    // - VIX < 15: Low fear, complacency (SELL signal - market may be overconfident)
    // - VIX 15-20: Normal range (HOLD)
    // - VIX 20-25: Elevated fear (HOLD/SELL)
    // - VIX > 25: High fear (BUY - contrarian, fear means opportunity)
    // - VIX rising trend: Bearish for stocks (add negative bias)
    // - VIX falling trend: Bullish for stocks (add positive bias)

    std::string action;
    std::string reason;

    double vixScore = 0.0;

    // VIX level analysis
    if (vix.current < 12.0) {
        action = "SELL";
        vixScore = -0.20;
        reason = "Very low VIX (<12) - Complacency, overbought risk";
    } else if (vix.current < 15.0) {
        action = "SELL";
        vixScore = -0.15;
        reason = "Low VIX (<15) - Complacency, reduced upside";
    } else if (vix.current > 30.0) {
        action = "BUY";
        vixScore = 0.25;
        reason = "Very high VIX (>30) - Extreme fear, contrarian opportunity";
    } else if (vix.current > 25.0) {
        action = "BUY";
        vixScore = 0.15;
        reason = "High VIX (>25) - Elevated fear, potential bottom";
    } else if (vix.current > 20.0) {
        action = "HOLD";
        vixScore = -0.05;
        reason = "Moderate-high VIX (>20) - Elevated volatility";
    } else {
        action = "HOLD";
        vixScore = 0.0;
        reason = "Normal VIX range (15-20) - Neutral";
    }

    // Adjust based on trend
    if (vix.trend > 2.0) {
        // Rising VIX - fear increasing
        if (action == "BUY") {
            reason += ", but rising (cautious)";
        } else if (action == "SELL") {
            action = "HOLD";
            reason = "VIX rising - waiting for stabilization";
        }
    } else if (vix.trend < -2.0) {
        // Falling VIX - fear decreasing
        if (action == "SELL") {
            reason += ", but falling (cautious)";
        } else if (action == "BUY") {
            reason += ", falling (bullish)";
        }
    }

    return {action, reason};
}

void LiveSignalsRunner::run() {
    running_ = true;
    std::cout << "Starting live signals mode..." << std::endl;

    auto lastStatusTime = std::chrono::steady_clock::now();
    constexpr int STATUS_INTERVAL_MINUTES = 30;

    while (running_) {
        auto now = MarketClock::nowET();

        // Check if we're in extended trading period (8AM - 6PM)
        bool inTradingPeriod = MarketClock::isTradingPeriod(now);

        // Send periodic status update every 30 minutes during trading period
        if (inTradingPeriod) {
            auto nowTime = std::chrono::steady_clock::now();
            auto minutesSinceStatus = std::chrono::duration_cast<std::chrono::minutes>(
                nowTime - lastStatusTime).count();

            if (minutesSinceStatus >= STATUS_INTERVAL_MINUTES) {
                sendPeriodicStatus();
                lastStatusTime = nowTime;
            }
        }

        // Wait for market to be open (8AM - 6PM)
        if (!MarketClock::isMarketOpen(now)) {
            std::cout << "Market closed. Sleeping..." << std::endl;
            std::this_thread::sleep_for(std::chrono::minutes(1));
            continue;
        }

        // Sleep until next bar close
        MarketClock::sleepUntilNextBarClose(now, config_.barSizeMinutes, config_.settleDelaySeconds);

        // Process the bar
        if (running_) {
            std::cout << "\n=== Processing bar at " << TimeUtils::formatTimeET(TimeUtils::now()) << " ===" << std::endl;
            processBar();
        }
    }

    std::cout << "Live signals mode stopped" << std::endl;
}

void LiveSignalsRunner::stop() {
    running_ = false;
}

void LiveSignalsRunner::selectHotTickers() {
    // Select up to maxPythonModels_ tickers based on volatility and news sentiment
    std::cout << "[PythonSignals] Selecting hot tickers..." << std::endl;

    struct TickerScore {
        std::string symbol;
        double volatility;
        double sentiment;
        double totalScore;
    };

    std::vector<TickerScore> scores;

    for (const auto& [symbol, type] : tickers_) {
        double volatility = calculateVolatility(symbol);
        double sentiment = getNewsSentiment(symbol);

        // Score: higher volatility + positive sentiment = higher priority
        // Also favor tickers with recent news
        double totalScore = volatility * 0.6 + (sentiment + 1.0) * 0.4;

        scores.push_back({symbol, volatility, sentiment, totalScore});
    }

    // Sort by total score (highest first)
    std::sort(scores.begin(), scores.end(),
        [](const TickerScore& a, const TickerScore& b) {
            return a.totalScore > b.totalScore;
        });

    // Select top N
    int count = std::min(maxPythonModels_, static_cast<int>(scores.size()));
    selectedTickers_.clear();
    for (int i = 0; i < count; ++i) {
        selectedTickers_.push_back(scores[i].symbol);
    }

    std::cout << "[PythonSignals] Selected " << selectedTickers_.size() << " hot tickers:" << std::endl;
    for (size_t i = 0; i < selectedTickers_.size(); ++i) {
        std::cout << "  " << (i+1) << ". " << selectedTickers_[i]
                  << " (vol: " << scores[i].volatility << ", sent: " << scores[i].sentiment << ")" << std::endl;
    }

    // Report selected tickers to Python API
    if (pythonProvider_ && pythonProvider_->isAvailable()) {
        std::cout << "[PythonSignals] Reporting selected tickers to Python API..." << std::endl;
        auto result = pythonProvider_->reportSelectedTickers(selectedTickers_);
        if (result.isOk()) {
            std::cout << "[PythonSignals] Successfully reported tickers to Python API" << std::endl;
        } else {
            std::cerr << "[PythonSignals] Failed to report tickers: " << result.error().details << std::endl;
        }
    }
}

double LiveSignalsRunner::calculateVolatility(const std::string& symbol) {
    // Calculate volatility using ATR (Average True Range)
    auto it = seriesMap_.find(symbol);
    if (it == seriesMap_.end() || it->second.size() < 14) {
        return 0.5; // Default medium volatility
    }

    const auto& bars = it->second.bars();
    if (bars.size() < 14) {
        return 0.5;
    }

    // Calculate ATR manually
    double atr = 0.0;
    for (size_t i = bars.size() - 14; i < bars.size(); ++i) {
        double tr = bars[i].high - bars[i].low;
        atr += tr;
    }
    atr /= 14.0;

    // Normalize by current price to get percentage volatility
    double currentPrice = bars.back().close;
    if (currentPrice > 0) {
        return (atr / currentPrice) * 100.0; // As percentage
    }

    return 0.5;
}

double LiveSignalsRunner::getNewsSentiment(const std::string& symbol) {
    // Get sentiment from sentiment provider
    auto result = sentimentProvider_->getSentiment(symbol);
    if (result.isOk()) {
        return result.value();
    }

    // Fallback: check if there's a Python sentiment available
    if (pythonProvider_ && pythonProvider_->isAvailable()) {
        auto pythonResult = pythonProvider_->getSentiment(symbol);
        if (pythonResult.isOk()) {
            return pythonResult.value().sentiment_score;
        }
    }

    return 0.0; // Neutral sentiment
}

// Entry point
void runLiveSignals(const LiveSignalsConfig& config) {
    LiveSignalsRunner runner(config);
    runner.loadTickers("tickers.csv");
    runner.initialize();
    runner.run();
}
