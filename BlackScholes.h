#pragma once
#include <string>
#include <vector>

#pragma once

struct Greeks {
    float delta;
    float gamma;
    float theta;
    float vega;
};

// Options Pricing Model
class BlackScholes {
public:
    static float callPrice(float S, float K, float T, float r, float sigma);
    static float putPrice(float S, float K, float T, float r, float sigma);
    static float impliedVolatility(float marketPrice, float S, float K, float T, float r, bool isCall);
    static Greeks calculateGreeks(float S, float K, float T, float r, float sigma, bool isCall);
};
