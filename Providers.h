#pragma once
#include "MarketData.h"
#include "Result.h"
#include "TimeUtils.h"
#include <vector>
#include <string>
#include <chrono>
#include <memory>

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
