// Simple test to verify Python API integration
#include <iostream>
#include <string>
#include "Providers.h"
#include "Config.h"

int main() {
    std::cout << "=== Testing Python Signal Provider ===" << std::endl;

    // Initialize config
    auto configResult = Config::getInstance().initialize();
    if (configResult.isError()) {
        std::cerr << "Config init error: " << configResult.error().message << std::endl;
        return 1;
    }

    // Get Python service config
    std::string pythonUrl = Config::getInstance().getPythonServiceUrl();
    bool usePython = Config::getInstance().getUsePythonService();

    std::cout << "Python Service URL: " << pythonUrl << std::endl;
    std::cout << "Use Python Service: " << (usePython ? "true" : "false") << std::endl;

    // Create signal provider
    PythonSignalProvider provider(pythonUrl);

    // Check if service is available
    std::cout << "\nChecking service availability..." << std::endl;
    if (provider.isAvailable()) {
        std::cout << "Python service is available!" << std::endl;

        // Test single signal
        std::cout << "\nFetching signal for NVDA..." << std::endl;
        auto signalResult = provider.getSignal("NVDA");
        if (signalResult.isOk()) {
            auto& sig = signalResult.value();
            std::cout << "Signal: " << sig.signal << std::endl;
            std::cout << "Confidence: " << sig.confidence << std::endl;
            std::cout << "Price: " << sig.price << std::endl;
        } else {
            std::cerr << "Error: " << signalResult.error().message << std::endl;
        }

        // Test batch signals
        std::cout << "\nFetching batch signals for NVDA, INTC, AMD..." << std::endl;
        auto batchResult = provider.getBatchSignals({"NVDA", "INTC", "AMD"});
        if (batchResult.isOk()) {
            auto& signals = batchResult.value();
            std::cout << "Got " << signals.size() << " signals:" << std::endl;
            for (const auto& s : signals) {
                std::cout << "  " << s.ticker << ": " << s.signal
                          << " (confidence: " << s.confidence << ")" << std::endl;
            }
        } else {
            std::cerr << "Error: " << batchResult.error().message << std::endl;
        }

        // Test sentiment
        std::cout << "\nFetching sentiment for NVDA..." << std::endl;
        auto sentimentResult = provider.getSentiment("NVDA", 7);
        if (sentimentResult.isOk()) {
            auto& sent = sentimentResult.value();
            std::cout << "Sentiment Score: " << sent.sentiment_score << std::endl;
            std::cout << "Confidence: " << sent.confidence << std::endl;
            std::cout << "Articles: " << sent.article_count << std::endl;
        } else {
            std::cerr << "Error: " << sentimentResult.error().message << std::endl;
        }

    } else {
        std::cout << "Python service is NOT available." << std::endl;
        std::cout << "Make sure the service is running: cd Trading_Python && python scripts/run_service.py" << std::endl;
    }

    std::cout << "\n=== Test Complete ===" << std::endl;
    return 0;
}
