package com.syncflow

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import java.io.File

class DeviceInfoActivity : AppCompatActivity() {
    private lateinit var infoView: TextView
    private val handler = Handler(Looper.getMainLooper())
    private val refreshRunnable = object : Runnable {
        override fun run() {
            updateInfo()
            handler.postDelayed(this, 1000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_device_info)

        infoView = findViewById(R.id.deviceInfo)
        updateInfo()
    }

    override fun onResume() {
        super.onResume()
        handler.post(refreshRunnable)
    }

    override fun onPause() {
        handler.removeCallbacks(refreshRunnable)
        super.onPause()
    }

    private fun updateInfo() {
        val statusFile = filesDir.resolve("sync_status.json")
        val serviceState = runCatching { NativeBridge.getStatus() }.getOrDefault("stopped")
        val peerText = if (statusFile.exists()) statusFile.readText() else "No peers discovered yet."
        infoView.text = "Service: $serviceState\n\n$peerText"
    }
}
