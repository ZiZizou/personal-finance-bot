#include <iostream>
#include "Config.h"

int main() {
    std::cout << "Testing .env file loading..." << std::endl;

    // Initialize config (loads from .env file)
    auto result = Config::getInstance().initialize();
    if (result.isError()) {
        std::cerr << "Config init error: " << result.error().message << std::endl;
    }

    // Try to get Telegram credentials
    std::string token = Config::getInstance().getString("STOCK_TELEGRAM_BOT_TOKEN", "NOT_FOUND");
    std::string chatId = Config::getInstance().getString("STOCK_TELEGRAM_CHAT_ID", "NOT_FOUND");

    std::cout << "Token: " << (token == "NOT_FOUND" ? "NOT_SET" : token.substr(0, 10) + "...") << std::endl;
    std::cout << "Chat ID: " << chatId << std::endl;

    return 0;
}
