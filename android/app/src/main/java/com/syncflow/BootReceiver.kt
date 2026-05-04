package com.syncflow

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action == Intent.ACTION_BOOT_COMPLETED) {
            // Restart the sync service after boot
            val svc = Intent(context, SyncService::class.java)
            // If you need to pass config path, service can resolve its default
            context.startForegroundService(svc)
        }
    }
}
