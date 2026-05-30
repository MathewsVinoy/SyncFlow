package com.syncflow

import android.util.Log

object SyncPeerManager {
    private const val TAG = "SyncPeerManager"

    private var statusListener: ((PeerStatus) -> Unit)? = null

    fun setStatusListener(listener: (PeerStatus) -> Unit) {
        statusListener = listener
    }

    fun snapshot(): PeerStatus {
        // Minimal placeholder status until native integration pushes real status
        return PeerStatus(
            running = false,
            deviceName = "Unknown",
            localIp = "0.0.0.0",
            connections = emptyList()
        )
    }

    fun setDownloadDirectory(path: String) {
        Log.d(TAG, "setDownloadDirectory: $path")
    }
}
