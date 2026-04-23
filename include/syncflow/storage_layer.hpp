#pragma once

namespace syncflow {

/**
 * @brief Handles persistent storage and database operations
 */
class StorageLayer {
 public:
  StorageLayer() = default;
  virtual ~StorageLayer() = default;
};

}  // namespace syncflow
