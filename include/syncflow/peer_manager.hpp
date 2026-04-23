#pragma once

namespace syncflow {

/**
 * @brief Manages peer connections and device discovery
 */
class PeerManager {
 public:
  PeerManager() = default;
  virtual ~PeerManager() = default;
};

}  // namespace syncflow
