#pragma once

#include <cstdint>
#include <string>

namespace syncflow::networking {

struct PeerInfo {
    std::string magic;
    std::string name;
    std::string ip;
    std::uint16_t tcp_port = 0;
};

std::string serialize_peer_info(const PeerInfo& peer);
bool parse_peer_info(const std::string& line, PeerInfo& peer);
std::string endpoint_key(const PeerInfo& peer);

}  // namespace syncflow::networking
