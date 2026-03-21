#pragma once
#include "MarketData.h"
#include "Result.h"
#include "TimeUtils.h"
#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include <map>
#include <mutex>

// Interface for price data providers
class IPriceProvider {
public:
    virtual ~IPriceProvider() = default;

    // Get historical bars for warmup/backtest
    virtual Result<std::vector<Candle>> getHistory(
        const std::string& symbol,
        std::chrono::minutes barSize,
        TimePoint start,
        TimePoint end
    ) = 0;

    // Get the last completed bar (for live trading)
    virtual Result<Candle> getLastCompletedBar(
        const std::string& symbol,
        std::chrono::minutes barSize
    ) = 0;

    // Get recent bars (for incremental updates)
    virtual Result<std::vector<Candle>> getRecentBars(
        const std::string& symbol,
        std::chrono::minutes barSize,
        int numBars
    ) = 0;
};

// Interface for news data providers
class INewsProvider {
public:
    virtual ~INewsProvider() = default;

    // Fetch recent news headlines for a symbol
    virtual Result<std::vector<std::string>> getHeadlines(
        const std::string& symbol,
        int maxResults = 10
    ) = 0;
};

// Interface for fundamental data providers
class IFundamentalsProvider {
public:
    virtual ~IFundamentalsProvider() = default;

    // Fetch fundamentals for a symbol
    virtual Result<Fundamentals> getFundamentals(const std::string& symbol) = 0;
};

// Yahoo Finance implementation of IPriceProvider
class YahooPriceProvider : public IPriceProvider {
public:
    Result<std::vector<Candle>> getHistory(
        const std::string& symbol,
        std::chrono::minutes barSize,
        TimePoint start,
        TimePoint end
    ) override;

    Result<Candle> getLastCompletedBar(
        const std::string& symbol,
        std::chrono::minutes barSize
    ) override;

    Result<std::vector<Candle>> getRecentBars(
        const std::string& symbol,
        std::chrono::minutes barSize,
        int numBars
    ) override;

private:
    std::string buildYahooUrl(const std::string& symbol, const std::string& interval, const std::string& range);
    std::vector<Candle> parseYahooResponse(const std::string& response);
};

// Yahoo Finance implementation of IFundamentalsProvider
class YahooFundamentalsProvider : public IFundamentalsProvider {
public:
    Result<Fundamentals> getFundamentals(const std::string& symbol) override;
};

// Signal from Python service
struct PythonSignal {
    std::string ticker;
    std::string timestamp;
    std::string signal;  // "buy", "sell", "hold"
    double confidence;   // 0.0 to 1.0
    double price;
    double volume;
    std::map<std::string, double> indicators;
    std::string source;
};

// Sentiment from Python service
struct PythonSentiment {
    std::string ticker;
    std::string timestamp;
    double sentiment_score;  // -1.0 to 1.0
    double confidence;       // 0.0 to 1.0
    int article_count;
    std::string headline;
    std::string source;
};

// Python Signal Provider - fetches signals from Python API service
class PythonSignalProvider {
public:
    PythonSignalProvider(const std::string& baseUrl = "http://localhost:8000");

    // Get signal for a single ticker
    Result<PythonSignal> getSignal(const std::string& ticker);

    // Get signals for multiple tickers
    Result<std::vector<PythonSignal>> getBatchSignals(const std::vector<std::string>& tickers);

    // Get sentiment for a ticker
    Result<PythonSentiment> getSentiment(const std::string& ticker, int days = 7);

    // Report selected tickers to Python (for model training)
    Result<bool> reportSelectedTickers(const std::vector<std::string>& tickers);

    // Check if service is available
    bool isAvailable() const;

    // Get/set base URL
    void setBaseUrl(const std::string& url);
    std::string getBaseUrl() const;

private:
    std::string baseUrl_;
    mutable std::mutex mutex_;

    std::string buildSignalUrl(const std::string& ticker);
    std::string buildBatchSignalUrl(const std::vector<std::string>& tickers);
    std::string buildSentimentUrl(const std::string& ticker, int days);
    PythonSignal parseSignalResponse(const std::string& response);
    PythonSentiment parseSentimentResponse(const std::string& response);
};

// Helper to determine if a bar is complete (not still forming)
inline std::optional<Candle> findLastCompletedBar(
    const std::vector<Candle>& bars,
    TimePoint now,
    std::chrono::minutes barSize,
    std::chrono::seconds settleDelay = std::chrono::seconds(2)
) {
    for (int i = static_cast<int>(bars.size()) - 1; i >= 0; --i) {
        // A bar is complete if: bar.ts + barSize <= now - settleDelay
        auto barEnd = bars[i].ts + barSize;
        if (barEnd <= now - settleDelay) {
            return bars[i];
        }
    }
    return std::nullopt;
}
