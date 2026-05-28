package com.syncflow

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.syncflow.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        SyncPeerManager.setStatusListener { status ->
            runOnUiThread { renderStatus(status) }
        }
        SyncPeerManager.start(applicationContext)
        renderStatus(SyncPeerManager.snapshot())
    }

    override fun onDestroy() {
        SyncPeerManager.stop()
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
