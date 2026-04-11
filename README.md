# SyncFlow

SyncFlow is a Rust peer-to-peer LAN file synchronization tool.

It syncs one folder between devices without a central server.

## Architecture

### Modules

- `file_watcher`
  - Watches filesystem events (`create`, `modify`, `delete`) using `notify`.
  - Converts local events into sync messages.

- `sync_engine`
  - Core reconciliation logic.
  - Scans local manifest, compares with remote manifest, applies conflict policy.
  - Applies incoming chunks and finalizes files with integrity verification.

- `network`
  - Message protocol (`serde` + `bincode`).
  - Length-prefixed framed TCP message read/write.
  - Basic device authentication via shared token in handshake.

- `transfer`
  - Chunked file sender.
  - Supports resume from byte offset.
  - Final chunk includes SHA-256 for integrity verification.

- `discovery`
  - UDP broadcast peer discovery on LAN.
  - Finds peers automatically and triggers TCP connection.

- `utils`
  - Safe path normalization/join helpers.
  - SHA-256 hashing helpers.
  - Time conversion helpers.

## Conflict strategy

Current policy: **last-write-wins** using `(modified_ts, sha256)`.

- If timestamps differ, newer timestamp wins.
- If equal timestamp and different content, lexicographically larger hash wins (deterministic tie-break).

## MVP and iterative improvements

This implementation includes the requested steps:

1. MVP: TCP sync between peers for one folder.
2. File watching: real-time local change propagation.
3. Chunked transfer + resume + SHA-256 verification.
4. Peer discovery: UDP broadcast LAN discovery.

## Build

Requirements:

- Rust stable (1.75+ recommended)

Build:

```bash
cargo build --release
```

## Run

### Terminal A (Device 1)

```bash
cargo run -- \
	--dir ./sync_a \
	--listen 0.0.0.0:7878 \
	--device-name laptop \
	--token my-shared-token
```

### Terminal B (Device 2, manual peer)

```bash
cargo run -- \
	--dir ./sync_b \
	--listen 0.0.0.0:7879 \
	--peer 192.168.1.10:7878 \
	--device-name desktop \
	--token my-shared-token
```

You can omit `--peer` and rely on discovery (enabled by default).

## CLI options

- `--dir <PATH>`: folder to sync
- `--listen <IP:PORT>`: TCP listen address (default `0.0.0.0:7878`)
- `--peer <IP:PORT>`: manually connect to peer (repeatable)
- `--device-name <NAME>`: local device label
- `--token <STRING>`: shared auth token
- `--watch <true|false>`: enable file watching (default `true`)
- `--discover <true|false>`: enable discovery (default `true`)
- `--discovery-port <PORT>`: UDP discovery port (default `9999`)

## Security notes

- Current implementation uses **token-based peer authentication**.
- For production hardening, next step is adding TLS (e.g. `rustls`) and cert pinning.

## Limitations (current)

- Delete reconciliation from historical state is event-driven (watch/delete notice), not a persisted tombstone DB.
- No compression yet.
- No bandwidth throttling/QoS yet.

## License

Apache License 2.0
