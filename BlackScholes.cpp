#include "BlackScholes.h"
#include <cmath>
#include <algorithm>

const double PI = 3.14159265358979323846;

double norm_cdf(double x) {
    return 0.5 * std::erfc(-x * std::sqrt(0.5));
}

double norm_pdf(double x) {
    return (1.0 / std::sqrt(2.0 * PI)) * std::exp(-0.5 * x * x);
}

double BlackScholes::callPrice(double S, double K, double T, double r, double sigma) {
    double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / (sigma * std::sqrt(T));
    double d2 = d1 - sigma * std::sqrt(T);
    return S * norm_cdf(d1) - K * std::exp(-r * T) * norm_cdf(d2);
}

double BlackScholes::putPrice(double S, double K, double T, double r, double sigma) {
    double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / (sigma * std::sqrt(T));
    double d2 = d1 - sigma * std::sqrt(T);
    return K * std::exp(-r * T) * norm_cdf(-d2) - S * norm_cdf(-d1);
}

double BlackScholes::impliedVolatility(double marketPrice, double S, double K, double T, double r, bool isCall) {
    double sigma = 0.5; // Initial guess
    for (int i = 0; i < 100; ++i) {
        double price = isCall ? callPrice(S, K, T, r, sigma) : putPrice(S, K, T, r, sigma);
        double diff = marketPrice - price;
        if (std::abs(diff) < 1e-6) return sigma;
        
        double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / (sigma * std::sqrt(T));
        double vega = S * std::sqrt(T) * norm_pdf(d1);
        
        if (std::abs(vega) < 1e-6) break;
        sigma += diff / vega;
    }
    return sigma;
}

Greeks BlackScholes::calculateGreeks(double S, double K, double T, double r, double sigma, bool isCall) {
    Greeks g;
    double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / (sigma * std::sqrt(T));
    double d2 = d1 - sigma * std::sqrt(T);
    double pdf_d1 = norm_pdf(d1);

    if (isCall) {
        g.delta = norm_cdf(d1);
        g.theta = -(S * pdf_d1 * sigma) / (2.0 * std::sqrt(T)) - r * K * std::exp(-r * T) * norm_cdf(d2);
    } else {
        g.delta = norm_cdf(d1) - 1.0;
        g.theta = -(S * pdf_d1 * sigma) / (2.0 * std::sqrt(T)) + r * K * std::exp(-r * T) * norm_cdf(-d2);
    }

    g.gamma = pdf_d1 / (S * sigma * std::sqrt(T));
    g.vega = S * std::sqrt(T) * pdf_d1;

    return g;
}