#pragma once

namespace syncflow {

/**
 * @brief Manages networking operations, peer connections, and data transfer
 */
class NetworkingStack {
 public:
  NetworkingStack() = default;
  virtual ~NetworkingStack() = default;
};

}  // namespace syncflow
