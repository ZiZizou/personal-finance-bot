#include "NetworkUtils.h"
#include "Cache.h"
#include "Result.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <map>
#include <filesystem>
#include <functional>
#include <regex>

#ifdef ENABLE_CURL
#include <curl/curl.h>
#endif

namespace NetworkUtils {

// Global state
static std::map<std::string, std::string> apiKeys;
static LRUCache<std::string, std::string> responseCache(200, std::chrono::seconds(300));
static RateLimiter rateLimiter(60, std::chrono::seconds(60), std::chrono::milliseconds(100));

// Extract domain from URL
static std::string extractDomain(const std::string& url) {
    std::regex domainRegex(R"(https?://([^/]+))");
    std::smatch match;
    if (std::regex_search(url, match, domainRegex)) {
        std::string domain = match[1].str();
        // Group all Yahoo subdomains to share the same rate limit bucket (IP protection)
        if (domain.find("yahoo.com") != std::string::npos) {
            return "yahoo.com";
        }
        return domain;
    }
    return "unknown";
}

// Simple hash for cache keys
static std::string hashUrl(const std::string& url) {
    std::hash<std::string> hasher;
    return std::to_string(hasher(url));
}

void setApiKey(const std::string& service, const std::string& key) {
    apiKeys[service] = key;
}

std::string getApiKey(const std::string& service) {
    if (apiKeys.find(service) != apiKeys.end()) {
        return apiKeys[service];
    }
    return "";  // No default fallback for security
}

#ifdef ENABLE_CURL
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

static Result<std::string> performCurlRequest(const std::string& url,
                                              const std::vector<std::string>& headers,
                                              bool isPost = false,
                                              const std::string& payload = "") {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return Result<std::string>::err(Error::internal("Failed to initialize CURL"));
    }

    std::string readBuffer;
    CURLcode res;

    struct curl_slist* chunk = NULL;
    for (const auto& h : headers) {
        chunk = curl_slist_append(chunk, h.c_str());
    }
    if (chunk) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    // Use a modern Chrome User-Agent
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    if (isPost) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    }

    res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_easy_cleanup(curl);
    if (chunk) {
        curl_slist_free_all(chunk);
    }

    if (res != CURLE_OK) {
        std::string errorMsg = curl_easy_strerror(res);
        if (res == CURLE_OPERATION_TIMEDOUT) {
            return Result<std::string>::err(Error::timeout(url));
        }
        return Result<std::string>::err(Error::network(errorMsg));
    }

    if (httpCode == 429) {
        return Result<std::string>::err(Error::rateLimit("HTTP 429 received"));
    }

    if (httpCode == 401 || httpCode == 403) {
        return Result<std::string>::err(Error::auth("HTTP " + std::to_string(httpCode)));
    }

    if (httpCode >= 400) {
        return Result<std::string>::err(Error::network("HTTP " + std::to_string(httpCode)));
    }

    return Result<std::string>::ok(readBuffer);
}

#else
static Result<std::string> performCurlRequest(const std::string& url,
                                              const std::vector<std::string>& headers,
                                              bool isPost = false,
                                              const std::string& payload = "") {
    (void)url; (void)headers; (void)isPost; (void)payload;
    return Result<std::string>::err(Error::internal("CURL not enabled"));
}
#endif

// Enhanced fetch with caching, rate limiting, and Result type
Result<std::string> fetchDataWithResult(const std::string& url,
                                        int cacheDurationSeconds,
                                        const std::vector<std::string>& headers) {
    // 1. Check in-memory cache first
    std::string cacheKey = hashUrl(url);
    if (cacheDurationSeconds > 0) {
        auto cached = responseCache.get(cacheKey);
        if (cached) {
            return Result<std::string>::ok(*cached);
        }
    }

    // 2. Check file cache for longer-term caching
    std::string cacheDir = ".cache";
    std::string cacheFile = cacheDir + "/" + cacheKey + ".json";

    if (cacheDurationSeconds > 0 && std::filesystem::exists(cacheFile)) {
        auto lastWrite = std::filesystem::last_write_time(cacheFile);
        auto now = std::filesystem::file_time_type::clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - lastWrite).count();

        if (duration < cacheDurationSeconds) {
            std::ifstream ifs(cacheFile);
            std::stringstream buffer;
            buffer << ifs.rdbuf();
            std::string data = buffer.str();

            // Also update in-memory cache
            responseCache.put(cacheKey, data, std::chrono::seconds(cacheDurationSeconds));

            return Result<std::string>::ok(data);
        }
    }

    // 3. Apply rate limiting
    std::string domain = extractDomain(url);
    rateLimiter.waitForAllowance(domain);

    // 4. Fetch with retry (exponential backoff)
    int retries = 3;
    int waitTimeSeconds = 1;
    Result<std::string> result = Result<std::string>::err(Error::network("Request failed"));

    for (int i = 0; i < retries; ++i) {
        result = performCurlRequest(url, headers);

        if (result.isOk()) {
            break;
        }

        // Don't retry on auth errors
        if (result.error().code == Error::AuthError) {
            break;
        }

        // Don't retry on rate limit (wait instead) - though performCurlRequest returns 429 as RateLimitError
        if (result.error().code == Error::RateLimitError) {
            std::this_thread::sleep_for(std::chrono::seconds(waitTimeSeconds));
            waitTimeSeconds *= 2;
            continue;
        }

        if (i < retries - 1) {
            std::this_thread::sleep_for(std::chrono::seconds(waitTimeSeconds));
            waitTimeSeconds *= 2;
        }
    }

    // 5. Cache successful response
    if (result.isOk() && cacheDurationSeconds > 0) {
        std::string data = result.value();

        // In-memory cache
        responseCache.put(cacheKey, data, std::chrono::seconds(cacheDurationSeconds));

        // File cache
        if (!std::filesystem::exists(cacheDir)) {
            std::filesystem::create_directory(cacheDir);
        }
        std::ofstream ofs(cacheFile);
        ofs << data;
    } else if (result.isError()) {
        std::cerr << "[Network Error] " << url << " : " << result.error().message << std::endl;
    }

    return result;
}

// Original interface maintained for backward compatibility
std::string fetchData(const std::string& url, int cacheDurationSeconds,
                      const std::vector<std::string>& headers) {
    auto result = fetchDataWithResult(url, cacheDurationSeconds, headers);
    return result.valueOr("");
}

// Post data with Result type
Result<std::string> postDataWithResult(const std::string& url,
                                       const std::string& payload,
                                       const std::vector<std::string>& headers) {
    std::string domain = extractDomain(url);

    // Rate limiting
    auto waitTime = rateLimiter.getWaitTime(domain);
    if (waitTime.count() > 0) {
        std::this_thread::sleep_for(waitTime);
    }

    if (!rateLimiter.allowRequest(domain)) {
        return Result<std::string>::err(Error::rateLimit("Rate limit for " + domain));
    }

    return performCurlRequest(url, headers, true, payload);
}

// Original interface for backward compatibility
std::string postData(const std::string& url, const std::string& payload,
                     const std::vector<std::string>& headers) {
    auto result = postDataWithResult(url, payload, headers);
    return result.valueOr("");
}

// Cache management
void clearCache() {
    responseCache.clear();
}

void setCacheCapacity(size_t capacity) {
    responseCache.setCapacity(capacity);
}

void setCacheTTL(int seconds) {
    responseCache.setTTL(std::chrono::seconds(seconds));
}

// Rate limiter configuration
void setRateLimit(const std::string& domain, int requestsPerMinute) {
    // Note: Current implementation uses global settings
    // A more sophisticated version would have per-domain settings
    (void)domain;
    (void)requestsPerMinute;
}

}  // namespace NetworkUtils
