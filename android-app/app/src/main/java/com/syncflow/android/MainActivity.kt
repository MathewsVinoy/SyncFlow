package com.syncflow.android

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.URL

class MainActivity : AppCompatActivity() {
    companion object {
        private const val PREFS = "syncflow_prefs"
        private const val KEY_ENDPOINT = "endpoint"
        private const val DEFAULT_ENDPOINT = "http://127.0.0.1:8080"
    }

    private lateinit var endpointInput: EditText
    private lateinit var logOutput: TextView

    private lateinit var transferIpInput: EditText
    private lateinit var transferPortInput: EditText
    private lateinit var transferPathInput: EditText
    private lateinit var transferTransportInput: EditText

    private lateinit var syncPeerInput: EditText
    private lateinit var syncPortInput: EditText
    private lateinit var syncDirInput: EditText
    private lateinit var syncIntervalInput: EditText
    private lateinit var syncTransportInput: EditText

    @SuppressLint("SetTextI18n")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        endpointInput = findViewById(R.id.endpointInput)
        endpointInput.setText(loadEndpoint())
        logOutput = findViewById(R.id.logOutput)

        transferIpInput = findViewById(R.id.transferIpInput)
        transferPortInput = findViewById(R.id.transferPortInput)
        transferPathInput = findViewById(R.id.transferPathInput)
        transferTransportInput = findViewById(R.id.transferTransportInput)

        syncPeerInput = findViewById(R.id.syncPeerInput)
        syncPortInput = findViewById(R.id.syncPortInput)
        syncDirInput = findViewById(R.id.syncDirInput)
        syncIntervalInput = findViewById(R.id.syncIntervalInput)
        syncTransportInput = findViewById(R.id.syncTransportInput)

        val openButton = findViewById<Button>(R.id.openButton)
        openButton.setOnClickListener {
            persistEndpoint()
            appendLog("Endpoint saved: ${baseUrl()}")
        }

        val discoveryStart = findViewById<Button>(R.id.discoveryStartButton)
        discoveryStart.setOnClickListener { apiCall("/api/discovery/start") }

        val discoveryStop = findViewById<Button>(R.id.discoveryStopButton)
        discoveryStop.setOnClickListener { apiCall("/api/discovery/stop") }

        val listDevices = findViewById<Button>(R.id.listDevicesButton)
        listDevices.setOnClickListener { apiCall("/api/discovery/list") }

        val discoveryStatus = findViewById<Button>(R.id.discoveryStatusButton)
        discoveryStatus.setOnClickListener { apiCall("/api/discovery/status") }

        val sendFile = findViewById<Button>(R.id.sendFileButton)
        sendFile.setOnClickListener {
            val ip = transferIpInput.text.toString().trim()
            val port = transferPortInput.text.toString().trim()
            val path = transferPathInput.text.toString().trim()
            val transport = normalizeTransport(transferTransportInput.text.toString())
            if (ip.isEmpty() || port.isEmpty() || path.isEmpty()) {
                appendLog("Fill transfer IP, port, and path")
                return@setOnClickListener
            }
            apiCall("/api/transfer/send?transport=$transport&ip=${enc(ip)}&port=${enc(port)}&path=${enc(path)}")
        }

        val transferStart = findViewById<Button>(R.id.transferStartButton)
        transferStart.setOnClickListener {
            val port = transferPortInput.text.toString().trim().ifEmpty { "37030" }
            val transport = normalizeTransport(transferTransportInput.text.toString())
            apiCall("/api/transfer/start?transport=$transport&port=${enc(port)}&dir=received")
        }

        val transferStop = findViewById<Button>(R.id.transferStopButton)
        transferStop.setOnClickListener { apiCall("/api/transfer/stop") }

        val transferStatus = findViewById<Button>(R.id.transferStatusButton)
        transferStatus.setOnClickListener { apiCall("/api/transfer/status") }

        val syncStart = findViewById<Button>(R.id.syncStartButton)
        syncStart.setOnClickListener {
            val peer = syncPeerInput.text.toString().trim()
            val port = syncPortInput.text.toString().trim().ifEmpty { "37030" }
            val dir = syncDirInput.text.toString().trim().ifEmpty { "project_dir" }
            val interval = syncIntervalInput.text.toString().trim().ifEmpty { "2000" }
            val transport = normalizeTransport(syncTransportInput.text.toString())
            if (peer.isEmpty()) {
                appendLog("Fill sync peer IP")
                return@setOnClickListener
            }
            apiCall("/api/sync/start?mode=auto&transport=$transport&peer=${enc(peer)}&port=${enc(port)}&dir=${enc(dir)}&interval=${enc(interval)}")
        }

        val syncStop = findViewById<Button>(R.id.syncStopButton)
        syncStop.setOnClickListener { apiCall("/api/sync/stop") }

        val syncStatus = findViewById<Button>(R.id.syncStatusButton)
        syncStatus.setOnClickListener { apiCall("/api/sync/status") }

        val bgStart = findViewById<Button>(R.id.backgroundStartButton)
        bgStart.setOnClickListener {
            persistEndpoint()
            val intent = Intent(this, SyncflowBackgroundService::class.java)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(intent)
            } else {
                startService(intent)
            }
            appendLog("Background service started")
        }

        val bgStop = findViewById<Button>(R.id.backgroundStopButton)
        bgStop.setOnClickListener {
            stopService(Intent(this, SyncflowBackgroundService::class.java))
            appendLog("Background service stopped")
        }

        appendLog("Syncflow native mobile UI ready")
        appendLog("Endpoint: ${baseUrl()}")
    }

    private fun loadEndpoint(): String {
        val prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        return prefs.getString(KEY_ENDPOINT, DEFAULT_ENDPOINT) ?: DEFAULT_ENDPOINT
    }

    private fun persistEndpoint() {
        val raw = endpointInput.text?.toString()?.trim().orEmpty()
        val normalized = if (raw.isEmpty()) DEFAULT_ENDPOINT else if (raw.startsWith("http://") || raw.startsWith("https://")) raw else "http://$raw"
        getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_ENDPOINT, normalized)
            .apply()
    }

    private fun baseUrl(): String {
        val raw = endpointInput.text?.toString()?.trim().orEmpty()
        if (raw.isEmpty()) return loadEndpoint()
        return if (raw.startsWith("http://") || raw.startsWith("https://")) raw else "http://$raw"
    }

    private fun enc(value: String): String {
        return java.net.URLEncoder.encode(value, "UTF-8")
    }

    private fun normalizeTransport(raw: String): String {
        val t = raw.trim().lowercase()
        return if (t == "udp") "udp" else "tcp"
    }

    private fun appendLog(message: String) {
        val existing = logOutput.text?.toString().orEmpty()
        val next = if (existing.isEmpty()) message else "$message\n$existing"
        logOutput.text = next
    }

    private fun apiCall(path: String) {
        val url = baseUrl().trimEnd('/') + path
        appendLog("→ $url")
        Thread {
            val result = safeGet(url)
            runOnUiThread {
                appendLog(result)
            }
        }.start()
    }

    private fun safeGet(url: String): String {
        return try {
            val conn = URL(url).openConnection() as HttpURLConnection
            conn.requestMethod = "GET"
            conn.connectTimeout = 5000
            conn.readTimeout = 10000
            val code = conn.responseCode
            val stream = if (code in 200..299) conn.inputStream else conn.errorStream
            val body = if (stream != null) {
                BufferedReader(InputStreamReader(stream)).use { it.readText() }
            } else {
                ""
            }
            conn.disconnect()
            "[$code] ${body.ifBlank { "ok" }}"
        } catch (e: Exception) {
            "[error] ${e.message ?: "request failed"}"
        }
    }
}
