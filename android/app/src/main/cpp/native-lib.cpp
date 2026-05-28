#include <jni.h>
#include <string>
#include "syncflow/networking/peer_node.h"

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_MainActivity_getPeerStatus(
        JNIEnv* env,
        jobject /* this */) {
    try {
        syncflow::networking::PeerNode node("", {});
        const std::string status = node.status_summary();
        return env->NewStringUTF(status.c_str());
    } catch (...) {
        const char* err = "{\"error\":\"native_exception\"}";
        return env->NewStringUTF(err);
    }
}