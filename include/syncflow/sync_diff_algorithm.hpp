#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include "types.hpp"

namespace syncflow {

// ============================================================================
// Sync Diff Algorithm
// ============================================================================

/**
 * @brief Computes minimal delta between local and remote file state.
 *
 * Algorithm:
 * 1. Receive remote file metadata (name, size, mod_time, version_vector, hash)
 * 2. Compare with local file:
 *    a) Doesn't exist locally → NEED_DOWNLOAD
 *    b) Same hash → SYNCED
 *    c) Remote newer by VV → NEED_DOWNLOAD
 *    d) Local newer by VV → NEED_UPLOAD
 *    e) Concurrent changes → CONFLICT
 * 3. For large files: chunk-level comparison (only transfer changed chunks)
 */
class SyncDiffAlgorithm {
 public:
  enum class DiffResult {
    SYNCED,          // Already synchronized
    NEED_UPLOAD,     // Local is newer, send to remote
    NEED_DOWNLOAD,   // Remote is newer, pull from remote
    CONFLICT,        // Concurrent modifications (requires manual resolution)
    NOT_FOUND,       // Remote doesn't exist, local exists → upload
    DELETED,         // Local doesn't exist, remote exists → download or delete
  };

  /**
   * @brief Compute sync direction for a single file
   *
   * @param local_metadata Local file metadata (nullopt if not present locally)
   * @param remote_metadata Remote file metadata
   * @param local_vv Local device's version vector
   * @param remote_vv Remote device's version vector
   * @return DiffResult indicating required action
   */
  static DiffResult compute_diff(
      const std::optional<FileMetadata>& local_metadata,
      const std::optional<FileMetadata>& remote_metadata,
      const VersionVector& local_vv,
      const VersionVector& remote_vv);

  /**
   * @brief Compute chunk-level delta for large files
   *
   * When files are large (> 10MB), chunking improves efficiency.
   * This returns which chunks need to be transferred.
   *
   * @param file_path Local file path
   * @param local_chunks Local chunk hashes
   * @param remote_chunks Remote chunk hashes
   * @return Indices of chunks that need transfer
   */
  static std::vector<uint64_t> compute_chunk_delta(
      const std::string& file_path,
      const std::vector<FileHash>& local_chunks,
      const std::vector<FileHash>& remote_chunks);

  /**
   * @brief Estimate transfer size for delta sync
   * @param delta_chunks Chunk indices to transfer
   * @param chunk_size Size of each chunk (bytes)
   * @return Estimated bytes to transfer
   */
  static uint64_t estimate_transfer_size(const std::vector<uint64_t>& delta_chunks,
                                          uint64_t chunk_size);
};

}  // namespace syncflow
