#include <jni.h>

#include <android/log.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {

std::mutex g_mutex;
std::string g_status_json;
std::thread g_peer_thread;
std::atomic<bool> g_running{false};

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

void simulate_peer() {
    __android_log_print(ANDROID_LOG_INFO, "syncflow", "Native peer simulation started");

    int connection_count = 0;
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // Simulate discovering a peer
        if (++connection_count % 5 == 0) {
            std::lock_guard<std::mutex> guard(g_mutex);
            std::string peer_name = "peer-" + std::to_string(connection_count / 5);
            std::string peer_ip = "192.168.1." + std::to_string(100 + (connection_count / 5));

            g_status_json = R"({
  "running": true,
  "device_name": "android-phone",
  "local_ip": "0.0.0.0",
  "connections": [")" + peer_name + "@" + peer_ip + R"(:45455"]
})";

            __android_log_print(ANDROID_LOG_INFO, "syncflow", "Discovered peer: %s at %s",
                                peer_name.c_str(), peer_ip.c_str());
        }
    }

    __android_log_print(ANDROID_LOG_INFO, "syncflow", "Native peer simulation stopped");
}

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_syncflow_NativeBridge_startPeer(JNIEnv* env, jobject /* this */, jstring configPath) {
    const std::string cfg = jstring_to_utf8(env, configPath);

    std::lock_guard<std::mutex> guard(g_mutex);
    if (g_running.load()) {
        return;
    }

    __android_log_print(ANDROID_LOG_INFO, "syncflow", "Starting peer with config: %s", cfg.c_str());

    g_running.store(true);
    g_status_json = R"({
  "running": true,
  "device_name": "android-phone",
  "local_ip": "0.0.0.0",
  "connections": []
})";

    g_peer_thread = std::thread(simulate_peer);
}

extern "C" JNIEXPORT void JNICALL
Java_com_syncflow_NativeBridge_stopPeer(JNIEnv* /* env */, jobject /* this */) {
    std::lock_guard<std::mutex> guard(g_mutex);

    __android_log_print(ANDROID_LOG_INFO, "syncflow", "Stopping peer");

    g_running.store(false);
    if (g_peer_thread.joinable()) {
        g_peer_thread.join();
    }

    g_status_json = R"({"running":false,"connections":[]})";
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_NativeBridge_getStatus(JNIEnv* env, jobject /* this */) {
    std::lock_guard<std::mutex> guard(g_mutex);

    if (g_status_json.empty()) {
        return env->NewStringUTF(R"({"running":false,"connections":[]})");
    }

    return env->NewStringUTF(g_status_json.c_str());
}
