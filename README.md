# syncflow

Cross-platform UDP device discovery for a mini AirDrop / Nearby Share style project.

Transport model:

- UDP: device discovery broadcast
- TCP: device-to-device handshake
- TCP (chunk-based): reliable file transfer engine

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## CLI commands

Use the `syncflow` executable:

```bash
./build/syncflow start
./build/syncflow list-devices
./build/syncflow stop
```

Optional status check:

```bash
./build/syncflow status
```

## File Transfer Engine (Chunk-Based)

Start receiver on Device B:

```bash
./build/syncflow recv-file 37030 ./received
```

Send file from Device A:

```bash
./build/syncflow send-file <device-ip> 37030 /path/to/file.bin
```

Notes:

- Chunk size: 256 KiB
- Per-chunk CRC32 integrity check
- Per-chunk ACK for transfer progress and reliability

## Production configuration

You can configure runtime settings with environment variables:

- `SYNCFLOW_DISCOVERY_UDP_PORT` (default: `37020`)
- `SYNCFLOW_HANDSHAKE_TCP_PORT` (default: `37021`)
- `SYNCFLOW_DISCOVERY_TIMEOUT_MS` (default: `3000`)
- `SYNCFLOW_TCP_TIMEOUT_MS` (default: `2000`)
- `SYNCFLOW_AUTH_TOKEN` (default: `syncflow-dev-token`)

Example:

```bash
export SYNCFLOW_AUTH_TOKEN="replace-with-strong-shared-secret"
export SYNCFLOW_DISCOVERY_UDP_PORT=47020
export SYNCFLOW_HANDSHAKE_TCP_PORT=47021
./build/syncflow start
./build/syncflow list-devices
./build/syncflow stop
```

## CMake run targets

```bash
cmake --build build --target run_cli_start
cmake --build build --target run_cli_list_devices
cmake --build build --target run_cli_stop
cmake --build build --target run_transfer_recv
```
