# Syncflow - High-Performance Cross-Platform File Synchronization

![Syncflow](https://img.shields.io/badge/version-0.1.0-blue) ![C++](https://img.shields.io/badge/language-C++17-blue) ![License](https://img.shields.io/badge/license-MIT-green)

**Syncflow** is a high-performance, peer-to-peer file synchronization system designed for seamless file sharing across devices on the same network. Similar to AirDrop or Quick Share, it enables fast, secure, and efficient file transfers without relying on central servers.

## 🚀 Features

- **🔍 Device Discovery**: Automatic UDP broadcast-based discovery of devices on your network
- **🤝 Peer-to-Peer**: Direct TCP connections for transfers without intermediaries
- **📦 Resumable Transfers**: Interrupt-tolerant file transfers with automatic resume
- **⚡ High Performance**: Multi-threaded transfers with configurable concurrency
- **💾 Smart Sync**: Bidirectional folder synchronization with conflict resolution
- **🔔 File Watching**: Real-time detection of file changes using platform-specific APIs (inotify, FSEvents, ReadDirectoryChangesW)
- **🔐 Secure**: Support for device pairing and encrypted communications (planned)
- **🖥️ Cross-Platform**: Windows, Linux, macOS, and Android (via NDK)
- **🎯 Minimal Dependencies**: Lean implementation using only standard libraries + platform APIs

## 🏗️ Architecture

Syncflow follows a **modular, layered architecture** with clear separation of concerns. See **[ARCHITECTURE.md](docs/ARCHITECTURE.md)** for detailed design.

Core modules:

- **Discovery**: UDP broadcast-based device detection
- **Transfer**: TCP file transfer with chunking and resume
- **Watcher**: Platform-specific file change monitoring
- **Sync**: Bidirectional sync with conflict resolution
- **Platform**: Cross-platform abstraction layer
- **Common**: Utilities (logging, serialization, hashing)
- **CLI**: Command-line interface

## 🔌 Protocol Specification

### Device Discovery (UDP Port 15947)

- Broadcast every 5 seconds
- Timeout after 15 seconds of inactivity
- Binary format with magic number and version

### File Transfer (TCP Port 15948)

- Custom binary protocol (not HTTP/REST)
- 1 MB chunks with CRC32 validation
- Support for concurrent transfers and resume
- Compression flag for optional data compression

## 📋 Quick Start

### Prerequisites

**Linux:**

```bash
sudo apt-get install build-essential cmake git
```

**macOS:**

```bash
brew install cmake
```

**Windows:**

- Visual Studio 2019+ or MinGW-w64
- CMake 3.20+

### Build & Install

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

### Usage Examples

**List nearby devices:**

```bash
syncflow list-devices
```

**Send a file:**

```bash
syncflow send /path/to/file device-name
```

**Show transfer status:**

```bash
syncflow status
```

**Add folder to sync:**

```bash
syncflow add-folder ~/Documents device-id ~/RemoteDocuments --mode bidirectional
```

## 📚 Documentation

- **[ARCHITECTURE.md](docs/ARCHITECTURE.md)** - Comprehensive design documentation, algorithms, protocol details
- **[BUILD_AND_USAGE.md](docs/BUILD_AND_USAGE.md)** - Build instructions, complete command reference, troubleshooting

## 📁 Project Structure

```
syncflow/
├── src/
│   ├── common/              # Logger, utilities, types
│   ├── platform/            # Cross-platform abstraction
│   ├── discovery/           # Device discovery via UDP
│   ├── transfer/            # File transfer protocol & logic
│   ├── watcher/             # File system monitoring
│   ├── sync/                # Sync engine & conflict resolution
│   └── cli/                 # CLI interface
├── include/                 # Public headers
├── tests/                   # Unit tests
├── docs/                    # Complete documentation
└── CMakeLists.txt          # CMake build configuration
```

## 🧪 Testing

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make
ctest --verbose
```

## 🚀 Performance Targets

| Metric                | Target          |
| --------------------- | --------------- |
| Device Discovery Time | <5 seconds      |
| Transfer Throughput   | >100 MB/s (LAN) |
| Memory per Transfer   | <50 MB          |
| Idle CPU              | <1%             |
| Reconnect Time        | <2 seconds      |

## 🔐 Security

- Device pairing with handshake verification
- CRC32 checksum validation for data integrity
- File permissions preservation
- Optional encryption support (planned)

## 🤖 Android Integration

Syncflow can be integrated into Android apps via NDK with JNI bindings. See [ARCHITECTURE.md](docs/ARCHITECTURE.md#android-integration-via-ndk) for details.

## 📄 License

This project is licensed under the MIT License - see [LICENSE](LICENSE) file for details.

## 🤝 Contributing

Contributions are welcome! Please see documentation for code style guidelines and testing requirements.

## 📞 Support

For issues and questions:

- Check [BUILD_AND_USAGE.md](docs/BUILD_AND_USAGE.md#troubleshooting) troubleshooting guide
- Review [ARCHITECTURE.md](docs/ARCHITECTURE.md) for design details
- Open GitHub issues for bugs and feature requests

---

**Made with ❤️ for seamless file sharing** | [Documentation](docs/ARCHITECTURE.md)
