# Syncflow Architecture & Design Guide

## Overview

Syncflow is a high-performance, cross-platform file synchronization system designed for peer-to-peer file transfers similar to AirDrop or Quick Share.

### Key Features

- **Device Discovery**: Automatic UDP broadcast-based device discovery over local networks
- **P2P Transfer**: Direct TCP connections for file transfers without central servers
- **Resume Support**: Interruption-tolerant transfers with chunk-based resumption
- **Multi-threaded**: Concurrent transfers with configurable thread pools
- **Cross-platform**: Support for Windows, Linux, macOS, and Android (via NDK)
- **Platform Abstraction**: Unified APIs for file system, networking, and threading

---

## Architecture Overview

### Module Layers

```
┌─────────────────────────────────────────┐
│           CLI Interface                 │
│        (Commands & Control)             │
└──────────────────┬──────────────────────┘
                   │
┌──────────────────┴──────────────────────┐
│      Sync Engine (High-level)           │
│   (Conflict resolution, Versioning)     │
└──────────────────┬──────────────────────┘
                   │
   ┌───────────────┼───────────────┐
   │               │               │
┌──▼────┐    ┌─────▼──┐    ┌─────▼─────┐
│Discovery    │Transfer│    │File Watcher
│Engine       │Module  │    │Module
└──┬─────┘    └─────┬──┘    └─────┬─────┘
   │               │              │
   └───────────────┼──────────────┘
                   │
┌──────────────────▼──────────────────────┐
│  Platform Abstraction Layer             │
│  ├─ FileSystem (fs operations)          │
│  ├─ Network (sockets, UDP/TCP)          │
│  ├─ Threading (thread pools)            │
│  └─ Platform Info                       │
└──────────────────┬──────────────────────┘
                   │
┌──────────────────▼──────────────────────┐
│    OS-Specific Implementations          │
│  ├─ Windows (WinSock, ReadDirChanges)   │
│  ├─ Linux (sockets, inotify)            │
│  ├─ macOS (sockets, FSEvents)           │
│  └─ Android (NDK, platform APIs)        │
└─────────────────────────────────────────┘
```

### Module Responsibilities

#### 1. **Common Module** (`src/common/`)

- **logger.h/cpp**: Structured logging with multiple levels
- **utils.h/cpp**: Binary serialization, CRC32, hashing, string utilities
- **types.h**: Core data structures and enumerations

#### 2. **Platform Module** (`src/platform/`)

Provides cross-platform abstraction:

- **platform.h/cpp**: Platform detection and utilities
- **file_system.cpp**: File I/O, directory operations, path handling
- **networking.cpp**: Socket creation, UDP/TCP, network utilities
- **thread_pool.cpp**: Thread pool for concurrent operations
- **Platform-specific files**:
  - `linux/file_watcher.cpp`: inotify-based file monitoring
  - `windows/file_watcher.cpp`: ReadDirectoryChangesW implementation
  - `macos/file_watcher.cpp`: FSEvents integration

#### 3. **Discovery Module** (`src/discovery/`)

Handles device discovery via UDP:

- **device.cpp**: Device representation and lifecycle
- **device_manager.cpp**: Singleton device registry
- **discovery_engine.cpp**: UDP broadcast/receive loop
  - Sends device info periodically
  - Listens for remote device announcements
  - Maintains device timeout tracking

#### 4. **Transfer Module** (`src/transfer/`)

Handles file transfer operations:

- **transfer_protocol.cpp**: Binary protocol codecs for message serialization
- **file_transfer.cpp**: Per-file transfer state machine
- **transfer_manager.cpp**: Transfer orchestration and concurrency
- **chunk_manager.cpp**: Chunk metadata and optimization

#### 5. **Watcher Module** (`src/watcher/`)

Monitors file system changes:

- **file_watcher.cpp**: High-level file change notification API
- **fs_monitor.cpp**: Platform-specific implementations (inotify, FSEvents, etc.)

#### 6. **Sync Module** (`src/sync/`)

Implements synchronization logic:

- **sync_engine.cpp**: Orchestrates bidirectional sync
- **file_manifest.cpp**: Tracks synced files and state
- **conflict_resolver.cpp**: Handles conflicting changes (versioning, overwrite, skip)

#### 7. **CLI Module** (`src/cli/`)

Command-line interface:

- **main.cpp**: Entry point and command routing
- **cli_handler.cpp**: Command parsing
- **commands.cpp**: Individual command implementations

---

## Communication Protocol

### Discovery Protocol (UDP)

**Message Format:**

```
┌─────────┬──────────────┬─────────┬─────────┬──────────┬──────┬─────────┐
│ Magic   │ Version      │ Device  │ Device  │ Platform │ IP   │ Port    │
│ (4B)    │ (4B)         │ ID      │ Name    │ (1B)     │ Str  │ (2B)    │
└─────────┴──────────────┴─────────┴─────────┴──────────┴──────┴─────────┘
```

**Flow:**

1. Device broadcasts device info every 5 seconds
2. Other devices receive and register device (or update if exists)
3. Devices marked as stale if not heard from in 15 seconds
4. Callbacks fire on device discovery/loss

### File Transfer Protocol (TCP)

**Message Types:**

```c
HANDSHAKE_REQ (0x01)      - Initial connection request
HANDSHAKE_RESP (0x02)     - Handshake response
FILE_OFFER (0x03)         - Offer to transfer file
FILE_ACCEPT (0x04)        - Accept file transfer
FILE_REJECT (0x05)        - Reject file transfer
CHUNK_REQUEST (0x06)      - Request specific chunk
CHUNK_DATA (0x07)         - Transfer chunk data
CHUNK_ACK (0x08)          - Acknowledge chunk receipt
TRANSFER_COMPLETE (0x09)  - Transfer finished
ERROR (0x0A)              - Error condition
```

**Chunk Format:**

```
┌────┬────────┬────────┬──────────┬────────┬──────────┬──────────┐
│Type│ChunkID │Offset  │DataSize  │CRC32   │Compressed│Data...  │
│(1B)│ (4B)   │ (8B)   │ (4B)     │ (4B)   │ (1B)     │ (var)    │
└────┴────────┴────────┴──────────┴────────┴──────────┴──────────┘
```

**Transfer Flow:**

1. Initiator sends HANDSHAKE_REQ with local device info
2. Receiver responds with HANDSHAKE_RESP
3. Initiator sends FILE_OFFER with file metadata
4. Receiver sends FILE_ACCEPT or FILE_REJECT
5. Initiator sends chunks sequentially or in parallel
6. Receiver acknowledges each chunk
7. Upon completion, send TRANSFER_COMPLETE

---

## Design Patterns

### 1. **Pimpl (Pointer to Implementation)**

```cpp
class DiscoveryEngine {
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
```

Benefits:

- Hide platform-specific details
- Enable different implementations per platform
- Reduce compilation dependencies

### 2. **Singleton Pattern**

```cpp
class DeviceManager {
public:
    static DeviceManager& instance();
private:
    DeviceManager() = default;
};
```

Used for: DeviceManager, TransferManager, Logger

### 3. **Abstract Factory**

```cpp
std::unique_ptr<FileSystem> FileSystem::create();
std::unique_ptr<Network> Network::create();
std::unique_ptr<ThreadPool> ThreadPool::create(size_t num_threads);
```

### 4. **Observer Pattern**

```cpp
using OnDeviceDiscovered = std::function<void(const DeviceInfo&)>;
using OnTransferProgress = std::function<void(const SessionID&, uint64_t, uint64_t)>;
```

---

## Key Algorithms

### Device Discovery Algorithm

```
┌──────────────────────────────────────────┐
│ Broadcast Thread (every 5s)              │
└──────────────────────────────────────────┘
    │
    ├─ Collect local device info
    ├─ Encode to binary format
    └─ Send UDP broadcast to 255.255.255.255:15947

┌──────────────────────────────────────────┐
│ Receive Thread (listening)               │
└──────────────────────────────────────────┘
    │
    ├─ Receive UDP packet
    ├─ Decode device info
    ├─ Add/update in DeviceManager
    └─ Fire callback if new device

┌──────────────────────────────────────────┐
│ Cleanup Thread (every 5s)                │
└──────────────────────────────────────────┘
    │
    └─ Remove devices not heard from in 15s
```

### File Transfer Algorithm

**Sender:**

```
1. Open source file
2. Calculate file hash (SHA256)
3. Split into 1MB chunks
4. Connect to receiver
5. Send handshake + file metadata
6. For each chunk:
   - Read chunk from disk
   - Calculate CRC32
   - Send CHUNK_DATA
   - Wait for CHUNK_ACK
   - (Optional) retry on timeout
7. Send TRANSFER_COMPLETE
```

**Receiver:**

```
1. Accept connection
2. Receive and validate handshake
3. Prepare output file with correct size
4. For each chunk:
   - Receive CHUNK_DATA
   - Validate CRC32
   - Write to disk at correct offset
   - Send CHUNK_ACK
5. Verify file hash matches metadata
6. Fire completion callback
```

**Resume Mechanism:**

```
- Track received chunks in bitset
- Skip chunks already received
- Continue from interrupted position
- Validate consistency on resume
```

---

## Performance Optimizations

### 1. **Memory-Mapped Files**

For large files, use mmap to avoid copying:

```cpp
// Future enhancement
std::shared_ptr<MemoryMappedFile> mf = MemoryMappedFile::open(path);
socket->send(mf->data() + offset, chunk_size);
```

### 2. **Sendfile/TransmitFile**

Zero-copy transfer on supported platforms:

```cpp
// Linux: sendfile()
// Windows: TransmitFile()
// macOS: sendfile() with special handling
```

### 3. **Parallel Chunk Transfer**

Multiple connections for single file:

```cpp
concurrent_transfers = min(4, num_cores)
```

### 4. **Compression**

Optional per-file:

```cpp
if (file_size > threshold && enable_compression) {
    compress_chunk(data);
    set_compressed_flag();
}
```

### 5. **Binary Protocol**

No JSON overhead:

- Handshake: ~100 bytes
- File offer: ~300 bytes
- Chunk header: ~29 bytes
- Vs JSON: 3-5x larger

---

## File Watcher Implementation

### Linux (inotify)

```cpp
int fd = inotify_init();
int wd = inotify_add_watch(fd, path, IN_CREATE | IN_DELETE | IN_MODIFY);
```

Events: CREATE, DELETE, MODIFY, RENAME

### Windows (ReadDirectoryChangesW)

```cpp
ReadDirectoryChangesW(handle, buffer, size, subtree,
                      FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE)
```

### macOS (FSEvents)

```cpp
FSEventStreamCreate(allocator, callback, NULL, pathsToWatch,
                    kFSEventStreamEventIdSinceNow, 1.0, flags)
```

---

## Configuration & State Management

### Configuration Locations

| Platform | Path                                                |
| -------- | --------------------------------------------------- |
| Windows  | `%APPDATA%\syncflow\config.ini`                     |
| Linux    | `~/.config/syncflow/config.ini`                     |
| macOS    | `~/Library/Application Support/syncflow/config.ini` |
| Android  | App private data directory                          |

### State Files

| File                 | Purpose                  |
| -------------------- | ------------------------ |
| `.syncflow_manifest` | Per-folder file tracking |
| `devices.json`       | Known devices            |
| `transfers.log`      | Transfer history         |
| `sync.log`           | Sync operation log       |

---

## Android Integration via NDK

### Directory Structure

```
android/
├── app/src/main/
│   ├── cpp/
│   │   ├── CMakeLists.txt
│   │   ├── native_bindings.cpp
│   │   └── jni_interface.h
│   ├── java/com/syncflow/
│   │   ├── MainActivity.java
│   │   ├── SyncService.java
│   │   └── TransferAdapter.java
│   └── AndroidManifest.xml
└── build.gradle
```

### JNI Bindings

```cpp
extern "C" {
    JNIEXPORT jboolean JNICALL
    Java_com_syncflow_SyncService_startDiscovery(JNIEnv *env, jobject obj) {
        // Call discovery engine
        auto engine = std::make_unique<DiscoveryEngine>();
        return engine->start([](const DeviceInfo& info) {
            // Callback to Java
        }, nullptr);
    }
}
```

### Native Permissions

```xml
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
<uses-permission android:name="android.permission.CHANGE_NETWORK_STATE" />
<uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" />
<uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE" />
```

---

## Conflict Resolution Strategies

### 1. **OVERWRITE**

Remote file always wins. Simple but risky.

### 2. **SKIP**

Keep local file, don't sync. Conservative approach.

### 3. **VERSION**

Create timestamped copy of conflicting file:

```
file.txt       (original local)
file.2024-04-11_14-30-45.txt  (remote version)
```

### 4. **ASK_USER**

Prompt user for decision (for interactive clients).

---

## Testing Strategy

### Unit Tests

```cpp
// test_discovery.cpp
// - Device add/retrieve
// - Device timeout detection
// - Message encoding/decoding
```

### Integration Tests

```cpp
// test_transfer.cpp
// - Chunk encoding/decoding
// - Transfer state machine
// - Resume capability
```

### Performance Tests

```cpp
// benchmark/throughput.cpp
// - Measure bytes/sec
// - Profile memory usage
// - Thread contention analysis
```

---

## Build Instructions

### Linux/macOS

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
sudo make install
```

### Windows (Visual Studio)

```bash
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019"
cmake --build . --config Release
cmake --install .
```

### Android

```bash
# Place CMakeLists.txt in android/app/src/main/cpp/
# Use Android Studio with NDK 23 or later
```

---

## Future Enhancements

1. **QUIC Protocol**: For better mobile support
2. **Encrypted Channels**: TLS 1.3 or custom AES layer
3. **Cloud Sync**: Optional cloud fallback for offline transfer
4. **Web UI**: Dashboard and management interface
5. **Bandwidth Limiting**: User-configurable rate limiting
6. **Incremental Backup**: Snapshot-based versioning
7. **Group Sync**: Multi-device synchronization groups
8. **Scheduled Sync**: Time-based automatic synchronization

---

## Performance Targets

| Metric                   | Target           |
| ------------------------ | ---------------- |
| Device Discovery Time    | < 5 seconds      |
| File Transfer Throughput | > 100 MB/s (LAN) |
| Memory per Transfer      | < 50 MB          |
| Chunk Latency            | < 100ms          |
| CPU Usage (idle)         | < 1%             |
| Reconnect Time           | < 2 seconds      |
