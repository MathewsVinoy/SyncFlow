#include "core/Logger.h"

#include <filesystem>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace {
std::shared_ptr<spdlog::logger> g_logger;
}

void Logger::init(const std::string& folder) {
    const std::filesystem::path log_folder(folder);
    std::filesystem::create_directories(log_folder);

    const std::filesystem::path log_file = log_folder / "app.log";
    g_logger = spdlog::get("syncflow");
    if (!g_logger) {
        g_logger = spdlog::basic_logger_mt("syncflow", log_file.string(), true);
        g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        g_logger->flush_on(spdlog::level::info);
    }

    g_logger->info("Logger initialized");
}

void Logger::info(const std::string& message) {
    write("info", message);
}

void Logger::warn(const std::string& message) {
    write("warn", message);
}

void Logger::error(const std::string& message) {
    write("error", message);
}

void Logger::debug(const std::string& message) {
    write("debug", message);
}

void Logger::write(const std::string& level, const std::string& message) {
    if (!g_logger) {
        init();
    }

    if (level == "info") {
        g_logger->info(message);
    } else if (level == "warn") {
        g_logger->warn(message);
    } else if (level == "error") {
        g_logger->error(message);
    } else {
        g_logger->debug(message);
    }
}
