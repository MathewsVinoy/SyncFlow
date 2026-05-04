#include <jni.h>
#include <string>
#include <atomic>

static std::atomic<bool> running{false};

extern "C" JNIEXPORT jint JNICALL
Java_com_syncflow_SyncService_nativeStart(JNIEnv* env, jobject /* this */, jstring configPath) {
    const char* p = env->GetStringUTFChars(configPath, nullptr);
    // TODO: integrate with real syncflow core on Android (NDK build)
    (void)p;
    env->ReleaseStringUTFChars(configPath, p);
    running = true;
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_syncflow_SyncService_nativeStop(JNIEnv* env, jobject /* this */) {
    running = false;
    return 0;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_DeviceInfoActivity_nativeGetStatus(JNIEnv* env, jobject /* this */) {
    std::string s = running ? "Running (native stub)" : "Stopped";
    return env->NewStringUTF(s.c_str());
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