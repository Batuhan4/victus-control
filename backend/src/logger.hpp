#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <mutex>
#include <ctime>
#include <iomanip>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

class Logger {
private:
    static Logger* instance;
    std::mutex log_mutex;
    LogLevel min_level;
    std::ofstream log_file;
    
    Logger() : min_level(LogLevel::INFO) {
        // Try to open log file
        log_file.open("/var/log/victus-control.log", std::ios::app);
    }
    
    std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
    
    std::string level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARNING: return "WARNING";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::CRITICAL: return "CRITICAL";
            default: return "UNKNOWN";
        }
    }
    
public:
    static Logger& getInstance() {
        if (!instance) {
            instance = new Logger();
        }
        return *instance;
    }
    
    void set_min_level(LogLevel level) {
        min_level = level;
    }
    
    void log(LogLevel level, const std::string& message) {
        if (level < min_level) return;
        
        std::lock_guard<std::mutex> lock(log_mutex);
        std::string log_line = "[" + get_timestamp() + "] [" + level_to_string(level) + "] " + message;
        
        // Always log to stderr for now
        std::cerr << log_line << std::endl;
        
        // Also log to file if available
        if (log_file.is_open()) {
            log_file << log_line << std::endl;
            log_file.flush();
        }
    }
    
    // Convenience methods
    void debug(const std::string& message) { log(LogLevel::DEBUG, message); }
    void info(const std::string& message) { log(LogLevel::INFO, message); }
    void warning(const std::string& message) { log(LogLevel::WARNING, message); }
    void error(const std::string& message) { log(LogLevel::ERROR, message); }
    void critical(const std::string& message) { log(LogLevel::CRITICAL, message); }
};

// Initialize static member
inline Logger* Logger::instance = nullptr;

// Convenience macro
#define LOG(level, message) Logger::getInstance().log(level, message)
#define LOG_DEBUG(message) Logger::getInstance().debug(message)
#define LOG_INFO(message) Logger::getInstance().info(message)
#define LOG_WARNING(message) Logger::getInstance().warning(message)
#define LOG_ERROR(message) Logger::getInstance().error(message)
#define LOG_CRITICAL(message) Logger::getInstance().critical(message)

#endif // LOGGER_HPP