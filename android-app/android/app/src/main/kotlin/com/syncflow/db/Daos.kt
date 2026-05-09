package com.syncflow.db

import androidx.room.*

@Dao
interface DeviceDao {
    @Query("SELECT * FROM devices")
    suspend fun getAllDevices(): List<Device>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertDevice(device: Device)

    @Delete
    suspend fun deleteDevice(device: Device)
}

@Dao
interface SyncFolderDao {
    @Query("SELECT * FROM sync_folders")
    suspend fun getAllFolders(): List<SyncFolder>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertFolder(folder: SyncFolder)

    @Update
    suspend fun updateFolder(folder: SyncFolder)
}
