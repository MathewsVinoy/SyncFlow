package com.syncflow

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.syncflow.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.btnEditConfig.setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }

        binding.btnDeviceInfo.setOnClickListener {
            startActivity(Intent(this, DeviceInfoActivity::class.java))
        }

        binding.btnStart.setOnClickListener {
            val svc = Intent(this, SyncService::class.java)
            svc.action = SyncService.ACTION_START
            startService(svc)
        }

        binding.btnStop.setOnClickListener {
            val svc = Intent(this, SyncService::class.java)
            svc.action = SyncService.ACTION_STOP
            startService(svc)
        }
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