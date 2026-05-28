package com.syncflow.file

import android.os.FileObserver
import android.util.Log
import java.io.File

class FileMonitor(private val path: String, private val callback: (event: FileChangeEvent) -> Unit) {

    companion object {
        private const val TAG = "SyncFlow"
    }

    data class FileChangeEvent(
        val type: String,
        val path: String,
        val timestamp: Long = System.currentTimeMillis()
    )

    private val observer = object : FileObserver(path, ALL_EVENTS) {
        override fun onEvent(event: Int, path: String?) {
            if (path == null) return

            val eventType = when (event) {
                CREATE -> "CREATE"
                DELETE -> "DELETE"
                MOVED_FROM -> "MOVED_FROM"
                MOVED_TO -> "MOVED_TO"
                MODIFY -> "MODIFY"
                else -> "OTHER"
            }

            Log.d(TAG, "file monitor event: $eventType on $path")
            callback(FileChangeEvent(eventType, path))
        }
    }

    fun start() {
        observer.startWatching()
        Log.d(TAG, "file monitor started for $path")
    }

    fun stop() {
        observer.stopWatching()
        Log.d(TAG, "file monitor stopped for $path")
    }

    fun isValid(): Boolean {
        return File(path).exists() && File(path).isDirectory
    }
}
