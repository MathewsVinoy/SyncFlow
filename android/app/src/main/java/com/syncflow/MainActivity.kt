package com.syncflow

import android.os.Bundle
import android.content.Intent
import androidx.appcompat.app.AppCompatActivity
import com.syncflow.databinding.ActivityMainBinding
import com.syncflow.ui.SettingsActivity
import com.syncflow.service.SyncService

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private var serviceStartRequested = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.openSettingsButton.setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }

        SyncPeerManager.setStatusListener { status ->
            runOnUiThread { renderStatus(status) }
        }

        renderStatus(SyncPeerManager.snapshot())
    }

    override fun onResume() {
        super.onResume()

        if (!serviceStartRequested) {
            serviceStartRequested = true
            SyncService.startService(this)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
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
}
