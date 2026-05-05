package com.syncflow

import android.os.Bundle
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.syncflow.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

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

        val prefs = getSharedPreferences(SyncConnectionService.PREFS_NAME, MODE_PRIVATE)
        binding.desktopHostInput.setText(prefs.getString(SyncConnectionService.KEY_HOST, "192.168.1.10"))
        binding.desktopPortInput.setText(prefs.getInt(SyncConnectionService.KEY_PORT, SyncConnectionService.DEFAULT_PORT).toString())
        binding.deviceNameInput.setText(prefs.getString(SyncConnectionService.KEY_DEVICE_NAME, defaultDeviceName()))

        binding.startButton.setOnClickListener {
            val host = binding.desktopHostInput.text.toString().trim()
            val port = binding.desktopPortInput.text.toString().trim().toIntOrNull() ?: SyncConnectionService.DEFAULT_PORT
            val deviceName = binding.deviceNameInput.text.toString().trim().ifBlank { defaultDeviceName() }

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
    }

    override fun onStart() {
        super.onStart()
        val filter = IntentFilter().apply {
            addAction(SyncConnectionService.ACTION_STATUS)
            addAction(SyncConnectionService.ACTION_CONNECTION)
            addAction(SyncConnectionService.ACTION_LOG)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(statusReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(statusReceiver, filter)
        }
    }

    override fun onStop() {
        super.onStop()
        unregisterReceiver(statusReceiver)
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