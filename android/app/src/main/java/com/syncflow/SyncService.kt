package com.syncflow

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.*

class SyncService : Service() {

    private val serviceScope = CoroutineScope(Dispatchers.Main + Job())
    private var isRunning = false

    override fun onBind(intent: Intent?): IBinder? {
        return null
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        LogManager.addLog("SyncService: onCreate()")
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        LogManager.addLog("SyncService: onStartCommand()")

        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Syncflow")
            .setContentText("Sync running...")
            .setSmallIcon(R.mipmap.ic_launcher)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setOngoing(true)
            .build()

        try {
            startForeground(1, notification)
        } catch (e: Exception) {
            Log.e("syncflow", "Foreground start denied", e)
            LogManager.addLog("Foreground service start denied: ${e.message}")
            stopSelf()
            return START_NOT_STICKY
        }

        // Start networking in background
        if (!isRunning) {
            isRunning = true
            startNetworking()
        }

        return START_STICKY
    }

    private fun startNetworking() {
        serviceScope.launch(Dispatchers.Default) {
            try {
                LogManager.addLog("Starting TCP/UDP networking...")

                // Call native networking initialization
                val result = initializeNetworking()
                LogManager.addLog("Networking init result: $result")

                // Simulated network monitoring loop
                while (isRunning && isActive) {
                    updateNetworkStatus()
                    delay(5000) // Update every 5 seconds
                }
            } catch (e: Exception) {
                LogManager.addLog("Networking error: ${e.message}")
            }
        }
    }

    private fun updateNetworkStatus() {
        try {
            val isConnected = ConnectionManager.isWifiConnected(this)
            val ipAddr = ConnectionManager.getLocalIPAddress()

            if (isConnected) {
                ConnectionManager.setState(ConnectionManager.ConnectionState.CONNECTED)
                LogManager.addLog("Network status: Connected ($ipAddr)")
            } else {
                ConnectionManager.setState(ConnectionManager.ConnectionState.DISCONNECTED)
                LogManager.addLog("Network status: Disconnected")
            }
        } catch (e: Exception) {
            LogManager.addLog("Status update error: ${e.message}")
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        isRunning = false
        serviceScope.cancel()
        LogManager.addLog("SyncService: onDestroy()")
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val name = "Syncflow Service"
            val descriptionText = "Foreground service for file synchronization"
            val importance = NotificationManager.IMPORTANCE_LOW
            val channel = NotificationChannel(CHANNEL_ID, name, importance)
            channel.description = descriptionText
            val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            notificationManager.createNotificationChannel(channel)
        }
    }

    /**
     * Native method to initialize TCP/UDP networking from C++.
     * This calls into the syncflow C++ library.
     */
    external fun initializeNetworking(): String

    companion object {
        const val CHANNEL_ID = "syncflow_channel"

        init {
            System.loadLibrary("syncflow")
        }
    }
}
