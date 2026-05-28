package com.syncflow

data class PeerStatus(
    val running: Boolean,
    val deviceName: String,
    val localIp: String,
    val connections: List<String>
)
