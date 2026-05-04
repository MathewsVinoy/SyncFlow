package com.syncflow

import android.content.Intent
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {

    private lateinit var configPathView: TextView
    private lateinit var deviceNameView: TextView
    private lateinit var editButton: Button
    private lateinit var deviceInfoButton: Button
    private lateinit var startButton: Button

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

        editButton.setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }

        deviceInfoButton.setOnClickListener {
            startActivity(Intent(this, DeviceInfoActivity::class.java))
        }

        startButton.setOnClickListener {
            // Start foreground background service
            val intent = Intent(this, SyncService::class.java)
            intent.putExtra("config_path", configFile)
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
}
package com.syncflow

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.widget.TextView
import com.syncflow.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        // Example of a call to a native method
        binding.sampleText.text = stringFromJNI()
    }

    /**
     * A native method that is implemented by the 'syncflow' native library,
     * which is packaged with this application.
     */
    external fun stringFromJNI(): String

    companion object {
        // Used to load the 'syncflow' library on application startup.
        init {
            System.loadLibrary("syncflow")
        }
    }
}