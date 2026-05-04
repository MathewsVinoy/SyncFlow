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
    companion object {
        const val ACTION_START = "com.syncflow.action.START"
        const val ACTION_STOP = "com.syncflow.action.STOP"
        init { System.loadLibrary("syncflowjni") }
    }

    external fun nativeStart(configPath: String): Int
    external fun nativeStop(): Int

    private val channelId = "syncflow_channel"

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val nm = getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(NotificationChannel(channelId, "Syncflow", NotificationManager.IMPORTANCE_LOW))
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> startForegroundServiceWork()
            ACTION_STOP -> stopForeground(true)
        }
        return START_STICKY
    }

    private fun startForegroundServiceWork() {
        val notif: Notification = NotificationCompat.Builder(this, channelId)
            .setContentTitle("Syncflow")
            .setContentText("Running sync in background")
            .setSmallIcon(R.mipmap.ic_launcher)
            .build()

        startForeground(1, notif)

        // Start native engine with app config path
        val cfg = File(filesDir, "config.json")
        val path = cfg.absolutePath
        try {
            nativeStart(path)
        } catch (e: Throwable) {
            // native not available - ignore
        }
    }

    override fun onDestroy() {
        try { nativeStop() } catch (e: Throwable) {}
        super.onDestroy()
    }
}
