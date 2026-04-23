package com.syncflow

import android.content.Context
import android.content.Intent
import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import android.widget.Toast

/**
 * Main Activity: User interface for SyncFlow Android app
 *
 * Displays:
 * - Sync status
 * - Connected peers
 * - Sync queue size
 * - Control buttons (Start, Stop, Add Folder)
 */
class MainActivity : AppCompatActivity() {
    companion object {
        init {
            System.loadLibrary("syncflow_jni")
        }
    }

    private lateinit var statusTextView: TextView
    private lateinit var peersTextView: TextView
    private lateinit var queueTextView: TextView
    private lateinit var startButton: Button
    private lateinit var stopButton: Button

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // Initialize views
        statusTextView = findViewById(R.id.status_text)
        peersTextView = findViewById(R.id.peers_text)
        queueTextView = findViewById(R.id.queue_text)
        startButton = findViewById(R.id.start_button)
        stopButton = findViewById(R.id.stop_button)

        // Setup button listeners
        startButton.setOnClickListener { startSyncService() }
        stopButton.setOnClickListener { stopSyncService() }

        // Update UI periodically
        updateStatus()
    }

    private fun startSyncService() {
        val intent = Intent(this, SyncService::class.java)
        startForegroundService(intent)
        Toast.makeText(this, "SyncFlow started", Toast.LENGTH_SHORT).show()
    }

    private fun stopSyncService() {
        val intent = Intent(this, SyncService::class.java)
        stopService(intent)
        Toast.makeText(this, "SyncFlow stopped", Toast.LENGTH_SHORT).show()
    }

    private fun updateStatus() {
        // This will be replaced with actual status queries
        statusTextView.text = "Status: Initializing..."
        peersTextView.text = "Connected Peers: 0"
        queueTextView.text = "Pending Syncs: 0"

        // TODO: Query sync status periodically
        // TODO: Update UI with real data
    }
}
