# Syncflow for Android Termux - README

## What is This?

This is **Syncflow** - a peer-to-peer file synchronization system - adapted to run on **Android** using the **Termux** terminal emulator with **root access**.

### Why Termux?

- ✅ Full Linux environment on Android
- ✅ Access to standard C++ build tools (CMake, Clang, GCC)
- ✅ POSIX socket APIs for networking
- ✅ inotify support for file monitoring
- ✅ Root access for system-wide operations

## Quick Start (30 seconds)

### Step 1: Install Termux

Download **Termux** from [F-Droid](https://f-droid.org/en/packages/com.termux/) (recommended)

### Step 2: Copy Syncflow

Transfer the `syncflow` folder to your Termux home directory (`~/syncflow`)

### Step 3: Run Setup

```bash
bash ~/syncflow/scripts/install-termux.sh ~/syncflow
```

This automatically:
- Installs build tools
- Optionally sets up root access
- Builds Syncflow
- Creates convenience shortcuts

### Step 4: Use Syncflow

```bash
# Discover devices
syncflow list-devices

# Send a file (with root)
su -c "syncflow send /path/to/file device-id"

# Or use the wrapper script
~/syncflow/scripts/run-syncflow.sh list-devices
```

## What Can It Do?

### Device Discovery
- Automatically find other Syncflow users on your network
- Works across WiFi and local networks

### File Transfer
- Send files from Android to other devices (Linux, Windows, macOS)
- Receive files from other devices
- Resume interrupted transfers

### Folder Synchronization
- Set up automatic two-way sync between folders
- Handles file conflicts intelligently
- Works with multiple devices

### Real-time Monitoring
- Detects file changes instantly (using inotify in Termux)
- Syncs automatically when files change

## Installation Methods

### Method 1: Automatic Setup (Recommended)

```bash
bash scripts/install-termux.sh .
```

### Method 2: Manual Build

```bash
# Install tools
pkg install build-essential cmake clang

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run
su -c "./syncflow list-devices"
```

### Method 3: Using Provided Scripts

```bash
# 1. Setup environment
bash scripts/setup-termux.sh

# 2. Build
bash scripts/build-termux.sh .

# 3. Run with privilege escalation
bash scripts/run-syncflow.sh list-devices
```

## Root Access Setup

### Option 1: Termux:Boot (Recommended for persistent access)

1. Install `termux-boot` package:
   ```bash
   pkg install termux-boot
   ```

2. Enable in System Settings → Accessibility → Termux:Boot

3. Boot into root session:
   ```bash
   su
   ```

### Option 2: Use sudo

```bash
pkg install sudo
sudo syncflow list-devices
```

### Option 3: Direct su (if device is rooted)

```bash
su -c "syncflow list-devices"
```

## Common Use Cases

### Send Files to Another Device

```bash
# 1. Discover devices on network
syncflow list-devices

# 2. Send a file
su -c "syncflow send /sdcard/Documents/file.pdf device-123"

# 3. Check transfer progress
su -c "syncflow list-transfers"
```

### Set Up Automatic Folder Sync

```bash
# 1. Create sync folder
su -c "mkdir -p /sdcard/Syncflow && chmod 755 /sdcard/Syncflow"

# 2. Add to Syncflow
su -c "syncflow add-folder /sdcard/Syncflow"

# 3. Start sync (runs in background)
su -c "syncflow start-sync"

# 4. Monitor sync status
su -c "syncflow list-transfers"
```

### Monitor Multiple Folders

```bash
# Add multiple folders
su -c "syncflow add-folder /sdcard/Documents"
su -c "syncflow add-folder /sdcard/Pictures"
su -c "syncflow add-folder /sdcard/Music"

# Start monitoring all
su -c "syncflow start-sync"
```

## File Locations

| Item | Location |
|------|----------|
| Executable | `~/syncflow/build/syncflow` |
| Scripts | `~/syncflow/scripts/` |
| Source code | `~/syncflow/src/` |
| Configuration | `~/.config/syncflow/` |
| Data/logs | `~/.local/share/syncflow/` |
| Android storage | `/sdcard/` or `/storage/emulated/0/` |

## Storage Access

### Without Root (Limited Access)

Termux can access user-friendly folders:
```bash
~/storage/downloads      # Downloads
~/storage/documents      # Documents
~/storage/pictures       # Pictures
~/storage/music          # Music
~/storage/movies         # Movies
```

### With Root Access

Full access to device storage:
```bash
su -c "ls /sdcard/"           # Main storage
su -c "ls /storage/emulated/0/" # Emulated storage
su -c "ls /data/"              # System data
```

## Troubleshooting

### Build Errors

**Error**: `Command 'cmake' not found`
```bash
pkg install cmake
```

**Error**: `C++ compiler errors`
```bash
pkg install clang
```

**Error**: `make: not found`
```bash
pkg install build-essential
```

### Runtime Errors

**Error**: `Address already in use`
```bash
# Kill existing process
killall syncflow
```

**Error**: `Permission denied`
```bash
# Run with root
su -c "~/syncflow/build/syncflow ..."
# Or use the wrapper script
./scripts/run-syncflow.sh ...
```

**Error**: `Peers not discovered`
- Check both devices are on same WiFi network
- Disable VPN if active
- Check firewall isn't blocking UDP port 15947

### Performance Issues

**Slow transfers?**
```bash
# Rebuild with optimizations
cd ~/syncflow/build
cmake .. -DCMAKE_CXX_FLAGS="-O3 -march=native"
make clean && make -j$(nproc)
```

**High CPU usage?**
- Reduce number of watched folders
- Increase sync interval
- Check logs: `export SYNCFLOW_LOG_LEVEL=DEBUG`

## Advanced Features

### Enable Debug Logging

```bash
export SYNCFLOW_LOG_LEVEL=DEBUG
su -c "SYNCFLOW_LOG_LEVEL=DEBUG syncflow list-devices"
```

### Custom Configuration

```bash
# Edit configuration
nano ~/.config/syncflow/config.conf

# Restart syncflow
killall syncflow
su -c "syncflow start-sync"
```

### Run as Background Service

With `termux-services` installed:

```bash
pkg install termux-services

# Create service
mkdir -p ~/.termux/services
cat > ~/.termux/services/syncflow.service << 'EOF'
[Service]
Type=simple
ExecStart=$HOME/syncflow/build/syncflow start-sync
Restart=always
EOF

# Enable and start
systemctl --user enable syncflow.service
systemctl --user start syncflow.service

# Check status
systemctl --user status syncflow.service
```

## Network Configuration

### Check Network Status

```bash
# View active interfaces
ifconfig

# Test connectivity
ping 8.8.8.8

# View current WiFi info
iwconfig
```

### Set Up Private Network

For large files on local network only:
```bash
# Check local IP
ifconfig wlan0

# Configure to use specific interface
su -c "syncflow config --interface wlan0"
```

## Platform-Specific Notes

### Architecture Support

- ✅ ARM64 (most common - `aarch64`)
- ✅ ARM (32-bit - `armv7l`)
- ✅ x86_64 (rare on phones)
- ✅ x86 (rare on phones)

Termux auto-detects and builds for your device.

### Storage Compatibility

- ✅ Works with internal storage
- ✅ Works with microSD cards (if mounted)
- ✅ Works with cloud storage (OneDrive, Google Drive if mounted)
- ⚠️ May have issues with some vendor-specific storage

### Android Version Requirements

- ✅ Android 7.0+ (recommended)
- ✅ Android 10+ (best compatibility)
- ⚠️ Android 6.0 and below (limited support)

## What's Included?

```
syncflow/
├── scripts/
│   ├── install-termux.sh    # One-command setup
│   ├── setup-termux.sh      # Install dependencies
│   ├── build-termux.sh      # Build Syncflow
│   └── run-syncflow.sh      # Run with privilege escalation
├── src/
│   ├── platform/android/    # Android-specific code
│   │   ├── file_watcher.cpp # File monitoring (inotify)
│   │   └── network_config.cpp # Network setup
│   ├── cli/                 # Command-line interface
│   ├── discovery/           # Device discovery
│   ├── transfer/            # File transfer
│   ├── sync/                # Synchronization
│   └── watcher/             # File change detection
├── docs/
│   ├── TERMUX_BUILD.md      # Detailed build guide
│   ├── TERMUX_QUICK_REF.md  # Quick reference
│   ├── BUILD_AND_USAGE.md   # General documentation
│   └── ARCHITECTURE.md      # Technical details
└── README.md                # Main readme
```

## Getting Help

1. **Quick Reference**: [TERMUX_QUICK_REF.md](TERMUX_QUICK_REF.md)
2. **Detailed Guide**: [TERMUX_BUILD.md](TERMUX_BUILD.md)
3. **Architecture**: [ARCHITECTURE.md](ARCHITECTURE.md)
4. **General Help**: Run `syncflow --help`

## Advanced: Development on Android

### Setting Up Development Environment

```bash
# Install additional tools
pkg install vim nano git

# Clone your own fork
git clone <your-fork-url> ~/syncflow-dev
cd ~/syncflow-dev

# Build and test
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
ctest --verbose
```

### Building Custom Versions

Modify source code as needed, then rebuild:

```bash
cd ~/syncflow/build
make clean
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Limitations & Known Issues

| Issue | Workaround |
|-------|-----------|
| Battery drain during continuous sync | Reduce sync frequency |
| WiFi dropout breaks transfer | Automatic resume on reconnect |
| Storage permissions issues | Run with root access |
| No Bluetooth sync | Use WiFi network instead |
| Large files slow | Increase chunk size in config |

## Performance Expectations

| Operation | Typical Time |
|-----------|-------------|
| Device discovery | <5 seconds |
| Small file transfer (1 MB) | <2 seconds |
| Large file transfer (100 MB) | 10-30 seconds |
| Folder scan | <5 seconds |

## Security Notes

⚠️ **Important**: When using root access:
- Only grant root to trusted applications
- Be careful with file permissions
- Avoid running untrusted code as root

Syncflow includes safeguards, but root privileges should be used carefully.

## Uninstallation

```bash
# Remove build artifacts
rm -rf ~/syncflow/build

# Remove source (optional)
rm -rf ~/syncflow

# Remove configuration (optional)
rm -rf ~/.config/syncflow
rm -rf ~/.local/share/syncflow
```

## Contributing

Found a bug or want to add features? 

1. Check [ARCHITECTURE.md](ARCHITECTURE.md) to understand the codebase
2. Read [BUILD_AND_USAGE.md](BUILD_AND_USAGE.md) for build details
3. Submit issues or pull requests

## License

MIT License - See LICENSE file for details

## Next Steps

1. ✅ Install Termux
2. ✅ Run `bash scripts/install-termux.sh .`
3. ✅ Test with `syncflow list-devices`
4. ✅ Read [TERMUX_QUICK_REF.md](TERMUX_QUICK_REF.md) for common tasks
5. ✅ Check [TERMUX_BUILD.md](TERMUX_BUILD.md) for advanced setup

---

**Ready to sync?** Start with: `bash scripts/install-termux.sh .`
