#include <jni.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "syncflow/networking/peer_node.h"

namespace {

std::mutex g_mutex;
std::shared_ptr<syncflow::networking::PeerNode> g_peer;
std::thread g_peer_thread;

std::string jstring_to_utf8(JNIEnv* env, jstring value) {
    if (value == nullptr) {
        return {};
    }

    const char* raw = env->GetStringUTFChars(value, nullptr);
    std::string result = raw ? raw : "";
    if (raw != nullptr) {
        env->ReleaseStringUTFChars(value, raw);
    }
    return result;
}

void join_peer_thread() {
    if (g_peer_thread.joinable()) {
        g_peer_thread.join();
    }
}

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_syncflow_NativeBridge_startPeer(JNIEnv* env, jobject /* this */, jstring configPath) {
    const std::string cfg = jstring_to_utf8(env, configPath);

    std::lock_guard<std::mutex> guard(g_mutex);
    if (g_peer != nullptr) {
        return;
    }

    g_peer = std::make_shared<syncflow::networking::PeerNode>("", std::filesystem::path(cfg));
    auto peer = g_peer;
    g_peer_thread = std::thread([peer]() {
        try {
            peer->run();
        } catch (...) {
            // keep JNI stable; errors are surfaced in logcat by the peer itself
        }
    });
}

extern "C" JNIEXPORT void JNICALL
Java_com_syncflow_NativeBridge_stopPeer(JNIEnv* /* env */, jobject /* this */) {
    std::shared_ptr<syncflow::networking::PeerNode> peer;
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        peer.swap(g_peer);
    }

    if (peer != nullptr) {
        peer->stop();
    }

    std::lock_guard<std::mutex> guard(g_mutex);
    join_peer_thread();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_NativeBridge_getStatus(JNIEnv* env, jobject /* this */) {
    std::shared_ptr<syncflow::networking::PeerNode> peer;
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        peer = g_peer;
    }

    if (peer == nullptr) {
        return env->NewStringUTF("{\"running\":false,\"connections\":[]}");
    }

    const std::string summary = peer->status_summary();
    return env->NewStringUTF(summary.c_str());
}
