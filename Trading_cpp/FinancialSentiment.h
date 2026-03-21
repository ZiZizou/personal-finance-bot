#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <unordered_map>

struct SentimentResult {
    double score;      // -1.0 (Strong Neg) to 1.0 (Strong Pos)
    double confidence; // 0-100%
    std::string label; // "Strong Positive", "Positive", "Neutral", "Negative", "Strong Negative"
};

class FinancialSentimentAnalyzer {
public:
    static FinancialSentimentAnalyzer& getInstance();

    // Initialize the analyzer
    bool init();

    // Analyze a single headline
    SentimentResult analyzeHeadline(const std::string& headline);

    // Batch analysis
    double analyze(const std::vector<std::string>& headlines);

    void setVerbose(bool v);

private:
    FinancialSentimentAnalyzer();
    ~FinancialSentimentAnalyzer() = default;

    FinancialSentimentAnalyzer(const FinancialSentimentAnalyzer&) = delete;
    FinancialSentimentAnalyzer& operator=(const FinancialSentimentAnalyzer&) = delete;

    // Keyword-based scoring
    double scoreKeywords(const std::string& text);

    // Financial-specific helpers
    bool isEarningsRelated(const std::string& text);
    double getEarningsSentiment(const std::string& text);
    bool isMergerRelated(const std::string& text);
    double getMergerSentiment(const std::string& text);

    // Sentiment lexicon with weights
    std::unordered_map<std::string, double> positiveWords_;
    std::unordered_map<std::string, double> negativeWords_;
    std::unordered_map<std::string, double> modifierWords_;

    bool initialized_ = false;
    bool verbose_ = false;
    std::mutex mutex_;
};

// Alias for backward compatibility
using SentimentAnalyzer = FinancialSentimentAnalyzer;
