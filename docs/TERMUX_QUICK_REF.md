# Syncflow Termux - Quick Reference Card

## One-Command Installation

```bash
# Download to Termux and run:
bash scripts/install-termux.sh .
```

## Manual Quick Start

```bash
# 1. Install prerequisites
pkg install build-essential cmake clang

# 2. Build
cd ~/syncflow/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 3. Run
su -c "~/syncflow/build/syncflow list-devices"
```

## Essential Commands

| Command | Purpose |
|---------|---------|
| `list-devices` | Discover peers on network |
| `send <file> <device>` | Send file to device |
| `add-folder <path>` | Add folder to sync |
| `start-sync` | Start synchronization |
| `show-config` | Show configuration |
| `--help` | Show help |

## Root Access Methods

| Method | How | Pros | Cons |
|--------|-----|------|------|
| `su` | `su -c "command"` | Pre-built | Requires rooted device |
| `sudo` | `sudo command` | Modern | Needs installation |
| Termux:Boot | Auto-start daemon | Persistent | Setup required |

## Environment Setup

```bash
# Make scripts executable
chmod +x scripts/*.sh

# Run build script
./scripts/build-termux.sh .

# Run with privilege escalation
./scripts/run-syncflow.sh list-devices

# Create permanent wrapper
cat > ~/bin/syncflow << 'EOF'
#!/bin/bash
su -c "$HOME/syncflow/build/syncflow $*"
EOF
chmod +x ~/bin/syncflow
```

## File Locations

| Item | Path |
|------|------|
| Binary | `~/syncflow/build/syncflow` |
| Source | `~/syncflow/src/` |
| Config | `~/.config/syncflow/` |
| Data | `~/.local/share/syncflow/` |
| Logs | Check config directory |

## Termux Storage Access

```bash
# User-accessible (no root)
~/storage/downloads
~/storage/documents
~/storage/pictures

# With root
su -c "ls /sdcard/"
su -c "ls /storage/emulated/0"
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "Command not found" | `pkg install build-essential cmake` |
| "Permission denied" | Add execute: `chmod +x file` |
| "Address in use" | Kill existing: `killall syncflow` |
| "Network discovery fails" | Check WiFi connected |
| Build fails | `pkg update && pkg upgrade` |

## Performance Tips

```bash
# Build with optimizations
cmake .. -DCMAKE_CXX_FLAGS="-O3 -march=native"

# Use all cores
make -j$(nproc)

# Run as background service (with termux-services)
pkg install termux-services
systemctl --user enable syncflow
systemctl --user start syncflow
```

## Full Documentation

- **Installation**: [docs/TERMUX_BUILD.md](../docs/TERMUX_BUILD.md)
- **Architecture**: [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md)
- **Building**: [docs/BUILD_AND_USAGE.md](../docs/BUILD_AND_USAGE.md)
- **Usage**: [README.md](../README.md)

## Scripts Reference

| Script | Purpose |
|--------|---------|
| `scripts/setup-termux.sh` | Install dependencies |
| `scripts/build-termux.sh` | Build Syncflow |
| `scripts/run-syncflow.sh` | Run with privilege escalation |
| `scripts/install-termux.sh` | Complete setup (all-in-one) |

## Environment Variables

```bash
# Enable debug logging
export SYNCFLOW_LOG_LEVEL=DEBUG

# Custom config directory
export SYNCFLOW_CONFIG=~/.config/syncflow-custom

# Run with debugging
SYNCFLOW_LOG_LEVEL=DEBUG su -c "~/syncflow/build/syncflow list-devices"
```

## Common Tasks

### Send a file
```bash
su -c "~/syncflow/build/syncflow send /path/to/file 192-168-1-100"
```

### Set up sync folder
```bash
su -c "~/syncflow/build/syncflow add-folder /sdcard/Syncflow"
su -c "~/syncflow/build/syncflow start-sync"
```

### Monitor transfers
```bash
su -c "~/syncflow/build/syncflow list-transfers"
```

### View devices
```bash
su -c "~/syncflow/build/syncflow list-devices"
```

## Creating Aliases

Add to `~/.bashrc`:

```bash
alias sync='su -c "$HOME/syncflow/build/syncflow"'
alias sync-devices='sync list-devices'
alias sync-send='sync send'
```

Then use: `sync-devices`

## Network Troubleshooting

```bash
# Check if on same network
ping 8.8.8.8

# Check active network interfaces
ifconfig

# Monitor network traffic
tcpdump -i any -n udp port 15947

# Restart networking (if needed)
su -c "netcfg eth0 down && netcfg eth0 up"
```

---

**For more help**: Read [docs/TERMUX_BUILD.md](../docs/TERMUX_BUILD.md) or run `syncflow --help`
