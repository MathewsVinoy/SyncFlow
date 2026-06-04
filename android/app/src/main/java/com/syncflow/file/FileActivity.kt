package com.syncflow.file

import java.io.Serializable

data class FileActivity(
    val id: String = "",
    val fileName: String = "",
    val filePath: String = "",
    val type: String = "", // "received", "sent", "updated"
    val size: Long = 0L,
    val timestamp: Long = System.currentTimeMillis(),
    val status: String = "completed", // "pending", "in_progress", "completed", "failed"
    val sourceDevice: String = "",
    val errorMessage: String = ""
) : Serializable {
    fun getTypeColor(): Int = when (type) {
        "received" -> android.graphics.Color.parseColor("#4CAF50")
        "sent" -> android.graphics.Color.parseColor("#2196F3")
        "updated" -> android.graphics.Color.parseColor("#FF9800")
        else -> android.graphics.Color.GRAY
    }

    fun getStatusIcon(): String = when (status) {
        "pending" -> "⏳"
        "in_progress" -> "↻"
        "completed" -> "✓"
        "failed" -> "✗"
        else -> "?"
    }

    fun getSizeFormatted(): String {
        return when {
            size < 1024 -> "$size B"
            size < 1024 * 1024 -> "${size / 1024} KB"
            size < 1024 * 1024 * 1024 -> "${size / (1024 * 1024)} MB"
            else -> "${size / (1024 * 1024 * 1024)} GB"
        }
    }
}
