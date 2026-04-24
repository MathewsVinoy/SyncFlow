#include "jni_bridge.hpp"
#include <android/log.h>
#include <string>

#define LOG_TAG "SyncFlow"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace syncflow {

static SyncFlowJNIBridge* g_bridge = nullptr;

bool SyncFlowJNIBridge::initialize(JNIEnv* env, JavaVM* vm) {
  if (g_bridge != nullptr) {
    return true;
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
  status_ = "SYNCING";
  LOGI("Sync started");
  return "";
}

void SyncFlowJNIBridge::stop_sync() {
  status_ = "IDLE";
  LOGI("Sync stopped");
}

std::string SyncFlowJNIBridge::add_sync_folder(const std::string& folder_path) {
  LOGI("Added folder: %s", folder_path.c_str());
  return "";
}

std::string SyncFlowJNIBridge::get_status() const {
  return status_;
}

int SyncFlowJNIBridge::get_connected_peers_count() const {
  return 0;
}

int SyncFlowJNIBridge::get_sync_queue_size() const {
  return 0;
}

}  // namespace syncflow

// ============================================================================
// JNI Method Implementations
// ============================================================================

extern "C" {

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

}  // extern "C"
