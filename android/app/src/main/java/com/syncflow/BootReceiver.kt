package com.syncflow

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action == Intent.ACTION_BOOT_COMPLETED) {
            val svc = Intent(context, SyncService::class.java)
            svc.action = SyncService.ACTION_START
            context.startForegroundService(svc)
        }
    }
}
