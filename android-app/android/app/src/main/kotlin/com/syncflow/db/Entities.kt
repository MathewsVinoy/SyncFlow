package com.syncflow.db

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "devices")
data class Device(
    @PrimaryKey val id: String,
    val name: String,
    val ipAddress: String,
    val isTrusted: Boolean = false
)

@Entity(tableName = "sync_folders")
data class SyncFolder(
    @PrimaryKey val id: String,
    val localPath: String,
    val remoteDeviceId: String,
    val lastSyncTime: Long = 0
)
