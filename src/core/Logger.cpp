#include "core/Logger.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace {
std::shared_ptr<spdlog::logger> g_logger;
std::mutex g_logger_mutex;
bool g_sync_data_only = false;

std::shared_ptr<spdlog::logger> ensureLogger(const std::string& folder) {
    std::lock_guard<std::mutex> lock(g_logger_mutex);

    if (g_logger) {
        return g_logger;
    }

    const std::filesystem::path log_folder(folder);
    std::filesystem::create_directories(log_folder);

    const std::filesystem::path log_file = log_folder / "app.log";
    g_logger = spdlog::get("syncflow");
    if (!g_logger) {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file.string(), true);
        std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};

        g_logger = std::make_shared<spdlog::logger>("syncflow", sinks.begin(), sinks.end());
        spdlog::register_logger(g_logger);
        g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        g_logger->set_level(spdlog::level::info);
        g_logger->flush_on(spdlog::level::info);
    }

    return g_logger;
}
}

void Logger::init(const std::string& folder) {
    const auto logger = ensureLogger(folder);
    logger->info("Logger initialized");
}

void Logger::setLevel(const std::string& level) {
    const auto logger = ensureLogger("log");

    if (level == "debug") {
        logger->set_level(spdlog::level::debug);
    } else if (level == "info") {
        logger->set_level(spdlog::level::info);
    } else if (level == "warn" || level == "warning") {
        logger->set_level(spdlog::level::warn);
    } else if (level == "error") {
        logger->set_level(spdlog::level::err);
    } else {
        logger->set_level(spdlog::level::info);
        logger->warn("Unknown log level '" + level + "', defaulting to info");
    }
}

void Logger::setSyncDataOnly(bool syncOnly) {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    g_sync_data_only = syncOnly;
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    if (!g_logger) {
        return;
    }

    g_logger->info("Logger shutdown");
    g_logger->flush();
    spdlog::drop("syncflow");
    g_logger.reset();
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
    const auto logger = ensureLogger("log");

    // If sync-data-only mode is enabled, filter messages
    if (g_sync_data_only && !shouldLog(message)) {
        return;
    }

    if (level == "info") {
        logger->info(message);
    } else if (level == "warn") {
        logger->warn(message);
    } else if (level == "error") {
        logger->error(message);
    } else {
        logger->debug(message);
    }
}

bool Logger::shouldLog(const std::string& message) {
    // Keywords that indicate sync data logs (show these)
    const std::string syncKeywords[] = {
        "sync ", "copy", "removed", "archive", "version", "sync_",
        "Application initialized", "Application started"
    };

    // Keywords that indicate connection logs (hide these)
    const std::string connectionKeywords[] = {
        "Discovery", "TCP", "Device ", "Broadcast", "handshake", "listener",
        "peer", "thread", "Thread", "probe", "announce", "RX", "TX"
    };

    // Check if message contains connection keywords (filter out)
    for (const auto& keyword : connectionKeywords) {
        if (message.find(keyword) != std::string::npos) {
            return false;
        }
    }

    // Check if message contains sync keywords (show)
    for (const auto& keyword : syncKeywords) {
        if (message.find(keyword) != std::string::npos) {
            return true;
        }
    }

    // Default: hide if it looks like operational/generic logs
    if (message.find("initialized") != std::string::npos ||
        message.find("started") != std::string::npos ||
        message.find("stopped") != std::string::npos ||
        message.find("shutting") != std::string::npos) {
        return false;
    }

    // Show everything else by default (errors, warnings about sync issues)
    return true;
}

