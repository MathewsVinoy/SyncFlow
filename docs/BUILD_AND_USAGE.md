# Syncflow - Build and Usage Guide

## Quick Start

### Prerequisites

**Linux:**

```bash
sudo apt-get install build-essential cmake git
```

**macOS:**

```bash
xcode-select --install
brew install cmake
```

**Windows:**

- Visual Studio 2019 or later (with C++ workload)
- CMake 3.20+

### Building

```bash
cd /path/to/syncflow
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)          # Linux/macOS
cmake --build . --config Release  # Windows
```

### Installation

```bash
sudo make install        # Linux/macOS
cmake --install .        # Windows
```

This installs:

- Executable: `/usr/local/bin/syncflow`
- Libraries: `/usr/local/lib/libsyncflow_*.a`
- Headers: `/usr/local/include/syncflow/`

---

## Command Reference

### Device Management

**List discovered devices:**

```bash
syncflow list-devices
```

Output:

```
Discovered Devices:
--------------------
Name: MacBook-Pro
ID: 00:1A:2B:3C:4D:5E:MacBook-Pro
IP: 192.168.1.105:15948
Platform: 3
---
Name: Ubuntu-Laptop
ID: 00:1A:2B:3C:4D:5F:Ubuntu-Laptop
IP: 192.168.1.103:15948
Platform: 2
---
```

**Show current status:**

```bash
syncflow status
```

Output:

```
Connected devices: 2
Active transfers: 1
  - Documents/report.pdf: 45%
```

### File Transfer

**Send file to device:**

```bash
syncflow send /path/to/file "device-id"
```

**Receive file:**

```bash
syncflow receive [destination-path]
```

**List active transfers:**

```bash
syncflow list-transfers
```

**Pause transfer:**

```bash
syncflow pause "session-id"
```

**Resume transfer:**

```bash
syncflow resume "session-id"
```

**Cancel transfer:**

```bash
syncflow cancel "session-id"
```

### Folder Synchronization

**Add folder to sync:**

```bash
syncflow add-folder /local/path "device-id" /remote/path --mode bidirectional
```

**List sync folders:**

```bash
syncflow list-folders
```

**Remove sync folder:**

```bash
syncflow remove-folder /local/path
```

**Start sync daemon:**

```bash
syncflow start [--background]
```

**Stop sync daemon:**

```bash
syncflow stop
```

### Configuration

**Set conflict resolution strategy:**

```bash
syncflow config set conflict-strategy version  # overwrite | skip | version | ask
```

**Enable compression:**

```bash
syncflow config set enable-compression true
```

**Set transfer threads:**

```bash
syncflow config set transfer-threads 4
```

**Show current configuration:**

```bash
syncflow config list
```

---

## Architecture Details

### Module Interaction Flow

#### Device Discovery Flow

```
User Command: syncflow list-devices
        │
        ├─> DiscoveryEngine::start()
        │   ├─ Create UDP socket
        │   ├─ Start receive_thread()
        │   │  └─ Listen for broadcasts
        │   └─ Start broadcast_thread()
        │      └─ Send device info every 5s
        │
        └─> DeviceManager::get_all_devices()
            └─ Return registered devices
```

#### File Transfer Flow

```
User Command: syncflow send file.txt device-id
        │
        ├─ Create TCP connection to device
        ├─ Send HANDSHAKE_REQ
        ├─ Receive HANDSHAKE_RESP
        ├─ Calculate file hash & metadata
        ├─ Send FILE_OFFER
        ├─ Wait for FILE_ACCEPT
        ├─ Split file into 1MB chunks
        └─ For each chunk:
           ├─ Send CHUNK_DATA
           ├─ Wait for CHUNK_ACK
           └─ Update progress
```

#### File Watcher Flow (Linux example)

```
FileWatcher::start()
        │
        ├─ FileSystemMonitor::create() → LinuxFSMonitor
        ├─ inotify_init() → inotify_fd
        ├─ inotify_add_watch(path, IN_CREATE | IN_DELETE | IN_MODIFY)
        └─ poll() loop
           └─ When change detected:
              ├─ Parse inotify_event
              ├─ Create FileChangeEvent
              └─ Fire callback()
```

### Protocol Message Format

All binary messages follow network byte order (big-endian):

**Generic Header:**

```
Bytes  Type      Description
0      uint8     Message type (e.g., HANDSHAKE_REQ = 0x01)
1-4    uint32    Payload size (network byte order)
5+     variable  Payload (type-specific)
```

**Handshake Request (HANDSHAKE_REQ = 0x01):**

```
Offset Bytes  Field           Type
0      1      Message Type    uint8 (0x01)
1      4      Magic           uint32 (0x5346414E = "SFAN")
5      4      Version         uint32 (1)
9      4      ID Length       uint32
13+    var    Device ID       string
       4      Name Length     uint32
       var    Device Name     string
       4      Hostname Length uint32
       var    Hostname        string
       1      Platform        uint8 (1=Windows, 2=Linux, 3=macOS, 4=Android)
       4      Version Length  uint32
       var    Version         string
```

**Chunk Data (CHUNK_DATA = 0x07):**

```
Offset Bytes  Field           Type
0      1      Message Type    uint8 (0x07)
1      4      Chunk ID        uint32
5      8      Offset          uint64
13     4      Data Size       uint32
17     4      CRC32           uint32
21     1      Compressed Flag uint8 (0 or 1)
22+    var    Chunk Data      binary
```

---

## Platform-Specific Implementations

### Linux File Watcher (inotify)

```cpp
// File: src/platform/linux/file_watcher.cpp
class LinuxFSMonitor : public FileSystemMonitor {
    int inotify_fd_;
    std::map<int, std::string> watched_dirs_;

    bool watch_directory(const std::string& path) override {
        int wd = inotify_add_watch(inotify_fd_, path.c_str(),
                                   IN_CREATE | IN_DELETE | IN_MODIFY);
        watched_dirs_[wd] = path;
        return true;
    }
};
```

**Supported Events:**

- `IN_CREATE`: File or directory created
- `IN_DELETE`: File or directory deleted
- `IN_MODIFY`: File modified
- `IN_MOVED_TO`: File renamed/moved into directory
- `IN_MOVED_FROM`: File renamed/moved out of directory

### Windows File Watcher (ReadDirectoryChangesW)

```cpp
// File: src/platform/windows/file_watcher.cpp
class WindowsFSMonitor : public FileSystemMonitor {
    HANDLE directory_handle_;

    bool watch_directory(const std::string& path) override {
        directory_handle_ = CreateFileA(path.c_str(),
                                        GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_DELETE,
                                        NULL, OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS, NULL);
        return directory_handle_ != INVALID_HANDLE_VALUE;
    }
};
```

**Supported Changes:**

- `FILE_NOTIFY_CHANGE_FILE_NAME`: File renamed/created/deleted
- `FILE_NOTIFY_CHANGE_DIR_NAME`: Directory renamed/created/deleted
- `FILE_NOTIFY_CHANGE_LAST_WRITE`: File modified

### macOS File Watcher (FSEvents)

```cpp
// File: src/platform/macos/file_watcher.cpp
class macOSFSMonitor : public FileSystemMonitor {
    FSEventStreamRef stream_;

    bool watch_directory(const std::string& path) override {
        CFStringRef path_str = CFStringCreateWithCString(
            NULL, path.c_str(), kCFStringEncodingUTF8);
        CFArrayRef paths = CFArrayCreate(NULL, (const void**)&path_str, 1, NULL);

        stream_ = FSEventStreamCreate(NULL, callback, NULL, paths,
                                      kFSEventStreamEventIdSinceNow,
                                      1.0, kFSEventStreamCreateFlagNone);
        FSEventStreamStart(stream_);
        return true;
    }
};
```

---

## Network Protocol Details

### Discovery (UDP)

**Port:** 15947 (DISCOVERY_PORT)

**Broadcast Address:** 255.255.255.255:15947

**Interval:** 5 seconds

**Timeout:** 15 seconds (device marked as stale)

### File Transfer (TCP)

**Port:** 15948 (TRANSFER_PORT)

**Connection Type:** One-to-many (initiator connects to receiver)

**Chunk Size:** 1 MB (CHUNK_SIZE)

**Max Concurrent:** 4 transfers per session

**Timeout:** 30 seconds per chunk

---

## Performance Optimization Tips

### 1. Network Optimization

**Enable jumbo frames (MTU 9000) for local network:**

```bash
# Linux
sudo ip link set eth0 mtu 9000

# macOS
sudo ifconfig en0 mtu 9000

# Windows
netsh interface ipv4 set subinterface "Ethernet" mtu=9000
```

### 2. Filesystem Optimization

**Use SSD for transfer staging:**

```bash
syncflow config set staging-path /mnt/fast-ssd/syncflow
```

**Enable write caching (caution: risk of data loss):**

```cpp
// In write_file_chunk()
#ifdef _WIN32
    file.write(data, size);
    FlushFileBuffers(handle);  // Commit to disk
#else
    file.write(data, size);
    fsync(fileno(file));  // Force sync
#endif
```

### 3. Memory Optimization

**Reduce chunk size for limited memory devices:**

```bash
syncflow config set chunk-size 512K  # Default 1M
```

**Enable adaptive buffering:**

```cpp
// Monitor available memory and adjust buffer sizes
size_t available_mem = get_available_memory();
size_t buffer_size = std::min(CHUNK_SIZE, available_mem / 8);
```

### 4. Thread Optimization

**Set thread pool size based on CPU cores:**

```bash
# Auto-detect
syncflow config set transfer-threads auto

# Manual
syncflow config set transfer-threads 8
```

---

## Troubleshooting

### No devices discovered

**Cause:** Firewall blocking UDP 15947

**Solution:**

```bash
# Linux (ufw)
sudo ufw allow 15947/udp

# Windows Firewall (PowerShell as admin)
New-NetFirewallRule -DisplayName "Syncflow Discovery" -Direction Inbound `
  -Action Allow -Protocol UDP -LocalPort 15947

# macOS (pf)
echo "pass in proto udp from any to any port 15947" | sudo pfctl -f -
```

### Transfer connection failed

**Cause:** Firewall blocking TCP 15948

**Solution:**

```bash
# Linux (ufw)
sudo ufw allow 15948/tcp

# Windows Firewall
New-NetFirewallRule -DisplayName "Syncflow Transfer" -Direction Inbound `
  -Action Allow -Protocol TCP -LocalPort 15948
```

### Slow transfer speed

**Diagnostics:**

```bash
# Check network interface speed
ethtool eth0 | grep "Speed"  # Linux

# Monitor transfer
syncflow status --verbose

# Check bandwidth usage
iftop -i eth0
```

**Solutions:**

1. Use 5GHz WiFi instead of 2.4GHz
2. Enable jumbo frames
3. Reduce concurrent transfers
4. Check CPU usage (may be I/O bound)

---

## Configuration File Format

**Location:** `~/.config/syncflow/config.ini` (Linux)

**Example:**

```ini
[general]
device_name=MyDevice
log_level=info
auto_discovery=true

[network]
discovery_interval_ms=5000
discovery_timeout_ms=15000
transfer_port=15948
discovery_port=15947

[transfer]
chunk_size=1048576        # 1MB in bytes
max_concurrent=4
timeout_ms=30000
enable_compression=false

[sync]
conflict_strategy=version  # overwrite | skip | version | ask
enable_incremental=true
watch_interval_ms=1000

[paths]
staging_directory=/tmp/syncflow
config_directory=~/.config/syncflow
data_directory=~/.local/share/syncflow
```

---

## Android Integration Example

### Building for Android

```bash
# Set up NDK environment
export ANDROID_NDK=/path/to/ndk
export ANDROID_PLATFORM=android-21

# Configure CMake for Android
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-21
```

### JNI Bindings Example

```java
// MainActivity.java
public class MainActivity extends AppCompatActivity {
    static {
        System.loadLibrary("syncflow_jni");
    }

    public native int startDiscovery();
    public native int stopDiscovery();
    public native Device[] getDevices();

    private void startSync() {
        int result = startDiscovery();
        if (result == 0) {
            Toast.makeText(this, "Discovery started", Toast.LENGTH_SHORT).show();
        }
    }
}
```

---

## Contributing

### Code Style

- Use 4-space indentation
- Follow Google C++ Style Guide
- Use snake_case for functions and variables
- Use PascalCase for classes
- Document public APIs with doxygen

### Testing

```bash
# Run all tests
ctest

# Run specific test
ctest -R discovery

# With verbose output
ctest --verbose
```

### Debugging

```bash
# Build with debug symbols
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Run with gdb
gdb ./syncflow
(gdb) run list-devices
```

---

## License

Syncflow is released under the MIT License. See LICENSE file for details.
