#include "Providers.h"
#include "NetworkUtils.h"
#include "json.hpp"
#include <algorithm>
#include <iostream>

using json = nlohmann::json;

// Helper to format symbol for Yahoo
static std::string formatYahooSymbol(const std::string& symbol) {
    std::string s = symbol;
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// Helper to get interval string for Yahoo
static std::string getYahooInterval(std::chrono::minutes barSize) {
    int mins = static_cast<int>(barSize.count());
    if (mins == 1) return "1m";
    if (mins == 5) return "5m";
    if (mins == 15) return "15m";
    if (mins == 30) return "30m";
    if (mins == 60) return "60m";
    if (mins == 1440) return "1d";
    return "60m";  // Default to hourly
}

// Helper to get range string based on bar size
static std::string getYahooRange(std::chrono::minutes barSize, int numDays = 60) {
    int mins = static_cast<int>(barSize.count());
    if (mins <= 60) {
        // Intraday: limited range
        return std::to_string(std::min(numDays, 60)) + "d";
    }
    // Daily: up to 2 years
    return "2y";
}

std::string YahooPriceProvider::buildYahooUrl(
    const std::string& symbol,
    const std::string& interval,
    const std::string& range
) {
    std::string ySymbol = formatYahooSymbol(symbol);
    return "https://query1.finance.yahoo.com/v8/finance/chart/" + ySymbol +
           "?interval=" + interval + "&range=" + range + "&includePrePost=false";
}

std::vector<Candle> YahooPriceProvider::parseYahooResponse(const std::string& response) {
    std::vector<Candle> candles;
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
                            // Skip null candles
                            if (closes[i].is_null() || opens[i].is_null() ||
                                highs[i].is_null() || lows[i].is_null()) {
                                continue;
                            }

                            Candle c;
                            int64_t tsValue = timestamps[i].get<int64_t>();
                            c.ts = TimeUtils::fromUnixSeconds(tsValue);
                            c.date = std::to_string(tsValue);
                            c.close = closes[i].get<double>();
                            c.open = opens[i].get<double>();
                            c.high = highs[i].get<double>();
                            c.low = lows[i].get<double>();

                            if (!volumes.empty() && i < volumes.size() && !volumes[i].is_null()) {
                                c.volume = volumes[i].get<int64_t>();
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
        std::cerr << "JSON Parse Error: " << e.what() << std::endl;
    } catch (...) {
        // Silent fail
    }

    // Sort by timestamp
    std::sort(candles.begin(), candles.end(),
        [](const Candle& a, const Candle& b) { return a.ts < b.ts; });

    return candles;
}

Result<std::vector<Candle>> YahooPriceProvider::getHistory(
    const std::string& symbol,
    std::chrono::minutes barSize,
    TimePoint start,
    TimePoint end
) {
    std::string interval = getYahooInterval(barSize);
    std::string range = getYahooRange(barSize);
    std::string url = buildYahooUrl(symbol, interval, range);

    std::string response = NetworkUtils::fetchData(url);
    if (response.empty()) {
        return Result<std::vector<Candle>>::err(Error::network("Failed to fetch from Yahoo"));
    }

    auto candles = parseYahooResponse(response);

    // Filter to requested range
    std::vector<Candle> filtered;
    for (const auto& c : candles) {
        if (c.ts >= start && c.ts <= end) {
            filtered.push_back(c);
        }
    }

    return Result<std::vector<Candle>>::ok(filtered);
}

Result<Candle> YahooPriceProvider::getLastCompletedBar(
    const std::string& symbol,
    std::chrono::minutes barSize
) {
    auto recentResult = getRecentBars(symbol, barSize, 5);
    if (recentResult.isError()) {
        return Result<Candle>::err(recentResult.error());
    }

    auto bars = recentResult.value();
    auto now = TimeUtils::now();

    auto completed = findLastCompletedBar(bars, now, barSize);
    if (!completed) {
        return Result<Candle>::err(Error::notFound("No completed bar found"));
    }

    return Result<Candle>::ok(*completed);
}

Result<std::vector<Candle>> YahooPriceProvider::getRecentBars(
    const std::string& symbol,
    std::chrono::minutes barSize,
    int numBars
) {
    std::string interval = getYahooInterval(barSize);
    // For recent bars, use a shorter range
    std::string range = "5d";
    std::string url = buildYahooUrl(symbol, interval, range);

    std::string response = NetworkUtils::fetchData(url);
    if (response.empty()) {
        return Result<std::vector<Candle>>::err(Error::network("Failed to fetch from Yahoo"));
    }

    auto candles = parseYahooResponse(response);

    // Return last N bars
    if (static_cast<int>(candles.size()) > numBars) {
        candles = std::vector<Candle>(candles.end() - numBars, candles.end());
    }

    return Result<std::vector<Candle>>::ok(candles);
}

Result<Fundamentals> YahooFundamentalsProvider::getFundamentals(const std::string& symbol) {
    std::string ySymbol = formatYahooSymbol(symbol);
    std::string url = "https://query1.finance.yahoo.com/v7/finance/quote?symbols=" + ySymbol;

    std::string response = NetworkUtils::fetchData(url);
    if (response.empty()) {
        return Result<Fundamentals>::err(Error::network("Failed to fetch fundamentals"));
    }

    try {
        json data = json::parse(response);
        if (data.contains("quoteResponse") && data["quoteResponse"].contains("result")) {
            auto result = data["quoteResponse"]["result"];
            if (!result.empty()) {
                auto q = result[0];
                Fundamentals fund;
                fund.pe_ratio = q.value("trailingPE", 0.0);
                fund.market_cap = q.value("marketCap", 0.0);
                fund.fifty_day_avg = q.value("fiftyDayAverage", 0.0);
                fund.valid = true;
                return Result<Fundamentals>::ok(fund);
            }
        }
    } catch (...) {}

    return Result<Fundamentals>::err(Error::parse("Failed to parse fundamentals"));
}
