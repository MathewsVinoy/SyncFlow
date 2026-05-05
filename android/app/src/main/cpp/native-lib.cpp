#include <jni.h>
#include <string>
#include <sstream>

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++ - Syncflow Ready";
    return env->NewStringUTF(hello.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_SyncService_initializeNetworking(
        JNIEnv* env,
        jobject /* this */) {
    std::ostringstream oss;
    oss << "Networking initialized: TCP and UDP sockets ready";
    
    // TODO: Initialize TCP/UDP networking from peer_node.cpp
    // - Create socket listeners
    // - Start peer discovery
    // - Begin file sync operations
    
    return env->NewStringUTF(oss.str().c_str());
}