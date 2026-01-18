#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

struct llama_model;

struct SentimentResult {
    float score;      // -1.0 (Strong Neg) to 1.0 (Strong Pos)
    float confidence; // 0-100%
    std::string label; // "Strong Positive", "Positive", "Neutral", "Negative", "Strong Negative"
};

class SentimentAnalyzer {
public:
    static SentimentAnalyzer& getInstance();
    
    // Initialize with path to GGUF model (recommend FinBERT-GGUF or similar)
    bool init(const std::string& modelPath);
    
    // Batch analysis using parallel threads
    // Returns average score
    float analyze(const std::vector<std::string>& texts);
    
    // Single analysis (exposed if needed)
    SentimentResult analyzeSingle(const std::string& text);

    void setVerbose(bool v);

private:
    SentimentAnalyzer();
    ~SentimentAnalyzer();
    
    SentimentAnalyzer(const SentimentAnalyzer&) = delete;
    SentimentAnalyzer& operator=(const SentimentAnalyzer&) = delete;

    llama_model* model = nullptr;
    bool initialized = false;
    bool verbose = false; // Default false
    std::mutex mutex;
};

