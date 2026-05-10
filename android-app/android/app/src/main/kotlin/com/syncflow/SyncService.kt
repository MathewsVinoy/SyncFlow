package com.syncflow

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import java.io.File

class SyncService : Service() {

    private val engine = SyncEngineWrapper()
    @Volatile
    private var monitorRunning = false
    private var monitorThread: Thread? = null

    override fun onCreate() {
        super.onCreate()
        Log.i(TAG, "SyncService.onCreate")
        engine.init()
        engine.setDeviceName(Build.MODEL ?: "android")
        Log.i(TAG, "Native engine initialized. Device=${Build.MODEL}")
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val requestedHost = intent?.getStringExtra(EXTRA_REMOTE_HOST)
        val remoteHost = requestedHost
            ?: detectGatewayIp()
            ?: DEFAULT_REMOTE_HOST
        val remotePort = intent?.getIntExtra(EXTRA_REMOTE_PORT, DEFAULT_REMOTE_PORT) ?: DEFAULT_REMOTE_PORT
        val receiveDir = File(filesDir, DEFAULT_RECEIVE_DIR)

        Log.i(TAG, "onStartCommand remoteHost=$remoteHost remotePort=$remotePort receiveDir=${receiveDir.absolutePath}")
        if (requestedHost == null && remoteHost != DEFAULT_REMOTE_HOST) {
            Log.i(TAG, "No explicit remote host provided, using detected gateway IP: $remoteHost")
        }
        if (remoteHost == DEFAULT_REMOTE_HOST) {
            Log.w(TAG, "Using fallback remote host $DEFAULT_REMOTE_HOST. Pass system LAN IP via intent extras.")
        }

        engine.setRemotePeer(remoteHost, remotePort)
        engine.setReceiveDir(receiveDir.absolutePath)

        val notification = createNotification("Syncing files...")
        startForeground(1, notification)
        
        engine.startSync()
        Log.i(TAG, "engine.startSync called")
        startMonitorLoop()
        
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        Log.i(TAG, "SyncService.onDestroy")
        stopMonitorLoop()
        engine.stopSync()
        Log.i(TAG, "engine.stopSync called")
        super.onDestroy()
    }

    private fun startMonitorLoop() {
        if (monitorRunning) return
        monitorRunning = true
        monitorThread = Thread {
            while (monitorRunning) {
                try {
                    val status = engine.getStatus()
                    val progress = engine.getProgress()
                    val lastError = engine.getLastError()
                    Log.i(TAG, "native-status status=$status progress=$progress lastError=${lastError.ifEmpty { "<none>" }}")
                    Thread.sleep(3000)
                } catch (e: Throwable) {
                    Log.e(TAG, "monitor loop failed", e)
                    Thread.sleep(3000)
                }
            }
        }.also {
            it.name = "SyncFlowMonitor"
            it.isDaemon = true
            it.start()
        }
    }

    private fun stopMonitorLoop() {
        monitorRunning = false
        monitorThread?.interrupt()
        monitorThread = null
    }

    private fun detectGatewayIp(): String? {
        return try {
            val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
            for (network in cm.allNetworks) {
                val caps = cm.getNetworkCapabilities(network) ?: continue
                if (!caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)) continue

                val linkProps = cm.getLinkProperties(network) ?: continue
                val gateway = linkProps.routes
                    .firstOrNull { it.isDefaultRoute && it.gateway != null }
                    ?.gateway
                    ?.hostAddress

                if (!gateway.isNullOrBlank()) {
                    return gateway
                }
            }
            null
        } catch (t: Throwable) {
            Log.w(TAG, "Failed to detect gateway IP", t)
            null
        }
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
        private const val TAG = "SyncFlowService"
        const val CHANNEL_ID = "SyncFlowServiceChannel"
        const val EXTRA_REMOTE_HOST = "remote_host"
        const val EXTRA_REMOTE_PORT = "remote_port"
        private const val DEFAULT_REMOTE_HOST = "127.0.0.1"
        private const val DEFAULT_REMOTE_PORT = 45455
        private const val DEFAULT_RECEIVE_DIR = "syncflow_received"
    }
}
