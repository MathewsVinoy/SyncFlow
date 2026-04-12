# Syncflow - Android Termux Installation & Build Guide

## Overview

This guide explains how to build and run Syncflow on Android using **Termux** with root access. Termux provides a Linux environment on Android with access to standard POSIX APIs, making it ideal for running Syncflow.

## Prerequisites

### 1. Install Termux
- Download **Termux** from F-Droid (recommended) or Google Play Store
- Grant necessary permissions when prompted

### 2. Obtain Root Access (Optional but Recommended)
For full functionality:
```bash
# Option 1: Using Termux:Boot (recommended)
pkg install termux-boot
# Then enable with System Settings > Accessibility

# Option 2: Using su (if device is rooted)
pkg install root-repo
pkg install sudo
```

### 3. Install Required Build Tools

```bash
# Update package manager
pkg update
pkg upgrade -y

# Install essential build tools
pkg install -y build-essential
pkg install -y cmake
pkg install -y clang
pkg install -y binutils
pkg install -y git

# Verify installations
cmake --version
gcc --version
clang --version
```

## Building Syncflow in Termux

### Step 1: Clone or Copy the Repository

**Option A: Clone from Git**
```bash
cd ~
git clone <syncflow-repo-url> syncflow
cd syncflow
```

**Option B: Transfer Files via Termux:Tasker or Syncthing**
```bash
# Receive files via Termux
cd ~
# Use Syncthing, Dropbox, or your preferred method
```

### Step 2: Configure and Build

```bash
# Create build directory
mkdir -p build
cd build

# Configure CMake for Termux/Android
# Method 1: Automatic detection
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=17

# Method 2: Explicit Android configuration (if auto-detection fails)
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=17 \
    -DCMAKE_SYSTEM_NAME=Linux

# Build the project
make -j$(nproc)

# Verify build
./syncflow --help
```

### Step 3: Run with Root Access

```bash
# Option 1: Using su command (if available)
su -c "~/syncflow/build/syncflow list-devices"

# Option 2: Start root session first
su
cd ~/syncflow/build
./syncflow list-devices
exit

# Option 3: Using sudo (if installed)
sudo ~/syncflow/build/syncflow list-devices
```

## Installation

### Local Installation (Recommended for Termux)

```bash
# Option 1: Direct use from build directory
~/syncflow/build/syncflow [command] [args]

# Option 2: Install to Termux prefix
cd ~/syncflow/build
make install
# Binary will be at: $PREFIX/bin/syncflow
# Can now run: syncflow [command]

# Option 3: Create wrapper script
cat > ~/bin/syncflow << 'EOF'
#!/bin/bash
~/syncflow/build/syncflow "$@"
EOF
chmod +x ~/bin/syncflow
```

## Creating Termux Automation Scripts

### Script 1: Build and Install (`build-termux.sh`)

```bash
#!/bin/bash
# Syncflow Termux build script

set -e  # Exit on error

REPO_DIR="$HOME/syncflow"
BUILD_DIR="$REPO_DIR/build"

echo "[*] Building Syncflow for Termux..."

cd "$REPO_DIR"

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
echo "[*] Configuring CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=17

# Build
echo "[*] Building..."
make -j$(nproc)

echo "[*] Build complete! Binary at: $BUILD_DIR/syncflow"
echo "[*] Run with: su -c '$BUILD_DIR/syncflow list-devices'"
```

### Script 2: Quick Start with Root (`run-syncflow.sh`)

```bash
#!/bin/bash
# Syncflow Termux runner with root access

BINARY="$HOME/syncflow/build/syncflow"

if [ ! -f "$BINARY" ]; then
    echo "[!] Binary not found at $BINARY"
    echo "[*] Building first..."
    bash ~/syncflow/scripts/build-termux.sh
fi

# Run with available privilege escalation
if command -v sudo &> /dev/null; then
    exec sudo "$BINARY" "$@"
elif command -v su &> /dev/null; then
    exec su -c "$BINARY $*"
else
    exec "$BINARY" "$@"
fi
```

### Script 3: Continuous Monitoring (`monitor-sync.sh`)

```bash
#!/bin/bash
# Monitor file synchronization continuously

SYNC_DIR="${1:-.}"
BINARY="$HOME/syncflow/build/syncflow"

echo "[*] Monitoring sync directory: $SYNC_DIR"
echo "[*] Press Ctrl+C to stop"

if command -v sudo &> /dev/null; then
    sudo "$BINARY" add-folder "$SYNC_DIR"
    while true; do
        sudo "$BINARY" list-devices
        sleep 5
    done
else
    su -c "
        $BINARY add-folder $SYNC_DIR
        while true; do
            $BINARY list-devices
            sleep 5
        done
    "
fi
```

## Running Syncflow

### Basic Commands

```bash
# Discover devices on network
su -c "~/syncflow/build/syncflow list-devices"

# Send a file to device
su -c "~/syncflow/build/syncflow send /path/to/file device-id"

# Add folder for synchronization
su -c "~/syncflow/build/syncflow add-folder /path/to/folder"

# View configuration
su -c "~/syncflow/build/syncflow show-config"
```

### Example Workflow

```bash
# 1. Start Termux with root shell
su

# 2. Discover peers on network
~/syncflow/build/syncflow list-devices

# 3. Choose a device and send a file
~/syncflow/build/syncflow send /sdcard/Documents/file.pdf device-123

# 4. Or set up continuous sync
~/syncflow/build/syncflow add-folder /sdcard/Sync
~/syncflow/build/syncflow start-sync

# 5. Monitor progress
watch ~/syncflow/build/syncflow list-transfers

# 6. Exit root
exit
```

## File Permissions and Storage

### Accessing Android Storage from Termux

```bash
# User-accessible directories (no root needed)
cd ~/storage/downloads   # Downloads folder
cd ~/storage/documents   # Documents folder
cd ~/storage/pictures    # Pictures folder
cd ~/storage/music       # Music folder
cd ~/storage/movies      # Movies folder

# With root access - access all storage
su -c "ls /sdcard"
su -c "ls /storage/emulated/0"
```

### Storage Setup for Sync

```bash
# Create sync folder with proper permissions
su -c "mkdir -p /sdcard/Syncflow"
su -c "chmod 755 /sdcard/Syncflow"
su -c "chown $(id -u):$(id -g) /sdcard/Syncflow"

# Add to Syncflow
su -c "~/syncflow/build/syncflow add-folder /sdcard/Syncflow"
```

## Troubleshooting

### Build Issues

**Problem**: CMake not found
```bash
# Solution: Install cmake
pkg install cmake
```

**Problem**: C++ compiler errors
```bash
# Solution: Install clang
pkg install clang
```

**Problem**: Permission denied when running
```bash
# Solution: Ensure file has execute permissions
chmod +x ~/syncflow/build/syncflow
# Or run with su/sudo
su -c "~/syncflow/build/syncflow list-devices"
```

### Runtime Issues

**Problem**: "Address already in use" error
```bash
# Solution: Check if syncflow is already running
ps aux | grep syncflow
# Kill existing process
killall syncflow
```

**Problem**: Network device discovery fails
```bash
# Check network connectivity
ping 8.8.8.8

# Verify broadcast packets
# Run on another device:
# tcpdump -i any -n udp port 15947
```

**Problem**: File access denied
```bash
# Ensure proper permissions
su -c "ls -la /path/to/directory"
# Fix permissions if needed
su -c "chmod 755 /path/to/directory"
```

### Enable Logging for Debugging

```bash
# Export log level
export SYNCFLOW_LOG_LEVEL=DEBUG

# Run with detailed output
su -c "SYNCFLOW_LOG_LEVEL=DEBUG ~/syncflow/build/syncflow list-devices"

# Capture logs to file
su -c "SYNCFLOW_LOG_LEVEL=DEBUG ~/syncflow/build/syncflow list-devices > ~/syncflow.log 2>&1"
```

## Performance Optimization

### Termux Configuration for Better Performance

```bash
# Increase memory if device allows
# (Add to ~/.termux/termux.properties):
# terminal-transcript-rows=100

# Use clang for faster compilation
export CC=clang
export CXX=clang++

# Rebuild with optimizations
cd ~/syncflow/build
cmake .. -DCMAKE_CXX_FLAGS="-O3 -march=native"
make -j$(nproc)
```

### Running as Background Service

```bash
# Create systemd service file (requires termux-services)
pkg install termux-services

# Create service
mkdir -p ~/.termux/services
cat > ~/.termux/services/syncflow.service << 'EOF'
[Service]
Type=simple
User=%u
WorkingDirectory=%h
ExecStart=%h/syncflow/build/syncflow start-sync
Restart=always
RestartSec=5

[Install]
WantedBy=default.target
EOF

# Enable and start
systemctl --user enable syncflow.service
systemctl --user start syncflow.service
```

## Advanced: Building with NDK Integration

If you need full Android NDK features:

```bash
# Install Android NDK (requires extra setup)
# This is advanced and usually not needed for Termux

# Instead, use the standard Termux build:
cd ~/syncflow/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Uninstallation

```bash
# Remove build directory
rm -rf ~/syncflow/build

# Remove source (optional)
rm -rf ~/syncflow

# Remove wrapper scripts
rm -f ~/bin/syncflow
```

## Summary

| Task | Command |
|------|---------|
| Install tools | `pkg install build-essential cmake clang` |
| Clone repo | `cd ~ && git clone <url> syncflow` |
| Configure | `cd syncflow/build && cmake .. -DCMAKE_BUILD_TYPE=Release` |
| Build | `make -j$(nproc)` |
| Run | `su -c "~/syncflow/build/syncflow list-devices"` |
| Install | `make install` (installs to $PREFIX) |

For issues or questions, check the main [BUILD_AND_USAGE.md](../docs/BUILD_AND_USAGE.md) and [ARCHITECTURE.md](../docs/ARCHITECTURE.md) documentation.
