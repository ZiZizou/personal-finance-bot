#pragma once
#include <string>
#include <vector>

class NewsManager {
public:
    static std::vector<std::string> fetchNews(const std::string& symbol);
};
