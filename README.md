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

## Run

Run the executable on both devices on the same network.

Linux:

- `./build/syncflow_peer`
- or `./build/syncflow_peer laptop-a`

Termux:

- `./build/syncflow_peer`
- or `./build/syncflow_peer android-1`

Start it on the first device, then start it on the second device. Each device will:

- broadcast itself over UDP
- discover the other device
- open a TCP connection
- print connection logs with device name and IP
- if enabled in config.json, send the configured file after the connection is established

## File sync config

Edit [config.json](config.json) to choose the file to send:

- `enabled`: turn file sync on or off
- `source_path`: file or folder to send over the network
- `receive_dir`: folder where incoming files are saved

If `source_path` points to a folder, the whole directory tree is synced.

The default config uses [sample_sync_dir](sample_sync_dir) as a demo folder tree.

If no name is provided, the hostname is used.

## Ports

- UDP discovery: 45454
- TCP connection: 45455
