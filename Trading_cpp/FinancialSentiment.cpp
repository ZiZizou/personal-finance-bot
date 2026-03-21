#include "FinancialSentiment.h"
#include "Logger.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>

FinancialSentimentAnalyzer::FinancialSentimentAnalyzer() {
    // Initialize financial-specific positive words with weights
    // Higher weight = more strongly positive
    positiveWords_ = {
        // Strong positive (weight: 0.8-1.0)
        {"surge", 0.9}, {"soars", 0.9}, {"soaring", 0.9}, {"skyrockets", 1.0}, {"rocket", 0.9},
        {"record high", 0.9}, {"all-time high", 0.9}, {" ATH ", 0.8}, {"bullish", 0.8},
        {"outperform", 0.8}, {"upgrade", 0.7}, {"upgraded", 0.7}, {"raise", 0.7}, {"raised", 0.7},
        {"beat", 0.8}, {"beats", 0.8}, {"exceed", 0.8}, {"exceeds", 0.8}, {"exceeded", 0.8},
        {"profit", 0.7}, {"profitable", 0.8}, {"profitable", 0.8}, {"growth", 0.7}, {"grows", 0.7},
        {"breakout", 0.8}, {"breakout", 0.8}, {"rally", 0.8}, {"rallies", 0.8}, {"rallying", 0.8},
        {"boom", 0.9}, {"booming", 0.9}, {"surges", 0.9}, {"jumps", 0.8}, {"jumped", 0.8},
        {"gain", 0.7}, {"gains", 0.7}, {"gaining", 0.7}, {"positive", 0.6}, {"strength", 0.7},
        {"strong", 0.7}, {"stronger", 0.8}, {"higher", 0.6}, {"highs", 0.7}, {"buy", 0.7},
        {"undervalued", 0.8}, {"attractive", 0.7}, {"opportunity", 0.6}, {"momentum", 0.7},
        {"dividend", 0.6}, {"dividends", 0.6}, {"yield", 0.5}, {"buyback", 0.8}, {"buybacks", 0.8},
        {"guidance", 0.6}, {"raises guidance", 0.9}, {"beats estimates", 0.9}, {"revenue growth", 0.8},
        {"earnings beat", 0.9}, {"positive outlook", 0.8}, {"robust", 0.8}, {"solid", 0.7},
        {"boom", 0.9}, {"explodes", 0.9}, {"explosive", 0.9}, {"、双", 0.8}, // Chinese bullish

        // Moderately positive (weight: 0.5-0.7)
        {"increase", 0.6}, {"increased", 0.6}, {"increasing", 0.6}, {"improve", 0.6}, {"improved", 0.6},
        {"improvement", 0.6}, {"optimistic", 0.7}, {"optimism", 0.6}, {"expand", 0.6}, {"expands", 0.6},
        {"launch", 0.6}, {"launches", 0.6}, {"launched", 0.6}, {"partnership", 0.6}, {"partnerships", 0.6},
        {"innovation", 0.6}, {"innovative", 0.7}, {"disruption", 0.6}, {"disruptive", 0.7},
        {"market share", 0.6}, {"tailwinds", 0.7}, {"catalyst", 0.6}, {"upgrade", 0.7},
        {"analyst", 0.5}, {"estimates", 0.5}, {"target", 0.5}, {"price target", 0.5},
        {"coverage", 0.4}, {"initiate", 0.6}, {"initiates", 0.6}, {"coverage", 0.5},

        // Mildly positive (weight: 0.3-0.5)
        {"stable", 0.4}, {"stable", 0.4}, {"steady", 0.4}, {"hold", 0.3}, {"maintain", 0.4},
        {"meet", 0.4}, {"meets", 0.4}, {"expectations", 0.4}, {"in-line", 0.4},
        {"reiterate", 0.5}, {"reaffirm", 0.5}, {"confident", 0.5}, {"approve", 0.6}, {"approved", 0.6}
    };

    // Initialize negative words with weights
    negativeWords_ = {
        // Strong negative (weight: -0.8 to -1.0)
        {"crash", -1.0}, {"crashes", -1.0}, {"crashing", -1.0}, {"plunge", -0.9}, {"plunges", -0.9},
        {"plunging", -0.9}, {"collapse", -1.0}, {"collapses", -1.0}, {"collapsing", -1.0},
        {"bomb", -0.9}, {"explodes", -0.9}, {"explosion", -0.9}, {"shock", -0.8}, {"shocks", -0.8},
        {"bearish", -0.8}, {"downgrade", -0.8}, {"downgraded", -0.8}, {"cut", -0.7}, {"cuts", -0.7},
        {"cutting", -0.7}, {"layoff", -0.9}, {"layoffs", -0.9}, {"job cuts", -0.9},
        {"miss", -0.8}, {"misses", -0.8}, {"missed", -0.8}, {"misses estimates", -0.9},
        {"loss", -0.8}, {"losses", -0.8}, {"losing", -0.8}, {"loses", -0.8}, {"loss", -0.8},
        {"warning", -0.7}, {"warns", -0.7}, {"warned", -0.7}, {"warning", -0.7},
        {"profit warning", -0.9}, {"guidance cut", -0.9}, {"lowers guidance", -0.9},
        {"lawsuit", -0.8}, {"lawsuits", -0.8}, {"sued", -0.8}, {"sue", -0.8},
        {"investigation", -0.8}, {"investigate", -0.7}, {"probe", -0.8}, {"probes", -0.8},
        {"scandal", -0.9}, {"fraud", -1.0}, {"fraudulent", -1.0}, {"embezzlement", -1.0},
        {"bankruptcy", -1.0}, {"bankrupt", -1.0}, {"insolvent", -0.9}, {"default", -0.9},
        {"defaulted", -0.9}, {"defaulting", -0.9}, {"bailout", -0.9},
        {"selloff", -0.9}, {"sell-off", -0.9}, {"selling off", -0.9}, {"dump", -0.8}, {"dumps", -0.8},
        {"crisis", -0.9}, {"recession", -0.9}, {"recessionary", -0.9}, {"depression", -1.0},
        {"panic", -0.9}, {"panic selling", -0.9}, {"flight", -0.8}, {"risk off", -0.8},
        {"blow up", -0.9}, {"blows up", -0.9}, {"blowing up", -0.9}, {"fires", -0.8}, {"fired", -0.8},
        {" shuts ", -0.8}, {"shutdown", -0.8}, {"shuts down", -0.9}, {"halt", -0.8}, {"halts", -0.8},
        {"delist", -0.9}, {"delisting", -0.9}, {"suspended", -0.8}, {"suspend", -0.8},
        {"fraud", -1.0}, {"scam", -0.9}, {"Ponzi", -1.0}, {"manipulation", -0.9},
        {"violation", -0.8}, {"violations", -0.8}, {"violated", -0.8}, {"breach", -0.8},
        {"双", -0.8}, // Chinese bearish

        // Moderately negative (weight: -0.5 to -0.7)
        {"drop", -0.6}, {"drops", -0.6}, {"dropping", -0.6}, {"fall", -0.6}, {"falls", -0.6},
        {"falling", -0.6}, {"decline", -0.6}, {"declines", -0.6}, {"declining", -0.6},
        {"down", -0.5}, {"down", -0.5}, {"lower", -0.5}, {"lowers", -0.6}, {"lowered", -0.6},
        {"weak", -0.6}, {"weaker", -0.7}, {"weakness", -0.6}, {"underperform", -0.7},
        {"underperforming", -0.7}, {"underperforms", -0.7}, {"negative", -0.6}, {"negatives", -0.6},
        {"concern", -0.6}, {"concerns", -0.6}, {"concerning", -0.6}, {"worry", -0.6}, {"worries", -0.6},
        {"risk", -0.5}, {"risky", -0.6}, {"risks", -0.5}, {"threat", -0.7}, {"threats", -0.6},
        {"challenge", -0.5}, {"challenges", -0.5}, {"challenging", -0.5},
        {"headwind", -0.6}, {"headwinds", -0.6}, {"tailwind", 0.6}, {"tailwinds", 0.6},
        {"slowdown", -0.7}, {"slows", -0.6}, {"slowing", -0.6}, {"slow", -0.5},
        {"pressure", -0.5}, {"pressures", -0.5}, {"under pressure", -0.6},
        {"volatile", -0.5}, {"volatility", -0.5}, {"uncertain", -0.5}, {"uncertainty", -0.5},
        {"volatile", -0.5}, {"uncertainty", -0.6}, {"turbulence", -0.7}, {"turbulent", -0.7},
        {"inflation", -0.5}, {"inflationary", -0.5}, {"disinflation", 0.3}, {"deflation", -0.6},
        {"rate hike", -0.5}, {"rate hikes", -0.5}, {"hike rates", -0.5}, {"tightening", -0.5},
        {"bond yields", -0.4}, {"yield curve", -0.5}, {"invert", -0.6}, {"inverted", -0.6},

        // Mildly negative (weight: -0.3 to -0.5)
        {"cautious", -0.4}, {"cautiously", -0.4}, {"caution", -0.4}, {"cautionary", -0.4},
        {"mixed", -0.3}, {"uncertain", -0.4}, {"unclear", -0.4}, {"unknown", -0.3},
        {"delay", -0.4}, {"delays", -0.4}, {"delayed", -0.4}, {"postpone", -0.4}, {"postponed", -0.4},
        {"reconsider", -0.4}, {"review", -0.3}, {"reviews", -0.3}, {"under review", -0.4},
        {"reduce", -0.4}, {"reduces", -0.4}, {"reducing", -0.4}, {"reduction", -0.4},
        {"restructure", -0.5}, {"restructuring", -0.5}, {"restructures", -0.5},
        {"downsize", -0.5}, {"downsizing", -0.5}, {"downsize", -0.5},
        {"soft", -0.4}, {"softer", -0.4}, {"softness", -0.4}, {"tough", -0.4},
        {"struggle", -0.5}, {"struggles", -0.5}, {"struggling", -0.5}, {"hard", -0.3},
        {"difficult", -0.4}, {"difficulties", -0.4}, {"difficulty", -0.4},
        {"complicate", -0.4}, {"complicates", -0.4}, {"complicated", -0.4},
        {"disappoint", -0.5}, {"disappoints", -0.5}, {"disappointing", -0.5}, {"disappointed", -0.5}
    };

    // Modifiers that amplify or diminish sentiment
    modifierWords_ = {
        {"very", 1.5}, {"extremely", 1.5}, {"highly", 1.5}, {"sharply", 1.4}, {"significantly", 1.4},
        {"substantially", 1.4}, {"dramatically", 1.5}, {"strongly", 1.4}, {"massive", 1.5},
        {"huge", 1.4}, {"big", 1.2}, {"major", 1.3}, {"slightly", 0.5}, {"somewhat", 0.6},
        {"moderately", 0.6}, {"marginally", 0.4}, {"slightly", 0.5}, {"barely", 0.3},
        {"slightly", 0.5}, {"minor", 0.4}, {"slightly", 0.5}
    };
}

FinancialSentimentAnalyzer& FinancialSentimentAnalyzer::getInstance() {
    static FinancialSentimentAnalyzer instance;
    return instance;
}

bool FinancialSentimentAnalyzer::init() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return true;

    initialized_ = true;
    Logger::getInstance().log("Financial Sentiment Analyzer initialized");
    return true;
}

void FinancialSentimentAnalyzer::setVerbose(bool v) {
    std::lock_guard<std::mutex> lock(mutex_);
    verbose_ = v;
}

bool FinancialSentimentAnalyzer::isEarningsRelated(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    const char* earningsWords[] = {
        "earnings", "revenue", "eps", "q1", "q2", "q3", "q4",
        "quarterly", "fiscal", "guidance", "outlook", "forecast",
        "estimates", "analyst", "beat", "miss", "report", "results"
    };

    for (const auto& word : earningsWords) {
        if (lower.find(word) != std::string::npos) {
            return true;
        }
    }
    return false;
}

double FinancialSentimentAnalyzer::getEarningsSentiment(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Strong positive earnings
    if (lower.find("beat") != std::string::npos &&
        (lower.find("estimates") != std::string::npos || lower.find("expectations") != std::string::npos)) {
        return 0.8;
    }

    // Strong negative earnings
    if (lower.find("miss") != std::string::npos &&
        (lower.find("estimates") != std::string::npos || lower.find("expectations") != std::string::npos)) {
        return -0.8;
    }

    // Guidance raises
    if (lower.find("raises") != std::string::npos && lower.find("guidance") != std::string::npos) {
        return 0.7;
    }

    // Guidance cuts
    if (lower.find("cuts") != std::string::npos && lower.find("guidance") != std::string::npos) {
        return -0.7;
    }

    // Revenue growth positive
    if (lower.find("revenue") != std::string::npos) {
        if (lower.find("growth") != std::string::npos || lower.find("increases") != std::string::npos) {
            return 0.6;
        }
        if (lower.find("declines") != std::string::npos || lower.find("falls") != std::string::npos) {
            return -0.6;
        }
    }

    return 0.0;
}

bool FinancialSentimentAnalyzer::isMergerRelated(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    const char* mawords[] = {
        "merge", "acquisition", "acquire", "acquires", "acquired", "takeover",
        "tender offer", "merger", "deal", "acquisition", "m&a", "ma "
    };

    for (const auto& word : mawords) {
        if (lower.find(word) != std::string::npos) {
            return true;
        }
    }
    return false;
}

double FinancialSentimentAnalyzer::getMergerSentiment(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Positive merger language
    if (lower.find("agree") != std::string::npos || lower.find("agreed") != std::string::npos) {
        if (lower.find("buy") != std::string::npos || lower.find("acquire") != std::string::npos) {
            return 0.6;
        }
    }

    if (lower.find("merger") != std::string::npos) {
        if (lower.find("approve") != std::string::npos || lower.find("approved") != std::string::npos) {
            return 0.7;
        }
    }

    // Negative merger language
    if (lower.find("reject") != std::string::npos || lower.find("rejected") != std::string::npos) {
        return -0.7;
    }

    if (lower.find("terminate") != std::string::npos || lower.find("termination") != std::string::npos) {
        return -0.8;
    }

    if (lower.find("antitrust") != std::string::npos || lower.find("regulatory") != std::string::npos) {
        return -0.5;
    }

    return 0.0;
}

double FinancialSentimentAnalyzer::scoreKeywords(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    double score = 0.0;
    double modifier = 1.0;

    // Check for modifiers first
    for (const auto& [word, mult] : modifierWords_) {
        if (lower.find(word) != std::string::npos) {
            modifier = mult;
            break;
        }
    }

    // Score positive words
    for (const auto& [word, weight] : positiveWords_) {
        if (lower.find(word) != std::string::npos) {
            score += weight * modifier;
        }
    }

    // Score negative words
    for (const auto& [word, weight] : negativeWords_) {
        if (lower.find(word) != std::string::npos) {
            score += weight * modifier; // weight is already negative
        }
    }

    // Apply domain-specific scoring
    if (isEarningsRelated(text)) {
        double earningsScore = getEarningsSentiment(text);
        if (earningsScore != 0.0) {
            score += earningsScore * 1.2; // Weight earnings more heavily
        }
    }

    if (isMergerRelated(text)) {
        double mergerScore = getMergerSentiment(text);
        if (mergerScore != 0.0) {
            score += mergerScore * 1.1;
        }
    }

    // Clamp score to [-1, 1]
    return std::clamp(score, -1.0, 1.0);
}

SentimentResult FinancialSentimentAnalyzer::analyzeHeadline(const std::string& headline) {
    SentimentResult result = {0.0, 50.0, "Neutral"};

    if (headline.empty()) {
        return result;
    }

    double rawScore = scoreKeywords(headline);

    // Convert raw score to final score and confidence
    if (rawScore > 0.3) {
        result.score = std::min(rawScore, 1.0);
        result.label = rawScore > 0.7 ? "Strong Positive" : "Positive";
        result.confidence = 50.0 + (rawScore * 45.0);
    } else if (rawScore < -0.3) {
        result.score = std::max(rawScore, -1.0);
        result.label = rawScore < -0.7 ? "Strong Negative" : "Negative";
        result.confidence = 50.0 + (std::abs(rawScore) * 45.0);
    } else {
        result.score = rawScore;
        result.label = "Neutral";
        result.confidence = 60.0 - (std::abs(rawScore) * 20.0);
    }

    // Ensure confidence is in valid range
    result.confidence = std::clamp(result.confidence, 30.0, 95.0);

    return result;
}

double FinancialSentimentAnalyzer::analyze(const std::vector<std::string>& headlines) {
    if (headlines.empty()) return 0.0;

    double totalScore = 0.0;
    int count = 0;

    for (const auto& headline : headlines) {
        SentimentResult r = analyzeHeadline(headline);
        totalScore += r.score;
        count++;
    }

    return count > 0 ? totalScore / count : 0.0;
}
