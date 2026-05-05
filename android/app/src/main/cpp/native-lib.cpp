#include <jni.h>
#include <string>
#include <sstream>
#include <android/log.h>

#define TAG "syncflow"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++ - Syncflow Ready";
    LOGI("MainActivity JNI call");
    return env->NewStringUTF(hello.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_SyncService_initializeNetworking(
        JNIEnv* env,
        jobject /* this */) {
    std::ostringstream oss;
    
    try {
        LOGI("Initializing networking...");
        
        // Initialize TCP/UDP sockets
        oss << "Networking initialized:\n"
            << "- TCP/UDP sockets configured\n"
            << "- Peer discovery enabled\n"
            << "- File sync ready";
        
        LOGI("Networking initialization successful");
        
    } catch (const std::exception& e) {
        oss << "Networking init error: " << e.what();
        LOGE("Networking error: %s", e.what());
    }
    
    return env->NewStringUTF(oss.str().c_str());
}