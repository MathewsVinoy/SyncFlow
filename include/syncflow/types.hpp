#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>
#include <map>

namespace syncflow {

// ============================================================================
// Type Aliases
// ============================================================================

using DeviceId = std::string;
using FileId = std::string;
using FileHash = std::vector<uint8_t>;
using PublicKey = std::vector<uint8_t>;
using Timestamp = std::chrono::system_clock::time_point;

// ============================================================================
// Device Information
// ============================================================================

struct DeviceInfo {
  DeviceId device_id;
  std::string name;
  PublicKey public_key;
  std::string capabilities;  // JSON-serialized
  Timestamp last_seen;
  bool trusted = false;
};

// ============================================================================
// File Metadata
// ============================================================================

struct FileMetadata {
  FileId file_id;
  std::string path;
  DeviceId owner_device_id;
  uint64_t size = 0;
  Timestamp modified_time;
  FileHash file_hash;
  bool is_conflicted = false;
  std::string conflict_reason;
};

// ============================================================================
// Chunk Information
// ============================================================================

struct ChunkInfo {
  std::string chunk_id;
  FileId file_id;
  uint64_t offset = 0;
  uint64_t size = 0;
  FileHash hash;  // BLAKE3
  bool is_uploaded = false;
  Timestamp created_at;
};

// ============================================================================
// Version Vector (Causality Tracking)
// ============================================================================

class VersionVector {
 public:
  VersionVector() = default;
  explicit VersionVector(const std::map<DeviceId, uint64_t>& clocks);

  uint64_t get(const DeviceId& device) const;
  void increment(const DeviceId& device);
  void update(const DeviceId& device, uint64_t clock);

  bool happens_before(const VersionVector& other) const;
  bool concurrent_with(const VersionVector& other) const;
  bool equal_to(const VersionVector& other) const;

  std::string to_string() const;
  static VersionVector from_string(const std::string& str);

  const std::map<DeviceId, uint64_t>& clocks() const { return clocks_; }

 private:
  std::map<DeviceId, uint64_t> clocks_;
};

// ============================================================================
// Sync Queue Entry
// ============================================================================

enum class SyncOperation {
  UPLOAD,
  DOWNLOAD,
  DELETE,
};

enum class SyncStatus {
  PENDING,
  IN_PROGRESS,
  FAILED,
  COMPLETED,
};

struct SyncQueueEntry {
  uint64_t id = 0;
  FileId file_id;
  DeviceId target_device;
  SyncOperation operation = SyncOperation::UPLOAD;
  SyncStatus status = SyncStatus::PENDING;
  uint32_t retry_count = 0;
  Timestamp last_retry;
  std::string error_message;
  Timestamp created_at;
};

// ============================================================================
// Sync Checkpoint (for crash recovery)
// ============================================================================

struct SyncCheckpoint {
  uint64_t bytes_sent = 0;
  std::vector<uint64_t> completed_chunks;
  std::string last_error;
  Timestamp updated_at;

  std::string to_json() const;
  static SyncCheckpoint from_json(const std::string& json);
};

// ============================================================================
// Conflict Information
// ============================================================================

enum class ConflictType {
  CONCURRENT_EDIT,
  VERSION_MISMATCH,
  MALICIOUS_CONTENT,
  CORRUPTED_DATA,
};

struct SyncConflict {
  FileId file_id;
  std::string path;
  ConflictType type = ConflictType::CONCURRENT_EDIT;
  std::string reason;
  Timestamp detected_at;
  DeviceId conflicting_device;
  std::optional<std::string> local_version_path;
  std::optional<std::string> remote_version_path;
};

// ============================================================================
// Sync Statistics
// ============================================================================

struct SyncStatistics {
  uint64_t total_files = 0;
  uint64_t synced_files = 0;
  uint64_t bytes_transferred = 0;
  uint64_t conflicted_files = 0;
  uint32_t active_peers = 0;
  Timestamp last_sync_time;
  double avg_transfer_rate = 0.0;  // bytes/sec
};

// ============================================================================
// Peer Connection State
// ============================================================================

enum class ConnectionState {
  IDLE,
  DISCOVERING,
  CONNECTING,
  HANDSHAKING,
  CONNECTED,
  RECONNECTING,
  CLOSED,
  ERROR,
};

// ============================================================================
// Configuration Structure
// ============================================================================

struct SyncConfig {
  std::string device_name = "MyDevice";
  uint32_t listening_port = 22000;
  uint32_t max_peers = 10;
  uint32_t chunk_size = 16384;  // 16KB
  uint64_t max_bandwidth_upload = 0;     // 0 = unlimited
  uint64_t max_bandwidth_download = 0;
  uint32_t sync_interval_sec = 5;
  uint32_t retry_max_attempts = 5;
  uint32_t retry_backoff_base_ms = 1000;
  bool enable_tls = true;
  bool enable_file_encryption = false;
  std::string db_path = "~/.local/share/syncflow/db";
  std::string key_store_path = "~/.local/share/syncflow/keys";
};

// ============================================================================
// Error Handling
// ============================================================================

enum class ErrorCode {
  SUCCESS = 0,
  NETWORK_ERROR = 1,
  DATABASE_ERROR = 2,
  CRYPTO_ERROR = 3,
  FILE_IO_ERROR = 4,
  SYNC_ERROR = 5,
  AUTHENTICATION_FAILED = 6,
  TIMEOUT = 7,
  INVALID_ARGUMENT = 8,
  NOT_FOUND = 9,
  PERMISSION_DENIED = 10,
  UNKNOWN_ERROR = 255,
};

struct Error {
  ErrorCode code = ErrorCode::UNKNOWN_ERROR;
  std::string message;
  std::optional<std::string> details = std::nullopt;

  bool is_success() const { return code == ErrorCode::SUCCESS; }
  std::string to_string() const;

  static Error success() { return Error{ErrorCode::SUCCESS, "", std::nullopt}; }
};

}  // namespace syncflow
