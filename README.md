# Syncflow peer demo

This workspace includes a modular peer-to-peer file sync system with **cross-platform support** (Linux, macOS, Windows).

Each device runs both:

- a UDP discovery listener and broadcaster
- a TCP server for inbound connections
- a TCP client that connects to discovered peers
- optional file sync from config.json
- device authentication using certificates
- trusted device management

The log shows the device name and IP for every discovery and connection event.

## Supported Platforms

- **Linux** (Ubuntu, Debian, Fedora, etc.)
- **macOS** (Intel and Apple Silicon)
- **Windows** (10, 11)
- **Android/Termux**

## Android and Termux

The networking code uses POSIX sockets and standard C++, so it works on Linux and on Android through Termux.

In Termux, install the build tools first, then build with CMake.
Note: The Qt-based desktop GUI is not built in Termux/Android. Use the CLI target (`syncflow_peer`) when building on Termux. To explicitly enable GUI builds on supported systems, use `-DBUILD_GUI=ON` when running CMake (Qt is required).

## Build

Use CMake to build the `syncflow_peer` executable.

### Prerequisites

**Linux/macOS:**

```bash
sudo apt-get install build-essential cmake libssl-dev  # Ubuntu/Debian
brew install cmake openssl                             # macOS
```

**Windows:**

- Install [Visual Studio 2019+](https://visualstudio.microsoft.com/downloads/) with C++ tools
- Install [CMake](https://cmake.org/download/) (3.16+)
- OpenSSL is automatically found/linked

**macOS (Homebrew):**

```bash
brew install cmake openssl
```

### Build Steps

**Linux/macOS:**

```bash
cmake -S . -B build
cmake --build build -j4
```

**Windows (Command Prompt):**

```cmd
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build -j4 --config Release
```

**Windows (PowerShell):**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build -j4 --config Release
```

**Termux (Android):**

```bash
pkg install cmake openssl-dev clang
cmake -S . -B build
cmake --build build -j4
```

### Build with Custom Device Name

```bash
cmake -DDEVICE_NAME="my-laptop" -S . -B build
cmake --build build -j4
```

On Windows with custom device name:

```cmd
cmake -DDEVICE_NAME="my-windows-pc" -S . -B build -G "Visual Studio 17 2022"
cmake --build build -j4 --config Release
```

## Run

Run the executable on all devices on the same network.

## Qt GUI (desktop UI)

A Qt-based desktop GUI is available as a separate target `syncflow_gui` and provides a graphical interface for discovery, device approval, and file sync monitoring.

Build the GUI target after configuring CMake (in the repository root):

```bash
cmake -S . -B build
cmake --build build --target syncflow_gui -j$(nproc)
```

Run the GUI from the build directory:

```bash
cd build
./syncflow_gui
```

On Windows run `syncflow_gui.exe` from the build output folder. If the app fails to start due to missing Qt libraries, run it with the appropriate library path environment variable (for example `LD_LIBRARY_PATH` on Linux).

### Linux/macOS

```bash
# Basic usage
./build/syncflow_peer

# With custom device name
./build/syncflow_peer --device laptop-a

# Run in background
./build/syncflow_peer --device laptop-a --detach

# Show configuration
./build/syncflow_peer show-config
```

### Windows

```cmd
# Command Prompt - basic usage
.\build\Release\syncflow_peer.exe

# With custom device name
.\build\Release\syncflow_peer.exe --device my-windows-pc

# Background mode (opens in background console)
.\build\Release\syncflow_peer.exe --device my-windows-pc --detach

# Show configuration
.\build\Release\syncflow_peer.exe show-config
```

```powershell
# PowerShell - basic usage
./build/Release/syncflow_peer.exe

# With custom device name
./build/Release/syncflow_peer.exe --device my-windows-pc
```

### macOS (Homebrew)

If built with Homebrew's OpenSSL:

```bash
# May need to set library path
export DYLD_LIBRARY_PATH=$(brew --prefix)/opt/openssl/lib:$DYLD_LIBRARY_PATH

./build/syncflow_peer --device macbook-air
```

### Termux (Android)

```bash
# Build and run on Android
./build/syncflow_peer --device android-phone

# Run in background
./build/syncflow_peer --device android-phone --detach

# View logs
tail -f syncflow.log
```

### Starting Sync

Start on the first device, then on the second device. Each will:

- broadcast itself via UDP
- discover other devices
- establish TCP connections
- log connection events
- sync configured files if enabled

- broadcast itself over UDP
- discover the other device
- open a TCP connection
- print connection logs with device name and IP
- if enabled in config.json, send the configured file after the connection is established

### CLI Commands

```
./build/syncflow_peer [options] [command]

Options:
  -d, --device <name>    Specify device name (overrides config.json)
  -c, --config <path>    Use specified config.json path
  --detach               Run in background as daemon (output to syncflow.log)
  --help                 Show help message

Commands:
  start                  Start the peer node (default if no command given)
  show-config            Print resolved configuration and build settings
  set-device <name>      Set device_name in config.json
```

**Examples:**

- `./build/syncflow_peer --help` — view all commands
- `./build/syncflow_peer show-config` — print current config and device name
- `./build/syncflow_peer set-device my-laptop` — update config.json device_name to "my-laptop"
- `./build/syncflow_peer --device temp-name start` — temporarily use a different device name for this run
- `./build/syncflow_peer -c /path/to/custom.json show-config` — read from custom config file

### Running in the background

Use `--detach` to run the peer node as a background process:

**Linux/macOS:**

```bash
# Start in background daemon
./build/syncflow_peer --detach

# With custom device name
./build/syncflow_peer --device my-device --detach

# View logs
tail -f syncflow.log

# Kill the daemon
pkill -f syncflow_peer
```

**Windows:**

```cmd
# Runs in background (new console window)
.\build\Release\syncflow_peer.exe --device my-windows-pc --detach

# View logs
type syncflow.log

# Stop the process - use Task Manager or:
taskkill /IM syncflow_peer.exe
```

**Platform-specific behavior:**

- **Linux/macOS**: Traditional POSIX daemon (forks, detaches from terminal)
- **Windows**: Background process with output redirected to `syncflow.log`
- **Termux**: Background process (similar to Linux)

## File sync config

Edit [config.json](config.json) to configure file sync and device name:

- `device_name`: name for this device (empty = use hostname; CLI override with `--device`)
- `enabled`: turn file sync on or off
- `source_path`: file or folder to send over the network
- `receive_dir`: folder where incoming files are saved

Enable file sync only on the device that should send the folder tree.

If `source_path` points to a folder, the whole directory tree is synced.

The default config uses [sync/](sync/) as a demo folder tree.

## Ports

- UDP discovery: 45454
- TCP connection: 45455

## Platform-Specific Configuration

### Linux

Configuration files are stored in:

```
~/.config/syncflow/
├── config.json
└── .syncflow/
    ├── certs/
    ├── transfer.log
    └── trusted_devices.txt
```

Or use `XDG_CONFIG_HOME` environment variable to change the location.

### macOS

Configuration files are stored in:

```
~/Library/Application Support/syncflow/
├── config.json
└── .syncflow/
    ├── certs/
    ├── transfer.log
    └── trusted_devices.txt
```

Caches (if needed):

```
~/Library/Caches/syncflow/
```

### Windows

Configuration files are stored in:

```
%APPDATA%\syncflow\
├── config.json
└── .syncflow\
    ├── certs\
    ├── transfer.log
    └── trusted_devices.txt
```

Typical path (example):

```
C:\Users\YourUsername\AppData\Roaming\syncflow\
```

Local app data (caches):

```
%LOCALAPPDATA%\syncflow\
```

### Android/Termux

Configuration files are stored in:

```
$HOME/.config/syncflow/
├── config.json
└── .syncflow/
    ├── certs/
    ├── transfer.log
    └── trusted_devices.txt
```

Or in Termux storage:

```
/data/data/com.termux/files/home/.config/syncflow/
```

## Security

Syncflow includes comprehensive security features to ensure only trusted devices can connect and that all transferred data is protected:

### Features

- **Device Authentication**: Each device generates a self-signed X.509 certificate with a unique fingerprint (SHA256)
- **Trusted Device List**: Maintains a persistent list of approved devices stored in `.syncflow/trusted_devices.txt`
- **Data Integrity**: All transferred data is protected using HMAC-SHA256 for integrity verification
- **Certificate Management**: Automatic certificate generation and validation
- **Man-in-the-Middle Protection**: Device fingerprints ensure communication is only established with approved peers

### Certificate Management

Certificates are automatically generated on first run and stored in `.syncflow/certs/`:

```bash
.syncflow/
├── certs/
│   ├── device.crt        # Self-signed X.509 certificate
│   └── device.key        # Private key (protected)
├── transfer.log          # File sync audit log
└── trusted_devices.txt   # List of approved peers
```

### Trusted Devices

First connection from a new device:

1. Device sends its certificate with unique fingerprint
2. Device is added to trusted list (not yet approved)
3. User approves device:
   ```bash
   ./build/syncflow_peer approve-device <fingerprint>
   ```
4. Once approved, future connections are allowed

### Configuration

Edit `config.json` security section:

```json
"security": {
  "enabled": true,                              // Enable security features
  "cert_dir": ".syncflow/certs",               // Certificate storage directory
  "require_approval": true,                     // Require manual approval for new devices
  "trusted_devices_file": ".syncflow/trusted_devices.txt"  // Trusted devices list
}
```

### Best Practices

- **Secure Key Storage**: Private keys are stored locally with restricted file permissions (600)
- **Certificate Pinning**: Device fingerprints act as certificate pins for peer verification
- **Approval Workflow**: New devices require explicit user approval before sync begins
- **Audit Logging**: All transfers include timing and integrity information in `.syncflow/transfer.log`
- **Network Security**: Use on trusted networks (LAN); consider VPN for untrusted networks

### Data Protection

All transferred files are verified using HMAC-SHA256:

- File integrity: Detects accidental corruption or tampering
- Authentication: Proves sender identity using certificate fingerprint
- Non-repudiation: Transfer records include device identity and timestamps
