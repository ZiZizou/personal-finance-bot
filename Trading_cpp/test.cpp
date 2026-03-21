#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include "TechnicalAnalysis.h"
#include "TradingStrategy.h"

void testRSI() {
    std::cout << "Testing RSI..." << std::endl;
    std::vector<float> upTrend;
    for(int i=0; i<50; ++i) upTrend.push_back(100.0f + i);
    
    float rsiHigh = computeRSI(upTrend);
    std::cout << "  UpTrend RSI: " << rsiHigh << " (Expected > 70)" << std::endl;
    // RSI converges to 100 but Wilder's takes time.
    assert(rsiHigh > 70.0f);

    std::vector<float> downTrend;
    for(int i=0; i<50; ++i) downTrend.push_back(200.0f - i);
    
    float rsiLow = computeRSI(downTrend);
    std::cout << "  DownTrend RSI: " << rsiLow << " (Expected < 30)" << std::endl;
    assert(rsiLow < 30.0f);
    
    std::cout << "  RSI Test Passed." << std::endl;
}

void testMACD() {
    std::cout << "Testing MACD..." << std::endl;
    std::vector<float> prices;
    // Flat line: MACD should be 0
    for(int i=0; i<100; ++i) prices.push_back(100.0f);
    
    std::pair<float, float> res = computeMACD(prices);
    std::cout << "  Flat MACD: " << res.first << ", Signal: " << res.second << std::endl;
    assert(std::abs(res.first) < 0.01f);
    assert(std::abs(res.second) < 0.01f);
    
    std::cout << "  MACD Test Passed." << std::endl;
}

void testStrategy() {
    std::cout << "Testing Strategy..." << std::endl;
    
    std::vector<float> prices;
    // Create a scenario
    for(int i=0; i<50; ++i) prices.push_back(100.0f);
    
    Signal sig = generateSignal("TEST", prices);
    std::cout << "  Action for Flat: " << sig.action << " (Expected: hold)" << std::endl;
    assert(sig.action == "hold");
    
    std::cout << "  Strategy Test Passed." << std::endl;
}

int main() {
    testRSI();
    testMACD();
    testStrategy();
    return 0;
}
