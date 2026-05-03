# Syncflow peer demo

This workspace now includes a modular two-device LAN demo.

Each device runs both:

- a UDP discovery listener and broadcaster
- a TCP server for inbound connections
- a TCP client that connects to discovered peers

The log shows the device name and IP for every discovery and connection event.

## Android and Termux

The networking code uses POSIX sockets and standard C++, so it works on Linux and on Android through Termux.

In Termux, install the build tools first, then build with CMake.

## Build

Use CMake to build the `syncflow_peer` executable.

## Run

Start the program on both devices on the same network.

Optional device name:

- `./syncflow_peer laptop-a`
- `./syncflow_peer tablet-b`

If no name is provided, the hostname is used.

## Ports

- UDP discovery: 45454
- TCP connection: 45455
