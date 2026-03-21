#pragma once
#include <chrono>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>

// TimePoint type for consistent timestamp handling
using TimePoint = std::chrono::sys_seconds;

namespace TimeUtils {

// Convert Unix timestamp (seconds since epoch) to TimePoint
inline TimePoint fromUnixSeconds(int64_t seconds) {
    return TimePoint{std::chrono::seconds{seconds}};
}

// Convert TimePoint to Unix timestamp
inline int64_t toUnixSeconds(TimePoint tp) {
    return tp.time_since_epoch().count();
}

// Format TimePoint as ISO8601 string (UTC)
inline std::string formatISO8601(TimePoint tp) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_val;
#ifdef _WIN32
    gmtime_s(&tm_val, &time_t_val);
#else
    gmtime_r(&time_t_val, &tm_val);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_val, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

// Format TimePoint as date string (YYYY-MM-DD)
inline std::string formatDate(TimePoint tp) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_val;
#ifdef _WIN32
    gmtime_s(&tm_val, &time_t_val);
#else
    gmtime_r(&time_t_val, &tm_val);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_val, "%Y-%m-%d");
    return oss.str();
}

// Format TimePoint as datetime string for Eastern Time (approximate, no DST handling)
// For accurate ET conversion, use a proper timezone library
inline std::string formatTimeET(TimePoint tp) {
    // Subtract 5 hours for EST (approximate)
    auto et_tp = tp - std::chrono::hours(5);
    auto time_t_val = std::chrono::system_clock::to_time_t(et_tp);
    std::tm tm_val;
#ifdef _WIN32
    gmtime_s(&tm_val, &time_t_val);
#else
    gmtime_r(&time_t_val, &tm_val);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_val, "%Y-%m-%d %H:%M:%S") << " ET";
    return oss.str();
}

// Parse ISO8601 string to TimePoint
inline TimePoint parseISO8601(const std::string& str) {
    std::tm tm_val = {};
    std::istringstream iss(str);
    iss >> std::get_time(&tm_val, "%Y-%m-%dT%H:%M:%S");
    if (iss.fail()) {
        // Try date-only format
        iss.clear();
        iss.str(str);
        iss >> std::get_time(&tm_val, "%Y-%m-%d");
    }
#ifdef _WIN32
    auto time_t_val = _mkgmtime(&tm_val);
#else
    auto time_t_val = timegm(&tm_val);
#endif
    return std::chrono::time_point_cast<std::chrono::seconds>(
        std::chrono::system_clock::from_time_t(time_t_val));
}

// Get current time as TimePoint
inline TimePoint now() {
    return std::chrono::time_point_cast<std::chrono::seconds>(
        std::chrono::system_clock::now());
}

// Get day of week (0 = Sunday, 6 = Saturday)
inline int getDayOfWeek(TimePoint tp) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_val;
#ifdef _WIN32
    gmtime_s(&tm_val, &time_t_val);
#else
    gmtime_r(&time_t_val, &tm_val);
#endif
    return tm_val.tm_wday;
}

// Get hour of day (0-23) in ET (approximate)
inline int getHourET(TimePoint tp) {
    auto et_tp = tp - std::chrono::hours(5);  // EST offset
    auto time_t_val = std::chrono::system_clock::to_time_t(et_tp);
    std::tm tm_val;
#ifdef _WIN32
    gmtime_s(&tm_val, &time_t_val);
#else
    gmtime_r(&time_t_val, &tm_val);
#endif
    return tm_val.tm_hour;
}

// Get minute of hour
inline int getMinuteET(TimePoint tp) {
    auto et_tp = tp - std::chrono::hours(5);
    auto time_t_val = std::chrono::system_clock::to_time_t(et_tp);
    std::tm tm_val;
#ifdef _WIN32
    gmtime_s(&tm_val, &time_t_val);
#else
    gmtime_r(&time_t_val, &tm_val);
#endif
    return tm_val.tm_min;
}

// Check if two TimePoints are on the same calendar day (UTC)
inline bool isSameDay(TimePoint a, TimePoint b) {
    return formatDate(a) == formatDate(b);
}

// Get start of day (midnight UTC)
inline TimePoint startOfDay(TimePoint tp) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_val;
#ifdef _WIN32
    gmtime_s(&tm_val, &time_t_val);
#else
    gmtime_r(&time_t_val, &tm_val);
#endif
    tm_val.tm_hour = 0;
    tm_val.tm_min = 0;
    tm_val.tm_sec = 0;
#ifdef _WIN32
    auto midnight = _mkgmtime(&tm_val);
#else
    auto midnight = timegm(&tm_val);
#endif
    return std::chrono::time_point_cast<std::chrono::seconds>(
        std::chrono::system_clock::from_time_t(midnight));
}

} // namespace TimeUtils
