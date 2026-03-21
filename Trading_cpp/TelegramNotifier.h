#pragma once
#include "Result.h"
#include "NetworkUtils.h"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <iostream>

// Forward declaration - LiveSignalRow is defined in LiveSignals.h
struct LiveSignalRow;

class TelegramNotifier {
public:
    TelegramNotifier(const std::string& botToken, const std::string& chatId);
    bool isEnabled() const;

    // Send a periodic status update (no signals required)
    Result<void> sendStatusMessage(const std::string& text);

    // Send actionable signals notification (with VIX signal)
    Result<void> notify(const std::vector<LiveSignalRow>& rows, int barSizeMinutes,
                        const std::pair<std::string, std::string>& vixSignal = {"", ""},
                        double vixValue = 0.0);

private:
    std::string botToken_;
    std::string chatId_;

    std::string formatMessage(const std::vector<const LiveSignalRow*>& actionable,
                              size_t totalCount, int barSizeMinutes,
                              const std::pair<std::string, std::string>& vixSignal = {"", ""},
                              double vixValue = 0.0);
    Result<void> sendMessage(const std::string& text);
    static std::string escapeJson(const std::string& input);
    static std::string escapeHtml(const std::string& input);
};
