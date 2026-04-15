package com.syncflow.android

import android.annotation.SuppressLint
import android.os.Bundle
import android.webkit.WebChromeClient
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Button
import android.widget.EditText
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {
    private lateinit var webView: WebView
    private lateinit var endpointInput: EditText

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        endpointInput = findViewById(R.id.endpointInput)
        webView = findViewById(R.id.webView)

        webView.settings.javaScriptEnabled = true
        webView.settings.domStorageEnabled = true
        webView.webViewClient = WebViewClient()
        webView.webChromeClient = WebChromeClient()

        val openButton = findViewById<Button>(R.id.openButton)
        openButton.setOnClickListener {
            loadUi()
        }

        val discoveryStart = findViewById<Button>(R.id.discoveryStartButton)
        discoveryStart.setOnClickListener { api("/api/discovery/start") }

        val discoveryStop = findViewById<Button>(R.id.discoveryStopButton)
        discoveryStop.setOnClickListener { api("/api/discovery/stop") }

        val listDevices = findViewById<Button>(R.id.listDevicesButton)
        listDevices.setOnClickListener { api("/api/discovery/list") }

        loadUi()
    }

    private fun baseUrl(): String {
        val raw = endpointInput.text?.toString()?.trim().orEmpty()
        if (raw.isEmpty()) {
            return "http://127.0.0.1:8080"
        }
        return if (raw.startsWith("http://") || raw.startsWith("https://")) raw else "http://$raw"
    }

    private fun loadUi() {
        webView.loadUrl(baseUrl())
    }

    private fun api(path: String) {
        val url = baseUrl().trimEnd('/') + path
        webView.loadUrl(url)
    }
}
