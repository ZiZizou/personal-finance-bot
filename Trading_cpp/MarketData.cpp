#include "MarketData.h"
#include "NetworkUtils.h"
#include "json.hpp"
#include <iostream>
#include <algorithm>
#include <map>

using json = nlohmann::json;

// --- Helper Functions ---

std::string getCoinApiKey() {
    return NetworkUtils::getApiKey("COINAPI");
}

std::string formatSymbol(const std::string& symbol, const std::string& type) {
    std::string s = symbol;
    if (type == "crypto") {
        // Yahoo uses "BTC-USD" format for crypto, which matches our input usually.
        // If input is "bitcoin" (CoinGecko style), we might need mapping, 
        // but assuming input "BTC-USD" for Yahoo is correct.
        // Ensure it's uppercase
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        if (s.find("-USD") == std::string::npos && s.length() <= 5) {
            s += "-USD";
        }
    }
    return s;
}

// --- Implementation ---

std::vector<Candle> fetchCandles(const std::string& symbol, const std::string& type) {
    std::vector<Candle> candles;
    std::string ySymbol = formatSymbol(symbol, type);

    // Yahoo Chart API v8 - include crumb for authentication
    std::string crumb = NetworkUtils::getYahooCrumb();
    std::string url = "https://query1.finance.yahoo.com/v8/finance/chart/" + ySymbol + "?interval=1d&range=2y";
    if (!crumb.empty()) {
        url += "&crumb=" + crumb;
    }
    // std::cout << std::endl << "URL: " << url << std::endl;

    std::string response = NetworkUtils::fetchData(url);
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
                            if (closes[i].is_null()) continue;

                            Candle c;
                            int64_t tsValue = timestamps[i].get<int64_t>();
                            c.ts = TimeUtils::fromUnixSeconds(tsValue);
                            c.date = std::to_string(tsValue);  // Keep for backward compatibility
                            c.close = closes[i].get<double>();
                            c.open = !opens[i].is_null() ? opens[i].get<double>() : c.close;
                            c.high = !highs[i].is_null() ? highs[i].get<double>() : c.close;
                            c.low = !lows[i].is_null() ? lows[i].get<double>() : c.close;

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
        std::cerr << "JSON Parse Error (Candles " << symbol << "): " << e.what() << std::endl;
    } catch (...) {
        // std::cerr << "Unknown Error parsing candles for " << symbol << std::endl;
    }
    return candles;
}

Fundamentals fetchFundamentals(const std::string& symbol, const std::string& type) {
    Fundamentals fund;
    // Initialize all numeric fields to 0
    fund.pe_ratio = 0.0; fund.market_cap = 0.0; fund.fifty_day_avg = 0.0;
    fund.forward_pe = 0.0; fund.peg_ratio = 0.0; fund.price_to_book = 0.0;
    fund.price_to_sales = 0.0; fund.enterprise_to_revenue = 0.0; fund.enterprise_to_ebitda = 0.0;
    fund.debt_to_equity = 0.0; fund.current_ratio = 0.0; fund.quick_ratio = 0.0;
    fund.total_cash = 0.0; fund.total_debt = 0.0; fund.free_cashflow = 0.0;
    fund.earnings_growth = 0.0; fund.revenue_growth = 0.0; fund.eps = 0.0;
    fund.revenue_per_share = 0.0; fund.gross_margin = 0.0; fund.operating_margin = 0.0;
    fund.profit_margin = 0.0; fund.fifty_two_week_high = 0.0; fund.fifty_two_week_low = 0.0;
    fund.two_hundred_day_avg = 0.0; fund.beta = 0.0; fund.avg_volume = 0.0;
    fund.avg_volume_10day = 0.0; fund.dividend_yield = 0.0; fund.dividend_rate = 0.0;
    fund.payout_ratio = 0.0; fund.analyst_rating = 0.0; fund.short_percent_float = 0.0;
    fund.short_ratio = 0.0; fund.institutional_ownership_pct = 0.0;
    fund.earnings_estimate_avg = 0.0; fund.earnings_estimate_low = 0.0;
    fund.earnings_estimate_high = 0.0; fund.earnings_surprise_pct = 0.0;
    fund.valid = false;

    std::string ySymbol = formatSymbol(symbol, type);
    std::string crumb = NetworkUtils::getYahooCrumb();
    std::string url = "https://query1.finance.yahoo.com/v7/finance/quote?symbols=" + ySymbol;
    if (!crumb.empty()) {
        url += "&crumb=" + crumb;
    }

    std::string response = NetworkUtils::fetchData(url);
    if (response.empty()) return fund;

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
            }
        }
    } catch (...) {}
    return fund;
}

OnChainData fetchOnChainData(const std::string& symbol) {
    OnChainData data = {0.0, 0.0, false};
    
    // CoinAPI (Optional)
    std::string key = getCoinApiKey();
    if (key == "DEMO" || key.empty()) return data;

    // Mapping for CoinAPI (BTC, ETH)
    std::string assetId = symbol;
    if (assetId.find("-USD") != std::string::npos) {
        assetId = assetId.substr(0, assetId.find("-USD"));
    }

    // Example Endpoint: Exchange Flows (Simulated endpoint for CoinAPI Free Tier constraints)
    // Real endpoint: /v1/metrics/symbol/...
    std::string url = "https://rest.coinapi.io/v1/assets/" + assetId + "?apikey=" + key; 
    
    // Note: CoinAPI Free Tier is very limited. This often returns generic info.
    // For specific on-chain metrics like "Net Inflow", you need paid tiers or specific glassnode APIs.
    // We will do a basic fetch to validate the key works.
    
    std::vector<std::string> headers = { "X-CoinAPI-Key: " + key };
    std::string response = NetworkUtils::fetchData(url, 300, headers);
    
    if (!response.empty()) {
        try {
            json j = json::parse(response);
            if (j.is_array() && !j.empty()) {
                 data.valid = true;
                 // Mocking flow data based on volume trend if real API doesn't provide it freely
                 // Real implementation would parse "volume_1day_usd" etc.
                 data.net_inflow = 0.0; // Placeholder
            }
        } catch (...) {}
    }

    return data;
}