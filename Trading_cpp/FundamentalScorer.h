#pragma once
#include "MarketData.h"
#include <string>
#include <map>

// Sector averages for benchmarking (P/E ratios and growth rates)
struct SectorBenchmarks {
    double avg_pe;
    double avg_forward_pe;
    double avg_peg;
    double avg_growth;

    static SectorBenchmarks getForSector(const std::string& sector) {
        static std::map<std::string, SectorBenchmarks> benchmarks = {
            {"Technology", {28.0, 22.0, 1.8, 0.15}},
            {"Communication Services", {20.0, 16.0, 1.5, 0.12}},
            {"Consumer Cyclical", {25.0, 20.0, 1.6, 0.10}},
            {"Consumer Defensive", {20.0, 17.0, 2.0, 0.06}},
            {"Healthcare", {22.0, 18.0, 1.7, 0.10}},
            {"Financial Services", {15.0, 12.0, 1.2, 0.08}},
            {"Industrials", {20.0, 16.0, 1.5, 0.10}},
            {"Energy", {12.0, 10.0, 1.0, 0.05}},
            {"Utilities", {18.0, 15.0, 2.5, 0.04}},
            {"Real Estate", {35.0, 30.0, 3.0, 0.05}},
            {"Basic Materials", {14.0, 11.0, 1.3, 0.07}}
        };

        auto it = benchmarks.find(sector);
        if (it != benchmarks.end()) {
            return it->second;
        }
        // Default fallback
        return {20.0, 16.0, 1.8, 0.08};
    }
};

class FundamentalScorer {
public:
    struct ScoreResult {
        double total_score;        // -1.0 to 1.0
        double valuation_score;   // PE, PEG, P/B, P/S
        double growth_score;      // earnings/revenue growth
        double health_score;      // margins, debt, cash flow
        double sentiment_score;   // analyst rating, short interest
        double dividend_score;    // yield, coverage

        std::string toString() const {
            return "Total: " + std::to_string(total_score) +
                   " | Valuation: " + std::to_string(valuation_score) +
                   " | Growth: " + std::to_string(growth_score) +
                   " | Health: " + std::to_string(health_score) +
                   " | Sentiment: " + std::to_string(sentiment_score) +
                   " | Dividend: " + std::to_string(dividend_score);
        }
    };

    // Weight configuration
    struct Weights {
        double valuation = 0.30;   // 30%
        double growth = 0.25;      // 25%
        double health = 0.20;      // 20%
        double sentiment = 0.15;   // 15%
        double dividend = 0.10;    // 10%
    };

    FundamentalScorer() = default;
    explicit FundamentalScorer(const Weights& w) : weights_(w) {}

    void setWeights(const Weights& w) { weights_ = w; }

    ScoreResult computeScore(const Fundamentals& f) {
        ScoreResult result;
        result.total_score = 0.0;
        result.valuation_score = 0.0;
        result.growth_score = 0.0;
        result.health_score = 0.0;
        result.sentiment_score = 0.0;
        result.dividend_score = 0.0;

        if (!f.valid) {
            return result;
        }

        // Get sector benchmarks
        SectorBenchmarks sector = SectorBenchmarks::getForSector(f.sector);

        // --- 1. Valuation Score (30%) ---
        result.valuation_score = computeValuationScore(f, sector);

        // --- 2. Growth Score (25%) ---
        result.growth_score = computeGrowthScore(f, sector);

        // --- 3. Health Score (20%) ---
        result.health_score = computeHealthScore(f);

        // --- 4. Sentiment Score (15%) ---
        result.sentiment_score = computeSentimentScore(f);

        // --- 5. Dividend Score (10%) ---
        result.dividend_score = computeDividendScore(f);

        // Combine weighted scores
        result.total_score =
            result.valuation_score * weights_.valuation +
            result.growth_score * weights_.growth +
            result.health_score * weights_.health +
            result.sentiment_score * weights_.sentiment +
            result.dividend_score * weights_.dividend;

        // Clamp to [-1, 1]
        result.total_score = std::max(-1.0, std::min(1.0, result.total_score));

        return result;
    }

private:
    Weights weights_;

    double computeValuationScore(const Fundamentals& f, const SectorBenchmarks& sector) {
        double score = 0.0;

        // PEG Ratio (most important)
        if (f.peg_ratio > 0) {
            if (f.peg_ratio < 1.0) score += 0.5;      // Strong value
            else if (f.peg_ratio < 1.5) score += 0.25; // Reasonable
            else if (f.peg_ratio > 2.5) score -= 0.4;  // Expensive
            else if (f.peg_ratio > 2.0) score -= 0.2;
        }

        // P/E vs Sector
        if (f.pe_ratio > 0 && sector.avg_pe > 0) {
            double peDiff = (sector.avg_pe - f.pe_ratio) / sector.avg_pe;
            if (peDiff > 0.3) score += 0.3;      // Significantly cheaper than sector
            else if (peDiff > 0.15) score += 0.15;
            else if (peDiff < -0.3) score -= 0.3; // Significantly more expensive
        }

        // Forward P/E (cheaper = better, especially if lower than trailing)
        if (f.forward_pe > 0) {
            if (f.forward_pe < f.pe_ratio && f.pe_ratio > 0) {
                score += 0.15; // Expected earnings growth priced in
            }
            if (f.forward_pe < sector.avg_forward_pe) {
                score += 0.1;
            }
        }

        // Price to Book (value check)
        if (f.price_to_book > 0) {
            if (f.price_to_book < 1.0) score += 0.2;   // Potentially undervalued
            else if (f.price_to_book < 2.0) score += 0.1;
            else if (f.price_to_book > 5.0) score -= 0.2;
        }

        // Price to Sales
        if (f.price_to_sales > 0) {
            if (f.price_to_sales < 1.5) score += 0.1;
            else if (f.price_to_sales > 4.0) score -= 0.1;
        }

        return std::max(-0.5, std::min(0.5, score));
    }

    double computeGrowthScore(const Fundamentals& f, const SectorBenchmarks& sector) {
        double score = 0.0;

        // Earnings Growth
        if (f.earnings_growth > 0) {
            if (f.earnings_growth > 0.25) score += 0.4;     // >25% growth
            else if (f.earnings_growth > 0.15) score += 0.25; // >15%
            else if (f.earnings_growth > sector.avg_growth) score += 0.15;
        } else if (f.earnings_growth < -0.1) {
            score -= 0.3; // Declining earnings
        }

        // Revenue Growth
        if (f.revenue_growth > 0) {
            if (f.revenue_growth > 0.20) score += 0.3;     // >20%
            else if (f.revenue_growth > 0.10) score += 0.2;
            else if (f.revenue_growth > 0.05) score += 0.1;
        } else if (f.revenue_growth < -0.05) {
            score -= 0.2; // Declining revenue
        }

        // Positive EPS is important
        if (f.eps > 0) {
            score += 0.1;
        } else {
            score -= 0.3; // Negative earnings
        }

        // Compare to sector growth expectations
        if (f.earnings_growth > sector.avg_growth) {
            score += 0.1;
        }

        return std::max(-0.5, std::min(0.5, score));
    }

    double computeHealthScore(const Fundamentals& f) {
        double score = 0.0;

        // Debt to Equity
        if (f.debt_to_equity > 0) {
            if (f.debt_to_equity < 0.5) score += 0.25;  // Very healthy
            else if (f.debt_to_equity < 1.0) score += 0.15;
            else if (f.debt_to_equity > 2.0) score -= 0.25; // High leverage
        }

        // Current Ratio
        if (f.current_ratio > 0) {
            if (f.current_ratio > 2.0) score += 0.2;
            else if (f.current_ratio > 1.5) score += 0.15;
            else if (f.current_ratio < 1.0) score -= 0.2;
        }

        // Quick Ratio
        if (f.quick_ratio > 0) {
            if (f.quick_ratio > 1.0) score += 0.1;
        }

        // Free Cash Flow
        if (f.free_cashflow > 0) {
            score += 0.2;
        } else if (f.free_cashflow < 0) {
            // Only penalize significantly negative FCF
            if (f.total_cash > 0 && std::abs(f.free_cashflow) < f.total_cash * 0.5) {
                score -= 0.1; // Moderate negative FCF but still has cash
            } else {
                score -= 0.25;
            }
        }

        // Profit Margin
        if (f.profit_margin > 0.15) score += 0.15; // Strong
        else if (f.profit_margin > 0.05) score += 0.1;
        else if (f.profit_margin < 0) score -= 0.2;

        // Operating Margin
        if (f.operating_margin > 0.20) score += 0.1;
        else if (f.operating_margin > 0.10) score += 0.05;
        else if (f.operating_margin < 0) score -= 0.1;

        return std::max(-0.5, std::min(0.5, score));
    }

    double computeSentimentScore(const Fundamentals& f) {
        double score = 0.0;

        // Analyst Rating (1=strong buy, 5=strong sell)
        if (f.analyst_rating > 0) {
            if (f.analyst_rating <= 1.5) score += 0.3;  // Strong buy
            else if (f.analyst_rating <= 2.0) score += 0.2; // Buy
            else if (f.analyst_rating <= 2.5) score += 0.1; // Hold
            else if (f.analyst_rating >= 4.0) score -= 0.3; // Sell/Strong sell
            else if (f.analyst_rating >= 3.5) score -= 0.15;
        }

        // Short Interest (lower = better)
        if (f.short_percent_float > 0) {
            if (f.short_percent_float < 0.05) score += 0.15; // Low short interest
            else if (f.short_percent_float < 0.10) score += 0.1;
            else if (f.short_percent_float > 0.20) score -= 0.25; // High short interest
            else if (f.short_percent_float > 0.15) score -= 0.15;
        }

        // Institutional Ownership (higher = usually better for stability)
        if (f.institutional_ownership_pct > 0) {
            if (f.institutional_ownership_pct > 0.70) score += 0.15;
            else if (f.institutional_ownership_pct > 0.50) score += 0.1;
            else if (f.institutional_ownership_pct < 0.20) score -= 0.1;
        }

        // Beta (lower is less volatile)
        if (f.beta > 0) {
            if (f.beta < 0.8) score += 0.1;
            else if (f.beta > 1.5) score -= 0.1;
        }

        return std::max(-0.5, std::min(0.5, score));
    }

    double computeDividendScore(const Fundamentals& f) {
        double score = 0.0;

        // Dividend Yield (sweet spot 2-5%)
        if (f.dividend_yield > 0) {
            if (f.dividend_yield >= 0.02 && f.dividend_yield <= 0.05) {
                score += 0.35; // Sweet spot
            } else if (f.dividend_yield > 0.05 && f.dividend_yield < 0.08) {
                score += 0.2; // High yield but possibly risky
            } else if (f.dividend_yield > 0.08) {
                score += 0.1; // Very high yield, might be trap
            } else if (f.dividend_yield > 0.01) {
                score += 0.15; // Modest yield
            }
        }

        // Payout Ratio (lower is more sustainable)
        if (f.payout_ratio > 0) {
            if (f.payout_ratio < 0.30) score += 0.2;  // Very safe
            else if (f.payout_ratio < 0.50) score += 0.15; // Safe
            else if (f.payout_ratio < 0.70) score += 0.05; // Moderate
            else if (f.payout_ratio > 0.90) score -= 0.2; // Risky
        }

        // Dividend Rate (some is better than none)
        if (f.dividend_rate > 0) {
            score += 0.1;
        }

        return std::max(-0.5, std::min(0.5, score));
    }
};
