package com.syncflow

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.util.Log
import android.net.wifi.WifiManager
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.*
import java.io.BufferedReader
import java.io.BufferedWriter
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.InetSocketAddress
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.Socket
import java.net.SocketTimeoutException

class SyncService : Service() {

    private val serviceScope = CoroutineScope(Dispatchers.Main + Job())
    private var isRunning = false
    private var activeSocket: Socket? = null
    private var multicastLock: WifiManager.MulticastLock? = null

    private data class DiscoveredPeer(
        val magic: String,
        val name: String,
        val ip: String,
        val tcpPort: Int,
    )

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

                acquireMulticastLock()
                discoverAndConnectToSyncflow()

                // Monitoring loop while service is active
                while (isRunning && isActive) {
                    updateNetworkStatus()
                    delay(5000)
                }
            } catch (e: Exception) {
                LogManager.addLog("Networking error: ${e.message}")
            }
        }
    }

    private suspend fun discoverAndConnectToSyncflow() {
        withContext(Dispatchers.IO) {
            val deviceName = ConnectionManager.getDeviceName(this@SyncService)
            val localIp = ConnectionManager.getLocalIPAddress()
            val helloLine = "HELLO|SYNCFLOW_PEER|$deviceName|$localIp|45455\n"

            ConnectionManager.setState(ConnectionManager.ConnectionState.CONNECTING)
            LogManager.addLog("Listening for Syncflow UDP discovery on 45454...")

            val discoverySocket = DatagramSocket(45454).apply {
                soTimeout = 2000
                reuseAddress = true
                broadcast = true
            }

            val buffer = ByteArray(1024)
            var connected = false

            try {
                while (isRunning && !connected) {
                    val packet = DatagramPacket(buffer, buffer.size)
                    try {
                        discoverySocket.receive(packet)
                    } catch (_: SocketTimeoutException) {
                        continue
                    }

                    val payload = String(packet.data, 0, packet.length).trim()
                    val peer = parsePeerInfo(payload) ?: continue
                    if (peer.magic != "SYNCFLOW_PEER") continue

                    if (peer.ip == localIp) {
                        continue
                    }

                    LogManager.addLog("Discovered Syncflow peer ${peer.name} at ${peer.ip}:${peer.tcpPort}")
                    connected = tryConnectToPeer(peer, helloLine)
                }
            } finally {
                discoverySocket.close()
            }

            if (!connected) {
                ConnectionManager.setState(ConnectionManager.ConnectionState.DISCONNECTED)
                LogManager.addLog("No Syncflow peer discovered on the LAN.")
            }
        }
    }

    private fun tryConnectToPeer(peer: DiscoveredPeer, helloLine: String): Boolean {
        val socket = Socket()
        return try {
            socket.tcpNoDelay = true
            socket.soTimeout = 4000
            socket.connect(InetSocketAddress(peer.ip, peer.tcpPort), 4000)

            val writer = BufferedWriter(OutputStreamWriter(socket.getOutputStream()))
            val reader = BufferedReader(InputStreamReader(socket.getInputStream()))

            writer.write(helloLine)
            writer.flush()

            activeSocket = socket
            ConnectionManager.setState(ConnectionManager.ConnectionState.CONNECTED)
            LogManager.addLog("Connected to Syncflow at ${peer.ip}:${peer.tcpPort}")

            try {
                val response = reader.readLine()
                if (!response.isNullOrBlank()) {
                    LogManager.addLog("Syncflow response: $response")
                }
            } catch (_: SocketTimeoutException) {
                LogManager.addLog("Connected. Waiting for sync data...")
            }

            while (isRunning && socket.isConnected && !socket.isClosed) {
                try {
                    val line = reader.readLine() ?: break
                    if (line.isNotBlank()) {
                        LogManager.addLog("Syncflow: $line")
                    }
                } catch (_: SocketTimeoutException) {
                    // no-op
                }
            }

            true
        } catch (e: Exception) {
            LogManager.addLog("Connection failed to ${peer.ip}:${peer.tcpPort} - ${e.message}")
            false
        } finally {
            try {
                socket.close()
            } catch (_: Exception) {
            }
        }
    }

    private fun parsePeerInfo(payload: String): DiscoveredPeer? {
        val parts = payload.split('|')
        if (parts.size != 4) return null
        val port = parts[3].toIntOrNull() ?: return null
        return DiscoveredPeer(parts[0], parts[1], parts[2], port)
    }

    private fun acquireMulticastLock() {
        try {
            val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
            if (wifiManager != null && multicastLock == null) {
                multicastLock = wifiManager.createMulticastLock("syncflow_multicast").apply {
                    setReferenceCounted(false)
                    acquire()
                }
                LogManager.addLog("Acquired Wi-Fi multicast lock")
            }
        } catch (e: Exception) {
            LogManager.addLog("Failed to acquire multicast lock: ${e.message}")
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
        try {
            activeSocket?.close()
        } catch (_: Exception) {
        }
        activeSocket = null
        try {
            multicastLock?.let { if (it.isHeld) it.release() }
        } catch (_: Exception) {
        }
        multicastLock = null
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
