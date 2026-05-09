package com.syncflow

class SyncEngineWrapper {
    init {
        System.loadLibrary("syncflow")
    }

    external fun init()
    external fun addSyncFolder(path: String)
    external fun startSync()
    external fun stopSync()
    external fun getStatus(): Int
    external fun getProgress(): Float

    companion object {
        const val STATUS_IDLE = 0
        const val STATUS_SYNCING = 1
        const val STATUS_ERROR = 2
    }
}
