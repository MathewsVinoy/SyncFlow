#pragma once

namespace syncflow {

/**
 * @brief Manages encryption, authentication, and security operations
 */
class SecurityManager {
 public:
  SecurityManager() = default;
  virtual ~SecurityManager() = default;
};

}  // namespace syncflow
