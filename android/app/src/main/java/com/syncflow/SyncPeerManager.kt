package com.syncflow

import android.util.Log
import org.json.JSONObject

object SyncPeerManager {
    private const val TAG = "SyncPeerManager"

    private var statusListener: ((PeerStatus) -> Unit)? = null
    private var lastStatus: PeerStatus? = null

    fun setStatusListener(listener: (PeerStatus) -> Unit) {
        statusListener = listener
    }

    fun snapshot(): PeerStatus {
        return try {
            val statusJson = SyncNative.statusSummary()
            parseNativeStatus(statusJson)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to get native status", e)
            lastStatus ?: PeerStatus(
                running = false,
                deviceName = "Unknown",
                localIp = "0.0.0.0",
                connections = emptyList()
            )
        }
    }

    fun updateStatus() {
        val status = snapshot()
        lastStatus = status
        statusListener?.invoke(status)
    }

    private fun parseNativeStatus(jsonString: String): PeerStatus {
        return try {
            val json = JSONObject(jsonString)
            val running = json.optBoolean("running", false)
            val deviceName = json.optString("device_name", "Unknown")
            val localIp = json.optString("local_ip", "0.0.0.0")
            
            val connectionsArray = json.optJSONArray("connections")
            val connections = mutableListOf<String>()
            if (connectionsArray != null) {
                for (i in 0 until connectionsArray.length()) {
                    connections.add(connectionsArray.getString(i))
                }
            }
            
            PeerStatus(
                running = running,
                deviceName = deviceName,
                localIp = localIp,
                connections = connections
            )
        } catch (e: Exception) {
            Log.w(TAG, "Failed to parse native status JSON: $jsonString", e)
            PeerStatus(
                running = false,
                deviceName = "Unknown",
                localIp = "0.0.0.0",
                connections = emptyList()
            )
        }
    }

    fun setDownloadDirectory(path: String) {
        Log.d(TAG, "setDownloadDirectory: $path")
    }
}
