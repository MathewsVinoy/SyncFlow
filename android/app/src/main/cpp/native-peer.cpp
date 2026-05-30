#include <jni.h>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <android/log.h>

#include "syncflow/networking/peer_node.h"

static std::unique_ptr<syncflow::networking::PeerNode> g_node;
static std::thread g_node_thread;
static std::mutex g_node_mutex;

static void node_runner() {
    if (g_node) {
        try {
            g_node->run();
        } catch (const std::exception& e) {
            __android_log_print(ANDROID_LOG_ERROR, "SyncFlowNative", "PeerNode run exception: %s", e.what());
        } catch (...) {
            __android_log_print(ANDROID_LOG_ERROR, "SyncFlowNative", "PeerNode run unknown exception");
        }
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_syncflow_SyncNative_startPeer(JNIEnv* env, jclass /*cls*/, jstring jdevice, jstring jconfigPath) {
    const char* device = jdevice ? env->GetStringUTFChars(jdevice, nullptr) : nullptr;
    const char* cfg = jconfigPath ? env->GetStringUTFChars(jconfigPath, nullptr) : nullptr;

    std::lock_guard<std::mutex> guard(g_node_mutex);
    if (g_node) {
        // already running
        if (device) env->ReleaseStringUTFChars(jdevice, device);
        if (cfg) env->ReleaseStringUTFChars(jconfigPath, cfg);
        return JNI_FALSE;
    }

    try {
        std::string dev = device ? device : std::string();
        std::string conf = cfg ? cfg : std::string();
        g_node = std::make_unique<syncflow::networking::PeerNode>(dev, conf.empty() ? std::filesystem::path() : std::filesystem::path(conf));
        g_node_thread = std::thread(node_runner);
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "SyncFlowNative", "startPeer exception: %s", e.what());
        if (device) env->ReleaseStringUTFChars(jdevice, device);
        if (cfg) env->ReleaseStringUTFChars(jconfigPath, cfg);
        return JNI_FALSE;
    }

    if (device) env->ReleaseStringUTFChars(jdevice, device);
    if (cfg) env->ReleaseStringUTFChars(jconfigPath, cfg);
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_syncflow_SyncNative_stopPeer(JNIEnv* env, jclass /*cls*/) {
    std::lock_guard<std::mutex> guard(g_node_mutex);
    if (!g_node) return JNI_FALSE;

    try {
        g_node->stop();
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "SyncFlowNative", "stopPeer stop() exception: %s", e.what());
    }

    if (g_node_thread.joinable()) {
        g_node_thread.join();
    }

    g_node.reset();
    return JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_SyncNative_statusSummary(JNIEnv* env, jclass /*cls*/) {
    std::lock_guard<std::mutex> guard(g_node_mutex);
    if (!g_node) return env->NewStringUTF("");

    try {
        const std::string s = g_node->status_summary();
        return env->NewStringUTF(s.c_str());
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "SyncFlowNative", "statusSummary exception: %s", e.what());
        return env->NewStringUTF("");
    }
}
