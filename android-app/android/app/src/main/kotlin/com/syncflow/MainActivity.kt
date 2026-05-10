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

        setSupportActionBar(binding.toolbar)

        binding.startSyncButton.setOnClickListener {
            val intent = Intent(this, SyncService::class.java).apply {
                putExtra(SyncService.EXTRA_REMOTE_HOST, BuildConfig.SYNCFLOW_REMOTE_HOST)
                putExtra(SyncService.EXTRA_REMOTE_PORT, BuildConfig.SYNCFLOW_REMOTE_PORT)
            }
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
                startForegroundService(intent)
            } else {
                startService(intent)
            }
            binding.statusText.text = "Status: Syncing..."
        }

        binding.discoverFab.setOnClickListener {
            // Trigger device discovery UI
        }
    }
}
