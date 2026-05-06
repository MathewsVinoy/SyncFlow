package com.syncflow

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.wifi.WifiManager
import android.os.Build
import android.provider.Settings
import java.net.InetAddress
import java.net.NetworkInterface

object ConnectionManager {

    data class SyncflowEndpoint(
        val host: String,
        val tcpPort: Int = 45455,
        val udpPort: Int = 45454,
    )

    enum class ConnectionState {
        DISCONNECTED, CONNECTING, CONNECTED
    }

    private var state: ConnectionState = ConnectionState.DISCONNECTED
    private var logCallback: ((String) -> Unit)? = null

    fun setLogCallback(callback: (String) -> Unit) {
        logCallback = callback
    }

    fun log(message: String) {
        logCallback?.invoke("[${System.currentTimeMillis()}] $message")
    }

    fun getDeviceName(context: Context): String {
        return try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N_MR1) {
                val deviceName = Settings.Global.getString(context.contentResolver, Settings.Global.DEVICE_NAME)
                deviceName ?: android.os.Build.MODEL
            } else {
                @Suppress("DEPRECATION")
                Settings.Secure.getString(context.contentResolver, "bluetooth_name")
                    ?: android.os.Build.MODEL
            }
        } catch (e: Exception) {
            android.os.Build.MODEL
        }
    }

    fun getLocalIPAddress(): String {
        return try {
            val en = NetworkInterface.getNetworkInterfaces()
            while (en.hasMoreElements()) {
                val intf = en.nextElement()
                if (!intf.isUp || intf.isLoopback) continue
                val addrs = intf.inetAddresses
                while (addrs.hasMoreElements()) {
                    val addr = addrs.nextElement()
                    val hostAddress = addr.hostAddress ?: continue
                    // Prefer IPv4 addresses and skip link-local / loopback
                    if (addr is java.net.Inet4Address && !addr.isLoopbackAddress) {
                        // strip any zone id if present (rare for IPv4, defensive)
                        val clean = hostAddress.substringBefore('%')
                        return clean
                    }
                }
            }
            "No IP"
        } catch (e: Exception) {
            "Error: ${e::class.java.simpleName} ${e.message ?: "(no message)"}"
        }
    }

    fun isWifiConnected(context: Context): Boolean {
        return try {
            val connectivityManager = context.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
            if (connectivityManager == null) return false

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                val network = connectivityManager.activeNetwork ?: return false
                val capabilities = connectivityManager.getNetworkCapabilities(network) ?: return false
                // Return true only when connected over Wi-Fi (we want Wi‑Fi specifically)
                capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
            } else {
                @Suppress("DEPRECATION")
                val wifiManager = context.getSystemService(Context.WIFI_SERVICE) as? WifiManager
                @Suppress("DEPRECATION")
                wifiManager?.isWifiEnabled == true && (wifiManager.connectionInfo != null)
            }
        } catch (e: Exception) {
            false
        }
    }

    fun getSyncflowEndpoints(): List<SyncflowEndpoint> {
        val endpoints = mutableListOf<SyncflowEndpoint>()

        if (isEmulator()) {
            // Android emulator can reach the host machine through 10.0.2.2.
            endpoints += SyncflowEndpoint("10.0.2.2")
            endpoints += SyncflowEndpoint("10.0.3.2")
        }

        // Localhost is useful if the app is running on an Android-x86 device or test environment.
        endpoints += SyncflowEndpoint("127.0.0.1")

        return endpoints.distinctBy { it.host to it.tcpPort }
    }

    private fun isEmulator(): Boolean {
        val fingerprint = Build.FINGERPRINT.lowercase()
        val model = Build.MODEL.lowercase()
        val product = Build.PRODUCT.lowercase()
        return fingerprint.contains("generic") ||
            fingerprint.contains("vbox") ||
            fingerprint.contains("emulator") ||
            model.contains("sdk") ||
            model.contains("emulator") ||
            product.contains("sdk") ||
            product.contains("emulator")
    }

    fun setState(newState: ConnectionState) {
        state = newState
        log("Connection state: $newState")
    }

    fun getState(): ConnectionState = state
}
