package com.syncflow

import android.content.Context
import android.util.Log
import com.syncflow.file.FileActivity
import java.io.File
import java.util.concurrent.CopyOnWriteArrayList

object FileActivityManager {
    private const val TAG = "FileActivityManager"
    private val activities = CopyOnWriteArrayList<FileActivity>()
    private const val MAX_ACTIVITIES = 200

    fun addActivity(activity: FileActivity) {
        synchronized(activities) {
            activities.add(0, activity)
            if (activities.size > MAX_ACTIVITIES) {
                activities.removeAt(activities.size - 1)
            }
        }
        Log.d(TAG, "Added activity: ${activity.fileName} (${activity.type})")
    }

    fun updateActivity(id: String, status: String, errorMessage: String = "") {
        val index = activities.indexOfFirst { it.id == id }
        if (index >= 0) {
            activities[index] = activities[index].copy(
                status = status,
                errorMessage = errorMessage
            )
            Log.d(TAG, "Updated activity $id: $status")
        }
    }

    fun getAllActivities(): List<FileActivity> {
        return activities.toList()
    }

    fun getRecentActivities(limit: Int = 50): List<FileActivity> {
        return activities.take(limit)
    }

    fun getActivitiesByType(type: String): List<FileActivity> {
        return activities.filter { it.type == type }
    }

    fun getActivitiesByStatus(status: String): List<FileActivity> {
        return activities.filter { it.status == status }
    }

    fun clearActivities() {
        activities.clear()
    }

    fun logFileReceived(
        fileName: String,
        filePath: String,
        size: Long,
        sourceDevice: String
    ) {
        val activity = FileActivity(
            id = System.currentTimeMillis().toString(),
            fileName = fileName,
            filePath = filePath,
            type = "received",
            size = size,
            sourceDevice = sourceDevice,
            status = "completed"
        )
        addActivity(activity)
    }

    fun logFileSent(
        fileName: String,
        filePath: String,
        size: Long,
        targetDevice: String
    ) {
        val activity = FileActivity(
            id = System.currentTimeMillis().toString(),
            fileName = fileName,
            filePath = filePath,
            type = "sent",
            size = size,
            sourceDevice = targetDevice,
            status = "completed"
        )
        addActivity(activity)
    }

    fun logFileUpdated(
        fileName: String,
        filePath: String,
        size: Long
    ) {
        val activity = FileActivity(
            id = System.currentTimeMillis().toString(),
            fileName = fileName,
            filePath = filePath,
            type = "updated",
            size = size,
            status = "completed"
        )
        addActivity(activity)
    }

    fun getStatistics(): Map<String, Any> {
        return mapOf(
            "total" to activities.size,
            "received" to activities.count { it.type == "received" },
            "sent" to activities.count { it.type == "sent" },
            "updated" to activities.count { it.type == "updated" },
            "completed" to activities.count { it.status == "completed" },
            "failed" to activities.count { it.status == "failed" },
            "totalSize" to activities.sumOf { it.size }
        )
    }
}
