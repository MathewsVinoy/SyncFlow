#include "syncflow/networking/peer_protocol.h"

#include "syncflow/config.h"

#include <sstream>
#include <vector>

namespace syncflow::networking {

std::string serialize_peer_info(const PeerInfo& peer) {
    std::ostringstream oss;
    oss << peer.magic << '|' << peer.name << '|' << peer.ip << '|' << peer.tcp_port << '\n';
    return oss.str();
}

bool parse_peer_info(const std::string& line, PeerInfo& peer) {
    std::vector<std::string> parts;
    std::string current;

    for (char c : line) {
        if (c == '|') {
            parts.push_back(current);
            current.clear();
        } else if (c != '\r' && c != '\n') {
            current.push_back(c);
        }
    }

    if (!current.empty()) {
        parts.push_back(current);
    }

    if (parts.size() != 4 || parts[0] != syncflow::config::kMagic) {
        return false;
    }

    peer.magic = parts[0];
    peer.name = parts[1];
    peer.ip = parts[2];

    try {
        peer.tcp_port = static_cast<std::uint16_t>(std::stoi(parts[3]));
    } catch (...) {
        return false;
    }

    return true;
}

std::string endpoint_key(const PeerInfo& peer) {
    return peer.name + "@" + peer.ip + ":" + std::to_string(peer.tcp_port);
}

}  // namespace syncflow::networking
