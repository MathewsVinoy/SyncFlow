# SyncFlow

Cross-platform C++ file sync foundation with:

- app lifecycle, config, logging
- LAN discovery via UDP broadcast + unique device IDs
- stable TCP handshake/heartbeat/retry channel
- file monitoring and mirror sync
- hashed metadata, sync planning, conflict policy hooks
- chunked/resumable transfer primitives
- authentication/token signing primitives
- basic encrypted payload primitive
- versioned local backup snapshots before overwrite/delete
- unit/integration-style tests for protocol/auth/planner/transfer

## Installation (Termux only)

### 1) Install build tools in Termux

```bash
pkg update && pkg upgrade -y
pkg install -y git cmake clang make ninja openssl
```

git clone https://github.com/yourusername/syncflow.git
cd syncflow

### 2) Clone project and third-party libraries

```bash
git clone https://github.com/yourusername/syncflow.git
cd syncflow
# Clone third-party libraries (example: fmt and spdlog)
git clone https://github.com/fmtlib/fmt.git third_party/fmt
git clone https://github.com/gabime/spdlog.git third_party/spdlog
```

### 3) Build (Release)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 4) Run

```bash
./build/bin/syncflow
```

### 5) Optional: install binary into Termux prefix

```bash
cmake --install build --prefix "$PREFIX"
syncflow
```

## Run (Foreground)

```bash
./build/bin/syncflow
```

## Configuration location

On Termux, SyncFlow uses:

- `~/.config/syncflow/config.json`

Tip: easiest first setup is:

```bash
syncflow set-sync-path /path/to/your/folder
```

This creates/updates `sync_folder` and `mirror_folder` in config.

## CLI Commands

```bash
syncflow start
syncflow stop
syncflow set-sync-path /absolute/or/relative/path
syncflow run
```

- `start`: starts daemon mode (Linux/macOS) and writes a PID file in the OS temp/runtime location.
- `stop`: stops the daemon via SIGTERM using the PID file.
- `set-sync-path`: updates `sync_folder` in `config.json` and creates the folder.
- `run`: explicit foreground mode (same behavior as running without arguments).

## Important Production Notes

- Replace the default `security_shared_secret` in `config.json`.
- The current crypto/auth layer is lightweight and should be upgraded to a hardened cryptographic backend (e.g., TLS + modern AEAD) before Internet-facing use.
- For large deployments, add persistent transfer/session state storage and richer integration tests (network fault injection, long-running churn tests, and cross-platform CI matrix).
