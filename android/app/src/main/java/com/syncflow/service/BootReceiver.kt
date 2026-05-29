package com.syncflow.service

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) {
        if (intent?.action == Intent.ACTION_BOOT_COMPLETED && SyncService.isServiceEnabled(context)) {
            Log.d("SyncFlow", "boot completed; service will start after the app is opened")
        }
    }
}