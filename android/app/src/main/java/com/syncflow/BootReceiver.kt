package com.syncflow

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import androidx.core.content.ContextCompat

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) {
        if (intent?.action != Intent.ACTION_BOOT_COMPLETED) return

        val prefs = context.getSharedPreferences(SyncConnectionService.PREFS_NAME, Context.MODE_PRIVATE)
        if (!prefs.getBoolean(SyncConnectionService.KEY_AUTO_CONNECT, false)) return

        val serviceIntent = Intent(context, SyncConnectionService::class.java).apply {
            action = SyncConnectionService.ACTION_START
            putExtra(SyncConnectionService.EXTRA_HOST, prefs.getString(SyncConnectionService.KEY_HOST, "192.168.1.10"))
            putExtra(SyncConnectionService.EXTRA_PORT, prefs.getInt(SyncConnectionService.KEY_PORT, SyncConnectionService.DEFAULT_PORT))
            putExtra(SyncConnectionService.EXTRA_DEVICE_NAME, prefs.getString(SyncConnectionService.KEY_DEVICE_NAME, "Android"))
        }
        ContextCompat.startForegroundService(context, serviceIntent)
    }
}