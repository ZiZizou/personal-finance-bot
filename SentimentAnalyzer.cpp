#include "SentimentAnalyzer.h"
#include "NetworkUtils.h"
#include "json.hpp"
#include "Logger.h"
#include <iostream>
#include <numeric>
#include <vector>
#include <future>
#include <thread>
#include <algorithm>

using json = nlohmann::json;

#ifdef ENABLE_LLAMA
#include <llama.h>

// Log callback function
static void llama_log_callback(ggml_log_level level, const char * text, void * user_data) {
    bool* verbose_ptr = static_cast<bool*>(user_data);
    if (verbose_ptr && *verbose_ptr) {
        fputs(text, stderr);
    }
}
#endif

SentimentAnalyzer::SentimentAnalyzer() {
#ifdef ENABLE_LLAMA
    // Register log callback immediately
    llama_log_set(llama_log_callback, &this->verbose);
#endif
}

void SentimentAnalyzer::setVerbose(bool v) {
    std::lock_guard<std::mutex> lock(mutex);
    this->verbose = v;
}

SentimentAnalyzer& SentimentAnalyzer::getInstance() {
    static SentimentAnalyzer instance;
    return instance;
}

SentimentAnalyzer::~SentimentAnalyzer() {
#ifdef ENABLE_LLAMA
    if (model) llama_model_free(model);
#endif
}

bool SentimentAnalyzer::init(const std::string& modelPath) {
    std::lock_guard<std::mutex> lock(mutex);
    if (initialized) return true;

#ifdef ENABLE_LLAMA
    llama_backend_init();
    auto mparams = llama_model_default_params();
    mparams.n_gpu_layers = 0; // Force CPU
    
    model = llama_model_load_from_file(modelPath.c_str(), mparams);

    if (!model) {
        std::cerr << "Warning: Failed to load sentiment model from '" << modelPath << "'. Sentiment will be neutral." << std::endl;
        return false;
    }
#else
    if (verbose) std::cout << "[INFO] Llama disabled. Using API or keyword fallback." << std::endl;
#endif

    initialized = true;
    return true;
}

SentimentResult SentimentAnalyzer::analyzeSingle(const std::string& text) {
    SentimentResult res = {0.0f, 0.0f, "Neutral"};
    
    // 1. Try HuggingFace FinBERT API First
    // Model: ProsusAI/finbert
    std::string hfKey = NetworkUtils::getApiKey("HF");
    if (hfKey != "DEMO" && !hfKey.empty()) {
        std::string url = "https://api-inference.huggingface.co/models/ProsusAI/finbert";
        
        json payload = {
            {"inputs", text}
        };
        
        std::vector<std::string> headers = {
            "Authorization: Bearer " + hfKey,
            "Content-Type: application/json"
        };
        
        // Use POST
        std::string response = NetworkUtils::postData(url, payload.dump(), headers);
        
        if (!response.empty() && response.find("error") == std::string::npos) {
            try {
                json result = json::parse(response);
                // HF usually returns array of array of dicts: [[{"label":"positive", "score":0.9}, ...]]
                // Or sometimes just dict if error
                if (result.is_array() && !result.empty()) {
                    auto predictions = result[0];
                    if (predictions.is_array()) {
                        // Find highest score
                        float maxScore = -1.0f;
                        std::string label = "neutral";
                        
                        for (const auto& item : predictions) {
                            float s = item.value("score", 0.0f);
                            if (s > maxScore) {
                                maxScore = s;
                                label = item.value("label", "neutral");
                            }
                        }
                        
                        res.confidence = maxScore * 100.0f;
                        if (label == "positive") { res.score = 1.0f; res.label = "Positive"; }
                        else if (label == "negative") { res.score = -1.0f; res.label = "Negative"; }
                        else { res.score = 0.0f; res.label = "Neutral"; }
                        
                        return res; // Success
                    }
                }
            } catch (...) {
                // JSON parse error, fall through
            }
        }
    }

#ifdef ENABLE_LLAMA
    // 2. Try Local Llama (omitted for brevity, assume logic exists)
#endif

    // 3. Fallback Keyword Logic
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    std::vector<std::string> posWords = {
        "soar", "jump", "record", "surge", "climb", "rally", "beat", "profit", "gain", 
        "bull", "growth", "upgrade", "buy", "outperform", "dividend", "revenue up", 
        "optimis", "strong", "higher", "positive", "approval", "launch", "partnership"
    };
    
    std::vector<std::string> negWords = {
        "plunge", "crash", "drop", "fall", "miss", "loss", "bear", "down", "downgrade", 
        "sell", "lower", "negative", "warn", "risk", "lawsuit", "ban", "regulation", 
        "inflation", "recession", "weak", "cut", "fail", "halt"
    };

    int posCount = 0;
    int negCount = 0;

    for (const auto& w : posWords) {
        if (lower.find(w) != std::string::npos) posCount++;
    }
    for (const auto& w : negWords) {
        if (lower.find(w) != std::string::npos) negCount++;
    }

    if (posCount > negCount) {
        res.score = 0.5f + (0.1f * std::min(posCount, 5)); 
        res.label = "Positive";
        res.confidence = 60.0f + (posCount * 5.0f);
    } else if (negCount > posCount) {
        res.score = -0.5f - (0.1f * std::min(negCount, 5));
        res.label = "Negative";
        res.confidence = 60.0f + (negCount * 5.0f);
    } else {
        res.score = 0.0f;
        res.label = "Neutral";
        res.confidence = 50.0f;
    }
    
    res.score = std::clamp(res.score, -1.0f, 1.0f);
    res.confidence = std::clamp(res.confidence, 0.0f, 100.0f);

    return res;
}

float SentimentAnalyzer::analyze(const std::vector<std::string>& texts) {
    if (texts.empty()) return 0.0f;

    float totalScore = 0.0f;
    int processed = 0;
    
    // Serial or parallel depending on API limits? 
    // HF API has rate limits. Better to do serial or batched. 
    // For simplicity, serial loop for API.
    
    for(const auto& t : texts) {
        SentimentResult r = analyzeSingle(t);
        totalScore += r.score;
        processed++;
        // Small delay to be polite to free API
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return (processed > 0) ? (totalScore / processed) : 0.0f;
}
