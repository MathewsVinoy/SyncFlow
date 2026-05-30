package com.syncflow.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.ForegroundServiceStartNotAllowedException
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Environment
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import com.syncflow.MainActivity
import com.syncflow.SyncNative
import com.syncflow.file.FileMonitor
import java.io.File

class SyncService : Service() {

    companion object {
        private const val TAG = "SyncFlow"
        private const val NOTIFICATION_CHANNEL_ID = "syncflow_sync"
        private const val NOTIFICATION_ID = 1
        private const val PREFS_NAME = "syncflow_prefs"
        private const val PREF_SERVICE_ENABLED = "service_enabled"
        private const val PREF_SYNC_SOURCE_PATH = "sync_source_path"
        private const val PREF_DOWNLOAD_DIR = "download_dir"

        fun startService(context: Context) {
            setServiceEnabled(context, true)
            val intent = Intent(context, SyncService::class.java)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }

        fun stopService(context: Context) {
            setServiceEnabled(context, false)
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
    private var fileMonitor: FileMonitor? = null

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
            try {
                val foregroundServiceType = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
                } else {
                    0
                }
                ServiceCompat.startForeground(
                    this,
                    NOTIFICATION_ID,
                    createNotification("Initializing"),
                    foregroundServiceType
                )
                val started = try {
                    SyncNative.startPeer(null, null)
                } catch (t: Throwable) {
                    Log.e(TAG, "native peer start failed", t)
                    false
                }
                if (!started) {
                    Log.e(TAG, "failed to start native peer")
                }
                startFileMonitorIfConfigured()
                updateNotification("Running and listening for peers")
                Log.d(TAG, "native peer start requested")
            } catch (e: ForegroundServiceStartNotAllowedException) {
                Log.e(TAG, "foreground service start not allowed", e)
                isSyncRunning = false
                stopSelf()
                return START_NOT_STICKY
            } catch (e: Exception) {
                Log.e(TAG, "failed to start SyncPeerManager", e)
                isSyncRunning = false
                updateNotification("Error: Failed to start sync")
                stopSelf()
            }
        }

        return START_NOT_STICKY
    }

    override fun onDestroy() {
        Log.d(TAG, "SyncService destroying")
            try {
                stopFileMonitor()
                try {
                    SyncNative.stopPeer()
                } catch (t: Throwable) {
                    Log.e(TAG, "native peer stop failed", t)
                }
                isSyncRunning = false
            } catch (e: Exception) {
                Log.e(TAG, "failed to stop native peer", e)
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

    private fun startFileMonitorIfConfigured() {
        stopFileMonitor()

        val sourcePath = prefs.getString(PREF_SYNC_SOURCE_PATH, null)?.trim().orEmpty()
        if (sourcePath.isBlank()) {
            Log.d(TAG, "no sync source configured")
            return
        }

        val sourceFile = File(sourcePath)
        if (!sourceFile.exists()) {
            Log.w(TAG, "configured sync source does not exist: $sourcePath")
            updateNotification("Sync source not found")
            return
        }

        fileMonitor = FileMonitor(sourcePath) { event ->
            Log.d(TAG, "sync source changed: ${event.type} ${event.path}")
            try {
                // TODO: call into native peer to trigger sync for changed folder
                val status = SyncNative.statusSummary()
                Log.d(TAG, "native status: $status")
            } catch (t: Throwable) {
                Log.w(TAG, "failed to query native status", t)
            }
        }.also { monitor ->
            if (monitor.isValid()) {
                monitor.start()
                updateNotification("Watching sync source")
            }
        }
    }

    private fun stopFileMonitor() {
        fileMonitor?.stop()
        fileMonitor = null
    }

    private fun currentDownloadDirectory(): String {
        return prefs.getString(PREF_DOWNLOAD_DIR, null)
            ?: Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS).absolutePath
    }
}
