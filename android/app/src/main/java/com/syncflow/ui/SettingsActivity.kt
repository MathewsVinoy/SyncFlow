package com.syncflow.ui

import android.Manifest
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.util.Log
import android.util.TypedValue
import android.view.ViewGroup
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.syncflow.SyncPeerManager
import com.syncflow.R
import com.syncflow.service.SyncService
import java.io.File

class SettingsActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "SyncFlow"
        private const val PREFS_NAME = "syncflow_prefs"
        private const val PREF_SYNC_ENABLED = "sync_enabled"
        private const val PREF_AUTO_SYNC = "auto_sync"
        private const val PREF_SYNC_SOURCE_PATH = "sync_source_path"
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
            Log.d(TAG, "file permissions granted")
        } else {
            Log.w(TAG, "some file permissions denied")
            showMessage("Some permissions were denied. File sync may not work properly.")
        }
        updateUI()
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

    override fun onResume() {
        super.onResume()
        updateUI()
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
            promptForPath(
                title = "Set sync folder or file",
                currentValue = prefs.getString(PREF_SYNC_SOURCE_PATH, null).orEmpty(),
                allowFile = true
            ) { path ->
                prefs.edit().putString(PREF_SYNC_SOURCE_PATH, path).apply()
                showMessage("Sync source updated")
                restartServiceIfRunning()
                updateUI()
            }
        }

        findViewById<Button>(R.id.btn_select_download_dir).setOnClickListener {
            promptForPath(
                title = "Choose download directory",
                currentValue = prefs.getString(PREF_DOWNLOAD_DIR, null)
                    ?: Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS).absolutePath,
                allowFile = false
            ) { path ->
                prefs.edit().putString(PREF_DOWNLOAD_DIR, path).apply()
                SyncPeerManager.setDownloadDirectory(path)
                showMessage("Download directory updated")
                updateUI()
            }
        }

        updateUI()
    }

    private fun updateUI() {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val isServiceRunning = SyncService.isServiceEnabled(this)
        val syncSource = prefs.getString(PREF_SYNC_SOURCE_PATH, null)?.takeIf { it.isNotBlank() } ?: "Not set"
        val downloadDir = prefs.getString(PREF_DOWNLOAD_DIR, null)?.takeIf { it.isNotBlank() }
            ?: Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS).absolutePath
        val storageAccess = if (hasAllFilesAccess()) "Granted" else "Needed"

        syncStatusText.text = buildString {
            appendLine(if (isServiceRunning) "Background sync is RUNNING" else "Background sync is STOPPED")
            appendLine("Sync source: $syncSource")
            appendLine("Receive location: $downloadDir")
            appendLine("Download dir: $downloadDir")
            append("Storage access: $storageAccess")
        }
    }

    private fun requestPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!hasAllFilesAccess()) {
                requestAllFilesAccess()
            }
            return
        }

        val permissions = arrayOf(
            Manifest.permission.READ_EXTERNAL_STORAGE,
            Manifest.permission.WRITE_EXTERNAL_STORAGE,
        )

        val needRequest = permissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }.toTypedArray()

        if (needRequest.isNotEmpty()) {
            requestPermissionLauncher.launch(needRequest)
        }
    }

    private fun requestAllFilesAccess() {
        try {
            val intent = Intent(
                Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:$packageName")
            )
            startActivity(intent)
            showMessage("Grant file access so SyncFlow can use Downloads and custom folders")
        } catch (e: Exception) {
            Log.w(TAG, "failed to open all files access settings", e)
        }
    }

    private fun hasAllFilesAccess(): Boolean {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.R || Environment.isExternalStorageManager()
    }

    private fun promptForPath(title: String, currentValue: String, allowFile: Boolean, onSave: (String) -> Unit) {
        val input = EditText(this).apply {
            setText(currentValue)
            setSelection(text.length)
            setSingleLine(true)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
        }

        AlertDialog.Builder(this)
            .setTitle(title)
            .setMessage(if (allowFile) "Enter an existing folder or file path on the device." else "Enter an existing folder path on the device.")
            .setView(input)
            .setPositiveButton("Save") { _, _ ->
                val path = input.text?.toString()?.trim().orEmpty()
                val file = File(path)
                val valid = if (allowFile) file.exists() else file.exists() && file.isDirectory

                if (!valid) {
                    showMessage("Path does not exist")
                    return@setPositiveButton
                }

                onSave(path)
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun restartServiceIfRunning() {
        if (!SyncService.isServiceEnabled(this)) {
            return
        }

        val serviceIntent = Intent(this, SyncService::class.java)
        stopService(serviceIntent)
        SyncService.startService(this)
    }

    private fun showMessage(message: String) {
        Log.d(TAG, message)
        android.widget.Toast.makeText(this, message, android.widget.Toast.LENGTH_SHORT).show()
    }
}
