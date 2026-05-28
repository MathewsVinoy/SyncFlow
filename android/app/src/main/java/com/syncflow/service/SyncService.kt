package com.syncflow.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import com.syncflow.MainActivity
import com.syncflow.SyncPeerManager

class SyncService : Service() {

    companion object {
        private const val TAG = "SyncFlow"
        private const val NOTIFICATION_CHANNEL_ID = "syncflow_sync"
        private const val NOTIFICATION_ID = 1
        private const val PREFS_NAME = "syncflow_prefs"
        private const val PREF_SERVICE_ENABLED = "service_enabled"

        fun startService(context: Context) {
            val intent = Intent(context, SyncService::class.java)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }

        fun stopService(context: Context) {
            val intent = Intent(context, SyncService::class.java)
            context.stopService(intent)
        }

        fun isServiceEnabled(context: Context): Boolean {
            val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            return prefs.getBoolean(PREF_SERVICE_ENABLED, false)
        }

        fun setServiceEnabled(context: Context, enabled: Boolean) {
            val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            prefs.edit().putBoolean(PREF_SERVICE_ENABLED, enabled).apply()
        }
    }

    private lateinit var prefs: SharedPreferences
    private var isSyncRunning = false

    override fun onCreate() {
        super.onCreate()
        Log.d(TAG, "SyncService created")
        prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        Log.d(TAG, "SyncService starting")
        
        if (!isSyncRunning) {
            isSyncRunning = true
            startForeground(NOTIFICATION_ID, createNotification("Initializing"))
            
            try {
                SyncPeerManager.start(this)
                updateNotification("Running and listening for peers")
                setServiceEnabled(this, true)
                Log.d(TAG, "SyncPeerManager started in background service")
            } catch (e: Exception) {
                Log.e(TAG, "failed to start SyncPeerManager", e)
                updateNotification("Error: Failed to start sync")
                stopSelf()
            }
        }

        return START_STICKY
    }

    override fun onDestroy() {
        Log.d(TAG, "SyncService destroying")
        try {
            SyncPeerManager.stop()
            isSyncRunning = false
            setServiceEnabled(this, false)
        } catch (e: Exception) {
            Log.e(TAG, "failed to stop SyncPeerManager", e)
        }
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                NOTIFICATION_CHANNEL_ID,
                "SyncFlow Sync Service",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Notifications for file sync activity"
            }
            val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            manager.createNotificationChannel(channel)
        }
    }

    private fun createNotification(status: String): Notification {
        val intent = Intent(this, MainActivity::class.java)
        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, NOTIFICATION_CHANNEL_ID)
            .setContentTitle("SyncFlow")
            .setContentText(status)
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }

    private fun updateNotification(status: String) {
        val notification = createNotification(status)
        val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        manager.notify(NOTIFICATION_ID, notification)
    }
}
