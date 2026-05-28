package com.syncflow

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.syncflow.databinding.ActivityMainBinding
import org.json.JSONObject

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        // Query native peer status JSON and update UI
        try {
            val statusJson = getPeerStatus()
            val obj = JSONObject(statusJson)
            val connections = obj.optJSONArray("connections")

            if (connections != null && connections.length() > 0) {
                val endpoint = connections.getString(0) // format: name@ip:port
                val deviceName = endpoint.substringBefore('@')
                binding.connectivityStateText.text = "Connected"
                binding.connectivityDeviceName.text = "Device: $deviceName"
                binding.connectivityPill.text = "Online"
            } else {
                binding.connectivityStateText.text = "Offline"
                binding.connectivityDeviceName.text = "Device: none"
                binding.connectivityPill.text = "Offline"
            }
        } catch (e: Exception) {
            binding.connectivityStateText.text = "Unknown"
            binding.connectivityDeviceName.text = "Device: error"
            binding.connectivityPill.text = "N/A"
        }
    }

    external fun getPeerStatus(): String
}