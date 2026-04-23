#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include "types.hpp"

namespace syncflow {

// Forward declarations
class PeerManager;
class StorageLayer;
class SecurityManager;
class NetworkingStack;

// ============================================================================
// Main Sync Engine Interface
// ============================================================================

/**
 * @brief Main entry point for SyncFlow P2P synchronization.
 *
 * Orchestrates file sync across multiple peer devices. Manages lifecycle,
 * connection handling, conflict resolution, and offline-first sync queue.
 */
class SyncEngine {
 public:
  /**
   * @brief Constructor
   * @param config Configuration struct with network, storage, and sync settings
   */
  explicit SyncEngine(const SyncConfig& config);
  
  /// Destructor - defined in sync_engine.cpp to allow unique_ptr of forward-decl types
  ~SyncEngine();

  // Non-copyable, movable
  SyncEngine(const SyncEngine&) = delete;
  SyncEngine& operator=(const SyncEngine&) = delete;
  SyncEngine(SyncEngine&&) noexcept = default;
  SyncEngine& operator=(SyncEngine&&) noexcept = default;

  /**
   * @brief Initialize SyncEngine: load config, check keystore, verify database
   * @return Error code; check error.is_success()
   */
  Error initialize();

  /**
   * @brief Start sync operations and peer discovery
   * @return Error on startup failure (network, database, etc.)
   */
  Error start();

  /**
   * @brief Gracefully shut down sync operations
   */
  void shutdown();

  /**
   * @brief Add a folder to sync
   * @param folder_path Local filesystem path to sync
   * @return Error if folder doesn't exist or already synced
   */
  Error add_sync_folder(const std::string& folder_path);

  /**
   * @brief Add a trusted peer device
   * @param device_id Unique device identifier
   * @param name Human-readable device name
   * @param public_key Device's Ed25519 public key
   * @return Error if device already exists or key invalid
   */
  Error add_peer_device(const DeviceId& device_id,
                        const std::string& name,
                        const PublicKey& public_key);

  /**
   * @brief Remove a peer device from trusted list
   * @param device_id Device to remove
   * @return Error if not found
   */
  Error remove_peer_device(const DeviceId& device_id);

  /**
   * @brief Manually trigger sync for a folder with a specific peer
   * @param folder_path Folder to sync
   * @param peer_device_id Peer to sync with
   * @return Error if folder or peer not found
   */
  Error sync_folder_now(const std::string& folder_path,
                        const DeviceId& peer_device_id);

  /**
   * @brief Pause all sync operations
   */
  void pause_sync();

  /**
   * @brief Resume sync operations
   */
  void resume_sync();

  /**
   * @brief Resolve a file conflict by selecting winner
   * @param file_id ID of conflicted file
   * @param winner_device_id Device whose version wins
   * @return Error if conflict not found or resolution fails
   */
  Error resolve_conflict(const FileId& file_id,
                         const DeviceId& winner_device_id);

  /**
   * @brief Get current sync statistics
   * @return Sync statistics (files synced, bytes transferred, etc.)
   */
  SyncStatistics get_statistics() const;

  /**
   * @brief Get information about a connected peer
   * @param device_id Peer device ID
   * @return Device info if connected, nullopt otherwise
   */
  std::optional<DeviceInfo> get_peer_info(const DeviceId& device_id) const;

  /**
   * @brief Get list of connected peers
   * @return Vector of device IDs currently connected
   */
  std::vector<DeviceId> get_connected_peers() const;

  /**
   * @brief Callback type for sync events
   */
  using SyncEventCallback = std::function<void(const std::string& event_type,
                                               const std::string& data)>;

  /**
   * @brief Register callback for sync events
   * @param callback Function invoked on sync events
   */
  void register_sync_callback(SyncEventCallback callback);

  /**
   * @brief Get current engine state
   * @return Current state (IDLE, DISCOVERING, SYNCING, etc.)
   */
  std::string get_state() const;

  /**
   * @brief Get this device's ID (generated on first run)
   * @return Unique device ID
   */
  DeviceId get_local_device_id() const;

  /**
   * @brief Get pending sync queue size
   * @return Number of files waiting to sync
   */
  size_t get_sync_queue_size() const;

  /**
   * @brief Clear all unsynced files from queue
   */
  void clear_sync_queue();

 private:
  SyncConfig config_;
  std::unique_ptr<PeerManager> peer_manager_;
  std::unique_ptr<StorageLayer> storage_;
  std::unique_ptr<SecurityManager> security_;
  std::unique_ptr<NetworkingStack> networking_;
  DeviceId local_device_id_;
  bool is_running_ = false;
  bool is_paused_ = false;

  SyncEventCallback event_callback_;

  // Internal state machine
  void run_event_loop();
  Error recover_from_crash();
  void process_sync_queue();
  void handle_peer_discovery();
};

/**
 * @brief Create a new SyncEngine with default configuration
 */
std::unique_ptr<SyncEngine> create_sync_engine(const SyncConfig& config);

}  // namespace syncflow
