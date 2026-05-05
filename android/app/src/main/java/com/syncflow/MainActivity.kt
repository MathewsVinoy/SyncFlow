package com.syncflow

import android.os.Bundle
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.Manifest
import android.widget.ListView
import androidx.appcompat.app.AppCompatActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import com.syncflow.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private lateinit var peerDiscovery: PeerDiscoveryService
    private lateinit var peerAdapter: PeerAdapter

    private val peersReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action == PeerDiscoveryService.ACTION_PEERS_FOUND) {
                @Suppress("DEPRECATION")
                val peers = intent.getParcelableArrayExtra(
                    PeerDiscoveryService.EXTRA_PEERS,
                    DiscoveredPeer::class.java
                )?.toList() ?: emptyList()
                peerAdapter.updatePeers(peers)
                if (peers.isNotEmpty()) {
                    appendLog("Found ${peers.size} peer(s)")
                }
            }
        }
    }

    private val notificationPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (!granted) {
            appendLog("Notification permission denied; foreground sync may not start reliably")
        }
    }

    private val statusReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent == null) return
            when (intent.action) {
                SyncConnectionService.ACTION_STATUS -> {
                    binding.statusText.text = intent.getStringExtra(SyncConnectionService.EXTRA_MESSAGE).orEmpty()
                }
                SyncConnectionService.ACTION_CONNECTION -> {
                    val remote = intent.getStringExtra(SyncConnectionService.EXTRA_REMOTE_DEVICE).orEmpty()
                    val local = intent.getStringExtra(SyncConnectionService.EXTRA_LOCAL_DEVICE).orEmpty()
                    val remoteIp = intent.getStringExtra(SyncConnectionService.EXTRA_REMOTE_IP).orEmpty()
                    val state = intent.getStringExtra(SyncConnectionService.EXTRA_STATE).orEmpty()
                    binding.remoteDeviceText.text = if (remote.isNotEmpty()) {
                        "Desktop device: $remote${if (remoteIp.isNotEmpty()) " ($remoteIp)" else ""}"
                    } else {
                        "Desktop device: -"
                    }
                    binding.localDeviceText.text = if (local.isNotEmpty()) {
                        "Local device: $local"
                    } else {
                        "Local device: -"
                    }
                    binding.connectedDevicesText.text = if (remote.isNotEmpty()) {
                        "1 active connection\n• $remote"
                    } else {
                        "No active connections"
                    }
                    if (state.isNotEmpty()) {
                        binding.statusText.text = state
                    }
                }
                SyncConnectionService.ACTION_ERROR -> {
                    val error = intent.getStringExtra(SyncConnectionService.EXTRA_MESSAGE).orEmpty()
                    binding.lastErrorText.text = if (error.isNotBlank()) "Last error: $error" else "Last error: -"
                    appendLog("ERROR: $error")
                    binding.statusText.text = "Disconnected"
                }
                SyncConnectionService.ACTION_LOG -> {
                    appendLog(intent.getStringExtra(SyncConnectionService.EXTRA_MESSAGE).orEmpty())
                }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        peerDiscovery = PeerDiscoveryService(this)
        peerAdapter = PeerAdapter(this, mutableListOf())
        binding.peersListView.adapter = peerAdapter
        binding.peersListView.setOnItemClickListener { _, _, position, _ ->
            val peer = peerAdapter.getItem(position)
            binding.desktopHostInput.setText(peer.ip)
            binding.desktopPortInput.setText(peer.port.toString())
            appendLog("Selected peer: ${peer.name} at ${peer.ip}:${peer.port}")
        }

        val prefs = getSharedPreferences(SyncConnectionService.PREFS_NAME, MODE_PRIVATE)
        binding.desktopHostInput.setText(prefs.getString(SyncConnectionService.KEY_HOST, "192.168.1.10"))
        binding.desktopPortInput.setText(prefs.getInt(SyncConnectionService.KEY_PORT, SyncConnectionService.DEFAULT_PORT).toString())
        binding.deviceNameInput.setText(prefs.getString(SyncConnectionService.KEY_DEVICE_NAME, defaultDeviceName()))

        binding.startButton.setOnClickListener {
            val host = binding.desktopHostInput.text.toString().trim()
            val port = binding.desktopPortInput.text.toString().trim().toIntOrNull() ?: SyncConnectionService.DEFAULT_PORT
            val deviceName = binding.deviceNameInput.text.toString().trim().ifBlank { defaultDeviceName() }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != android.content.pm.PackageManager.PERMISSION_GRANTED
            ) {
                notificationPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
            }

            prefs.edit()
                .putString(SyncConnectionService.KEY_HOST, host)
                .putInt(SyncConnectionService.KEY_PORT, port)
                .putString(SyncConnectionService.KEY_DEVICE_NAME, deviceName)
                .putBoolean(SyncConnectionService.KEY_AUTO_CONNECT, true)
                .apply()

            val intent = Intent(this, SyncConnectionService::class.java).apply {
                action = SyncConnectionService.ACTION_START
                putExtra(SyncConnectionService.EXTRA_HOST, host)
                putExtra(SyncConnectionService.EXTRA_PORT, port)
                putExtra(SyncConnectionService.EXTRA_DEVICE_NAME, deviceName)
            }
            ContextCompat.startForegroundService(this, intent)
            appendLog("Starting background connection to $host:$port as $deviceName")
        }

        binding.stopButton.setOnClickListener {
            prefs.edit().putBoolean(SyncConnectionService.KEY_AUTO_CONNECT, false).apply()
            val intent = Intent(this, SyncConnectionService::class.java).apply {
                action = SyncConnectionService.ACTION_STOP
            }
            stopService(intent)
            appendLog("Background connection stopped")
        }

        binding.discoverButton.setOnClickListener {
            appendLog("Discovering peers on network...")
            peerDiscovery.startDiscovery { peers ->
                if (peers.isEmpty()) {
                    appendLog("No peers found")
                } else {
                    appendLog("Discovered ${peers.size} peer(s)")
                }
            }
        }

        binding.testButton.setOnClickListener {
            val host = binding.desktopHostInput.text.toString().trim()
            val port = binding.desktopPortInput.text.toString().trim().toIntOrNull() ?: SyncConnectionService.DEFAULT_PORT
            val deviceName = binding.deviceNameInput.text.toString().trim().ifBlank { defaultDeviceName() }

            val intent = Intent(this, SyncConnectionService::class.java).apply {
                action = SyncConnectionService.ACTION_TEST
                putExtra(SyncConnectionService.EXTRA_HOST, host)
                putExtra(SyncConnectionService.EXTRA_PORT, port)
                putExtra(SyncConnectionService.EXTRA_DEVICE_NAME, deviceName)
            }
            ContextCompat.startForegroundService(this, intent)
            appendLog("Testing connection to $host:$port")
        }
    }

    override fun onStart() {
        super.onStart()
        val filter = IntentFilter().apply {
            addAction(SyncConnectionService.ACTION_STATUS)
            addAction(SyncConnectionService.ACTION_CONNECTION)
            addAction(SyncConnectionService.ACTION_ERROR)
            addAction(SyncConnectionService.ACTION_LOG)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(statusReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
            registerReceiver(peersReceiver, IntentFilter(PeerDiscoveryService.ACTION_PEERS_FOUND), Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(statusReceiver, filter)
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(peersReceiver, IntentFilter(PeerDiscoveryService.ACTION_PEERS_FOUND))
        }
    }

    override fun onStop() {
        super.onStop()
        peerDiscovery.stopDiscovery()
        unregisterReceiver(statusReceiver)
        try {
            unregisterReceiver(peersReceiver)
        } catch (_: Exception) {
        }
    }

    private fun appendLog(message: String) {
        if (message.isBlank()) return
        val current = binding.connectionLogText.text?.toString().orEmpty()
        binding.connectionLogText.text = if (current == "Waiting for connection…" || current.isBlank()) {
            message
        } else {
            "$current\n$message"
        }
    }

    private fun defaultDeviceName(): String {
        return "Android-${Build.MODEL.ifBlank { Build.DEVICE }}"
    }
}