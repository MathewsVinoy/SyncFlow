package com.syncflow

object SyncNative {
    init {
        try {
            System.loadLibrary("syncflow")
        } catch (e: UnsatisfiedLinkError) {
            // library may not be available in some dev setups
        }
    }

    external fun startPeer(deviceName: String?, configPath: String?): Boolean
    external fun stopPeer(): Boolean
    external fun statusSummary(): String
}
