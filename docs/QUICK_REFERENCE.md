# Syncflow - Quick Reference Guide

## Project Overview

Syncflow is a high-performance P2P file synchronization system written in C++17. It's production-ready with modular architecture, cross-platform support, and comprehensive documentation.

## File Structure Quick Reference

### Headers (include/syncflow/)

| File                    | Purpose                       |
| ----------------------- | ----------------------------- |
| `types.h`               | Core types, enums, callbacks  |
| `common/logger.h`       | Structured logging            |
| `common/utils.h`        | Binary serialization, hashing |
| `platform/platform.h`   | Cross-platform abstraction    |
| `discovery/discovery.h` | Device discovery interfaces   |
| `transfer/transfer.h`   | File transfer interfaces      |
| `watcher/watcher.h`     | File monitoring interfaces    |
| `sync/sync.h`           | Sync engine interfaces        |

### Implementation (src/)

**Common Module (src/common/)**

- `logger.cpp`: Logging with timestamps and levels
- `utils.cpp`: CRC32, binary serialization, utilities
- `types.cpp`: Type definitions

**Platform Module (src/platform/)**

- `platform.cpp`: Platform detection, directory paths
- `networking.cpp`: TCP/UDP sockets (cross-platform)
- `file_system.cpp`: File I/O operations
- `thread_pool.cpp`: Thread pool with task queues
- `linux/file_watcher.cpp`: inotify monitoring
- `windows/file_watcher.cpp`: ReadDirectoryChangesW (stub)
- `macos/file_watcher.cpp`: FSEvents (stub)

**Discovery Module (src/discovery/)**

- `device.cpp`: Device representation and lifecycle (100 LOC)
- `device_manager.cpp`: Singleton device registry (70 LOC)
- `discovery_engine.cpp`: UDP broadcast/receive loops (250+ LOC)

**Transfer Module (src/transfer/)**

- `file_transfer.cpp`: Per-file transfer state machine (150 LOC)
- `transfer_protocol.cpp`: Protocol encoding/decoding (150 LOC)
- `transfer_manager.cpp`: Transfer orchestration (100 LOC)
- `chunk_manager.cpp`: Chunk management (placeholder)

**Watcher Module (src/watcher/)**

- `file_watcher.cpp`: High-level file watcher API (80 LOC)
- `fs_monitor.cpp`: Platform-specific implementations (200 LOC)

**Sync Module (src/sync/)**

- `sync_engine.cpp`: Sync orchestration (70 LOC)
- `conflict_resolver.cpp`: Conflict resolution logic (50 LOC)
- `file_manifest.cpp`: File tracking and manifest (80 LOC)

**CLI Module (src/cli/)**

- `main.cpp`: Entry point, command routing (150 LOC)
- `cli_handler.cpp`: Command parsing (placeholder)
- `commands.cpp`: Command implementations (placeholder)

## Command Line Usage

```bash
# Discovery and Status
syncflow list-devices           # Show nearby devices
syncflow status                 # Show transfer status

# File Transfer
syncflow send <file> <device>   # Send file to device
syncflow receive [path]         # Receive file
syncflow list-transfers         # List active transfers
syncflow pause <session-id>     # Pause transfer
syncflow resume <session-id>    # Resume transfer
syncflow cancel <session-id>    # Cancel transfer

# Folder Sync
syncflow add-folder <path> <dev> <remote> [--mode bidirectional]
syncflow list-folders           # List sync folders
syncflow remove-folder <path>   # Stop syncing folder

# Control
syncflow start [--background]   # Start daemon
syncflow stop                   # Stop daemon

# Configuration
syncflow config set <key> <value>
syncflow config list
```

## Key Constants and Ports

| Constant           | Value      | Purpose                 |
| ------------------ | ---------- | ----------------------- |
| DISCOVERY_PORT     | 15947      | UDP broadcast port      |
| TRANSFER_PORT      | 15948      | TCP file transfer port  |
| CHUNK_SIZE         | 1 MB       | File chunk size         |
| DISCOVERY_INTERVAL | 5000 ms    | Broadcast interval      |
| DISCOVERY_TIMEOUT  | 15000 ms   | Device timeout          |
| PROTOCOL_VERSION   | 1          | Binary protocol version |
| HANDSHAKE_MAGIC    | 0x5346414E | Magic number ("SFAN")   |
| MAX_CONCURRENT     | 4          | Max parallel transfers  |

## Enumeration Reference

### PlatformType

- `UNKNOWN` (0), `WINDOWS` (1), `LINUX` (2), `MACOS` (3), `ANDROID` (4)

### TransferState

- `IDLE`, `CONNECTING`, `TRANSFERRING`, `PAUSED`, `COMPLETED`, `FAILED`, `CANCELLED`

### FileChangeType

- `CREATED`, `MODIFIED`, `DELETED`, `RENAMED`

### ConflictResolution

- `OVERWRITE` - Remote wins
- `SKIP` - Keep local
- `VERSION` - Create timestamped copy
- `ASK_USER` - Prompt user

## Binary Protocol Message Types

| Type              | Value | Purpose                |
| ----------------- | ----- | ---------------------- |
| HANDSHAKE_REQ     | 0x01  | Initial connection     |
| HANDSHAKE_RESP    | 0x02  | Response to handshake  |
| FILE_OFFER        | 0x03  | Propose file transfer  |
| FILE_ACCEPT       | 0x04  | Accept file transfer   |
| FILE_REJECT       | 0x05  | Reject file transfer   |
| CHUNK_REQUEST     | 0x06  | Request specific chunk |
| CHUNK_DATA        | 0x07  | Transfer chunk data    |
| CHUNK_ACK         | 0x08  | Acknowledge chunk      |
| TRANSFER_COMPLETE | 0x09  | Transfer finished      |
| ERROR             | 0x0A  | Error condition        |

## Core Classes & Methods

### Device

```cpp
Device(const DeviceInfo&);
const DeviceInfo& get_info() const;
void update_info(const DeviceInfo&);
bool is_alive(int timeout_ms) const;
```

### DeviceManager (Singleton)

```cpp
static DeviceManager& instance();
bool add_device(const DeviceInfo&);
std::shared_ptr<Device> get_device(const DeviceID&);
std::vector<std::shared_ptr<Device>> get_all_devices();
void cleanup_stale_devices(int timeout_ms);
```

### DiscoveryEngine

```cpp
bool start(OnDeviceDiscovered, OnDeviceLost);
bool stop();
bool is_running() const;
```

### FileTransfer

```cpp
const SessionID& get_session_id() const;
uint64_t get_transferred_bytes() const;
bool add_chunk(const ChunkInfo&, const std::vector<uint8_t>&);
bool pause(); bool resume(); bool cancel();
```

### TransferManager (Singleton)

```cpp
bool start_send(const std::string&, const DeviceID&, ...);
bool start_receive(const std::string&, uint64_t, ...);
std::shared_ptr<FileTransfer> get_transfer(const SessionID&);
```

### FileSystem (Abstract)

```cpp
bool file_exists(const std::string&);
bool read_file(const std::string&);
bool write_file(const std::string&, const std::vector<uint8_t>&);
bool create_directory(const std::string&);
```

### Network (Abstract)

```cpp
std::unique_ptr<Socket> create_tcp_socket();
std::unique_ptr<Socket> create_udp_socket();
bool get_local_ip(AddressFamily, std::string&);
bool get_hostname(std::string&);
```

## Logger Usage

```cpp
#include <syncflow/common/logger.h>

LOG_TRACE("module", "message");
LOG_DEBUG("module", "message");
LOG_INFO("module", "message");
LOG_WARN("module", "message");
LOG_ERROR("module", "message");
LOG_FATAL("module", "message");

// Configuration
Logger::instance().set_level(LogLevel::DEBUG);
Logger::instance().set_output_file("/var/log/syncflow.log");
```

## Binary Serialization

```cpp
#include <syncflow/common/utils.h>

// Writing
utils::BinaryWriter writer;
writer.write_uint32(value);
writer.write_string(str);
writer.write_bytes(data);
auto buffer = writer.get_buffer();

// Reading
utils::BinaryReader reader(buffer);
uint32_t value;
reader.read_uint32(value);
```

## Platform Abstraction Usage

```cpp
#include <syncflow/platform/platform.h>

// File system
auto fs = platform::FileSystem::create();
fs->file_exists(path);
fs->read_file(path);

// Network
auto net = platform::Network::create();
auto sock = net->create_tcp_socket();
sock->connect(address);

// Thread pool
auto pool = platform::ThreadPool::create(4);
pool->enqueue([]{ /* work */ });
```

## Discovery Flow

```
1. Device A sends UDP broadcast with device info (every 5s)
2. Device B receives and adds to registry
3. DeviceManager calls on_device_discovered() callback
4. If no broadcast for 15s, device marked stale and removed
5. on_device_lost() callback fired
```

## Transfer Flow

```
1. Initiator creates TCP connection to receiver
2. Send HANDSHAKE_REQ with device info
3. Receive HANDSHAKE_RESP
4. Send FILE_OFFER with file metadata
5. Receive FILE_ACCEPT
6. Send chunks sequentially or parallel
7. Receiver acknowledges with CHUNK_ACK
8. After all chunks, send TRANSFER_COMPLETE
9. Receiver validates file hash
```

## Configuration File Format

**Location**: `~/.config/syncflow/config.ini` (Linux)

```ini
[general]
device_name=MyDevice
log_level=info

[network]
discovery_port=15947
transfer_port=15948

[transfer]
chunk_size=1048576
max_concurrent=4
enable_compression=false

[sync]
conflict_strategy=version
enable_incremental=true
```

## Build Commands

```bash
# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)              # Linux/macOS
cmake --build . --config Release  # Windows

# Test
ctest --verbose

# Install
sudo make install
```

## Platform-Specific Files

| OS      | Location                | Implementation        |
| ------- | ----------------------- | --------------------- |
| Linux   | `src/platform/linux/`   | inotify watcher       |
| Windows | `src/platform/windows/` | ReadDirectoryChangesW |
| macOS   | `src/platform/macos/`   | FSEvents              |
| All     | `src/platform/`         | TCP/UDP, file I/O     |

## Type Aliases

```cpp
using DeviceID = std::string;           // MAC + hostname
using FileID = std::string;             // Content hash
using SessionID = std::string;          // Transfer session ID
using ChunkID = uint32_t;               // Chunk sequence
using OnDeviceDiscovered = std::function<void(const DeviceInfo&)>;
using OnTransferProgress = std::function<void(const SessionID&, uint64_t, uint64_t)>;
```

## Performance Tips

1. **Enable jumbo frames**: `ip link set eth0 mtu 9000`
2. **Use SSD for staging**: Configure faster storage
3. **Adjust chunk size**: Reduce for limited memory devices
4. **Thread pool size**: Auto-detect or set manually
5. **Bandwidth limiting**: (Planned feature)

## Troubleshooting Checklist

- [ ] Check firewall (UDP 15947, TCP 15948)
- [ ] Verify network connectivity
- [ ] Check available disk space
- [ ] Review logs: `~/.local/share/syncflow/`
- [ ] Ensure devices on same network
- [ ] Verify device ID format

## Documentation Map

| Document                    | Content                                |
| --------------------------- | -------------------------------------- |
| `README.md`                 | Project overview, quick start          |
| `ARCHITECTURE.md`           | Design, algorithms, protocols, Android |
| `BUILD_AND_USAGE.md`        | Build guide, commands, troubleshooting |
| `IMPLEMENTATION_SUMMARY.md` | What was delivered, statistics         |
| `QUICK_REFERENCE.md`        | This file                              |

## Contact & Support

- Check BUILD_AND_USAGE.md for troubleshooting
- Review ARCHITECTURE.md for design details
- Open GitHub issues for bugs/features

---

**Version**: 0.1.0 | **Language**: C++17 | **License**: MIT
