package com.syncflow

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.core.content.ContextCompat.startForegroundService
import com.syncflow.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private var isServiceRunning = false

    private val manageAllFilesLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        ensurePermissions()
    }

    private fun getRuntimePermissions(): Array<String> {
        val permissions = mutableListOf<String>()

        // Only request dangerous storage permissions that are valid for the current API level.
        when {
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.R -> {
                // Android 11+: storage access is handled via MANAGE_EXTERNAL_STORAGE settings screen.
            }
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q -> {
                permissions.add(Manifest.permission.READ_EXTERNAL_STORAGE)
            }
            else -> {
                permissions.add(Manifest.permission.READ_EXTERNAL_STORAGE)
                permissions.add(Manifest.permission.WRITE_EXTERNAL_STORAGE)
            }
        }

        return permissions.toTypedArray()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        // Set up logging
        LogManager.logs.observe(this) { logs ->
            binding.debugLogText.text = logs
            // Auto-scroll to bottom
            binding.debugLogText.post {
                binding.debugLogText.layout?.let {
                    val scrollAmount = binding.debugLogText.layout.height - binding.debugLogText.height
                    if (scrollAmount > 0) {
                        binding.debugLogText.scrollTo(0, scrollAmount)
                    }
                }
            }
        }

        // Call native method to verify JNI works
        try {
            val nativeMsg = stringFromJNI()
            LogManager.addLog("Native: $nativeMsg")
        } catch (e: Exception) {
            LogManager.addLog("Native call error: ${e.message}")
        }

        // Update device info
        updateDeviceInfo()

        // Set up button listeners
        binding.btnStart.setOnClickListener { startSyncService() }
        binding.btnStop.setOnClickListener { stopSyncService() }
        binding.btnClearLogs.setOnClickListener { 
            LogManager.clear()
            LogManager.addLog("Logs cleared by user")
        }

        // Request permissions only; start is user-triggered from the Start button
        ensurePermissions()
    }

    private fun updateDeviceInfo() {
        val deviceName = ConnectionManager.getDeviceName(this)
        val deviceIp = ConnectionManager.getLocalIPAddress(this)
        val isConnected = ConnectionManager.isWifiConnected(this)

        binding.deviceName.text = "Device: $deviceName"
        binding.deviceIp.text = "IP: $deviceIp"

        if (isConnected) {
            binding.statusIndicator.setBackgroundColor(getColor(android.R.color.holo_green_light))
            binding.connectionStatusText.text = getString(R.string.connected)
            ConnectionManager.setState(ConnectionManager.ConnectionState.CONNECTED)
        } else {
            binding.statusIndicator.setBackgroundColor(getColor(android.R.color.holo_red_light))
            binding.connectionStatusText.text = getString(R.string.disconnected)
            ConnectionManager.setState(ConnectionManager.ConnectionState.DISCONNECTED)
        }

        LogManager.addLog("Device: $deviceName, IP: $deviceIp, WiFi: $isConnected")
    }

    private fun ensurePermissions() {
        val runtimePermissions = getRuntimePermissions()
        if (!hasAllPermissions()) {
            ActivityCompat.requestPermissions(this, runtimePermissions, PERMISSION_REQUEST_CODE)
            return
        }

        // For Android 11+ request MANAGE_EXTERNAL_STORAGE if needed
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !android.os.Environment.isExternalStorageManager()) {
            try {
                val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                intent.data = Uri.parse("package:$packageName")
                manageAllFilesLauncher.launch(intent)
                return
            } catch (e: Exception) {
                val intent = Intent()
                intent.action = Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION
                manageAllFilesLauncher.launch(intent)
                return
            }
        }

        LogManager.addLog("Permissions ready. Tap Start Sync to launch foreground service.")
    }

    private fun hasAllPermissions(): Boolean {
        val runtimePermissions = getRuntimePermissions()
        for (p in runtimePermissions) {
            if (ContextCompat.checkSelfPermission(this, p) != PackageManager.PERMISSION_GRANTED) {
                return false
            }
        }
        return true
    }

    private fun startSyncService() {
        LogManager.addLog("User requested to start sync service")
        if (!hasAllPermissions()) {
            LogManager.addLog("Missing runtime permissions. Requesting now...")
            ensurePermissions()
            return
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !android.os.Environment.isExternalStorageManager()) {
            LogManager.addLog("All-files permission required. Requesting now...")
            ensurePermissions()
            return
        }

        startSyncServiceInternal()
    }

    private fun startSyncServiceInternal() {
        if (isServiceRunning) {
            LogManager.addLog("Service already running")
            return
        }

        val svcIntent = Intent(this, SyncService::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(this, svcIntent)
        } else {
            startService(svcIntent)
        }
        isServiceRunning = true
        binding.btnStart.isEnabled = false
        binding.btnStop.isEnabled = true
        LogManager.addLog("Sync service started")
    }

    private fun stopSyncService() {
        LogManager.addLog("User requested to stop sync service")
        val svcIntent = Intent(this, SyncService::class.java)
        stopService(svcIntent)
        isServiceRunning = false
        binding.btnStart.isEnabled = true
        binding.btnStop.isEnabled = false
        LogManager.addLog("Sync service stopped")
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == PERMISSION_REQUEST_CODE) {
            if (hasAllPermissions()) {
                LogManager.addLog("All permissions granted")
                LogManager.addLog("Tap Start Sync to start foreground service")
            } else {
                LogManager.addLog("Some permissions were denied")
            }
        }
    }

    override fun onResume() {
        super.onResume()
        updateDeviceInfo()
    }

    /**
     * A native method that is implemented by the 'syncflow' native library,
     * which is packaged with this application.
     */
    external fun stringFromJNI(): String

    companion object {
        const val PERMISSION_REQUEST_CODE = 1001

        // Used to load the 'syncflow' library on application startup.
        init {
            System.loadLibrary("syncflow")
        }
    }
}