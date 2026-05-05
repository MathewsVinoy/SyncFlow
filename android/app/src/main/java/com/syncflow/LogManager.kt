package com.syncflow

import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData

object LogManager {
    private val _logs = MutableLiveData<String>()
    val logs: LiveData<String> = _logs

    private val logBuffer = StringBuilder()
    private val maxLogLines = 500

    fun addLog(message: String) {
        val timestamp = java.text.SimpleDateFormat("HH:mm:ss.SSS").format(System.currentTimeMillis())
        val logLine = "[$timestamp] $message\n"
        logBuffer.append(logLine)

        // Trim buffer if it gets too large
        val lines = logBuffer.toString().split("\n")
        if (lines.size > maxLogLines) {
            logBuffer.clear()
            logBuffer.append(lines.takeLast(maxLogLines).joinToString("\n"))
        }

        _logs.postValue(logBuffer.toString())
    }

    fun clear() {
        logBuffer.clear()
        _logs.postValue("Logs cleared")
    }

    fun getLogs(): String = logBuffer.toString()
}
