package com.syncflow

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import java.io.File

class DeviceInfoActivity : AppCompatActivity() {
    private lateinit var infoView: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_device_info)

        infoView = findViewById(R.id.deviceInfo)
        updateInfo()
    }

    private fun updateInfo() {
        // Read a status file written by native layer (stub) to display peers
        val statusFile = filesDir.resolve("sync_status.json")
        if (!statusFile.exists()) {
            infoView.text = "No peers discovered yet."
            return
        }
        infoView.text = statusFile.readText()
    }
}
