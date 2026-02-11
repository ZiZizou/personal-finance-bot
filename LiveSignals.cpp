#include "LiveSignals.h"
#include "Strategies/MeanReversionStrategy.h"
#include "TradingStrategy.h"
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
    strategy_ = std::make_unique<MeanReversionStrategy>();
}

LiveSignalsRunner::~LiveSignalsRunner() {
    stop();
    if (outputFile_.is_open()) {
        outputFile_.close();
    }
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

    // Warmup each symbol
    std::cout << "Warming up history for " << tickers_.size() << " symbols..." << std::endl;

    for (const auto& [symbol, type] : tickers_) {
        warmupSymbol(symbol, type);
    }

    std::cout << "Warmup complete" << std::endl;
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
    for (auto& fut : futures) {
        try {
            auto row = fut.get();
            writeRow(row);
        } catch (const std::exception& e) {
            std::cerr << "Error processing: " << e.what() << std::endl;
        }
    }

    // Flush output
    outputFile_.flush();
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
        row.reason = "Failed to fetch bars: " + recentResult.error().message;
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

    // Try to append the new bar
    if (!series.tryAppend(*completedBar)) {
        row.reason = "Bar already processed";
        return row;
    }

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

    // Extract close prices
    auto closes = series.closePrices();

    // Detect regime
    row.regime = detectRegime(closes);

    // Generate signal
    auto signal = strategy_->generateSignal(series.bars(), series.size() - 1);

    // Map signal to output
    switch (signal.type) {
        case SignalType::Buy:
            row.action = "BUY";
            break;
        case SignalType::Sell:
            row.action = "SELL";
            break;
        default:
            row.action = "HOLD";
            break;
    }

    row.strength = signal.strength;
    row.confidence = signal.confidence;
    row.reason = signal.reason;

    // Calculate ATR and levels for order ideas
    double atr = computeATR(series.bars(), 14);
    auto levels = identifyLevels(closes, 60);

    // Build order idea if actionable
    if (signal.type == SignalType::Buy) {
        auto idea = buildBuyIdea(
            row.lastClose, atr, levels.support,
            signal.stopLossPrice, signal.takeProfitPrice, levels.resistance
        );
        row.limitPrice = idea.limitPrice;
        row.stopLoss = idea.stopLossPrice;
        row.takeProfit = idea.takeProfitPrice;
    } else if (signal.type == SignalType::Sell) {
        auto idea = buildSellIdea(
            row.lastClose, atr, levels.resistance,
            signal.stopLossPrice, signal.takeProfitPrice, levels.support
        );
        row.limitPrice = idea.limitPrice;
        row.stopLoss = idea.stopLossPrice;
        row.takeProfit = idea.takeProfitPrice;
    }

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
    // Use the existing detectMarketRegime function
    return detectMarketRegime(prices);
}

void LiveSignalsRunner::run() {
    running_ = true;
    std::cout << "Starting live signals mode..." << std::endl;

    while (running_) {
        auto now = MarketClock::nowET();

        // Wait for market to be open
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

// Entry point
void runLiveSignals(const LiveSignalsConfig& config) {
    LiveSignalsRunner runner(config);
    runner.loadTickers("tickers.csv");
    runner.initialize();
    runner.run();
}
