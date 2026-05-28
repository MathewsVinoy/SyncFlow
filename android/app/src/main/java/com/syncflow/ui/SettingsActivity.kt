package com.syncflow.ui

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.util.Log
import android.widget.Button
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.syncflow.R
import com.syncflow.service.SyncService
import java.io.File

class SettingsActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "SyncFlow"
        private const val PREFS_NAME = "syncflow_prefs"
        private const val PREF_SYNC_ENABLED = "sync_enabled"
        private const val PREF_AUTO_SYNC = "auto_sync"
        private const val PREF_DOWNLOAD_DIR = "download_dir"
    }

    private lateinit var serviceToggle: CheckBox
    private lateinit var autoSyncToggle: CheckBox
    private lateinit var syncStatusText: TextView
    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        val allGranted = permissions.values.all { it }
        if (allGranted) {
            Log.d(TAG, "All file permissions granted")
            updateUI()
        } else {
            Log.w(TAG, "Some file permissions denied")
            showMessage("Some permissions were denied. File sync may not work properly.")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        serviceToggle = findViewById(R.id.service_toggle)
        autoSyncToggle = findViewById(R.id.auto_sync_toggle)
        syncStatusText = findViewById(R.id.sync_status_text)

        setupUI()
        requestPermissions()
    }

    private fun setupUI() {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        
        serviceToggle.isChecked = SyncService.isServiceEnabled(this)
        autoSyncToggle.isChecked = prefs.getBoolean(PREF_AUTO_SYNC, true)

        serviceToggle.setOnCheckedChangeListener { _, isChecked ->
            prefs.edit().putBoolean(PREF_SYNC_ENABLED, isChecked).apply()
            if (isChecked) {
                SyncService.startService(this)
                showMessage("Background sync started")
            } else {
                SyncService.stopService(this)
                showMessage("Background sync stopped")
            }
            updateUI()
        }

        autoSyncToggle.setOnCheckedChangeListener { _, isChecked ->
            prefs.edit().putBoolean(PREF_AUTO_SYNC, isChecked).apply()
            if (isChecked) {
                showMessage("Auto-sync enabled")
            } else {
                showMessage("Auto-sync disabled - use manual sync only")
            }
        }

        findViewById<Button>(R.id.btn_select_sync_folder).setOnClickListener {
            showMessage("Sync folder selection coming soon")
        }

        findViewById<Button>(R.id.btn_select_download_dir).setOnClickListener {
            selectDownloadDirectory()
        }

        updateUI()
    }

    private fun updateUI() {
        val isServiceRunning = SyncService.isServiceEnabled(this)
        val status = if (isServiceRunning) {
            "Background sync is RUNNING"
        } else {
            "Background sync is STOPPED"
        }
        syncStatusText.text = status
    }

    private fun selectDownloadDirectory() {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val currentDir = prefs.getString(PREF_DOWNLOAD_DIR, null) ?: 
            Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS).absolutePath
        
        showMessage("Download directory: $currentDir")
    }

    private fun requestPermissions() {
        val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            arrayOf(
                Manifest.permission.MANAGE_EXTERNAL_STORAGE,
                Manifest.permission.INTERNET,
                Manifest.permission.ACCESS_NETWORK_STATE
            )
        } else {
            arrayOf(
                Manifest.permission.READ_EXTERNAL_STORAGE,
                Manifest.permission.WRITE_EXTERNAL_STORAGE,
                Manifest.permission.INTERNET,
                Manifest.permission.ACCESS_NETWORK_STATE
            )
        }

        val needRequest = permissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }.toTypedArray()

        if (needRequest.isNotEmpty()) {
            requestPermissionLauncher.launch(needRequest)
        }
    }

    private fun showMessage(message: String) {
        Log.d(TAG, message)
        android.widget.Toast.makeText(this, message, android.widget.Toast.LENGTH_SHORT).show()
    }
}
