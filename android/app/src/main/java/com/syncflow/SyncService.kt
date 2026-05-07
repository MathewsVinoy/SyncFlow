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
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketTimeoutException

class SyncService : Service() {

    private val serviceScope = CoroutineScope(Dispatchers.Main + Job())
    private var isRunning = false
    private var activeSocket: Socket? = null
    private var tcpServerSocket: ServerSocket? = null
    private var multicastLock: WifiManager.MulticastLock? = null

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

                // Get device info
                val deviceName = ConnectionManager.getDeviceName(this@SyncService)
                val localIP = ConnectionManager.getLocalIPAddress(this@SyncService)
                val isWifi = ConnectionManager.isWifiConnected(this@SyncService)
                
                LogManager.addLog("Device: $deviceName")
                LogManager.addLog("Local IP: $localIP")
                LogManager.addLog("WiFi connected: $isWifi")

                // Call native networking initialization
                val result = initializeNetworking()
                LogManager.addLog("Networking init result: $result")

                acquireMulticastLock()
                startMobilePeerMode()

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

    private suspend fun startMobilePeerMode() {
        withContext(Dispatchers.IO) {
            val deviceName = ConnectionManager.getDeviceName(this@SyncService)
            val localIp = ConnectionManager.getLocalIPAddress(this@SyncService)

            ConnectionManager.setState(ConnectionManager.ConnectionState.CONNECTING)
            LogManager.addLog("Mobile peer mode enabled. Advertising $deviceName at $localIp:45455")

            val udpThread = launch { broadcastPresenceLoop(deviceName, localIp) }
            val tcpThread = launch { acceptIncomingConnections(deviceName, localIp) }

            try {
                joinAll(udpThread, tcpThread)
            } finally {
                try {
                    tcpServerSocket?.close()
                } catch (_: Exception) {
                }
                tcpServerSocket = null
            }
        }
    }

    private suspend fun broadcastPresenceLoop(deviceName: String, localIp: String) {
        withContext(Dispatchers.IO) {
            val payload = "SYNCFLOW_PEER|$deviceName|$localIp|45455\n".toByteArray()
            val broadcastSocket = DatagramSocket().apply {
                broadcast = true
                reuseAddress = true
            }

            try {
                while (isRunning) {
                    val destinations = listOf(
                        InetAddress.getByName("255.255.255.255"),
                        InetAddress.getByName("239.255.42.99")
                    )

                    for (destination in destinations) {
                        try {
                            val packet = DatagramPacket(payload, payload.size, destination, 45454)
                            broadcastSocket.send(packet)
                        } catch (e: Exception) {
                            LogManager.addLog("UDP broadcast failed to $destination: ${e.message}")
                        }
                    }

                    delay(2000)
                }
            } finally {
                try {
                    broadcastSocket.close()
                } catch (_: Exception) {
                }
            }
        }
    }

    private suspend fun acceptIncomingConnections(deviceName: String, localIp: String) {
        withContext(Dispatchers.IO) {
            val helloLine = "HELLO|SYNCFLOW_PEER|$deviceName|$localIp|45455\n"

            try {
                tcpServerSocket = ServerSocket(45455).apply {
                    reuseAddress = true
                }
                LogManager.addLog("TCP server listening on 45455 for incoming desktop Syncflow connections")

                while (isRunning) {
                    val socket = try {
                        tcpServerSocket?.accept()
                    } catch (_: Exception) {
                        null
                    } ?: continue

                    launch {
                        handleIncomingDesktopConnection(socket, helloLine)
                    }
                }
            } catch (e: Exception) {
                LogManager.addLog("Failed to open TCP server on mobile IP: ${e.message}")
                ConnectionManager.setState(ConnectionManager.ConnectionState.DISCONNECTED)
            }
        }
    }

    private suspend fun handleIncomingDesktopConnection(socket: Socket, helloLine: String) {
        withContext(Dispatchers.IO) {
            try {
                socket.tcpNoDelay = true
                socket.soTimeout = 4000

                val writer = BufferedWriter(OutputStreamWriter(socket.getOutputStream()))
                val reader = BufferedReader(InputStreamReader(socket.getInputStream()))

                writer.write(helloLine)
                writer.flush()

                activeSocket = socket
                ConnectionManager.setState(ConnectionManager.ConnectionState.CONNECTED)
                LogManager.addLog("Desktop Syncflow connected from ${socket.inetAddress.hostAddress}:${socket.port}")

                try {
                    val response = reader.readLine()
                    if (!response.isNullOrBlank()) {
                        LogManager.addLog("Desktop response: $response")
                    }
                } catch (_: SocketTimeoutException) {
                    LogManager.addLog("Handshake complete. Waiting for desktop Syncflow messages...")
                }

                while (isRunning && socket.isConnected && !socket.isClosed) {
                    try {
                        val line = reader.readLine() ?: break
                        if (line.isNotBlank()) {
                            LogManager.addLog("Desktop Syncflow: $line")
                        }
                    } catch (_: SocketTimeoutException) {
                        // no-op
                    }
                }
            } catch (e: Exception) {
                LogManager.addLog("Incoming connection error: ${e.message}")
            } finally {
                try {
                    socket.close()
                } catch (_: Exception) {
                }
                if (activeSocket === socket) {
                    activeSocket = null
                    ConnectionManager.setState(ConnectionManager.ConnectionState.DISCONNECTED)
                }
            }
        }
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
            val ipAddr = ConnectionManager.getLocalIPAddress(this)

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
            tcpServerSocket?.close()
        } catch (_: Exception) {
        }
        tcpServerSocket = null
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
