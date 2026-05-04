#include <jni.h>
#include <string>
#include <thread>
#include <atomic>
#include <fstream>
#include <android/log.h>

static std::atomic<bool> g_running{false};
static std::thread g_worker;

extern "C" JNIEXPORT void JNICALL
Java_com_syncflow_NativeBridge_startPeer(JNIEnv* env, jobject /* this */, jstring configPath) {
    const char* path = env->GetStringUTFChars(configPath, nullptr);
    std::string cfg(path ? path : "");
    env->ReleaseStringUTFChars(configPath, path);

    if (g_running.load()) return;
    g_running.store(true);
    g_worker = std::thread([cfg]() {
        __android_log_print(ANDROID_LOG_INFO, "syncflow", "native peer started using %s", cfg.c_str());
        const std::string statusFile = cfg.substr(0, cfg.find_last_of("/\\")) + "/sync_status.json";
        while (g_running.load()) {
            std::ofstream out(statusFile, std::ofstream::trunc);
            out << "{\n  \"peers\": [\n    { \"name\": \"peer-1\", \"ip\": \"192.0.2.1\" }\n  ]\n}\n";
            out.flush();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        __android_log_print(ANDROID_LOG_INFO, "syncflow", "native peer stopped");
    });
}

extern "C" JNIEXPORT void JNICALL
Java_com_syncflow_NativeBridge_stopPeer(JNIEnv* env, jobject /* this */) {
    if (!g_running.load()) return;
    g_running.store(false);
    if (g_worker.joinable()) g_worker.join();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_NativeBridge_getStatus(JNIEnv* env, jobject /* this */) {
    const char* s = g_running.load() ? "running" : "stopped";
    return env->NewStringUTF(s);
}
#include <jni.h>
#include <string>

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}