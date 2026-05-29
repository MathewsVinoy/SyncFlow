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

    private val targetFile = File(path)
    private val watchPath = when {
        targetFile.isDirectory -> targetFile.absolutePath
        targetFile.parentFile != null -> targetFile.parentFile.absolutePath
        else -> targetFile.absolutePath
    }
    private val watchedFileName = if (targetFile.isFile) targetFile.name else null

    private val observer = object : FileObserver(watchPath, ALL_EVENTS) {
        override fun onEvent(event: Int, path: String?) {
            if (path == null) return

            if (watchedFileName != null && path != watchedFileName) {
                return
            }

            val eventType = when (event) {
                CREATE -> "CREATE"
                DELETE -> "DELETE"
                MOVED_FROM -> "MOVED_FROM"
                MOVED_TO -> "MOVED_TO"
                MODIFY -> "MODIFY"
                else -> "OTHER"
            }

            Log.d(TAG, "file monitor event: $eventType on $path")
            callback(FileChangeEvent(eventType, File(watchPath, path).absolutePath))
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
        return targetFile.exists() && (targetFile.isDirectory || targetFile.isFile)
    }
}
