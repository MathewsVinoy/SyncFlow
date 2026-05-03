#pragma once

#include <atomic>
#include <string>

namespace syncflow::platform {

std::string get_hostname();
std::string get_local_ipv4();
std::string timestamp_now();
void install_signal_handlers(std::atomic_bool& running);

}  // namespace syncflow::platform
