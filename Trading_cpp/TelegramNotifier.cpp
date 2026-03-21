#include "TelegramNotifier.h"
#include "LiveSignals.h"
#include <iostream>

TelegramNotifier::TelegramNotifier(const std::string& botToken, const std::string& chatId)
    : botToken_(botToken), chatId_(chatId) {
    if (isEnabled()) {
        std::cout << "Telegram notifications enabled" << std::endl;
    } else {
        std::cout << "Telegram notifications disabled (no token/chatId)" << std::endl;
    }
}

bool TelegramNotifier::isEnabled() const {
    return !botToken_.empty() && !chatId_.empty();
}

// Send a periodic status update (no signals required)
Result<void> TelegramNotifier::sendStatusMessage(const std::string& text) {
    if (!isEnabled()) {
        return Result<void>::ok();
    }
    return sendMessage(text);
}

Result<void> TelegramNotifier::notify(const std::vector<LiveSignalRow>& rows, int barSizeMinutes,
                                       const std::pair<std::string, std::string>& vixSignal,
                                       double vixValue) {
    if (!isEnabled()) {
        return Result<void>::ok();
    }

    // Filter for actionable signals only
    std::vector<const LiveSignalRow*> actionable;
    for (const auto& row : rows) {
        if (row.action == "BUY" || row.action == "SELL") {
            actionable.push_back(&row);
        }
    }

    if (actionable.empty() && vixSignal.first.empty()) {
        return Result<void>::ok();
    }

    std::string message = formatMessage(actionable, rows.size(), barSizeMinutes, vixSignal, vixValue);
    return sendMessage(message);
}

std::string TelegramNotifier::formatMessage(const std::vector<const LiveSignalRow*>& actionable,
                                            size_t totalCount, int barSizeMinutes,
                                            const std::pair<std::string, std::string>& vixSignal,
                                            double vixValue) {
    std::ostringstream ss;
    ss << std::fixed;

    ss << "<b>Trading Signals (" << barSizeMinutes << "-min bar)</b>\n\n";

    // VIX Signal
    if (!vixSignal.first.empty()) {
        std::string vixEmoji;
        if (vixSignal.first == "BUY") vixEmoji = "🔵";
        else if (vixSignal.first == "SELL") vixEmoji = "🔴";
        else vixEmoji = "⚪";

        ss << vixEmoji << " <b>VIX " << vixSignal.first << "</b> ("
           << std::setprecision(2) << vixValue << ")\n";
        ss << "  " << escapeHtml(vixSignal.second) << "\n\n";
    }

    for (const auto* row : actionable) {
        ss << "<b>" << row->action << " " << row->symbol
           << "</b> @ $" << std::setprecision(2) << row->lastClose << "\n";

        ss << "  Strength: " << std::setprecision(2) << row->strength
           << " | Sentiment: " << std::setprecision(2) << row->sentiment << "\n";

        if (row->stopLoss > 0 || row->takeProfit > 0) {
            ss << "  SL: $" << std::setprecision(2) << row->stopLoss
               << " | TP: $" << std::setprecision(2) << row->takeProfit << "\n";
        }

        if (!row->reason.empty()) {
            ss << "  Reason: " << escapeHtml(row->reason) << "\n";
        }

        ss << "\n";
    }

    ss << actionable.size() << " actionable / " << totalCount << " total symbols";

    return ss.str();
}

Result<void> TelegramNotifier::sendMessage(const std::string& text) {
    std::string url = "https://api.telegram.org/bot" + botToken_ + "/sendMessage";

    std::string payload = "{\"chat_id\":\"" + chatId_
        + "\",\"text\":\"" + escapeJson(text)
        + "\",\"parse_mode\":\"HTML\""
        + ",\"disable_web_page_preview\":true}";

    std::cout << "[DEBUG] Attempting to send Telegram message. URL: " << "https://api.telegram.org/bot" << botToken_.substr(0, 5) << ".../sendMessage" << std::endl;

    std::vector<std::string> headers = {
        "Content-Type: application/json"
    };

    auto result = NetworkUtils::postDataWithResult(url, payload, headers);
    if (result.isError()) {
        std::cerr << "[DEBUG] Telegram send failed at network level: " << result.error().message << std::endl;
        return Result<void>::err(Error::network("Telegram send failed: " + result.error().message));
    }

    // Check for Telegram API error in response
    const auto& response = result.value();
    std::cout << "[DEBUG] Telegram API response received: " << response << std::endl;
    
    if (response.find("\"ok\":true") == std::string::npos &&
        response.find("\"ok\": true") == std::string::npos) {
        std::cerr << "[DEBUG] Telegram API returned error response." << std::endl;
        return Result<void>::err(Error::network("Telegram API error: " + response));
    }

    std::cout << "[DEBUG] Telegram message sent successfully!" << std::endl;
    return Result<void>::ok();
}

std::string TelegramNotifier::escapeJson(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\n': output += "\\n";  break;
            case '\r': output += "\\r";  break;
            case '\t': output += "\\t";  break;
            default:   output += c;      break;
        }
    }
    return output;
}

std::string TelegramNotifier::escapeHtml(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '<':  output += "&lt;";  break;
            case '>':  output += "&gt;";  break;
            case '&':  output += "&amp;"; break;
            default:   output += c;       break;
        }
    }
    return output;
}
