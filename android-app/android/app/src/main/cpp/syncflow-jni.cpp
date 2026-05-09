#include <jni.h>
#include <string>
#include <memory>
#include "SyncEngine.hpp"

static std::unique_ptr<syncflow::SyncEngine> g_sync_engine;

extern "C" JNIEXPORT void JNICALL
Java_com_syncflow_SyncEngineWrapper_init(JNIEnv* env, jobject thiz) {
    g_sync_engine = std::make_unique<syncflow::SyncEngine>();
}

extern "C" JNIEXPORT void JNICALL
Java_com_syncflow_SyncEngineWrapper_addSyncFolder(JNIEnv* env, jobject thiz, jstring path) {
    const char* native_path = env->GetStringUTFChars(path, nullptr);
    if (g_sync_engine) {
        g_sync_engine->add_sync_folder(native_path);
    }
    env->ReleaseStringUTFChars(path, native_path);
}

extern "C" JNIEXPORT void JNICALL
Java_com_syncflow_SyncEngineWrapper_startSync(JNIEnv* env, jobject thiz) {
    if (g_sync_engine) {
        g_sync_engine->start_sync();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_syncflow_SyncEngineWrapper_stopSync(JNIEnv* env, jobject thiz) {
    if (g_sync_engine) {
        g_sync_engine->stop_sync();
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_syncflow_SyncEngineWrapper_getStatus(JNIEnv* env, jobject thiz) {
    if (g_sync_engine) {
        return static_cast<jint>(g_sync_engine->get_status());
    }
    return 0;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_syncflow_SyncEngineWrapper_getProgress(JNIEnv* env, jobject thiz) {
    if (g_sync_engine) {
        return g_sync_engine->get_progress();
    }
    return 0.0f;
}
