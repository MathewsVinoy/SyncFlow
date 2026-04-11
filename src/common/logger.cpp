// src/common/logger.cpp

#include <syncflow/common/logger.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <ctime>

namespace syncflow {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_level(LogLevel level) {
    level_ = level;
}

void Logger::set_output_file(const std::string& filename) {
    output_file_ = std::make_unique<std::ofstream>(filename, std::ios::app);
}

void Logger::set_console_output(bool enabled) {
    console_output_ = enabled;
}

void Logger::log(LogLevel level, const std::string& category, const std::string& message) {
    if (level < level_) {
        return;
    }
    
    std::stringstream ss;
    ss << "[" << get_timestamp() << "] "
       << "[" << level_to_string(level) << "] "
       << "[" << category << "] "
       << message;
    
    std::string log_line = ss.str();
    
    if (console_output_) {
        std::cout << log_line << std::endl;
    }
    
    if (output_file_ && output_file_->is_open()) {
        *output_file_ << log_line << std::endl;
        output_file_->flush();
    }
}

std::string Logger::level_to_string(LogLevel level) const {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

std::string Logger::get_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
       << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}

} // namespace syncflow
