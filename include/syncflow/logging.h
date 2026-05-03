#pragma once

#include <mutex>
#include <string>

namespace syncflow {

class Logger {
public:
    Logger(std::string device_name, std::string ip_address);

    void info(const std::string& message);

private:
    std::string device_name_;
    std::string ip_address_;
    std::mutex mutex_;
};

}  // namespace syncflow
