package com.syncflow

import android.content.Context
import android.net.wifi.WifiManager
import android.os.Build
import android.provider.Settings
import java.net.InetAddress
import java.net.NetworkInterface

object ConnectionManager {

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
                Settings.Global.getString(context.contentResolver, Settings.Global.DEVICE_NAME)
                    ?: android.os.Build.MODEL
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
            val interfaces = NetworkInterface.getNetworkInterfaces()
            for (iface in interfaces) {
                if (iface.isUp && !iface.isLoopback) {
                    for (address in iface.inetAddresses) {
                        if (!address.isLoopbackAddress && address.hostAddress.contains(".")) {
                            return address.hostAddress
                        }
                    }
                }
            }
            "No IP"
        } catch (e: Exception) {
            "Error: ${e.message}"
        }
    }

    fun isWifiConnected(context: Context): Boolean {
        return try {
            val wifiManager = context.getSystemService(Context.WIFI_SERVICE) as? WifiManager
            wifiManager?.connectionInfo != null && wifiManager.isWifiEnabled
        } catch (e: Exception) {
            false
        }
    }

    fun setState(newState: ConnectionState) {
        state = newState
        log("Connection state: $newState")
    }

    fun getState(): ConnectionState = state
}
