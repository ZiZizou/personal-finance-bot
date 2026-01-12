#include "SentimentAnalyzer.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <vector>
#include <numeric>
#include <llama.h>

// Log callback function
static void llama_log_callback(ggml_log_level level, const char * text, void * user_data) {
    bool* verbose_ptr = static_cast<bool*>(user_data);
    if (verbose_ptr && *verbose_ptr) {
        fputs(text, stderr);
    }
}

SentimentAnalyzer::SentimentAnalyzer() {
    // Register log callback immediately
    llama_log_set(llama_log_callback, &this->verbose);
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
    // Context is now local, only free model
    if (model) llama_model_free(model);
}

bool SentimentAnalyzer::init(const std::string& modelPath) {
    std::lock_guard<std::mutex> lock(mutex);
    if (initialized) return true;

    llama_backend_init();

    auto mparams = llama_model_default_params();
    mparams.n_gpu_layers = 0; // Force CPU for stability
    
    model = llama_model_load_from_file(modelPath.c_str(), mparams);

    if (!model) {
        std::cerr << "Warning: Failed to load sentiment model from '" << modelPath << "'. Sentiment will be neutral." << std::endl;
        return false;
    }

    // We no longer create a global context here.
    
    initialized = true;
    if (verbose) std::cout << "Sentiment Model loaded successfully." << std::endl;
    return true;
}

std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& text) {
    std::vector<llama_token> tokens(text.length() + 64); // Extra space for formatting
    int n = llama_tokenize(vocab, text.c_str(), text.length(), tokens.data(), tokens.size(), true, false);
    if (n < 0) {
        tokens.resize(-n);
        n = llama_tokenize(vocab, text.c_str(), text.length(), tokens.data(), tokens.size(), true, false);
    }
    tokens.resize(n);
    return tokens;
}

SentimentResult SentimentAnalyzer::analyzeSingle(const std::string& text) {
    SentimentResult res = {0.0f, 0.0f, "Neutral"};
    if (!initialized || !model) return res;

    // 1. Create Local Context (Fixes Memory Issue)
    auto cparams = llama_context_default_params();
    cparams.n_ctx = 512; // Small context is enough for one headline
    
    struct llama_context* ctx = llama_init_from_model(model, cparams);
    if (!ctx) {
        std::cerr << "Error: Failed to create local llama context." << std::endl;
        return res;
    }

    const llama_vocab* vocab = llama_model_get_vocab(model);

    // 2. Improved Prompt for Phi-3 (Instruct Format)
    std::string prompt = "<|user|>\nClassify the sentiment of this headline as Positive, Negative, or Neutral.\nHeadline: " + text + "\nAnswer with one word only.\n<|end|>\n<|assistant|>\n";
    
    std::vector<llama_token> tokens = tokenize(vocab, prompt);
    llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
    
    if (llama_decode(ctx, batch) != 0) {
        std::cerr << "llama_decode failed" << std::endl;
        llama_free(ctx);
        return res;
    }

    // 3. Get Logits & Calculate Confidence
    auto* logits = llama_get_logits_ith(ctx, batch.n_tokens - 1);
    int n_vocab = llama_vocab_n_tokens(vocab);

    // Find top K candidates to calculate softmax for confidence
    // We only care about the best token, but need sum(exp) for probability
    int bestTokenId = 0;
    float maxLogit = -1e9;
    
    // Naive Softmax implementation over all vocab (slow? no, 32k is fast on CPU)
    float sumExp = 0.0f;
    std::vector<float> exps(n_vocab);
    
    // Find max for numerical stability
    for (int i = 0; i < n_vocab; ++i) {
        if (logits[i] > maxLogit) {
            maxLogit = logits[i];
            bestTokenId = i;
        }
    }
    
    // Calculate Softmax Denominator
    for (int i = 0; i < n_vocab; ++i) {
        exps[i] = std::exp(logits[i] - maxLogit); // Subtract max for stability
        sumExp += exps[i];
    }
    
    // Probability of the best token
    float confidence = (exps[bestTokenId] / sumExp) * 100.0f;
    res.confidence = confidence;

    // 4. Decode Token
    char buf[256];
    int n = llama_token_to_piece(vocab, bestTokenId, buf, sizeof(buf), 0, true);
    if (n < 0) n = 0; 
    std::string result(buf, n);
    
    // 5. Parse Result
    std::string clean = result;
    if (clean.length() > 0) clean.erase(0, clean.find_first_not_of(" "));
    std::transform(clean.begin(), clean.end(), clean.begin(), ::tolower);

    // Debug output if neutral (comment out in prod)
    // std::cout << " [Debug Raw: '" << result << "'] ";

    if (clean.find("pos") != std::string::npos) { // Matches "positive", "pos", "Positive"
        res.score = 1.0f;
        res.label = "Positive";
    } else if (clean.find("neg") != std::string::npos) { // Matches "negative", "neg"
        res.score = -1.0f;
        res.label = "Negative";
    } else {
        res.label = "Neutral (" + clean + ")";
        res.score = 0.0f;
    }

    // Cleanup
    llama_free(ctx);
    return res;
}

float SentimentAnalyzer::analyze(const std::vector<std::string>& texts) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!initialized) return 0.0f;

    if (texts.empty()) return 0.0f;

    float totalScore = 0.0f;
    for (const auto& text : texts) {
        SentimentResult res = analyzeSingle(text);
        totalScore += res.score;
    }

    return totalScore / texts.size();
}
