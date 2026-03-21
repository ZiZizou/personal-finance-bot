#pragma once
#include <string>
#include <vector>
#include "Result.h"

namespace NetworkUtils {
    // Fetch data with retry logic and caching
    // cacheDurationSeconds: 0 to disable cache, >0 to enable
    std::string fetchData(const std::string& url, int cacheDurationSeconds = 300,
                          const std::vector<std::string>& headers = {});

    // Enhanced fetch with Result type for explicit error handling
    Result<std::string> fetchDataWithResult(const std::string& url,
                                            int cacheDurationSeconds = 300,
                                            const std::vector<std::string>& headers = {});

    // Send HTTP POST request
    std::string postData(const std::string& url, const std::string& payload,
                         const std::vector<std::string>& headers);

    // Enhanced POST with Result type
    Result<std::string> postDataWithResult(const std::string& url,
                                           const std::string& payload,
                                           const std::vector<std::string>& headers);

    // Set API Keys
    void setApiKey(const std::string& service, const std::string& key);
    std::string getApiKey(const std::string& service);

    // Cache management
    void clearCache();
    void setCacheCapacity(size_t capacity);
    void setCacheTTL(int seconds);

    // Rate limiter configuration
    void setRateLimit(const std::string& domain, int requestsPerMinute);

    // Yahoo-specific: Get crumb token for Yahoo Finance API
    std::string getYahooCrumb();

    // Fetch next earnings date for a symbol (returns ISO date string or empty)
    std::string fetchEarningsDate(const std::string& symbol);
}
