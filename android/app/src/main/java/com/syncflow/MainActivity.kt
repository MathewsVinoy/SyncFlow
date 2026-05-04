package com.syncflow

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Button
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat

class MainActivity : AppCompatActivity() {

    private lateinit var configPathView: TextView
    private lateinit var deviceNameView: TextView
    private lateinit var editButton: Button
    private lateinit var deviceInfoButton: Button
    private lateinit var startButton: Button
    private val handler = Handler(Looper.getMainLooper())
    private val refreshRunnable = object : Runnable {
        override fun run() {
            refreshStatus()
            handler.postDelayed(this, 1000)
        }
    }

    private val requestNotificationPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        // no-op, permission result handled if needed
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        configPathView = findViewById(R.id.configPath)
        deviceNameView = findViewById(R.id.deviceName)
        editButton = findViewById(R.id.editButton)
        deviceInfoButton = findViewById(R.id.deviceInfoButton)
        startButton = findViewById(R.id.startButton)

        val configFile = getConfigFilePath()
        configPathView.text = configFile
        refreshStatus()

        editButton.setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }

        deviceInfoButton.setOnClickListener {
            startActivity(Intent(this, DeviceInfoActivity::class.java))
        }

        startButton.setOnClickListener {
            startSyncService(configFile)
        }

        // Request notification permission on Android 13+
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) {
                requestNotificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
            }
        }

        // Auto-start the service when app opens
        startSyncService(configFile)
    }

    override fun onResume() {
        super.onResume()
        handler.post(refreshRunnable)
    }

    override fun onPause() {
        handler.removeCallbacks(refreshRunnable)
        super.onPause()
    }

    private fun startSyncService(configFile: String) {
        val intent = Intent(this, SyncService::class.java)
        intent.putExtra("config_path", configFile)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent)
        } else {
            startService(intent)
        }
    }

    private fun getConfigFilePath(): String {
        val f = filesDir.resolve("config.json")
        if (!f.exists()) {
            // create default minimal config
            f.writeText("{\n  \"file_sync\": {\n    \"enabled\": true,\n    \"source_path\": \"sync/\",\n    \"receive_dir\": \"received\",\n    \"device_name\": \"\"\n  },\n  \"security\": {\n    \"enabled\": true,\n    \"require_approval\": true\n  }\n}\n")
        }
        return f.absolutePath
    }

    private fun refreshStatus() {
        val serviceState = runCatching { NativeBridge.getStatus() }.getOrDefault("stopped")
        val statusFile = filesDir.resolve("sync_status.json")
        val peerState = if (statusFile.exists()) "Peers discovered" else "Waiting for peers"
        deviceNameView.text = "Service: $serviceState | $peerState"
    }
}