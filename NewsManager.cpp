#include "NewsManager.h"
#include <iostream>
#include <vector>
#include <string>
#include <regex>
#include "json.hpp"

#include <curl/curl.h>

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

std::string fetchURL(const std::string& url) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        // Mimic a browser to avoid being blocked
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
             std::cerr << "News Fetch Error: " << curl_easy_strerror(res) << std::endl;
        }
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

// Helper to remove XML tags and clean text
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

std::vector<std::string> NewsManager::fetchNews(const std::string& symbol) {
    std::vector<std::string> headlines;
    std::string target = symbol;
    
    // Adjust symbol for Yahoo RSS if needed
    // Crypto on Yahoo is usually BTC-USD, stocks are just tickers
    
    std::string url = "https://feeds.finance.yahoo.com/rss/2.0/headline?s=" + target + "&region=US&lang=en-US";
    
    std::cout << "     Fetching RSS: " << url << "..." << std::endl;
    std::string rssContent = fetchURL(url);

    if (rssContent.empty()) {
        std::cerr << "     Warning: Empty response from RSS feed." << std::endl;
        return headlines;
    }

    // Simple XML Parsing for <title> tags inside <item>
    // We avoid full XML libraries to keep dependencies low, using string search logic.
    
    std::string itemTag = "<item>";
    std::string endItemTag = "</item>";
    std::string titleTag = "<title>";
    std::string endTitleTag = "</title>";
    
    size_t pos = 0;
    int count = 0;
    
    while ((pos = rssContent.find(itemTag, pos)) != std::string::npos && count < 5) { // Limit to 5 latest articles
        size_t endPos = rssContent.find(endItemTag, pos);
        if (endPos == std::string::npos) break;
        
        // Look for title inside this item
        size_t titleStart = rssContent.find(titleTag, pos);
        if (titleStart != std::string::npos && titleStart < endPos) {
            titleStart += titleTag.length();
            size_t titleEnd = rssContent.find(endTitleTag, titleStart);
            
            if (titleEnd != std::string::npos) {
                std::string rawTitle = rssContent.substr(titleStart, titleEnd - titleStart);
                std::string clean = cleanTitle(rawTitle);
                
                // Filter out generic Yahoo titles
                if (clean.find("Yahoo Finance") == std::string::npos) {
                    headlines.push_back(clean);
                    // std::cout << "     - Found: " << clean << std::endl;
                    count++;
                }
            }
        }
        pos = endPos + endItemTag.length();
    }
    
    if (headlines.empty()) {
        // Fallback for empty feeds (sometimes happens with generic tickers)
        // std::cout << "     No news found in RSS." << std::endl;
    }

    return headlines;
}
