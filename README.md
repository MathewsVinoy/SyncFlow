# syncflow

Cross-platform UDP device discovery for a mini AirDrop / Nearby Share style project.

Transport model:

- UDP: device discovery broadcast
- TCP: device-to-device handshake
- TCP (chunk-based): reliable file transfer engine
- UDP (chunk-based + ACK/retransmit): fast LAN file transfer mode

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

Directory sync commands:

```bash
./build/syncflow sync-recv --tcp 37030 ./synced_recv
./build/syncflow sync-dir --tcp <device-ip> 37030 ./project_dir 2000

./build/syncflow sync-recv --udp 37030 ./synced_recv
./build/syncflow sync-dir --udp <device-ip> 37030 ./project_dir 2000

# bidirectional auto-sync on each device (A points to B, B points to A)
./build/syncflow sync-auto --tcp <peer-ip> 37030 ./project_dir 2000
```

Optional status check:

```bash
./build/syncflow status
```

## File Transfer Engine (TCP + UDP)

Start receiver on Device B:

```bash
./build/syncflow recv-file 37030 ./received                 # default TCP
./build/syncflow_transfer recv --tcp 37030 ./received
./build/syncflow_transfer recv --udp 37030 ./received
```

Send file from Device A:

```bash
./build/syncflow send-file <device-ip> 37030 /path/to/file.bin   # default TCP
./build/syncflow_transfer send --tcp <device-ip> 37030 /path/to/file.bin
./build/syncflow_transfer send --udp <device-ip> 37030 /path/to/file.bin
```

Notes:

- TCP mode:
  - Parallel pipelined chunk preparation + sending
  - Delta sync by chunk fingerprint (only changed chunks are sent)
  - Adaptive per-chunk compression (RLE when beneficial)
  - Per-chunk CRC32 integrity check
  - Chunk size: 128 KiB (balanced CPU/memory throughput)
- UDP mode:
  - Chunk size: 1024 bytes
  - Sliding window transfer
  - Per-chunk CRC32 + ACK + retransmit timeout
  - Best for low-latency LAN; default mode is still TCP for stability

## Directory Sync Engine (Multi-file + Auto Sync)

The `syncflow_sync` engine provides continuous directory synchronization:

- `send` mode: recursively scans a source directory, detects changed files (size + modified time), packages only changed files, and sends them through `syncflow_transfer`.
- `recv` mode: continuously receives sync packages, unpacks them, and restores relative paths in the destination directory.
- `auto` mode: runs sender + receiver together for bidirectional sync.

### High-Speed Optimization Engine

Directory sync now uses a high-speed delta package path:

- Batches many file changes into a single transfer (lower process/network overhead)
- Syncs file updates and file deletions
- Prevents immediate echo loops in bidirectional mode
- Keeps backward compatibility with older single-file package format (`SFP1`)

Direct usage:

```bash
./build/syncflow_sync recv --tcp 37030 ./synced_recv
./build/syncflow_sync send --tcp <device-ip> 37030 ./project_dir 2000
```

Or via CLI wrapper:

```bash
./build/syncflow sync-recv --tcp 37030 ./synced_recv
./build/syncflow sync-dir --tcp <device-ip> 37030 ./project_dir 2000
./build/syncflow sync-auto --tcp <peer-ip> 37030 ./project_dir 2000
```

`interval_ms` controls polling frequency for change detection (default: `2000`).

`auto` mode runs sender + receiver together and suppresses immediate echo loops for freshly received files.

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
