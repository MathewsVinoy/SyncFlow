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

    private lateinit var context: Context

    fun init(ctx: Context) {
        context = ctx
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

    fun getLocalIPAddress(context: Context? = null): String {
        return try {
            val ctx = context ?: this.context
            android.util.Log.d("ConnectionManager", "getLocalIPAddress: Starting enumeration... (context=$ctx)")
            
            // First try ConnectivityManager (more reliable on newer Android)
            val connectivityManager = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
            android.util.Log.d("ConnectionManager", "ConnectivityManager: $connectivityManager")
            
            if (connectivityManager != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                android.util.Log.d("ConnectionManager", "Using ConnectivityManager path (API >= M)")
                val network = connectivityManager.activeNetwork
                android.util.Log.d("ConnectionManager", "activeNetwork: $network")
                
                if (network != null) {
                    val linkProperties = connectivityManager.getLinkProperties(network)
                    android.util.Log.d("ConnectionManager", "linkProperties: $linkProperties")
                    
                    if (linkProperties != null) {
                        for (addr in linkProperties.linkAddresses) {
                            val ip = addr.address.hostAddress
                            android.util.Log.d("ConnectionManager", "  Address: $ip, IPv4=${addr.address is java.net.Inet4Address}")
                            if (ip != null && addr.address is java.net.Inet4Address) {
                                android.util.Log.i("ConnectionManager", "Found local IP via ConnectivityManager: $ip")
                                return ip.substringBefore('%')
                            }
                        }
                    }
                }
            }

            // Fallback to NetworkInterface enumeration
            android.util.Log.d("ConnectionManager", "Falling back to NetworkInterface enumeration")
            val en = NetworkInterface.getNetworkInterfaces()
            android.util.Log.d("ConnectionManager", "getLocalIPAddress: Enumeration result: $en")
            
            if (en == null) {
                android.util.Log.w("ConnectionManager", "getLocalIPAddress: NetworkInterface.getNetworkInterfaces() returned null")
                return "No interfaces"
            }
            
            while (en.hasMoreElements()) {
                val intf = en.nextElement()
                android.util.Log.d("ConnectionManager", "Interface: ${intf.name}, up=${intf.isUp}, loopback=${intf.isLoopback}")
                
                if (!intf.isUp || intf.isLoopback) continue
                val addrs = intf.inetAddresses ?: continue
                while (addrs.hasMoreElements()) {
                    val addr = addrs.nextElement()
                    val hostAddress = addr.hostAddress ?: continue
                    android.util.Log.d("ConnectionManager", "  Address: $hostAddress, IPv4=${addr is java.net.Inet4Address}, loopback=${addr.isLoopbackAddress}")
                    // Prefer IPv4 addresses and skip link-local / loopback
                    if (addr is java.net.Inet4Address && !addr.isLoopbackAddress) {
                        // strip any zone id if present (rare for IPv4, defensive)
                        val clean = hostAddress.substringBefore('%')
                        android.util.Log.i("ConnectionManager", "Found local IP: $clean")
                        return clean
                    }
                }
            }
            "No IPv4 found"
        } catch (e: Exception) {
            android.util.Log.e("ConnectionManager", "getLocalIPAddress: Exception", e)
            "Error: ${e::class.java.simpleName}: ${e.message ?: "(no detail)"}"
        }
    }

    fun isWifiConnected(context: Context): Boolean {
        return try {
            val connectivityManager = context.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
                ?: return false

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
