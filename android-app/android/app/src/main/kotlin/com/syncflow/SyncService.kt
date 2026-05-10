package com.syncflow

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import java.io.File

class SyncService : Service() {

    private val engine = SyncEngineWrapper()

    override fun onCreate() {
        super.onCreate()
        engine.init()
        engine.setDeviceName(Build.MODEL ?: "android")
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val remoteHost = intent?.getStringExtra(EXTRA_REMOTE_HOST) ?: DEFAULT_REMOTE_HOST
        val remotePort = intent?.getIntExtra(EXTRA_REMOTE_PORT, DEFAULT_REMOTE_PORT) ?: DEFAULT_REMOTE_PORT
        val receiveDir = File(filesDir, DEFAULT_RECEIVE_DIR)

        engine.setRemotePeer(remoteHost, remotePort)
        engine.setReceiveDir(receiveDir.absolutePath)

        val notification = createNotification("Syncing files...")
        startForeground(1, notification)
        
        engine.startSync()
        
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        engine.stopSync()
        super.onDestroy()
    }

    private fun createNotification(content: String): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("SyncFlow")
            .setContentText(content)
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .build()
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Sync Service Channel",
                NotificationManager.IMPORTANCE_LOW
            )
            val manager = getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(channel)
        }
    }

    companion object {
        const val CHANNEL_ID = "SyncFlowServiceChannel"
        const val EXTRA_REMOTE_HOST = "remote_host"
        const val EXTRA_REMOTE_PORT = "remote_port"
        private const val DEFAULT_REMOTE_HOST = "127.0.0.1"
        private const val DEFAULT_REMOTE_PORT = 45455
        private const val DEFAULT_RECEIVE_DIR = "syncflow_received"
    }
}
