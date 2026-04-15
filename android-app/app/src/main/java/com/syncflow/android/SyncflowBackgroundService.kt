package com.syncflow.android

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import androidx.core.app.NotificationCompat
import java.net.HttpURLConnection
import java.net.URL

class SyncflowBackgroundService : Service() {
    companion object {
        private const val CHANNEL_ID = "syncflow_bg"
        private const val NOTIF_ID = 1001
        private const val PREFS = "syncflow_prefs"
        private const val KEY_ENDPOINT = "endpoint"
        private const val DEFAULT_ENDPOINT = "http://127.0.0.1:8080"
        private const val INTERVAL_MS = 15_000L
    }

    private lateinit var thread: HandlerThread
    private lateinit var handler: Handler
    private var running = false

    override fun onCreate() {
        super.onCreate()
        createChannel()
        startForeground(NOTIF_ID, buildNotification("Syncflow background active"))

        thread = HandlerThread("SyncflowBackground")
        thread.start()
        handler = Handler(thread.looper)
        running = true
        scheduleTick()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        return START_STICKY
    }

    override fun onDestroy() {
        running = false
        handler.removeCallbacksAndMessages(null)
        thread.quitSafely()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun scheduleTick() {
        handler.post {
            if (!running) return@post
            pingKeepAlive()
            handler.postDelayed({ scheduleTick() }, INTERVAL_MS)
        }
    }

    private fun endpoint(): String {
        val prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        return prefs.getString(KEY_ENDPOINT, DEFAULT_ENDPOINT) ?: DEFAULT_ENDPOINT
    }

    private fun pingKeepAlive() {
        val base = endpoint().trimEnd('/')
        safeGet("$base/api/discovery/start")
        safeGet("$base/api/discovery/status")
    }

    private fun safeGet(url: String) {
        try {
            val conn = URL(url).openConnection() as HttpURLConnection
            conn.connectTimeout = 3000
            conn.readTimeout = 3000
            conn.requestMethod = "GET"
            conn.inputStream.use { it.readBytes() }
            conn.disconnect()
        } catch (_: Exception) {
        }
    }

    private fun createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Syncflow Background",
                NotificationManager.IMPORTANCE_LOW
            )
            val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            nm.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(text: String): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Syncflow")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setOngoing(true)
            .build()
    }
}
