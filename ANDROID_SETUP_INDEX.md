# Syncflow Android Termux - Complete Setup Index

## 📍 START HERE

### For First-Time Users
1. **Read**: [README_TERMUX.md](README_TERMUX.md) (5 min read)
2. **Setup**: Run `bash scripts/install-termux.sh .` (auto setup)
3. **Use**: `syncflow list-devices` (test it out)

### For Developers
1. **Learn**: [ANDROID_TERMUX_INTEGRATION.md](ANDROID_TERMUX_INTEGRATION.md)
2. **Build**: `bash scripts/build-termux.sh .`
3. **Code**: Review `src/platform/android/`

### For Troubleshooting
1. **Quick Help**: [docs/TERMUX_QUICK_REF.md](docs/TERMUX_QUICK_REF.md)
2. **Detailed Help**: [docs/TERMUX_BUILD.md](docs/TERMUX_BUILD.md)
3. **Verify**: `bash scripts/verify-android-support.sh .`

---

## 📚 Complete File Guide

### Documentation (Read These)
| File | Purpose | Read Time |
|------|---------|-----------|
| [README_TERMUX.md](README_TERMUX.md) | User guide for Android | 10 min |
| [docs/TERMUX_QUICK_REF.md](docs/TERMUX_QUICK_REF.md) | Quick lookup | 5 min |
| [docs/TERMUX_BUILD.md](docs/TERMUX_BUILD.md) | Detailed setup guide | 20 min |
| [ANDROID_TERMUX_INTEGRATION.md](ANDROID_TERMUX_INTEGRATION.md) | Technical deep dive | 15 min |
| [SETUP_COMPLETE.md](SETUP_COMPLETE.md) | Implementation summary | 15 min |

### Scripts (Use These)
| Script | Purpose | Usage |
|--------|---------|-------|
| `scripts/setup-termux.sh` | Install dependencies | `bash scripts/setup-termux.sh` |
| `scripts/build-termux.sh` | Build Syncflow | `bash scripts/build-termux.sh .` |
| `scripts/run-syncflow.sh` | Run with privilege | `bash scripts/run-syncflow.sh [cmd]` |
| `scripts/install-termux.sh` | One-command setup | `bash scripts/install-termux.sh .` |
| `scripts/verify-android-support.sh` | Verify setup | `bash scripts/verify-android-support.sh .` |

### Implementation (Study These)
| File | Purpose | Lines |
|------|---------|-------|
| `src/platform/android/file_watcher.cpp` | File monitoring | 143 |
| `src/platform/android/network_config.cpp` | Network setup | 102 |
| `CMakeLists.txt` | Build config (modified) | +20 |

---

## 🚀 Quick Start Paths

### Path 1: Fastest Way to Get Running (5 minutes)
```bash
# 1. Copy to Termux home
cp -r /path/to/syncflow ~/syncflow

# 2. Run one command
cd ~/syncflow
bash scripts/install-termux.sh .

# 3. Done! Use it:
su -c "syncflow list-devices"
```

### Path 2: Manual Setup for Developers (15 minutes)
```bash
# 1. Install tools
bash scripts/setup-termux.sh

# 2. Build manually
bash scripts/build-termux.sh ~/syncflow

# 3. Test binary
~/syncflow/build/syncflow --help

# 4. Run with root
su -c "~/syncflow/build/syncflow list-devices"
```

### Path 3: Custom Build with Optimization (20 minutes)
```bash
# 1. Setup
bash scripts/setup-termux.sh

# 2. Build with custom flags
cd ~/syncflow/build
cmake .. -DCMAKE_CXX_FLAGS="-O3 -march=native"
make -j$(nproc)

# 3. Verify
./syncflow --help
```

---

## 🎯 Common Commands

### Discover & Transfer
```bash
# List devices on network
su -c "syncflow list-devices"

# Send a file
su -c "syncflow send /sdcard/file.pdf device-id"

# Monitor transfer
su -c "syncflow list-transfers"
```

### Folder Sync
```bash
# Add folder to sync
su -c "syncflow add-folder /sdcard/Syncflow"

# Start sync daemon
su -c "syncflow start-sync"

# Show configuration
su -c "syncflow show-config"
```

### Convenience Wrapper
```bash
# Or use the run script
bash scripts/run-syncflow.sh list-devices
bash scripts/run-syncflow.sh send /file device
bash scripts/run-syncflow.sh add-folder /path
```

---

## 🔧 Customization

### Enable Debug Logging
```bash
export SYNCFLOW_LOG_LEVEL=DEBUG
su -c "SYNCFLOW_LOG_LEVEL=DEBUG syncflow list-devices"
```

### Custom Build Flags
```bash
cd build
cmake .. -DCMAKE_CXX_FLAGS="-O3 -march=native -flto"
make clean && make -j$(nproc)
```

### Background Service
```bash
# With termux-services
pkg install termux-services

# Create service file
mkdir -p ~/.termux/services
cat > ~/.termux/services/syncflow.service << 'EOF'
[Service]
Type=simple
ExecStart=$HOME/syncflow/build/syncflow start-sync
Restart=always
