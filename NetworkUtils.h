#pragma once
#include <string>
#include <functional>

namespace NetworkUtils {
    // Fetch data with retry logic and caching
    // cacheDurationSeconds: 0 to disable cache, >0 to enable
    std::string fetchData(const std::string& url, int cacheDurationSeconds = 300, const std::vector<std::string>& headers = {});

    // Send HTTP POST request
    std::string postData(const std::string& url, const std::string& payload, const std::vector<std::string>& headers);
    
    // Set API Keys (simplification)
    void setApiKey(const std::string& service, const std::string& key);
    std::string getApiKey(const std::string& service);
}
