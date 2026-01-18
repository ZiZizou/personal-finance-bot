#include "NewsManager.h"
#include "NetworkUtils.h"
#include "json.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <regex>

using json = nlohmann::json;

// --- Helper Functions ---

std::string cleanTitle(std::string text) {
    // Remove CDATA if present
    size_t cdata_start = text.find("<![CDATA[");
    if (cdata_start != std::string::npos) {
        size_t cdata_end = text.find("]]>");
        if (cdata_end != std::string::npos) {
            text = text.substr(cdata_start + 9, cdata_end - (cdata_start + 9));
        }
    }
    return text;
}

#include "Logger.h" // Add Logger

// ... (existing includes)

std::vector<std::string> fetchYahooRSS(const std::string& symbol) {
    std::vector<std::string> headlines;
    // Yahoo RSS for specific ticker
    std::string url = "https://feeds.finance.yahoo.com/rss/2.0/headline?s=" + symbol + "&region=US&lang=en-US";
    std::string rssContent = NetworkUtils::fetchData(url); // Uses cache & retry

    if (rssContent.empty()) {
        Logger::getInstance().log("RSS fetch failed/empty for " + symbol);
        return headlines;
    }

    std::string itemTag = "<item>";
    std::string endItemTag = "</item>";
    std::string titleTag = "<title>";
    std::string endTitleTag = "</title>";
    
    size_t pos = 0;
    int count = 0;
    
    while ((pos = rssContent.find(itemTag, pos)) != std::string::npos && count < 5) {
        size_t endPos = rssContent.find(endItemTag, pos);
        if (endPos == std::string::npos) break;
        
        size_t titleStart = rssContent.find(titleTag, pos);
        if (titleStart != std::string::npos && titleStart < endPos) {
            titleStart += titleTag.length();
            size_t titleEnd = rssContent.find(endTitleTag, titleStart);
            
            if (titleEnd != std::string::npos) {
                std::string rawTitle = rssContent.substr(titleStart, titleEnd - titleStart);
                std::string clean = cleanTitle(rawTitle);
                if (clean.find("Yahoo Finance") == std::string::npos) {
                    headlines.push_back(clean);
                    count++;
                }
            }
        }
        pos = endPos + endItemTag.length();
    }
    
    Logger::getInstance().log("Fetched " + std::to_string(headlines.size()) + " headlines for " + symbol);
    return headlines;
}

std::vector<std::string> fetchNewsAPI(const std::string& symbol) {
    std::vector<std::string> headlines;
    std::string key = NetworkUtils::getApiKey("NEWSAPI");
    if (key == "DEMO" || key.empty()) return headlines;

    std::string url = "https://newsapi.org/v2/everything?q=" + symbol + "&apiKey=" + key + "&pageSize=5&language=en";
    std::string response = NetworkUtils::fetchData(url);

    if (response.empty()) return headlines;

    try {
        json data = json::parse(response);
        if (data.contains("articles")) {
            for (const auto& article : data["articles"]) {
                if (article.contains("title") && !article["title"].is_null()) {
                    headlines.push_back(article["title"].get<std::string>());
                }
            }
        }
    } catch (...) {}
    return headlines;
}

std::vector<std::string> NewsManager::fetchNews(const std::string& symbol) {
    // 1. Try NewsAPI (High Quality, Sentiment Preprocessed potential)
    std::vector<std::string> news = fetchNewsAPI(symbol);
    
    // 2. Supplement with Yahoo RSS if needed
    if (news.empty()) {
        std::vector<std::string> rssNews = fetchYahooRSS(symbol);
        news.insert(news.end(), rssNews.begin(), rssNews.end());
    }
    
    return news;
}