// include/syncflow/common/logger.h
// Logging system

#ifndef SYNCFLOW_LOGGER_H
#define SYNCFLOW_LOGGER_H

#include <string>
#include <memory>
#include <sstream>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>

namespace syncflow {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5,
};

class Logger {
public:
    static Logger& instance();
    
    void set_level(LogLevel level);
    void set_output_file(const std::string& filename);
    void set_console_output(bool enabled);
    
    void log(LogLevel level, const std::string& category, const std::string& message);
    
    // Convenience methods
    void trace(const std::string& cat, const std::string& msg) { log(LogLevel::TRACE, cat, msg); }
    void debug(const std::string& cat, const std::string& msg) { log(LogLevel::DEBUG, cat, msg); }
    void info(const std::string& cat, const std::string& msg) { log(LogLevel::INFO, cat, msg); }
    void warn(const std::string& cat, const std::string& msg) { log(LogLevel::WARN, cat, msg); }
    void error(const std::string& cat, const std::string& msg) { log(LogLevel::ERROR, cat, msg); }
    void fatal(const std::string& cat, const std::string& msg) { log(LogLevel::FATAL, cat, msg); }
    
private:
    Logger() : level_(LogLevel::INFO), console_output_(true) {}
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    std::string level_to_string(LogLevel level) const;
    std::string get_timestamp() const;
    
    LogLevel level_;
    std::unique_ptr<std::ofstream> output_file_;
    bool console_output_;
};

// Macros for convenient logging
#define LOG_TRACE(cat, msg) syncflow::Logger::instance().trace(cat, msg)
#define LOG_DEBUG(cat, msg) syncflow::Logger::instance().debug(cat, msg)
#define LOG_INFO(cat, msg) syncflow::Logger::instance().info(cat, msg)
#define LOG_WARN(cat, msg) syncflow::Logger::instance().warn(cat, msg)
#define LOG_ERROR(cat, msg) syncflow::Logger::instance().error(cat, msg)
#define LOG_FATAL(cat, msg) syncflow::Logger::instance().fatal(cat, msg)

} // namespace syncflow

#endif // SYNCFLOW_LOGGER_H
