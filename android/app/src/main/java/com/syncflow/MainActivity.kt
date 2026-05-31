package com.syncflow

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.content.Intent
import android.os.Environment
import android.os.Handler
import android.os.Looper
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.activity.result.contract.ActivityResultContracts
import com.syncflow.databinding.ActivityMainBinding
import com.syncflow.ui.SettingsActivity
import com.syncflow.service.SyncService

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private var serviceStartRequested = false
    private val handler = Handler(Looper.getMainLooper())
    private val statusUpdateRunnable = object : Runnable {
        override fun run() {
            SyncPeerManager.updateStatus()
            handler.postDelayed(this, 1000) // Update every 1 second
        }
    }

    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        val allGranted = permissions.values.all { it }
        if (allGranted) {
            android.util.Log.d("SyncFlow", "File permissions granted")
        } else {
            android.util.Log.w("SyncFlow", "Some file permissions denied")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.openSettingsButton.setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }

        SyncPeerManager.setStatusListener { status ->
            renderStatus(status)
        }

        renderStatus(SyncPeerManager.snapshot())
        
        // Request file permissions on app startup
        requestFilePermissions()
    }

    override fun onResume() {
        super.onResume()

        if (!serviceStartRequested) {
            serviceStartRequested = true
            SyncService.startService(this)
        }

        handler.post(statusUpdateRunnable)
    }

    override fun onPause() {
        super.onPause()
        handler.removeCallbacks(statusUpdateRunnable)
    }

    override fun onDestroy() {
        super.onDestroy()
        handler.removeCallbacks(statusUpdateRunnable)
    }

    private fun renderStatus(status: PeerStatus) {
        if (status.connections.isNotEmpty()) {
            val endpoint = status.connections.first()
            val device = endpoint.substringBefore('@')
            binding.connectivityStateText.text = "Connected"
            binding.connectivityDeviceName.text = "Device: $device"
            binding.connectivityPill.text = "Online"
        } else {
            binding.connectivityStateText.text = if (status.running) "Discovering" else "Offline"
            binding.connectivityDeviceName.text = "Device: ${status.deviceName}"
            binding.connectivityPill.text = if (status.running) "Listening" else "Offline"
        }
    }

    private fun requestFilePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                android.util.Log.w("SyncFlow", "MANAGE_EXTERNAL_STORAGE permission not granted")
            }
            return
        }

        val permissions = arrayOf(
            Manifest.permission.READ_EXTERNAL_STORAGE,
            Manifest.permission.WRITE_EXTERNAL_STORAGE,
        )

        val needRequest = permissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }.toTypedArray()

        if (needRequest.isNotEmpty()) {
            android.util.Log.d("SyncFlow", "Requesting file permissions: ${needRequest.joinToString()}")
            requestPermissionLauncher.launch(needRequest)
        }
    }
}
