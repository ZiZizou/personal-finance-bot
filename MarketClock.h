#pragma once
#include "TimeUtils.h"
#include <thread>
#include <chrono>

// MarketClock provides NYSE regular session schedule awareness
// Simple implementation: Mon-Fri, 9:30 AM - 4:00 PM Eastern Time
class MarketClock {
public:
    // Check if the market is currently open (regular session)
    static bool isMarketOpen(TimePoint nowET) {
        int dow = TimeUtils::getDayOfWeek(nowET);

        // Weekend check (0 = Sunday, 6 = Saturday)
        if (dow == 0 || dow == 6) {
            return false;
        }

        int hour = TimeUtils::getHourET(nowET);
        int minute = TimeUtils::getMinuteET(nowET);
        int timeMinutes = hour * 60 + minute;

        // Market hours: 9:30 AM (570 minutes) to 4:00 PM (960 minutes)
        constexpr int marketOpen = 9 * 60 + 30;   // 9:30 AM = 570 minutes
        constexpr int marketClose = 16 * 60;      // 4:00 PM = 960 minutes

        return timeMinutes >= marketOpen && timeMinutes < marketClose;
    }

    // Check if current time is before market open
    static bool isPreMarket(TimePoint nowET) {
        int dow = TimeUtils::getDayOfWeek(nowET);
        if (dow == 0 || dow == 6) return false;

        int hour = TimeUtils::getHourET(nowET);
        int minute = TimeUtils::getMinuteET(nowET);
        int timeMinutes = hour * 60 + minute;

        constexpr int marketOpen = 9 * 60 + 30;
        return timeMinutes < marketOpen;
    }

    // Check if current time is after market close
    static bool isAfterHours(TimePoint nowET) {
        int dow = TimeUtils::getDayOfWeek(nowET);
        if (dow == 0 || dow == 6) return false;

        int hour = TimeUtils::getHourET(nowET);
        int minute = TimeUtils::getMinuteET(nowET);
        int timeMinutes = hour * 60 + minute;

        constexpr int marketClose = 16 * 60;
        return timeMinutes >= marketClose;
    }

    // Get the next bar close time based on current time and bar size
    // barSizeMinutes: typically 60 for hourly bars
    static TimePoint nextBarClose(TimePoint nowET, int barSizeMinutes) {
        int dow = TimeUtils::getDayOfWeek(nowET);
        int hour = TimeUtils::getHourET(nowET);
        int minute = TimeUtils::getMinuteET(nowET);
        int currentMinutes = hour * 60 + minute;

        constexpr int marketOpen = 9 * 60 + 30;   // 9:30 AM
        constexpr int marketClose = 16 * 60;      // 4:00 PM

        // If weekend, advance to Monday 9:30 AM + barSize
        if (dow == 0) {  // Sunday
            // Next market open is Monday
            auto nextOpen = nowET + std::chrono::hours(24);
            return adjustToBarClose(nextOpen, barSizeMinutes);
        }
        if (dow == 6) {  // Saturday
            // Next market open is Monday (2 days)
            auto nextOpen = nowET + std::chrono::hours(48);
            return adjustToBarClose(nextOpen, barSizeMinutes);
        }

        // If before market open, return first bar close
        if (currentMinutes < marketOpen) {
            int firstBarClose = marketOpen + barSizeMinutes;
            int hoursToAdd = firstBarClose / 60 - hour;
            int minsToAdd = firstBarClose % 60 - minute;
            return nowET + std::chrono::hours(hoursToAdd) + std::chrono::minutes(minsToAdd);
        }

        // If after market close, advance to next trading day
        if (currentMinutes >= marketClose) {
            if (dow == 5) {  // Friday
                // Next is Monday
                auto nextOpen = nowET + std::chrono::hours(72);
                return adjustToBarClose(nextOpen, barSizeMinutes);
            } else {
                // Next trading day
                auto nextOpen = nowET + std::chrono::hours(24);
                return adjustToBarClose(nextOpen, barSizeMinutes);
            }
        }

        // During market hours: find next bar boundary
        int minutesSinceOpen = currentMinutes - marketOpen;
        int currentBar = minutesSinceOpen / barSizeMinutes;
        int nextBarCloseMinutes = marketOpen + (currentBar + 1) * barSizeMinutes;

        // Cap at market close
        if (nextBarCloseMinutes > marketClose) {
            nextBarCloseMinutes = marketClose;
        }

        int targetHour = nextBarCloseMinutes / 60;
        int targetMinute = nextBarCloseMinutes % 60;

        // Calculate the difference
        int hoursToAdd = targetHour - hour;
        int minsToAdd = targetMinute - minute;

        return nowET + std::chrono::hours(hoursToAdd) + std::chrono::minutes(minsToAdd);
    }

    // Sleep until the next bar close (with a small delay for data to settle)
    static void sleepUntilNextBarClose(TimePoint nowET, int barSizeMinutes, int settleDelaySeconds = 3) {
        auto target = nextBarClose(nowET, barSizeMinutes);
        target += std::chrono::seconds(settleDelaySeconds);

        auto now = TimeUtils::now();
        if (target > now) {
            auto sleepDuration = target - now;
            std::this_thread::sleep_for(sleepDuration);
        }
    }

    // Sleep until market open (if currently closed)
    static void sleepUntilMarketOpen(TimePoint nowET) {
        while (!isMarketOpen(nowET)) {
            // Sleep for 1 minute and check again
            std::this_thread::sleep_for(std::chrono::minutes(1));
            nowET = TimeUtils::now();
        }
    }

    // Get current Eastern Time
    static TimePoint nowET() {
        return TimeUtils::now();
    }

private:
    // Helper to adjust a TimePoint to the first bar close of that day
    static TimePoint adjustToBarClose(TimePoint tp, int barSizeMinutes) {
        // Move to 9:30 AM + barSize of that day
        auto dayStart = TimeUtils::startOfDay(tp);
        // Add 5 hours for UTC to ET conversion (approximate)
        // Then add market open time (9:30 = 570 minutes) + barSize
        int totalMinutes = 9 * 60 + 30 + barSizeMinutes + 5 * 60;  // Include 5 hour UTC offset
        return dayStart + std::chrono::minutes(totalMinutes);
    }
};
