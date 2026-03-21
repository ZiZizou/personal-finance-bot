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
    std::string crumb = NetworkUtils::getYahooCrumb();
    std::string url = "https://query1.finance.yahoo.com/v8/finance/chart/" + ySymbol +
           "?interval=" + interval + "&range=" + range + "&includePrePost=false";
    if (!crumb.empty()) {
        url += "&crumb=" + crumb;
    }
    return url;
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
    std::string crumb = NetworkUtils::getYahooCrumb();
    std::string url = "https://query1.finance.yahoo.com/v7/finance/quote?symbols=" + ySymbol;
    if (!crumb.empty()) {
        url += "&crumb=" + crumb;
    }

    std::string response = NetworkUtils::fetchData(url);
    if (response.empty()) {
        return Result<Fundamentals>::err(Error::network("Failed to fetch fundamentals"));
    }

    Fundamentals fund;
    // Initialize all fields to 0
    fund.pe_ratio = 0.0;
    fund.market_cap = 0.0;
    fund.fifty_day_avg = 0.0;
    fund.forward_pe = 0.0;
    fund.peg_ratio = 0.0;
    fund.price_to_book = 0.0;
    fund.price_to_sales = 0.0;
    fund.enterprise_to_revenue = 0.0;
    fund.enterprise_to_ebitda = 0.0;
    fund.debt_to_equity = 0.0;
    fund.current_ratio = 0.0;
    fund.quick_ratio = 0.0;
    fund.total_cash = 0.0;
    fund.total_debt = 0.0;
    fund.free_cashflow = 0.0;
    fund.earnings_growth = 0.0;
    fund.revenue_growth = 0.0;
    fund.eps = 0.0;
    fund.revenue_per_share = 0.0;
    fund.gross_margin = 0.0;
    fund.operating_margin = 0.0;
    fund.profit_margin = 0.0;
    fund.fifty_two_week_high = 0.0;
    fund.fifty_two_week_low = 0.0;
    fund.two_hundred_day_avg = 0.0;
    fund.beta = 0.0;
    fund.avg_volume = 0.0;
    fund.avg_volume_10day = 0.0;
    fund.dividend_yield = 0.0;
    fund.dividend_rate = 0.0;
    fund.payout_ratio = 0.0;
    fund.analyst_rating = 0.0;
    fund.short_percent_float = 0.0;
    fund.short_ratio = 0.0;
    fund.institutional_ownership_pct = 0.0;
    fund.earnings_estimate_avg = 0.0;
    fund.earnings_estimate_low = 0.0;
    fund.earnings_estimate_high = 0.0;
    fund.earnings_surprise_pct = 0.0;

    try {
        json data = json::parse(response);
        if (data.contains("quoteResponse") && data["quoteResponse"].contains("result")) {
            auto result = data["quoteResponse"]["result"];
            if (!result.empty()) {
                auto q = result[0];

                // Basic fields
                fund.pe_ratio = q.value("trailingPE", 0.0);
                fund.market_cap = q.value("marketCap", 0.0);
                fund.fifty_day_avg = q.value("fiftyDayAverage", 0.0);

                // Valuation
                fund.forward_pe = q.value("forwardPE", 0.0);
                fund.peg_ratio = q.value("pegRatio", 0.0);
                fund.price_to_book = q.value("priceToBook", 0.0);
                fund.price_to_sales = q.value("priceToSalesTrailing12Months", 0.0);
                fund.enterprise_to_revenue = q.value("enterpriseToRevenue", 0.0);
                fund.enterprise_to_ebitda = q.value("enterpriseToEbitda", 0.0);

                // Financial Health
                fund.debt_to_equity = q.value("debtToEquity", 0.0);
                fund.current_ratio = q.value("currentRatio", 0.0);
                fund.quick_ratio = q.value("quickRatio", 0.0);
                fund.total_cash = q.value("totalCash", 0.0);
                fund.total_debt = q.value("totalDebt", 0.0);
                fund.free_cashflow = q.value("freeCashflow", 0.0);

                // Growth
                fund.earnings_growth = q.value("earningsGrowth", 0.0);
                fund.revenue_growth = q.value("revenueGrowth", 0.0);
                fund.eps = q.value("epsTrailingTwelveMonths", 0.0);
                fund.revenue_per_share = q.value("revenuePerShare", 0.0);

                // Margins
                fund.gross_margin = q.value("grossMargins", 0.0);
                fund.operating_margin = q.value("operatingMargins", 0.0);
                fund.profit_margin = q.value("profitMargins", 0.0);

                // Price/Volume
                fund.fifty_two_week_high = q.value("fiftyTwoWeekHigh", 0.0);
                fund.fifty_two_week_low = q.value("fiftyTwoWeekLow", 0.0);
                fund.two_hundred_day_avg = q.value("twoHundredDayAverage", 0.0);
                fund.beta = q.value("beta", 0.0);
                fund.avg_volume = q.value("averageVolume", 0.0);
                fund.avg_volume_10day = q.value("averageVolume10days", 0.0);

                // Dividends
                fund.dividend_yield = q.value("dividendYield", 0.0);
                fund.dividend_rate = q.value("dividendRate", 0.0);
                fund.payout_ratio = q.value("payoutRatio", 0.0);

                // Company Info
                fund.sector = q.value("sector", "");
                fund.industry = q.value("industry", "");
                fund.market_state = q.value("marketState", "UNKNOWN");

                // Analyst & Sentiment
                fund.analyst_rating = q.value("averageRating", 0.0);
                fund.short_percent_float = q.value("shortPercentFloat", 0.0);
                fund.short_ratio = q.value("shortRatio", 0.0);
                fund.institutional_ownership_pct = q.value("heldPercentInstitutions", 0.0);

                fund.valid = true;
                return Result<Fundamentals>::ok(fund);
            }
        }
    } catch (...) {}

    return Result<Fundamentals>::err(Error::parse("Failed to parse fundamentals"));
}

// ========== PythonSignalProvider Implementation ==========

PythonSignalProvider::PythonSignalProvider(const std::string& baseUrl)
    : baseUrl_(baseUrl) {}

std::string PythonSignalProvider::buildSignalUrl(const std::string& ticker) {
    return baseUrl_ + "/api/signals/" + ticker;
}

std::string PythonSignalProvider::buildBatchSignalUrl(const std::vector<std::string>& tickers) {
    std::string symbols;
    for (size_t i = 0; i < tickers.size(); ++i) {
        if (i > 0) symbols += ",";
        symbols += tickers[i];
    }
    return baseUrl_ + "/api/batch/signals?symbols=" + symbols;
}

std::string PythonSignalProvider::buildSentimentUrl(const std::string& ticker, int days) {
    return baseUrl_ + "/api/sentiment/" + ticker + "?days=" + std::to_string(days);
}

PythonSignal PythonSignalProvider::parseSignalResponse(const std::string& response) {
    PythonSignal signal;
    signal.confidence = 0.0;
    signal.price = 0.0;
    signal.volume = 0.0;

    try {
        json data = json::parse(response);
        signal.ticker = data.value("ticker", "");
        signal.timestamp = data.value("timestamp", "");
        signal.signal = data.value("signal", "hold");
        signal.confidence = data.value("confidence", 0.0);
        signal.price = data.value("price", 0.0);
        signal.volume = data.value("volume", 0.0);
        signal.source = data.value("source", "python_api");

        if (data.contains("indicators")) {
            for (auto& [key, value] : data["indicators"].items()) {
                signal.indicators[key] = value.get<double>();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing signal response: " << e.what() << std::endl;
    }

    return signal;
}

PythonSentiment PythonSignalProvider::parseSentimentResponse(const std::string& response) {
    PythonSentiment sentiment;
    sentiment.confidence = 0.0;
    sentiment.sentiment_score = 0.0;
    sentiment.article_count = 0;

    try {
        json data = json::parse(response);
        sentiment.ticker = data.value("ticker", "");
        sentiment.timestamp = data.value("timestamp", "");
        sentiment.sentiment_score = data.value("sentiment_score", 0.0);
        sentiment.confidence = data.value("confidence", 0.0);
        sentiment.article_count = data.value("article_count", 0);
        sentiment.headline = data.value("headline", "");
        sentiment.source = data.value("source", "python_api");
    } catch (const std::exception& e) {
        std::cerr << "Error parsing sentiment response: " << e.what() << std::endl;
    }

    return sentiment;
}

Result<PythonSignal> PythonSignalProvider::getSignal(const std::string& ticker) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string url = buildSignalUrl(ticker);
    std::string response = NetworkUtils::fetchData(url);

    if (response.empty()) {
        return Result<PythonSignal>::err(Error::network("Failed to fetch signal from Python service"));
    }

    auto signal = parseSignalResponse(response);
    return Result<PythonSignal>::ok(signal);
}

Result<std::vector<PythonSignal>> PythonSignalProvider::getBatchSignals(const std::vector<std::string>& tickers) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string url = buildBatchSignalUrl(tickers);
    std::string response = NetworkUtils::fetchData(url);

    if (response.empty()) {
        return Result<std::vector<PythonSignal>>::err(Error::network("Failed to fetch batch signals from Python service"));
    }

    std::vector<PythonSignal> signals;
    try {
        json data = json::parse(response);
        if (data.contains("signals")) {
            for (const auto& sig : data["signals"]) {
                PythonSignal s;
                s.ticker = sig.value("ticker", "");
                s.timestamp = sig.value("timestamp", "");
                s.signal = sig.value("signal", "hold");
                s.confidence = sig.value("confidence", 0.0);
                s.price = sig.value("price", 0.0);
                s.volume = sig.value("volume", 0.0);
                s.source = sig.value("source", "python_api");

                if (sig.contains("indicators")) {
                    for (auto& [key, value] : sig["indicators"].items()) {
                        s.indicators[key] = value.get<double>();
                    }
                }
                signals.push_back(s);
            }
        }
    } catch (const std::exception& e) {
        return Result<std::vector<PythonSignal>>::err(Error::parse(std::string("Failed to parse batch signals: ") + e.what()));
    }

    return Result<std::vector<PythonSignal>>::ok(signals);
}

Result<PythonSentiment> PythonSignalProvider::getSentiment(const std::string& ticker, int days) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string url = buildSentimentUrl(ticker, days);
    std::string response = NetworkUtils::fetchData(url);

    if (response.empty()) {
        return Result<PythonSentiment>::err(Error::network("Failed to fetch sentiment from Python service"));
    }

    auto sentiment = parseSentimentResponse(response);
    return Result<PythonSentiment>::ok(sentiment);
}

bool PythonSignalProvider::isAvailable() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string url = baseUrl_ + "/api/health";
    std::string response = NetworkUtils::fetchData(url);
    return !response.empty();
}

Result<bool> PythonSignalProvider::reportSelectedTickers(const std::vector<std::string>& tickers) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (tickers.empty()) {
        return Result<bool>::ok(true);
    }

    // Build JSON body
    std::string body = "{\"tickers\":[";
    for (size_t i = 0; i < tickers.size(); ++i) {
        if (i > 0) body += ",";
        body += "\"" + tickers[i] + "\"";
    }
    body += "]}";

    // Build URL
    std::string url = baseUrl_ + "/api/models/select-tickers";

    // Post to API
    std::string response = NetworkUtils::postData(url, body, "application/json");

    if (response.empty()) {
        return Result<bool>::err(Error::network("Failed to report selected tickers"));
    }

    // Check for success in response
    if (response.find("\"status\":\"ok\"") != std::string::npos ||
        response.find("\"status\": \"ok\"") != std::string::npos) {
        return Result<bool>::ok(true);
    }

    return Result<bool>::ok(true); // Assume success even if parse fails
}

void PythonSignalProvider::setBaseUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    baseUrl_ = url;
}

std::string PythonSignalProvider::getBaseUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return baseUrl_;
}
