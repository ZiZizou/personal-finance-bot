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
#include <sstream>
#include <regex>

using json = nlohmann::json;

#ifdef ENABLE_LLAMA
#include <llama.h>

// Log callback function
static void llama_log_callback(ggml_log_level level, const char * text, void * user_data) {
    (void)level;
    bool* verbose_ptr = static_cast<bool*>(user_data);
    if (verbose_ptr && *verbose_ptr) {
        fputs(text, stderr);
    }
}

// Helper: Add token to batch
static void llama_batch_add(struct llama_batch & batch, llama_token id, llama_pos pos, std::vector<llama_seq_id> seq_ids, bool logits) {
    batch.token   [batch.n_tokens] = id;
    batch.pos     [batch.n_tokens] = pos;
    batch.n_seq_id[batch.n_tokens] = seq_ids.size();
    for (size_t i = 0; i < seq_ids.size(); ++i) {
        batch.seq_id[batch.n_tokens][i] = seq_ids[i];
    }
    batch.logits  [batch.n_tokens] = logits;
    batch.n_tokens++;
}

// Helper: Generate text from model
static std::string llama_generate(llama_model* model, llama_context* ctx,
                                   const std::string& prompt, int max_tokens = 32) {
    const llama_vocab* vocab = llama_model_get_vocab(model);

    // Tokenize prompt
    std::vector<llama_token> tokens(prompt.size() + 16);
    int n_tokens = llama_tokenize(vocab, prompt.c_str(), prompt.size(),
                                   tokens.data(), tokens.size(), true, false);
    if (n_tokens < 0) {
        return "";
    }
    tokens.resize(n_tokens);

    // Clear KV cache (remove all sequences, all positions)
    llama_memory_t memory = llama_get_memory(ctx);
    llama_memory_seq_rm(memory, -1, 0, -1);

    // Create batch
    llama_batch batch = llama_batch_init(512, 0, 1);

    // Add tokens to batch
    for (int i = 0; i < n_tokens; i++) {
        llama_batch_add(batch, tokens[i], i, {0}, false);
    }
    batch.logits[batch.n_tokens - 1] = true;  // Get logits for last token

    // Decode prompt
    if (llama_decode(ctx, batch) != 0) {
        llama_batch_free(batch);
        return "";
    }

    // Generate tokens
    std::string result;
    llama_token new_token_id;

    for (int i = 0; i < max_tokens; i++) {
        // Sample next token
        float* logits = llama_get_logits_ith(ctx, batch.n_tokens - 1);
        int n_vocab = llama_vocab_n_tokens(vocab);

        // Simple greedy sampling
        new_token_id = 0;
        float max_logit = logits[0];
        for (int v = 1; v < n_vocab; v++) {
            if (logits[v] > max_logit) {
                max_logit = logits[v];
                new_token_id = v;
            }
        }

        // Check for EOS
        if (llama_vocab_is_eog(vocab, new_token_id)) {
            break;
        }

        // Convert token to text
        char buf[256];
        int len = llama_token_to_piece(vocab, new_token_id, buf, sizeof(buf), 0, false);
        if (len > 0) {
            result.append(buf, len);
        }

        // Stop at newline (end of answer)
        if (result.find('\n') != std::string::npos) {
            break;
        }

        // Prepare next batch
        // llama_batch_clear is not a standard function, we reset n_tokens
        batch.n_tokens = 0; 
        
        llama_batch_add(batch, new_token_id, n_tokens + i, {0}, true);

        if (llama_decode(ctx, batch) != 0) {
            break;
        }
    }

    llama_batch_free(batch);
    return result;
}

// Parse sentiment from model output
static SentimentResult parse_llama_sentiment(const std::string& output) {
    SentimentResult res = {0.0f, 50.0f, "Neutral"};

    std::string lower = output;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Check for sentiment keywords in response
    if (lower.find("positive") != std::string::npos ||
        lower.find("bullish") != std::string::npos) {
        res.score = 1.0f;
        res.label = "Positive";
        res.confidence = 75.0f;
    } else if (lower.find("negative") != std::string::npos ||
               lower.find("bearish") != std::string::npos) {
        res.score = -1.0f;
        res.label = "Negative";
        res.confidence = 75.0f;
    } else if (lower.find("neutral") != std::string::npos) {
        res.score = 0.0f;
        res.label = "Neutral";
        res.confidence = 70.0f;
    }

    return res;
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
    if (ctx) llama_free(ctx);
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
        std::cerr << "Warning: Failed to load sentiment model from '" << modelPath << "'. Using keyword fallback." << std::endl;
        initialized = true;
        llamaReady = false;
        return true; // Still return true - we can use keyword fallback
    }

    // Create context for inference
    auto cparams = llama_context_default_params();
    cparams.n_ctx = 512;      // Context size (enough for short headlines)
    cparams.n_batch = 512;
    cparams.n_threads = 4;
    cparams.n_threads_batch = 4;

    // llama_new_context_with_model is deprecated, use llama_init_from_model
    ctx = llama_init_from_model(model, cparams);
    
    if (!ctx) {
        std::cerr << "Warning: Failed to create llama context. Using keyword fallback." << std::endl;
        llama_model_free(model);
        model = nullptr;
        initialized = true;
        llamaReady = false;
        return true;
    }

    llamaReady = true;
    Logger::getInstance().log("Local sentiment model loaded successfully: " + modelPath);
#else
    if (verbose) std::cout << "[INFO] Llama disabled. Using keyword fallback." << std::endl;
#endif

    initialized = true;
    return true;
}

SentimentResult SentimentAnalyzer::analyzeSingle(const std::string& text) {
    SentimentResult res = {0.0f, 0.0f, "Neutral"};

#ifdef ENABLE_LLAMA
    // 1. Try Local LLM for sentiment analysis
    {
        std::lock_guard<std::mutex> lock(mutex); // Protect shared llama_context
        if (llamaReady && model && ctx) {
            // Construct a simple sentiment classification prompt
            std::string prompt =
                "Classify the financial sentiment of this headline as exactly one word: positive, negative, or neutral.\n\n"
                "Headline: \"" + text + "\"\n\n"
                "Sentiment:";

            std::string output = llama_generate(model, ctx, prompt, 16);

            if (!output.empty()) {
                SentimentResult llamaResult = parse_llama_sentiment(output);
                if (llamaResult.confidence > 50.0f) {
                    return llamaResult;
                }
            }
            // If llama didn't give clear result, fall through to keyword
        }
    }
#endif

    // 2. Fallback Keyword Logic
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

    for (const auto& t : texts) {
        SentimentResult r = analyzeSingle(t);
        totalScore += r.score;
        processed++;
    }

    return (processed > 0) ? (totalScore / processed) : 0.0f;
}