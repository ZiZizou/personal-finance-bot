#include <iostream>
#include <string>
#include <cstdlib>
#include "TelegramNotifier.h"
#include "Config.h"

int main() {
    std::cout << "Testing Telegram notification..." << std::endl;

    // Initialize config to load environment variables
    auto configResult = Config::getInstance().initialize();
    if (configResult.isError()) {
        std::cerr << "Config error: " << configResult.error().message << std::endl;
    }

    // Get Telegram credentials from environment variables
    std::string token = Config::getInstance().getString("STOCK_TELEGRAM_BOT_TOKEN", "");
    std::string chatId = Config::getInstance().getString("STOCK_TELEGRAM_CHAT_ID", "");

    std::cout << "Token set: " << (token.empty() ? "NO" : "YES") << std::endl;
    std::cout << "Chat ID set: " << (chatId.empty() ? "NO" : "YES") << std::endl;

    if (token.empty() || chatId.empty()) {
        std::cout << "Telegram credentials not set in environment variables!" << std::endl;
        std::cout << "Please set:" << std::endl;
        std::cout << "  STOCK_TELEGRAM_BOT_TOKEN" << std::endl;
        std::cout << "  STOCK_TELEGRAM_CHAT_ID" << std::endl;
        return 1;
    }

    // Mask token for display
    std::string maskedToken = token.substr(0, 5) + "..." + token.substr(token.length() - 5);
    std::cout << "Using token: " << maskedToken << std::endl;

    TelegramNotifier notifier(token, chatId);

    if (!notifier.isEnabled()) {
        std::cout << "Telegram not enabled" << std::endl;
        return 1;
    }

    // Try to send a test message
    auto result = notifier.sendStatusMessage("Test message from trading bot");
    if (result.isError()) {
        std::cerr << "Error: " << result.error().message << std::endl;
        return 1;
    }

    std::cout << "Test message sent successfully!" << std::endl;
    return 0;
}
