// include/syncflow/types.h
// Core types and structures for the synchronization system

#ifndef SYNCFLOW_TYPES_H
#define SYNCFLOW_TYPES_H

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <functional>

namespace syncflow {

// Forward declarations
class Device;
class FileTransfer;
class SyncSession;

// ============================================================================
// BASIC TYPES
// ============================================================================

using DeviceID = std::string;      // Unique device identifier (MAC + hostname)
using FileID = std::string;        // Hash-based file identifier
using SessionID = std::string;     // Transfer session identifier
using ChunkID = uint32_t;          // Chunk sequence number

// ============================================================================
// ENUMERATIONS
// ============================================================================

enum class PlatformType : uint8_t {
    UNKNOWN = 0,
    WINDOWS = 1,
    LINUX = 2,
    MACOS = 3,
    ANDROID = 4,
};

enum class TransferState : uint8_t {
    IDLE = 0,
    CONNECTING = 1,
    TRANSFERRING = 2,
    PAUSED = 3,
    COMPLETED = 4,
    FAILED = 5,
    CANCELLED = 6,
};

enum class FileChangeType : uint8_t {
    CREATED = 0,
    MODIFIED = 1,
    DELETED = 2,
    RENAMED = 3,
};

enum class ConflictResolution : uint8_t {
    OVERWRITE = 0,
    SKIP = 1,
    VERSION = 2,      // Create timestamped version
    ASK_USER = 3,
};

// ============================================================================
// PROTOCOL CONSTANTS
// ============================================================================

constexpr uint16_t DISCOVERY_PORT = 15947;
constexpr uint16_t TRANSFER_PORT = 15948;
constexpr size_t CHUNK_SIZE = 1024 * 1024;        // 1MB chunks
constexpr size_t MAX_CONCURRENT_TRANSFERS = 4;
constexpr uint32_t HANDSHAKE_MAGIC = 0x5346414E; // "SFAN"
constexpr uint32_t PROTOCOL_VERSION = 1;

// Discovery broadcast interval (milliseconds)
constexpr int32_t DISCOVERY_INTERVAL_MS = 5000;
constexpr int32_t DISCOVERY_TIMEOUT_MS = 15000;

// ============================================================================
// STRUCTURES
// ============================================================================

/**
 * Device information
 */
struct DeviceInfo {
    DeviceID id;
    std::string name;
    std::string hostname;
    PlatformType platform;
    std::string ip_address;
    uint16_t port;
    std::string version;
    std::chrono::system_clock::time_point last_seen;
    
    bool operator==(const DeviceInfo& other) const {
        return id == other.id;
    }
};

/**
 * File metadata for synchronization
 */
struct FileMetadata {
    std::string path;              // Relative path within sync folder
    FileID id;                     // Content hash for deduplication
    uint64_t size;
    std::chrono::system_clock::time_point modified_time;
    uint32_t crc32;                // For corruption detection
    bool is_directory;
    std::vector<uint8_t> permissions;
};

/**
 * Chunk information for transfer
 */
struct ChunkInfo {
    ChunkID id;
    uint64_t offset;
    size_t size;
    uint32_t crc32;
    bool is_compressed;
};

/**
 * Transfer session metadata
 */
struct TransferSessionInfo {
    SessionID id;
    std::string file_path;
    uint64_t total_size;
    std::vector<ChunkInfo> chunks;
    uint32_t concurrent_streams;
    TransferState state;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point pause_time;
};

/**
 * Sync folder configuration
 */
struct SyncFolderConfig {
    std::string local_path;
    std::string remote_device_id;
    std::string remote_path;
    bool bidirectional;
    ConflictResolution conflict_strategy;
    bool enable_compression;
    bool enable_incremental;
};

/**
 * File change event
 */
struct FileChangeEvent {
    FileChangeType type;
    std::string path;
    std::chrono::system_clock::time_point timestamp;
    uint64_t size;
    std::string old_path;  // For rename operations
};

/**
 * Network statistics
 */
struct NetworkStats {
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t total_files_transferred;
    std::chrono::duration<double> total_transfer_time;
    double average_speed_mbps;
};

// ============================================================================
// CALLBACK TYPES
// ============================================================================

using OnDeviceDiscovered = std::function<void(const DeviceInfo&)>;
using OnDeviceLost = std::function<void(const DeviceID&)>;
using OnTransferProgress = std::function<void(const SessionID&, uint64_t, uint64_t)>;
using OnTransferComplete = std::function<void(const SessionID&, bool)>;
using OnFileChange = std::function<void(const FileChangeEvent&)>;
using OnConflictDetected = std::function<void(const std::string&, const FileMetadata&, const FileMetadata&)>;

} // namespace syncflow

#endif // SYNCFLOW_TYPES_H
