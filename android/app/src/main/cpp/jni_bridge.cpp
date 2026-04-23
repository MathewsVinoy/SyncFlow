#include "jni_bridge.hpp"
#include <syncflow/sync_engine.hpp>
#include <syncflow/types.hpp>
#include <android/log.h>
#include <memory>
#include <string>

#define LOG_TAG "SyncFlow"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace syncflow {

static SyncFlowJNIBridge* g_bridge = nullptr;

bool SyncFlowJNIBridge::initialize(JNIEnv* env, JavaVM* vm) {
  if (g_bridge != nullptr) {
    return true;  // Already initialized
  }

  g_bridge = new SyncFlowJNIBridge();
  g_bridge->java_vm_ = vm;
  LOGI("SyncFlow JNI Bridge initialized");
  return true;
}

void SyncFlowJNIBridge::cleanup() {
  if (g_bridge != nullptr) {
    delete g_bridge;
    g_bridge = nullptr;
  }
  LOGI("SyncFlow JNI Bridge cleaned up");
}

SyncFlowJNIBridge& SyncFlowJNIBridge::instance() {
  if (g_bridge == nullptr) {
    g_bridge = new SyncFlowJNIBridge();
  }
  return *g_bridge;
}

std::string SyncFlowJNIBridge::start_sync() {
  try {
    if (!engine_) {
      SyncConfig config;
      config.device_name = "AndroidDevice";
      config.listening_port = 22000;
      engine_ = create_sync_engine(config);
    }

    auto err = engine_->start();
    if (!err.is_success()) {
      LOGE("Failed to start sync: %s", err.message.c_str());
      return err.message;
    }

    LOGI("Sync engine started successfully");
    return "";  // Empty string = success
  } catch (const std::exception& e) {
    LOGE("Exception in start_sync: %s", e.what());
    return std::string(e.what());
  }
}

void SyncFlowJNIBridge::stop_sync() {
  try {
    if (engine_) {
      engine_->shutdown();
      LOGI("Sync engine stopped");
    }
  } catch (const std::exception& e) {
    LOGE("Exception in stop_sync: %s", e.what());
  }
}

std::string SyncFlowJNIBridge::add_sync_folder(const std::string& folder_path) {
  try {
    if (!engine_) {
      return "Engine not initialized";
    }

    auto err = engine_->add_sync_folder(folder_path);
    if (!err.is_success()) {
      LOGE("Failed to add folder %s: %s", folder_path.c_str(), err.message.c_str());
      return err.message;
    }

    LOGI("Added folder: %s", folder_path.c_str());
    return "";
  } catch (const std::exception& e) {
    LOGE("Exception in add_sync_folder: %s", e.what());
    return std::string(e.what());
  }
}

std::string SyncFlowJNIBridge::get_status() const {
  if (!engine_) {
    return "STOPPED";
  }
  return engine_->get_state();
}

int SyncFlowJNIBridge::get_connected_peers_count() const {
  if (!engine_) {
    return 0;
  }
  auto peers = engine_->get_connected_peers();
  return peers.size();
}

int SyncFlowJNIBridge::get_sync_queue_size() const {
  if (!engine_) {
    return 0;
  }
  return engine_->get_sync_queue_size();
}

}  // namespace syncflow

// ============================================================================
// JNI Method Implementations
// ============================================================================

JNIEXPORT jint JNICALL Java_com_syncflow_SyncService_startSync(JNIEnv* env, jobject obj) {
  auto& bridge = syncflow::SyncFlowJNIBridge::instance();
  std::string err = bridge.start_sync();
  return err.empty() ? 0 : -1;
}

JNIEXPORT void JNICALL Java_com_syncflow_SyncService_stopSync(JNIEnv* env, jobject obj) {
  auto& bridge = syncflow::SyncFlowJNIBridge::instance();
  bridge.stop_sync();
}

JNIEXPORT jint JNICALL Java_com_syncflow_SyncService_addSyncFolder(
    JNIEnv* env, jobject obj, jstring folder_path) {
  const char* path_cstr = env->GetStringUTFChars(folder_path, nullptr);
  std::string path(path_cstr);
  env->ReleaseStringUTFChars(folder_path, path_cstr);

  auto& bridge = syncflow::SyncFlowJNIBridge::instance();
  std::string err = bridge.add_sync_folder(path);
  return err.empty() ? 0 : -1;
}

JNIEXPORT jstring JNICALL Java_com_syncflow_SyncService_getStatus(JNIEnv* env, jobject obj) {
  auto& bridge = syncflow::SyncFlowJNIBridge::instance();
  std::string status = bridge.get_status();
  return env->NewStringUTF(status.c_str());
}

JNIEXPORT jint JNICALL Java_com_syncflow_SyncService_getConnectedPeersCount(
    JNIEnv* env, jobject obj) {
  auto& bridge = syncflow::SyncFlowJNIBridge::instance();
  return bridge.get_connected_peers_count();
}

JNIEXPORT jint JNICALL Java_com_syncflow_SyncService_getSyncQueueSize(
    JNIEnv* env, jobject obj) {
  auto& bridge = syncflow::SyncFlowJNIBridge::instance();
  return bridge.get_sync_queue_size();
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return -1;
  }

  if (!syncflow::SyncFlowJNIBridge::initialize(env, vm)) {
    return -1;
  }

  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
  syncflow::SyncFlowJNIBridge::cleanup();
}
