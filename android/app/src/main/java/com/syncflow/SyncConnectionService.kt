package com.syncflow

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketTimeoutException
import java.util.concurrent.atomic.AtomicBoolean

class SyncConnectionService : Service() {

    private val running = AtomicBoolean(false)
    private val testing = AtomicBoolean(false)
    @Volatile private var workerThread: Thread? = null
    @Volatile private var currentRemoteDevice: String = ""
    @Volatile private var currentRemoteIp: String = ""
    @Volatile private var currentLocalDevice: String = ""

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                stopLoop("Stopping background connection")
                stopSelf()
                return START_NOT_STICKY
            }
            ACTION_TEST -> {
                val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                val host = intent.getStringExtra(EXTRA_HOST)
                    ?: prefs.getString(KEY_HOST, "192.168.1.10")
                    ?: "192.168.1.10"
                val port = intent.getIntExtra(EXTRA_PORT, DEFAULT_PORT)
                val deviceName = intent.getStringExtra(EXTRA_DEVICE_NAME)
                    ?: prefs.getString(KEY_DEVICE_NAME, "Android")
                    ?: "Android"

                startForeground(NOTIFICATION_ID, buildNotification("Testing connection to $host:$port"))
                testConnectionOnce(host, port, deviceName)
                return START_NOT_STICKY
            }
            ACTION_START, null -> {
                val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                val host = intent?.getStringExtra(EXTRA_HOST)
                    ?: prefs.getString(KEY_HOST, "192.168.1.10")
                    ?: "192.168.1.10"
                val port = intent?.getIntExtra(EXTRA_PORT, DEFAULT_PORT)
                    ?: prefs.getInt(KEY_PORT, DEFAULT_PORT)
                val deviceName = intent?.getStringExtra(EXTRA_DEVICE_NAME)
                    ?: prefs.getString(KEY_DEVICE_NAME, "Android")
                    ?: "Android"

                prefs.edit()
                    .putString(KEY_HOST, host)
                    .putInt(KEY_PORT, port)
                    .putString(KEY_DEVICE_NAME, deviceName)
                    .putBoolean(KEY_AUTO_CONNECT, true)
                    .apply()

                currentLocalDevice = deviceName
                startForeground(NOTIFICATION_ID, buildNotification("Connecting to $host:$port"))
                sendStatus("Connecting to $host:$port")
                startLoop(host, port, deviceName)
            }
        }

        return START_STICKY
    }

    override fun onDestroy() {
        stopLoop("Service stopped")
        super.onDestroy()
    }

    private fun startLoop(host: String, port: Int, deviceName: String) {
        if (running.getAndSet(true)) return
        testing.set(false)

        workerThread = Thread {
            while (running.get()) {
                val localIp = localIpv4Address()
                currentLocalDevice = deviceName
                var socket: Socket? = null
                try {
                    sendStatus("Connecting to $host:$port")
                    sendLog("Attempting TCP connection to $host:$port")

                    socket = Socket()
                    socket.soTimeout = 1000
                    socket.connect(InetSocketAddress(host, port), CONNECT_TIMEOUT_MS)

                    val reader = BufferedReader(InputStreamReader(socket.getInputStream(), Charsets.UTF_8))
                    val writer = OutputStreamWriter(socket.getOutputStream(), Charsets.UTF_8)

                    val hello = "HELLO|SYNCFLOW_PEER|$deviceName|$localIp|$CLIENT_TCP_PORT\n"
                    writer.write(hello)
                    writer.flush()

                    currentRemoteDevice = host
                    currentRemoteIp = host
                    sendStatus("Connected to $host:$port")
                    sendConnectionState("Connected", host, localIp, host)
                    sendLog("TCP connected to desktop sync peer")

                    while (running.get()) {
                        val line = try {
                            reader.readLine()
                        } catch (_: SocketTimeoutException) {
                            null
                        }

                        if (line == null) {
                            continue
                        }

                        handleIncomingLine(line, host)
                    }
                } catch (error: Exception) {
                    sendConnectionError(host, port, localIp, error)
                } finally {
                    try {
                        socket?.close()
                    } catch (_: Exception) {
                    }
                    if (running.get()) {
                        sendConnectionState("Reconnecting", currentRemoteDevice, currentRemoteIp, host)
                        sleepQuietly(RECONNECT_DELAY_MS)
                    }
                }
            }
        }.apply {
            name = "SyncFlow-Connection"
            start()
        }
    }

    private fun testConnectionOnce(host: String, port: Int, deviceName: String) {
        if (testing.getAndSet(true)) return

        Thread {
            val localIp = localIpv4Address()
            currentLocalDevice = deviceName
            var socket: Socket? = null
            try {
                sendLog("Running connection test to $host:$port")
                socket = Socket()
                socket.soTimeout = 1000
                socket.connect(InetSocketAddress(host, port), CONNECT_TIMEOUT_MS)

                val reader = BufferedReader(InputStreamReader(socket.getInputStream(), Charsets.UTF_8))
                val writer = OutputStreamWriter(socket.getOutputStream(), Charsets.UTF_8)
                writer.write("HELLO|SYNCFLOW_PEER|$deviceName|$localIp|$CLIENT_TCP_PORT\n")
                writer.flush()

                sendStatus("Test connected to $host:$port")
                sendConnectionState("Test connected", host, localIp, host)
                sendLog("Connection test succeeded")

                val line = try {
                    reader.readLine()
                } catch (_: SocketTimeoutException) {
                    null
                }
                if (!line.isNullOrBlank()) {
                    sendLog("Test RX: $line")
                }
            } catch (error: Exception) {
                sendConnectionError(host, port, localIp, error)
            } finally {
                try {
                    socket?.close()
                } catch (_: Exception) {
                }
                testing.set(false)
                stopForeground(STOP_FOREGROUND_REMOVE)
            }
        }.start()
    }

    private fun stopLoop(reason: String) {
        running.set(false)
        workerThread?.interrupt()
        workerThread = null
        sendLog(reason)
        sendStatus(reason)
        stopForeground(STOP_FOREGROUND_REMOVE)
    }

    private fun sendConnectionError(host: String, port: Int, localIp: String, error: Exception) {
        val message = buildString {
            append(error::class.java.simpleName)
            error.message?.takeIf { it.isNotBlank() }?.let {
                append(": ")
                append(it)
            }
            append(" [host=")
            append(host)
            append(':')
            append(port)
            append(", localIp=")
            append(localIp)
            append("]")
        }
        sendStatus("Disconnected")
        sendBroadcast(
            Intent(ACTION_ERROR)
                .putExtra(EXTRA_MESSAGE, message)
        )
        sendLog("Connection error: $message")
    }

    private fun handleIncomingLine(line: String, host: String) {
        sendLog("RX: $line")

        when {
            line.startsWith("CONNECTED_SUCCESS|") -> {
                val parts = line.split("|")
                currentRemoteDevice = parts.getOrNull(1).orEmpty()
                currentRemoteIp = parts.getOrNull(2).orEmpty()
                sendConnectionState("Connected", currentRemoteDevice.ifBlank { host }, currentRemoteIp, host)
            }
            line.startsWith("HELLO|") -> {
                val parts = line.removePrefix("HELLO|").split("|")
                currentRemoteDevice = parts.getOrNull(1).orEmpty()
                currentRemoteIp = parts.getOrNull(2).orEmpty()
                sendConnectionState("Connected", currentRemoteDevice.ifBlank { host }, currentRemoteIp, host)
            }
            line.startsWith("SHARE_BUSY|") -> {
                sendStatus("Desktop peer is busy")
            }
            line.startsWith("SYNC_BEGIN|") -> {
                sendStatus("Sync started")
            }
            line.startsWith("SYNC_END|") -> {
                sendStatus("Sync finished")
            }
        }
    }

    private fun sendStatus(message: String) {
        sendBroadcast(Intent(ACTION_STATUS).putExtra(EXTRA_MESSAGE, message))
        updateNotification(message)
    }

    private fun sendLog(message: String) {
        sendBroadcast(Intent(ACTION_LOG).putExtra(EXTRA_MESSAGE, message))
    }

    private fun sendConnectionState(state: String, remoteDevice: String, remoteIp: String, host: String) {
        sendBroadcast(
            Intent(ACTION_CONNECTION)
                .putExtra(EXTRA_STATE, state)
                .putExtra(EXTRA_REMOTE_DEVICE, remoteDevice.ifBlank { host })
                .putExtra(EXTRA_REMOTE_IP, remoteIp)
                .putExtra(EXTRA_LOCAL_DEVICE, currentLocalDevice)
        )
    }

    private fun buildNotification(content: String) = NotificationCompat.Builder(this, CHANNEL_ID)
        .setSmallIcon(R.drawable.ic_launcher_foreground)
        .setContentTitle("SyncFlow background sync")
        .setContentText(content)
        .setOngoing(true)
        .setOnlyAlertOnce(true)
        .build()

    private fun updateNotification(content: String) {
        val notification = buildNotification(content)
        NotificationManagerCompat.from(this).notify(NOTIFICATION_ID, notification)
    }

    private fun createChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val channel = NotificationChannel(
            CHANNEL_ID,
            "SyncFlow connection",
            NotificationManager.IMPORTANCE_LOW
        )
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(channel)
    }

    private fun sleepQuietly(durationMs: Long) {
        try {
            Thread.sleep(durationMs)
        } catch (_: InterruptedException) {
        }
    }

    private fun localIpv4Address(): String {
        return try {
            val interfaces = java.net.NetworkInterface.getNetworkInterfaces().toList()
            interfaces.asSequence()
                .flatMap { it.inetAddresses.toList().asSequence() }
                .firstOrNull { address -> !address.isLoopbackAddress && (address.hostAddress?.contains('.') == true) }
                ?.hostAddress
                ?: "0.0.0.0"
        } catch (_: Exception) {
            "0.0.0.0"
        }
    }

    override fun onCreate() {
        super.onCreate()
        createChannel()
    }

    companion object {
        const val ACTION_START = "com.syncflow.action.START"
        const val ACTION_STOP = "com.syncflow.action.STOP"
        const val ACTION_TEST = "com.syncflow.action.TEST"
        const val ACTION_STATUS = "com.syncflow.action.STATUS"
        const val ACTION_CONNECTION = "com.syncflow.action.CONNECTION"
        const val ACTION_LOG = "com.syncflow.action.LOG"
        const val ACTION_ERROR = "com.syncflow.action.ERROR"

        const val EXTRA_MESSAGE = "extra_message"
        const val EXTRA_HOST = "extra_host"
        const val EXTRA_PORT = "extra_port"
        const val EXTRA_DEVICE_NAME = "extra_device_name"
        const val EXTRA_STATE = "extra_state"
        const val EXTRA_REMOTE_DEVICE = "extra_remote_device"
        const val EXTRA_REMOTE_IP = "extra_remote_ip"
        const val EXTRA_LOCAL_DEVICE = "extra_local_device"

        const val PREFS_NAME = "syncflow_mobile_prefs"
        const val KEY_HOST = "desktop_host"
        const val KEY_PORT = "desktop_port"
        const val KEY_DEVICE_NAME = "device_name"
        const val KEY_AUTO_CONNECT = "auto_connect"

        const val DEFAULT_PORT = 45455
        private const val CLIENT_TCP_PORT = 45456
        private const val CONNECT_TIMEOUT_MS = 3000
        private const val RECONNECT_DELAY_MS = 4000L
        private const val CHANNEL_ID = "syncflow_connection"
        private const val NOTIFICATION_ID = 1001
    }
}