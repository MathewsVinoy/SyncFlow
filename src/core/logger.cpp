#include "syncflow/logging.h"

#include "syncflow/platform/system_info.h"

#include <iostream>

namespace syncflow {

Logger::Logger(std::string device_name, std::string ip_address)
    : device_name_(std::move(device_name)), ip_address_(std::move(ip_address)) {}

void Logger::info(const std::string& message) {
    std::lock_guard<std::mutex> guard(mutex_);
    std::cout << "[" << platform::timestamp_now() << "] "
              << "[device=" << device_name_ << "] "
              << "[ip=" << ip_address_ << "] "
              << message << std::endl;
}

}  // namespace syncflow
