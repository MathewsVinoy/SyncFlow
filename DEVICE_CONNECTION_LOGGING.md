# 2-Device Connection Improvements

## Overview
This update enhances the syncflow networking layer to provide comprehensive logging for 2-device connections with support for both TCP/TLS and UDP fallback protocols.

## Changes Made

### 1. Enhanced TCP Connection Logging (TcpHandshake.cpp)
When two devices successfully establish a TCP connection, detailed logs are now generated:

**Log Format:**
```
Device connection established (incoming/outgoing): 
  name=<device_name> 
  id=<device_id> 
  ip=<ip_address> 
  port=<port_number> 
  protocol=TLS/TCP 
  direction=inbound/outbound
```

**Examples:**
```
Device connection established (incoming): name=laptop-1 id=device-abc123 ip=192.168.1.100 port=8080 protocol=TLS/TCP direction=inbound
Device connection established (outgoing): name=phone-x id=device-xyz789 ip=192.168.1.101 port=8080 protocol=TLS/TCP direction=outbound
```

### 2. UDP Fallback Support (TcpHandshake.h & TcpHandshake.cpp)
Added a fallback UDP channel for connections:

**New Enums and Structures:**
- `ConnectionProtocol` enum:
  - `TCP_TLS` - Secure TCP with TLS encryption (primary)
  - `UDP` - UDP for faster, best-effort transfers
  - `UNKNOWN` - No active connection

**New Methods:**
- `getConnectionProtocol(deviceId)` - Query protocol for a specific device
- `sendMessageUdp(deviceId, payload)` - Send data via UDP
- `acceptIncomingUdp()` - Accept incoming UDP connections
- `tryUdpFallbackLocked(state)` - Attempt UDP fallback when TCP fails

**UDP Port Configuration:**
- UDP port = TCP port + 1 (e.g., if TCP on 8080, UDP on 8081)
- Automatically initialized when the TCP handshake starts
- Non-blocking socket for efficient I/O

### 3. Dual-Protocol Connection Logging (Application.cpp)
When 2 devices are connected, comprehensive status is logged:

**Output Example:**
```
=== 2-Device Connection Summary ===
  Device: name=device-1 | id=id-abc123def456 | ip=192.168.1.100 | port=8080 | protocol=TLS/TCP | status=CONNECTED
  Device: name=device-2 | id=id-xyz789uvi012 | ip=192.168.1.101 | port=8080 | protocol=TLS/TCP | status=CONNECTED
=== End Connection Summary ===
```

## Log Levels

### INFO Level Logs (Default)
- Connection established messages with full device details
- Connection status summaries (periodic every 5 seconds)
- Device protocol and direction indicators

### DEBUG Level Logs (When enabled)
- Broadcast probe messages
- Heartbeat (PING/PONG) exchanges
- Individual message transmissions

## Connection Flow

### TCP/TLS Flow (Primary)
1. Device A sends broadcast discovery probe
2. Device B receives probe, responds with device info
3. Device B establishes TCP connection to Device A (if ID ordering requires it)
4. TLS handshake performed
5. HELLO message exchange for device verification
6. Connection established - logs written
7. Periodic PING/PONG heartbeat

### UDP Fallback Flow (Optional)
1. If TCP connection fails, UDP fallback attempts
2. UDP HELLO handshake on port+1
3. Connection established via UDP if successful
4. Uses UDP for faster, best-effort message delivery

## Configuration

### Port Settings
```json
{
  "port": 8080,          // TCP/TLS port for secure connections
  "udp_fallback": true   // Enable UDP fallback (automatic, port+1)
}
```

### Log Configuration
```json
{
  "log_level": "info",           // Set to "debug" for verbose logs
  "sync_data_only_logs": false   // Set to true to log only sync events
}
```

## Monitoring Connected Devices

### Real-Time Status Check
```bash
tail -f log/syncflow.log | grep "Device connection"
```

### Connection Summary
```bash
grep "Connection Summary" log/syncflow.log
```

### Protocol Usage
```bash
grep "protocol=" log/syncflow.log
```

## Error Handling

### TCP Connection Failures
- Logs: "TLS handshake failed" or "TLS outbound certificate validation failed"
- Action: Automatic retry with exponential backoff (2 second intervals)
- Fallback: Attempt UDP if configured

### UDP Connection Failures
- Logs: "UDP write failed" or "UDP fallback rejected"
- Action: Continue with TCP only or retry after delay
- No automatic failover to TCP from UDP

### General Connection Issues
- "Heartbeat timeout" - Device became unresponsive
- "send-failed" - Failed to send data on connection
- "read-failed" - Failed to read from connection

## Security Notes

**TLS/TCP Connections:**
- All TCP connections use TLS 1.2+ encryption
- Certificates are self-signed and device-specific
- Device ID verified via certificate CN (Common Name)
- Mutual authentication between peers

**UDP Connections:**
- UDP is best-effort, no encryption by default in fallback mode
- Should only be used on trusted networks
- Consider using for local LAN only

## Performance Characteristics

**TCP/TLS:**
- Reliable delivery
- ~3000ms handshake timeout
- ~2 second heartbeat interval
- ~10 second heartbeat timeout

**UDP:**
- Best-effort delivery
- ~3000ms handshake timeout
- Faster message transmission
- No retransmission guarantees

## Testing 2-Device Connection

### Setup
1. Configure two devices with unique `device_name` values
2. Ensure both are on same network or routable network
3. Start syncflow on both devices

### Verify Connection
```bash
# On Device 1 or 2, check logs
tail -f log/syncflow.log | grep -E "(Device connection|Connection Summary|CONNECTED)"
```

### Expected Output
```
Peer discovery status: waiting for another device
[After other device comes online]
Device connection established (incoming): name=device-B id=device-xyz... ip=192.168.1.101 port=8080 protocol=TLS/TCP direction=inbound

=== 2-Device Connection Summary ===
  Device: name=device-A | id=device-abc... | ip=192.168.1.100 | port=8080 | protocol=TLS/TCP | status=CONNECTED
=== End Connection Summary ===
```

## Summary

The enhanced 2-device connection system now provides:
- ✅ Detailed logging of successful connections
- ✅ Device name and ID verification
- ✅ IP and port information
- ✅ Protocol indication (TCP/TLS or UDP)
- ✅ Connection direction tracking (inbound/outbound)
- ✅ Periodic connection status reporting
- ✅ UDP fallback option for alternative connectivity
- ✅ Secure TLS-encrypted connections by default
- ✅ Comprehensive error reporting

All logs are written to the configured log folder (default: `./log/syncflow.log`)
