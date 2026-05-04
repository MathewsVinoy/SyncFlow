# Syncflow peer demo

This workspace now includes a modular two-device LAN demo.

Each device runs both:

- a UDP discovery listener and broadcaster
- a TCP server for inbound connections
- a TCP client that connects to discovered peers
- optional file sync from config.json

The log shows the device name and IP for every discovery and connection event.

## Android and Termux

The networking code uses POSIX sockets and standard C++, so it works on Linux and on Android through Termux.

In Termux, install the build tools first, then build with CMake.

## Build

Use CMake to build the `syncflow_peer` executable.

Example:

- `cmake -S . -B build`
- `cmake --build build -j2`

You can optionally set a device name at build time:

- `cmake -DDEVICE_NAME=my-device -S . -B build` — embed device name into the binary
- `cmake --build build -j2`

## Run

Run the executable on both devices on the same network.

### Basic usage

Linux:

- `./build/syncflow_peer` — start with hostname or config.json device_name
- `./build/syncflow_peer --device laptop-a` — override device name via CLI

Termux:

- `./build/syncflow_peer` — start with hostname or config.json device_name
- `./build/syncflow_peer --device android-1` — override device name via CLI

Start it on the first device, then start it on the second device. Each device will:

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

Use `--detach` to run the peer node as a background daemon:

```bash
# Start in background
./build/syncflow_peer --detach

# With custom device name
./build/syncflow_peer --device my-device --detach

# View logs
tail -f syncflow.log

# Kill the background process
pkill -f syncflow_peer
# or find the PID and kill it
ps aux | grep syncflow_peer
kill <PID>
```

When running with `--detach`:

- The parent process forks and exits immediately
- The child runs as a daemon (detached from terminal)
- Output is redirected to `syncflow.log` in the current directory
- The process continues running even after you close your terminal

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
