package com.syncflow

import android.app.Service
import android.content.Intent
import android.os.IBinder
import android.os.Build
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import androidx.core.app.NotificationCompat
import android.util.Log

/**
 * Android Foreground Service for SyncFlow
 *
 * Runs in background even when app is closed. Handles:
 * - Peer discovery
 * - File synchronization
 * - Conflict detection
 * - Network connectivity monitoring
 */
class SyncService : Service() {
    companion object {
        private const val TAG = "SyncFlowService"
        private const val NOTIFICATION_ID = 1
        private const val CHANNEL_ID = "syncflow_sync"
    }

    private lateinit var notificationManager: NotificationManager

    // Load native library
    init {
        System.loadLibrary("syncflow_jni")
    }

    override fun onCreate() {
        super.onCreate()
        Log.d(TAG, "SyncService onCreate")
        notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        Log.d(TAG, "SyncService onStartCommand")

        // Start as foreground service
        val notification = createNotification()
        startForeground(NOTIFICATION_ID, notification)

        // Start sync engine
        val result = startSync()
        if (result != 0) {
            Log.e(TAG, "Failed to start sync engine")
            stopSelf()
            return START_NOT_STICKY
        }

        // Add sync folder (example: Downloads)
        val downloadsPath = "${getExternalFilesDir(null)}/SyncFlow"
        val addResult = addSyncFolder(downloadsPath)
        if (addResult != 0) {
            Log.e(TAG, "Failed to add sync folder: $downloadsPath")
        } else {
            Log.d(TAG, "Added sync folder: $downloadsPath")
        }

        // Continue running even if app is killed
        return START_STICKY
    }

    override fun onDestroy() {
        Log.d(TAG, "SyncService onDestroy")
        stopSync()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? {
        return null  // Not a bound service
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "SyncFlow Background Sync",
                NotificationManager.IMPORTANCE_LOW
            )
            channel.description = "Synchronizing files across devices"
            notificationManager.createNotificationChannel(channel)
        }
    }

    private fun createNotification(): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("SyncFlow")
            .setContentText("Syncing files...")
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
    }

    private fun updateNotification(status: String) {
        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("SyncFlow")
            .setContentText(status)
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
            if (android.content.ContextCompat.checkSelfPermission(
                this,
                android.Manifest.permission.POST_NOTIFICATIONS
            ) == android.content.pm.PackageManager.PERMISSION_GRANTED) {
                notificationManager.notify(NOTIFICATION_ID, notification)
            }
        } else {
            notificationManager.notify(NOTIFICATION_ID, notification)
        }
    }

    // Native JNI methods
    private external fun startSync(): Int
    private external fun stopSync()
    private external fun addSyncFolder(folderPath: String): Int
    private external fun getStatus(): String
    private external fun getConnectedPeersCount(): Int
    private external fun getSyncQueueSize(): Int
}
