#pragma once
#include <mutex>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <ctime>

class Logger {
private:
    std::mutex logMutex;
    std::ofstream logFile;
    bool verbose;

    Logger() {
        logFile.open("trading_bot.log", std::ios::app);
        verbose = true;
    }

public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void log(const std::string& message) {
        std::lock_guard<std::mutex> lock(logMutex);
        std::time_t now = std::time(nullptr);
        char buf[20];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        
        std::stringstream ss;
        ss << "[" << buf << "] " << message;
        
        if (verbose) std::cout << ss.str() << std::endl;
        if (logFile.is_open()) logFile << ss.str() << std::endl;
    }
    
    // Non-locking version for pure file output or specific needs
    void logFileOnly(const std::string& message) {
        std::lock_guard<std::mutex> lock(logMutex);
        if (logFile.is_open()) logFile << message << std::endl;
    }
};
