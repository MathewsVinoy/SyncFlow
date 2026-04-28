# Cross-Platform Build & Packaging Guide

## Overview

The Syncflow application now includes comprehensive cross-platform support with:

- **Unified networking abstraction** (POSIX vs Winsock)
- **Platform-specific path handling** (config, logs, data directories)
- **CMake cross-platform detection** and build configuration
- **CPack integration** for creating installable packages (.deb, .rpm, .exe, .dmg, .tar.gz)

## Platform Support

### Linux

- **Supported OS**: Ubuntu 20.04+, Debian 11+, Fedora 35+, CentOS 8+
- **Config Paths**: `~/.config/syncflow/` (XDG_CONFIG_HOME)
- **Log Paths**: `~/.cache/syncflow/` (XDG_CACHE_HOME)
- **Data Paths**: `~/.local/share/syncflow/` (XDG_DATA_HOME)
- **Packages**: .deb (Debian), .rpm (Fedora/RHEL), .tar.gz (universal)

### macOS

- **Supported Versions**: macOS 10.15+
- **Config Paths**: `~/Library/Application Support/syncflow/`
- **Log Paths**: `~/Library/Logs/syncflow/`
- **Data Paths**: `~/Library/Application Support/syncflow/`
- **Packages**: .dmg (disk image), .tar.gz

### Windows

- **Supported Versions**: Windows 10 21H2+, Windows 11
- **Config Paths**: `%APPDATA%\syncflow\`
- **Log Paths**: `%LOCALAPPDATA%\syncflow\`
- **Data Paths**: `%LOCALAPPDATA%\syncflow\`
- **Packages**: .exe (NSIS installer), .zip (portable)

## Building

### Quick Build (Linux/macOS)

```bash
./build.sh
```

### Manual Build

#### Linux/macOS (POSIX)

```bash
mkdir -p build
cd build
cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release
cmake --build . -j $(nproc)
```

#### Windows (Visual Studio)

```bash
mkdir build
cd build
cmake -S .. -B . -G "Visual Studio 17" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release -j %NUMBER_OF_PROCESSORS%
```

#### Windows (MinGW)

```bash
mkdir build
cd build
cmake -S .. -B . -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

## Platform Abstraction Layers

### PlatformSocket

Unified socket interface handling POSIX sockets and Winsock.

**Location**: `include/platform/PlatformSocket.h` / `src/platform/PlatformSocket.cpp`

**Key Features**:

- Cross-platform TCP/UDP socket creation
- Non-blocking I/O
- Timeout handling
- Automatic system initialization/cleanup

**Usage**:

```cpp
#include "platform/PlatformSocket.h"

// Initialize system
platform::PlatformSocket::initializeSocketSystem();

// Create TCP socket
auto sock = platform::PlatformSocket::createTCP();
if (sock) {
    sock->bind("0.0.0.0", 8080);
    sock->listen();
    // ...
}

// Cleanup
platform::PlatformSocket::shutdownSocketSystem();
```

### PlatformPaths

Platform-aware configuration, log, and data directory management.

**Location**: `include/platform/PlatformPaths.h` / `src/platform/PlatformPaths.cpp`

**Key Features**:

- XDG Base Directory specification support (Linux)
- macOS Application Support conventions
- Windows AppData handling
- Automatic directory creation

**Usage**:

```cpp
#include "platform/PlatformPaths.h"

// Initialize once at startup
platform::PlatformPaths::initialize("syncflow");

// Get directories
auto config = platform::PlatformPaths::getConfigDir();  // ~/.config/syncflow (Linux)
auto logs = platform::PlatformPaths::getLogDir();        // ~/.cache/syncflow (Linux)
auto data = platform::PlatformPaths::getDataDir();       // ~/.local/share/syncflow (Linux)

// Get specific file paths
auto configFile = platform::PlatformPaths::getConfigFile("config.json");
auto logFile = platform::PlatformPaths::getLogFile("syncflow.log");
```

## Creating Packages

### Linux (Debian package)

```bash
cd build
cpack -G DEB
# Output: syncflow-1.0.0-Linux.deb
```

**Installation**:

```bash
sudo dpkg -i syncflow-1.0.0-Linux.deb
syncflow
```

### Linux (RPM package)

```bash
cd build
cpack -G RPM
# Output: syncflow-1.0.0-Linux.rpm
```

**Installation**:

```bash
sudo rpm -i syncflow-1.0.0-Linux.rpm
syncflow
```

### Linux (Tarball)

```bash
cd build
cpack -G TGZ
# Output: syncflow-1.0.0-Linux.tar.gz
```

**Installation**:

```bash
tar -xzf syncflow-1.0.0-Linux.tar.gz
cd syncflow-1.0.0-Linux
./bin/app
```

### macOS (Disk Image)

```bash
cd build
cpack -G DragNDrop
# Output: syncflow-1.0.0-Darwin.dmg
```

**Installation**: Double-click the .dmg file and drag Syncflow.app to Applications.

### macOS (Tarball)

```bash
cd build
cpack -G TGZ
# Output: syncflow-1.0.0-Darwin.tar.gz
```

**Installation**:

```bash
tar -xzf syncflow-1.0.0-Darwin.tar.gz
open Syncflow.app
```

### Windows (NSIS Installer)

```bash
cd build
cpack -G NSIS
# Output: syncflow-1.0.0-win64.exe
```

**Installation**: Run the .exe file and follow the installer wizard.

### Windows (Portable ZIP)

```bash
cd build
cpack -G ZIP
# Output: syncflow-1.0.0-win64.zip
```

**Installation**: Extract the .zip file and run `app.exe`.

## Cross-Platform Testing

### Test Build on Multiple Platforms

#### GitHub Actions Example

```yaml
name: Build Matrix

on: [push, pull_request]

jobs:
  build:
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v3
      - uses: ilammy/msvc-dev-cmd@v1 # Windows only
        if: runner.os == 'Windows'
      - run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
      - run: cmake --build build -j
      - run: ctest --output-on-failure
        working-directory: build
```

### Local Testing

```bash
# Build and run tests
./build.sh
cd build_linux
ctest --output-on-failure
```

## File Structure

```
syncflow/
├── include/
│   ├── platform/
│   │   ├── PlatformSocket.h      # Cross-platform socket abstraction
│   │   └── PlatformPaths.h       # Platform-aware paths
│   ├── core/
│   ├── networking/
│   ├── sync_engine/
│   └── ...
├── src/
│   ├── platform/
│   │   ├── PlatformSocket.cpp
│   │   └── PlatformPaths.cpp
│   └── ...
├── CMakeLists.txt               # Includes CPack configuration
├── config.json                  # Default configuration
├── build.sh                     # Build script
└── README_PACKAGING.md          # This file
```

## CMake Additions

The `CMakeLists.txt` includes:

- Platform detection (`WIN32`, `APPLE`, `UNIX`)
- Platform-specific linking (Winsock on Windows, pthreads on POSIX)
- CPack configuration for all supported platforms
- Proper directory structure for final packages

## Configuration Management

`ConfigManager` now supports platform-aware configuration:

```cpp
#include "core/ConfigManager.h"
#include "platform/PlatformPaths.h"

int main() {
    // Initialize platform paths
    platform::PlatformPaths::initialize("syncflow");

    // Load configuration from platform-specific directory
    ConfigManager config;
    config.load();  // Searches in XDG_CONFIG_HOME/syncflow (Linux)
                    //              ~/Library/Application Support/syncflow (macOS)
                    //              %APPDATA%\syncflow (Windows)

    std::string syncFolder = config.getString("sync_folder", "./sync");
    return 0;
}
```

## Environment Variables

The platform layer respects standard environment variables:

- **Linux**: `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `HOME`
- **macOS**: `HOME`
- **Windows**: `APPDATA`, `LOCALAPPDATA`, `TEMP`, `USERPROFILE`

## Troubleshooting

### Build Issues

**Windows socket linking error**:

```
undefined reference to `WSAStartup'
```

Ensure the CMakeLists.txt links `ws2_32` on Windows (already configured).

**Linux missing headers**:

```
error: 'socklen_t' was not declared
```

Ensure `<sys/socket.h>` is included before using `socklen_t`.

### Package Issues

**NSIS not found on Windows**:
Install NSIS: https://nsis.sourceforge.io/Download

**RPM build tools missing on Linux**:

```bash
sudo dnf install rpm-build  # Fedora/RHEL
```

**Macintosh bundle creation**:
Ensure `Info.plist` is present in `assets/` directory.

## Performance Optimizations

- All platform layers use efficient OS APIs
- Socket operations support non-blocking I/O
- Path resolution caches results to avoid repeated stat calls
- Cross-platform code shares common abstractions, minimal duplication

## Security Notes

- Windows: Uses Winsock2 with modern security features
- Linux/macOS: Full POSIX compliance with O_NONBLOCK support
- TLS/SSL handled via OpenSSL (same on all platforms)
- Config files respect OS-level permissions
- Data directories follow platform security standards

## Contributing

When adding platform-specific code:

1. Use `#ifdef _WIN32` for Windows-only code
2. Use `#ifdef __APPLE__` for macOS-only code
3. Use `#ifdef __linux__` for Linux-only code
4. Abstract OS differences into platform/ modules
5. Test on all three platforms before submitting

---

**Last Updated**: 2026-04-28
**Version**: 1.0.0
