#include "NetworkUtils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <map>
#include <filesystem>
#include <functional>

#ifdef ENABLE_CURL
#include <curl/curl.h>
#endif

// Simple hash for filenames
size_t stringHash(const std::string& str) {
    return std::hash<std::string>{}(str);
}

namespace NetworkUtils {

    std::map<std::string, std::string> apiKeys;

    void setApiKey(const std::string& service, const std::string& key) {
        apiKeys[service] = key;
    }

    std::string getApiKey(const std::string& service) {
        if (apiKeys.find(service) != apiKeys.end()) return apiKeys[service];
        return "DEMO"; // Default or error
    }

#ifdef ENABLE_CURL
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        size_t totalSize = size * nmemb;
        userp->append((char*)contents, totalSize);
        return totalSize;
    }

    std::string performCurl(const std::string& url, const std::vector<std::string>& headers) {
        CURL* curl;
        CURLcode res;
        std::string readBuffer;
        curl = curl_easy_init();
        if (curl) {
            struct curl_slist* chunk = NULL;
            for (const auto& h : headers) {
                chunk = curl_slist_append(chunk, h.c_str());
            }
            if (chunk) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); 
            
            res = curl_easy_perform(curl);
            if (res != CURLE_OK) readBuffer = ""; 
            curl_easy_cleanup(curl);
            if (chunk) curl_slist_free_all(chunk);
        }
        return readBuffer;
    }

    std::string performCurlPost(const std::string& url, const std::string& payload, const std::vector<std::string>& headers) {
        CURL* curl;
        CURLcode res;
        std::string readBuffer;
        curl = curl_easy_init();
        if (curl) {
            struct curl_slist* chunk = NULL;
            for (const auto& h : headers) {
                chunk = curl_slist_append(chunk, h.c_str());
            }
            if (chunk) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
            
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); 
            
            res = curl_easy_perform(curl);
            if (res != CURLE_OK) readBuffer = ""; 
            curl_easy_cleanup(curl);
            if (chunk) curl_slist_free_all(chunk);
        }
        return readBuffer;
    }
#else
    std::string performCurl(const std::string& url, const std::vector<std::string>& headers) {
        std::cout << "[WARN] Curl disabled. Cannot fetch: " << url << std::endl;
        return "";
    }
    std::string performCurlPost(const std::string& url, const std::string& payload, const std::vector<std::string>& headers) {
        return "";
    }
#endif

    std::string fetchData(const std::string& url, int cacheDurationSeconds, const std::vector<std::string>& headers) {
        // 1. Check Cache
        std::string cacheDir = ".cache";
        if (cacheDurationSeconds > 0) {
            if (!std::filesystem::exists(cacheDir)) {
                std::filesystem::create_directory(cacheDir);
            }
            size_t hash = stringHash(url); 
            std::string cacheFile = cacheDir + "/" + std::to_string(hash) + ".json";
            
            if (std::filesystem::exists(cacheFile)) {
                auto lastWrite = std::filesystem::last_write_time(cacheFile);
                auto now = std::filesystem::file_time_type::clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - lastWrite).count();
                
                if (duration < cacheDurationSeconds) {
                    std::ifstream ifs(cacheFile);
                    std::stringstream buffer;
                    buffer << ifs.rdbuf();
                    return buffer.str();
                }
            }
        }

        // 2. Fetch with Retry (Exponential Backoff)
        int retries = 3;
        int waitTime = 1; // Seconds
        std::string data;
        
        for (int i = 0; i < retries; ++i) {
            data = performCurl(url, headers);
            if (!data.empty()) break;
            
            if (i < retries - 1) {
                // std::cout << "  Retry " << i+1 << "/" << retries << " in " << waitTime << "s..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(waitTime));
                waitTime *= 2;
            }
        }

        // 3. Save to Cache
        if (!data.empty() && cacheDurationSeconds > 0) {
            size_t hash = stringHash(url);
            std::string cacheFile = cacheDir + "/" + std::to_string(hash) + ".json";
            std::ofstream ofs(cacheFile);
            ofs << data;
        }

        return data;
    }

    std::string postData(const std::string& url, const std::string& payload, const std::vector<std::string>& headers) {
        return performCurlPost(url, payload, headers);
    }
}