#include "BlackScholes.h"
#include <cmath>
#include <algorithm>

const float PI = 3.14159265358979323846f;

// Standard Normal CDF
float norm_cdf(float x) {
    return 0.5f * std::erfc(-x * 0.70710678118654752440f);
}

// Standard Normal PDF
float norm_pdf(float x) {
    return (1.0f / std::sqrt(2.0f * PI)) * std::exp(-0.5f * x * x);
}

float BlackScholes::callPrice(float S, float K, float T, float r, float sigma) {
    float d1 = (std::log(S / K) + (r + 0.5f * sigma * sigma) * T) / (sigma * std::sqrt(T));
    float d2 = d1 - sigma * std::sqrt(T);
    return S * norm_cdf(d1) - K * std::exp(-r * T) * norm_cdf(d2);
}

float BlackScholes::putPrice(float S, float K, float T, float r, float sigma) {
    float d1 = (std::log(S / K) + (r + 0.5f * sigma * sigma) * T) / (sigma * std::sqrt(T));
    float d2 = d1 - sigma * std::sqrt(T);
    return K * std::exp(-r * T) * norm_cdf(-d2) - S * norm_cdf(-d1);
}

// Simple Newton-Raphson for IV
float BlackScholes::impliedVolatility(float marketPrice, float S, float K, float T, float r, bool isCall) {
    float sigma = 0.5f; // Initial guess
    for (int i = 0; i < 100; ++i) {
        float price = isCall ? callPrice(S, K, T, r, sigma) : putPrice(S, K, T, r, sigma);
        float diff = marketPrice - price;
        if (std::abs(diff) < 0.001f) return sigma;
        
        float d1 = (std::log(S / K) + (r + 0.5f * sigma * sigma) * T) / (sigma * std::sqrt(T));
        float vega = S * std::sqrt(T) * norm_pdf(d1);
        
        if (std::abs(vega) < 1e-8) break;
        sigma = sigma + diff / vega;
        sigma = std::clamp(sigma, 0.01f, 5.0f); // Clamp 1% to 500%
    }
    return sigma;
}

Greeks BlackScholes::calculateGreeks(float S, float K, float T, float r, float sigma, bool isCall) {
    Greeks g;
    float d1 = (std::log(S / K) + (r + 0.5f * sigma * sigma) * T) / (sigma * std::sqrt(T));
    float d2 = d1 - sigma * std::sqrt(T);
    float pdf_d1 = norm_pdf(d1);

    // Delta
    if (isCall) g.delta = norm_cdf(d1);
    else g.delta = norm_cdf(d1) - 1.0f;

    // Gamma (Same for Call & Put)
    g.gamma = pdf_d1 / (S * sigma * std::sqrt(T));

    // Vega (Same for Call & Put)
    g.vega = S * std::sqrt(T) * pdf_d1; // Usually divided by 100 for % change

    // Theta
    float term1 = -(S * pdf_d1 * sigma) / (2.0f * std::sqrt(T));
    if (isCall) {
        g.theta = term1 - r * K * std::exp(-r * T) * norm_cdf(d2);
    } else {
        g.theta = term1 + r * K * std::exp(-r * T) * norm_cdf(-d2);
    }
    // Theta is usually per day, T is years. 
    g.theta = g.theta / 365.0f;

    return g;
}
