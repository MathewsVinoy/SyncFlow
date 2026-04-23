#pragma once

#include <jni.h>
#include <memory>
#include <string>

namespace syncflow {

class SyncEngine;

/**
 * @brief JNI Bridge: C++ ↔ Java/Kotlin interface for SyncFlow
 *
 * Manages lifecycle of SyncEngine and provides JNI method implementations
 * for Android UI to control sync operations.
 */
class SyncFlowJNIBridge {
 public:
  /**
   * @brief Initialize JNI bridge (called from JNI_OnLoad)
   * @param env JNI environment pointer
   * @param vm Java VM pointer
   * @return true if initialization successful
   */
  static bool initialize(JNIEnv* env, JavaVM* vm);

  /**
   * @brief Cleanup JNI bridge (called from JNI_OnUnload)
   */
  static void cleanup();

  /**
   * @brief Get singleton instance
   * @return Reference to bridge instance
   */
  static SyncFlowJNIBridge& instance();

  /**
   * @brief Start sync engine
   * @return Error message if failed, empty string on success
   */
  std::string start_sync();

  /**
   * @brief Stop sync engine
   */
  void stop_sync();

  /**
   * @brief Add folder to sync
   * @param folder_path Absolute path to folder
   * @return Error message if failed, empty on success
   */
  std::string add_sync_folder(const std::string& folder_path);

  /**
   * @brief Get engine status
   * @return Status string (IDLE, SYNCING, ERROR, etc.)
   */
  std::string get_status() const;

  /**
   * @brief Get connected peers count
   */
  int get_connected_peers_count() const;

  /**
   * @brief Get pending sync queue size
   */
  int get_sync_queue_size() const;

 private:
  SyncFlowJNIBridge() = default;
  ~SyncFlowJNIBridge() = default;

  std::unique_ptr<SyncEngine> engine_;
  JavaVM* java_vm_ = nullptr;
};

}  // namespace syncflow

// ============================================================================
// JNI Method Declarations (C-compatible extern "C")
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief com.syncflow.SyncService#startSync()
 */
JNIEXPORT jint JNICALL Java_com_syncflow_SyncService_startSync(JNIEnv* env, jobject obj);

/**
 * @brief com.syncflow.SyncService#stopSync()
 */
JNIEXPORT void JNICALL Java_com_syncflow_SyncService_stopSync(JNIEnv* env, jobject obj);

/**
 * @brief com.syncflow.SyncService#addSyncFolder(String folderPath)
 */
JNIEXPORT jint JNICALL Java_com_syncflow_SyncService_addSyncFolder(
    JNIEnv* env, jobject obj, jstring folder_path);

/**
 * @brief com.syncflow.SyncService#getStatus()
 */
JNIEXPORT jstring JNICALL Java_com_syncflow_SyncService_getStatus(JNIEnv* env, jobject obj);

/**
 * @brief com.syncflow.SyncService#getConnectedPeersCount()
 */
JNIEXPORT jint JNICALL Java_com_syncflow_SyncService_getConnectedPeersCount(
    JNIEnv* env, jobject obj);

/**
 * @brief com.syncflow.SyncService#getSyncQueueSize()
 */
JNIEXPORT jint JNICALL Java_com_syncflow_SyncService_getSyncQueueSize(
    JNIEnv* env, jobject obj);

/**
 * @brief JNI_OnLoad: Called when native library is loaded
 */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved);

/**
 * @brief JNI_OnUnload: Called when native library is unloaded
 */
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved);

#ifdef __cplusplus
}
#endif
