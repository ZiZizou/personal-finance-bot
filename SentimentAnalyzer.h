#pragma once
#include <string>
#include <vector>
#include <memory>
#include <mutex>

// Forward declarations to avoid including llama.h in header
struct llama_model;
struct llama_context;

struct SentimentResult {
    float score;      // 1.0 (Pos), -1.0 (Neg), 0.0 (Neu)
    float confidence; // Logit based confidence (simplified)
    std::string label; // "Positive", "Negative", "Neutral"
};

class SentimentAnalyzer {
public:
    static SentimentAnalyzer& getInstance();
    bool init(const std::string& modelPath);
    
    // Updated to return detailed results per article
    SentimentResult analyzeSingle(const std::string& text);
    
    // Aggregates scores (returns average score)
    float analyze(const std::vector<std::string>& texts);

    // Set verbosity for llama.cpp logs
    void setVerbose(bool verbose);

private:
    SentimentAnalyzer(); // Constructor implementation in cpp
    ~SentimentAnalyzer();
    SentimentAnalyzer(const SentimentAnalyzer&) = delete;
    SentimentAnalyzer& operator=(const SentimentAnalyzer&) = delete;

    llama_model* model = nullptr;
    bool initialized = false;
    bool verbose = false; // Default to false
    std::mutex mutex;
};
