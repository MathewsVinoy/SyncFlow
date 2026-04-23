#include "syncflow/sync_diff_algorithm.hpp"
#include <algorithm>

namespace syncflow {

SyncDiffAlgorithm::DiffResult SyncDiffAlgorithm::compute_diff(
    const std::optional<FileMetadata>& local_metadata,
    const std::optional<FileMetadata>& remote_metadata,
    const VersionVector& local_vv,
    const VersionVector& remote_vv) {
  
  // Both don't exist: SYNCED
  if (!local_metadata && !remote_metadata) {
    return DiffResult::SYNCED;
  }

  // Remote doesn't exist, local does: NEED_UPLOAD
  if (local_metadata && !remote_metadata) {
    return DiffResult::NOT_FOUND;
  }

  // Local doesn't exist, remote does: NEED_DOWNLOAD
  if (!local_metadata && remote_metadata) {
    return DiffResult::DELETED;
  }

  // Both exist: compare
  if (local_metadata && remote_metadata) {
    // Same hash: SYNCED
    if (local_metadata->file_hash == remote_metadata->file_hash) {
      return DiffResult::SYNCED;
    }

    // Compare version vectors to determine causality
    bool local_newer = local_vv.happens_before(remote_vv);
    bool remote_newer = remote_vv.happens_before(local_vv);
    bool concurrent = !local_newer && !remote_newer;

    if (concurrent) {
      return DiffResult::CONFLICT;
    }

    if (remote_newer) {
      return DiffResult::NEED_DOWNLOAD;
    }

    if (local_newer) {
      return DiffResult::NEED_UPLOAD;
    }
  }

  // Default: assume conflict
  return DiffResult::CONFLICT;
}

std::vector<uint64_t> SyncDiffAlgorithm::compute_chunk_delta(
    const std::string& file_path [[maybe_unused]],
    const std::vector<FileHash>& local_chunks,
    const std::vector<FileHash>& remote_chunks) {
  
  std::vector<uint64_t> delta_chunks;

  // If remote has more chunks, download all
  if (remote_chunks.size() > local_chunks.size()) {
    for (uint64_t i = 0; i < remote_chunks.size(); ++i) {
      delta_chunks.push_back(i);
    }
    return delta_chunks;
  }

  // Compare chunk-by-chunk
  for (uint64_t i = 0; i < local_chunks.size() && i < remote_chunks.size(); ++i) {
    if (local_chunks[i] != remote_chunks[i]) {
      delta_chunks.push_back(i);
    }
  }

  return delta_chunks;
}

uint64_t SyncDiffAlgorithm::estimate_transfer_size(
    const std::vector<uint64_t>& delta_chunks,
    uint64_t chunk_size) {
  return delta_chunks.size() * chunk_size;
}

}  // namespace syncflow
