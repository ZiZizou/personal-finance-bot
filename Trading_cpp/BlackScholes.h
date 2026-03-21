#pragma once
#include <string>
#include <vector>

struct Greeks {
    double delta;
    double gamma;
    double theta;
    double vega;
};

// Options Pricing Model
class BlackScholes {
public:
    static double callPrice(double S, double K, double T, double r, double sigma);
    static double putPrice(double S, double K, double T, double r, double sigma);
    static double impliedVolatility(double marketPrice, double S, double K, double T, double r, bool isCall);
    static Greeks calculateGreeks(double S, double K, double T, double r, double sigma, bool isCall);
};