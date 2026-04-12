#ifndef SYNCFLOW_COMMON_LOGGER_H
#define SYNCFLOW_COMMON_LOGGER_H

#include <iostream>
#include <mutex>
#include <string>

namespace syncflow {

enum class LogLevel {
    ERROR = 0,
    WARN = 1,
    INFO = 2,
    DEBUG = 3,
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void set_level(LogLevel level) {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
    }

    LogLevel level() const {
        return level_;
    }

    void log(LogLevel level, const std::string& tag, const std::string& message) {
        if (static_cast<int>(level) > static_cast<int>(level_)) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        std::ostream& out = (level == LogLevel::ERROR) ? std::cerr : std::cout;
        out << "[" << tag << "] " << message << '\n';
    }

private:
    Logger() = default;
    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::INFO;
};

} // namespace syncflow

#define LOG_INFO(tag, message) ::syncflow::Logger::instance().log(::syncflow::LogLevel::INFO, (tag), (message))
#define LOG_ERROR(tag, message) ::syncflow::Logger::instance().log(::syncflow::LogLevel::ERROR, (tag), (message))
#define LOG_WARN(tag, message) ::syncflow::Logger::instance().log(::syncflow::LogLevel::WARN, (tag), (message))
#define LOG_DEBUG(tag, message) ::syncflow::Logger::instance().log(::syncflow::LogLevel::DEBUG, (tag), (message))

#endif // SYNCFLOW_COMMON_LOGGER_H
