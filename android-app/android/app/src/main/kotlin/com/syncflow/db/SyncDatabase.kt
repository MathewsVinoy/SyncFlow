package com.syncflow.db

import androidx.room.Database
import androidx.room.RoomDatabase

@Database(entities = [Device::class, SyncFolder::class], version = 1)
abstract class SyncDatabase : RoomDatabase() {
    abstract fun deviceDao(): DeviceDao
    abstract fun syncFolderDao(): SyncFolderDao
}
