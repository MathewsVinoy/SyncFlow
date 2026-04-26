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

## Build and Run

## Build (Debug)

```bash
cmake -S . -B build
cmake --build build -j
```

## Build (Release)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Run (Foreground)

```bash
./build/app
```

Keep `config.json` in the project root when running.

## CLI Commands

```bash
./build/app start
./build/app stop
./build/app set-sync-path /absolute/or/relative/path
./build/app run
```

- `start`: starts the daemon and writes a PID file (default: `/tmp/syncflow.pid`).
- `stop`: stops the daemon via SIGTERM using the PID file.
- `set-sync-path`: updates `sync_folder` in `config.json` and creates the folder.
- `run`: explicit foreground mode (same behavior as running without arguments).

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Important Production Notes

- Replace the default `security_shared_secret` in `config.json`.
- The current crypto/auth layer is lightweight and should be upgraded to a hardened cryptographic backend (e.g., TLS + modern AEAD) before Internet-facing use.
- For large deployments, add persistent transfer/session state storage and richer integration tests (network fault injection, long-running churn tests, and cross-platform CI matrix).
