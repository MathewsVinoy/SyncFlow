package com.syncflow

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import kotlin.concurrent.thread

class SyncService : Service() {
    private var running = false
    private var configPath: String? = null

    override fun onCreate() {
        super.onCreate()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        configPath = intent?.getStringExtra("config_path")
        startForegroundServiceWithNotification()
        startNativePeer()
        return START_STICKY
    }

    private fun startForegroundServiceWithNotification() {
        val channelId = "syncflow_channel"
        val nm = getSystemService(NotificationManager::class.java) as NotificationManager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val chan = NotificationChannel(channelId, "Syncflow", NotificationManager.IMPORTANCE_LOW)
            nm.createNotificationChannel(chan)
        }

        val notif: Notification = NotificationCompat.Builder(this, channelId)
            .setContentTitle("Syncflow")
            .setContentText("Background sync running")
            .setSmallIcon(R.mipmap.ic_launcher)
            .build()

        startForeground(101, notif)
    }

    private fun startNativePeer() {
        if (running) return
        running = true
        val path = configPath ?: filesDir.resolve("config.json").absolutePath

        // Call into JNI stub
        thread {
            NativeBridge.startPeer(path)
        }
    }

    override fun onDestroy() {
        NativeBridge.stopPeer()
        running = false
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? {
        return null
    }
}

object NativeBridge {
    init {
        System.loadLibrary("native-lib")
    }

    external fun startPeer(configPath: String)
    external fun stopPeer()
    external fun getStatus(): String
}
