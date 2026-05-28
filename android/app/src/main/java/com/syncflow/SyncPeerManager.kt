package com.syncflow

import android.content.Context
import android.net.wifi.WifiManager
import android.os.Build
import android.util.Log
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.PrintWriter
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.InetSocketAddress
import java.net.NetworkInterface
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketException
import java.net.SocketTimeoutException
import java.nio.charset.StandardCharsets
import java.util.Collections
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

object SyncPeerManager {

    private const val TAG = "SyncFlow"
    private const val UDP_DISCOVERY_PORT = 45454
    private const val TCP_PORT = 45455
    private const val BROADCAST_ADDRESS = "255.255.255.255"
    private const val PROTOCOL_MAGIC = "SYNCFLOW_PEER"
    private const val DISCOVERY_INTERVAL_MS = 2_000L

    private val running = AtomicBoolean(false)
    private val statusLock = Any()
    private val connections = LinkedHashSet<String>()

    @Volatile
    private var deviceName: String = resolveDeviceName()

    @Volatile
    private var localIp: String = "0.0.0.0"

    @Volatile
    private var statusListener: ((PeerStatus) -> Unit)? = null

    @Volatile
    private var serverSocket: ServerSocket? = null

    @Volatile
    private var multicastLock: WifiManager.MulticastLock? = null

    fun start(context: Context) {
        if (!running.compareAndSet(false, true)) {
            return
        }

        deviceName = resolveDeviceName()
        localIp = findLocalIpv4Address()
        acquireMulticastLock(context.applicationContext)

        startTcpServer()
        startDiscoveryBroadcaster()
        startDiscoveryListener()
        emitStatus("peer manager started")
    }

    fun stop() {
        if (!running.compareAndSet(true, false)) {
            return
        }

        runCatching { serverSocket?.close() }
        serverSocket = null

        runCatching { multicastLock?.release() }
        multicastLock = null

        synchronized(statusLock) {
            connections.clear()
        }

        emitStatus("peer manager stopped")
    }

    fun setStatusListener(listener: (PeerStatus) -> Unit) {
        statusListener = listener
        emitStatus("status listener attached")
    }

    fun snapshot(): PeerStatus {
        val connectionSnapshot = synchronized(statusLock) { connections.toList() }
        return PeerStatus(
            running = running.get(),
            deviceName = deviceName,
            localIp = localIp,
            connections = connectionSnapshot
        )
    }

    private fun emitStatus(reason: String) {
        val snapshot = snapshot()
        Log.d(TAG, "$reason: $snapshot")
        statusListener?.invoke(snapshot)
    }

    private fun resolveDeviceName(): String {
        val model = Build.MODEL?.trim().orEmpty()
        val device = Build.DEVICE?.trim().orEmpty()
        return when {
            model.isNotBlank() && device.isNotBlank() && model != device -> "$model ($device)"
            model.isNotBlank() -> model
            device.isNotBlank() -> device
            else -> "android-device"
        }
    }

    private fun findLocalIpv4Address(): String {
        return try {
            val interfaces = NetworkInterface.getNetworkInterfaces() ?: return "0.0.0.0"
            while (interfaces.hasMoreElements()) {
                val networkInterface = interfaces.nextElement()
                if (!networkInterface.isUp || networkInterface.isLoopback) {
                    continue
                }

                val addresses = networkInterface.inetAddresses
                while (addresses.hasMoreElements()) {
                    val address = addresses.nextElement()
                    if (address is Inet4Address && !address.isLoopbackAddress) {
                        return address.hostAddress ?: "0.0.0.0"
                    }
                }
            }
            "0.0.0.0"
        } catch (e: Exception) {
            Log.w(TAG, "failed to resolve local IPv4 address", e)
            "0.0.0.0"
        }
    }

    private fun acquireMulticastLock(context: Context) {
        try {
            val wifiManager = context.getSystemService(Context.WIFI_SERVICE) as? WifiManager ?: return
            multicastLock = wifiManager.createMulticastLock("syncflow-peer").apply {
                setReferenceCounted(false)
                acquire()
            }
        } catch (e: Exception) {
            Log.w(TAG, "failed to acquire multicast lock", e)
        }
    }

    private fun startDiscoveryBroadcaster() {
        thread(name = "syncflow-discovery", isDaemon = true) {
            val payload = discoveryPayload().toByteArray(StandardCharsets.UTF_8)
            var socket: DatagramSocket? = null
            try {
                socket = DatagramSocket()
                socket.reuseAddress = true
                socket.broadcast = true
                Log.d(TAG, "discovery broadcaster initialized on port ${socket.localPort}")

                while (running.get()) {
                    try {
                        socket.send(
                            DatagramPacket(
                                payload,
                                payload.size,
                                InetSocketAddress(BROADCAST_ADDRESS, UDP_DISCOVERY_PORT)
                            )
                        )
                    } catch (e: Exception) {
                        Log.w(TAG, "failed to broadcast discovery packet", e)
                    }

                    try {
                        Thread.sleep(DISCOVERY_INTERVAL_MS)
                    } catch (_: InterruptedException) {
                        break
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "failed to initialize discovery broadcaster", e)
            } finally {
                socket?.close()
            }
        }
    }

    private fun startDiscoveryListener() {
        thread(name = "syncflow-discovery-listener", isDaemon = true) {
            var socket: DatagramSocket? = null
            try {
                socket = DatagramSocket(UDP_DISCOVERY_PORT)
                socket.reuseAddress = true
                socket.broadcast = true
                Log.d(TAG, "discovery listener started on port $UDP_DISCOVERY_PORT")

                val buffer = ByteArray(1024)
                while (running.get()) {
                    try {
                        val packet = DatagramPacket(buffer, buffer.size)
                        socket.receive(packet)
                        val message = String(packet.data, 0, packet.length, StandardCharsets.UTF_8).trim()
                        
                        if (message.startsWith(PROTOCOL_MAGIC)) {
                            val parts = message.removePrefix("$PROTOCOL_MAGIC|").split('|')
                            if (parts.size >= 3) {
                                val peerName = parts[0]
                                val peerIp = parts[1]
                                val peerPort = parts[2]
                                val peerAddress = packet.address.hostAddress ?: peerIp
                                
                                if (peerAddress != localIp) {
                                    Log.d(TAG, "discovered peer: $peerName @ $peerAddress:$peerPort")
                                    connectToPeer(peerName, peerAddress, peerPort.toIntOrNull() ?: TCP_PORT)
                                }
                            }
                        }
                    } catch (e: SocketTimeoutException) {
                        continue
                    } catch (e: Exception) {
                        Log.w(TAG, "discovery listener error", e)
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "failed to initialize discovery listener", e)
            } finally {
                socket?.close()
            }
        }
    }

    private fun connectToPeer(peerName: String, peerIp: String, peerPort: Int) {
        thread(name = "syncflow-connect-$peerName", isDaemon = true) {
            try {
                val socket = Socket(peerIp, peerPort)
                socket.soTimeout = 5_000
                val reader = BufferedReader(InputStreamReader(socket.getInputStream(), StandardCharsets.UTF_8))
                val writer = PrintWriter(socket.getOutputStream(), true, StandardCharsets.UTF_8)
                
                writer.println("HELLO|$deviceName|$localIp|$TCP_PORT")
                
                val response = reader.readLine()
                if (response?.startsWith("CONNECTED_SUCCESS|") == true) {
                    val endpointKey = "$peerName@$peerIp:$peerPort"
                    synchronized(statusLock) {
                        connections.clear()
                        connections.add(endpointKey)
                    }
                    emitStatus("connected to $endpointKey")
                    
                    while (running.get() && !socket.isClosed) {
                        try {
                            val line = reader.readLine() ?: break
                            if (line.isNotBlank()) {
                                Log.d(TAG, "peer message: $line")
                            }
                        } catch (_: SocketTimeoutException) {
                            continue
                        }
                    }
                }
                socket.close()
            } catch (e: Exception) {
                Log.w(TAG, "failed to connect to peer $peerName", e)
            }
        }
    }


    private fun startTcpServer() {
        thread(name = "syncflow-tcp-server", isDaemon = true) {
            try {
                ServerSocket().use { server ->
                    server.reuseAddress = true
                    server.bind(InetSocketAddress(TCP_PORT))
                    serverSocket = server
                    emitStatus("tcp server listening on $TCP_PORT")

                    while (running.get()) {
                        try {
                            val socket = server.accept()
                            handleConnection(socket)
                        } catch (e: SocketException) {
                            if (running.get()) {
                                Log.w(TAG, "tcp server socket error", e)
                            }
                            break
                        }
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "failed to start tcp server", e)
                emitStatus("tcp server failed")
            }
        }
    }

    private fun handleConnection(socket: Socket) {
        thread(name = "syncflow-connection-${socket.inetAddress.hostAddress}", isDaemon = true) {
            var endpointKey = "${socket.inetAddress.hostAddress ?: "unknown"}:${socket.port}"
            try {
                socket.soTimeout = 1_000
                val reader = BufferedReader(InputStreamReader(socket.getInputStream(), StandardCharsets.UTF_8))
                val writer = PrintWriter(socket.getOutputStream(), true, StandardCharsets.UTF_8)

                writer.println("CONNECTED_SUCCESS|$deviceName|$localIp")

                while (running.get() && !socket.isClosed) {
                    val line = try {
                        reader.readLine()
                    } catch (_: SocketTimeoutException) {
                        continue
                    } ?: break

                    if (line.isBlank()) {
                        continue
                    }

                    Log.d(TAG, "tcp message: $line")

                    when {
                        line.startsWith("HELLO|") -> {
                            val parts = line.removePrefix("HELLO|").split('|')
                            if (parts.size >= 4) {
                                val peerName = parts[1]
                                val peerIp = parts[2]
                                val peerPort = parts[3]
                                endpointKey = "$peerName@$peerIp:$peerPort"
                                synchronized(statusLock) {
                                    connections.clear()
                                    connections.add(endpointKey)
                                }
                                emitStatus("connected to $endpointKey")
                                writer.println("CONNECTED_SUCCESS|$deviceName|$localIp")
                            }
                        }
                        line.startsWith("CONNECTED_SUCCESS|") -> {
                            val parts = line.split('|')
                            if (parts.size >= 3) {
                                val peerName = parts[1]
                                val peerIp = parts[2]
                                endpointKey = "$peerName@$peerIp:$TCP_PORT"
                                synchronized(statusLock) {
                                    connections.clear()
                                    connections.add(endpointKey)
                                }
                                emitStatus("connection acknowledged by $endpointKey")
                            }
                        }
                        line.startsWith("SHARE_BUSY|") -> {
                            Log.w(TAG, "peer reported busy: $line")
                        }
                        line.startsWith("FILE_RECEIVED|") || line.startsWith("SYNC_BEGIN|") || line.startsWith("SYNC_END|") || line.startsWith("FILE_ENTRY|") || line.startsWith("FILE_DONE|") -> {
                            Log.d(TAG, "peer sync message: $line")
                        }
                        else -> {
                            Log.d(TAG, "peer message: $line")
                        }
                    }
                }
            } catch (e: Exception) {
                Log.w(TAG, "connection handler failed", e)
            } finally {
                socket.close()
                synchronized(statusLock) {
                    connections.remove(endpointKey)
                }
                emitStatus("connection closed: $endpointKey")
            }
        }
    }

    private fun discoveryPayload(): String {
        return "$PROTOCOL_MAGIC|$deviceName|$localIp|$TCP_PORT\n"
    }
}
