package com.syncflow

import android.content.Context
import android.content.Intent
import android.os.Parcelable
import kotlinx.coroutines.*
import kotlinx.parcelize.Parcelize
import java.net.DatagramPacket
import java.net.DatagramSocket

@Parcelize
data class DiscoveredPeer(
    val name: String,
    val ip: String,
    val port: Int
) : Parcelable

class PeerDiscoveryService(private val context: Context) {
    companion object {
        const val DISCOVERY_PORT = 45454
        const val BROADCAST_ADDRESS = "255.255.255.255"
        const val MULTICAST_ADDRESS = "239.255.42.99"
        const val MAGIC = "SYNCFLOW_PEER"
        const val DISCOVERY_TIMEOUT_MS = 3000
        const val ACTION_PEERS_FOUND = "com.syncflow.action.PEERS_FOUND"
        const val EXTRA_PEERS = "extra_peers"
    }

    private var discoveryJob: Job? = null
    private val discoveredPeers = mutableMapOf<String, DiscoveredPeer>()

    fun startDiscovery(onPeersFound: (List<DiscoveredPeer>) -> Unit) {
        stopDiscovery()

        discoveryJob = CoroutineScope(Dispatchers.IO).launch {
            discoveredPeers.clear()

            try {
                val socket = DatagramSocket()
                socket.broadcast = true
                socket.reuseAddress = true
                socket.soTimeout = DISCOVERY_TIMEOUT_MS

                val buffer = ByteArray(1024)
                val startTime = System.currentTimeMillis()

                while (System.currentTimeMillis() - startTime < DISCOVERY_TIMEOUT_MS && isActive) {
                    try {
                        val packet = DatagramPacket(buffer, buffer.size)
                        socket.receive(packet)

                        val data = String(packet.data, 0, packet.length, Charsets.UTF_8)
                        val remoteIp = packet.address.hostAddress ?: continue

                        if (data.startsWith(MAGIC)) {
                            parsePeerInfo(data, remoteIp)?.let { peer ->
                                discoveredPeers[peer.ip] = peer
                            }
                        }
                    } catch (_: java.net.SocketTimeoutException) {
                        // Expected, continue
                    }
                }

                socket.close()
            } catch (e: Exception) {
                e.printStackTrace()
            }

            withContext(Dispatchers.Main) {
                val peers = discoveredPeers.values.toList()
                onPeersFound(peers)
                broadcastPeers(peers)
            }
        }
    }

    fun stopDiscovery() {
        discoveryJob?.cancel()
        discoveryJob = null
        discoveredPeers.clear()
    }

    private fun parsePeerInfo(line: String, senderIp: String): DiscoveredPeer? {
        return try {
            val parts = line.split("|")
            if (parts.size >= 4 && parts[0] == MAGIC) {
                DiscoveredPeer(
                    name = parts[1],
                    ip = parts[2],
                    port = parts[3].toIntOrNull() ?: 45455
                )
            } else {
                null
            }
        } catch (_: Exception) {
            null
        }
    }

    private fun broadcastPeers(peers: List<DiscoveredPeer>) {
        val intent = Intent(ACTION_PEERS_FOUND).apply {
            putExtra(EXTRA_PEERS, peers.toTypedArray())
        }
        context.sendBroadcast(intent)
    }
}
