# Decentralized P2P Sync Architecture

This document describes the target architecture for SyncFlow as a fully decentralized peer-to-peer file and directory synchronization system.

## Goals

- No central sync server for data transfer.
- Devices discover each other through LAN discovery, optional bootstrap discovery servers, or manually configured peers.
- Each device maintains a local index of files, folders, metadata, and block hashes.
- Sync is incremental, block-based, resumable, and parallel.
- Directory structure and metadata are preserved during replication.
- File changes are detected near real time using OS file watchers.
- Renames, deletions, and conflicts propagate across devices.
- Connectivity works across NAT via hole punching, relay fallback, or manual endpoints.
- All peer-to-peer control traffic is authenticated and encrypted.

## Relationship to the Current Codebase

The current repository already has building blocks for this target:

- `include/networking/DeviceDiscovery.h` and `src/networking/DeviceDiscovery.cpp` for LAN discovery.
- `include/networking/TcpHandshake.h` for TLS-backed peer connection state.
- `include/security/AuthManager.h` and `src/security/AuthManager.cpp` for device-token authentication primitives.
- `include/sync_engine/FileTransfer.h` and `src/sync_engine/FileTransfer.cpp` for chunked/resumable file primitives.
- `include/sync_engine/SyncPlanner.h` and `src/sync_engine/SyncPlanner.cpp` for metadata-based conflict planning.
- `include/sync_engine/ResumableTransferManager.h` and `src/sync_engine/ResumableTransferManager.cpp` for transfer state.
- `src/core/Application.cpp` for high-level orchestration.

This document defines the next-stage architecture that extends those pieces into a full decentralized sync system.

## High-Level System Model

Every device runs the same software and acts as:

1. a scanner of local shared folders,
2. a peer discoverer,
3. a TLS-authenticated sync endpoint,
4. a downloader/uploader of file blocks, and
5. a persistent local metadata/index owner.

No peer is special for storage. Bootstrap servers, if used, only help peers find one another or relay connectivity; they do not own the synchronized data.

## Core Data Model

### File and folder identity

Each synchronized path is represented by:

- canonical relative path,
- file type or directory type,
- size,
- timestamps,
- permissions/mode,
- owner/group where available,
- optional extended attributes,
- version/device origin metadata,
- block list and block hashes for files,
- deletion tombstone state.

### File blocks

Files are split into fixed-size blocks.
Recommended default block size:

- 128 KiB for general use,
- optionally adjustable to 256 KiB for high-throughput WAN links,
- smaller adaptive blocks may be used for tiny files.

Each block stores:

- block index,
- block offset,
- block length,
- block hash,
- compression flag,
- optional content signature.

Directory contents are not transferred as one monolithic object. Instead, directory structure is created first, then file blocks are fetched or pushed as needed.

## Local Index Database

A local embedded database is required to support fast delta computation and resumable transfers.
SQLite is a good fit.

### Proposed tables

#### `devices`

- `device_id` TEXT PRIMARY KEY
- `device_name` TEXT
- `first_seen_unix_seconds` INTEGER
- `last_seen_unix_seconds` INTEGER
- `trust_state` TEXT
- `tls_fingerprint` TEXT

#### `folders`

- `folder_id` TEXT PRIMARY KEY
- `local_path` TEXT
- `folder_label` TEXT
- `watch_state` TEXT
- `last_scan_unix_seconds` INTEGER

#### `entries`

One row per file or directory.

- `entry_id` INTEGER PRIMARY KEY
- `folder_id` TEXT
- `relative_path` TEXT
- `entry_type` TEXT // file, directory, symlink, tombstone
- `size_bytes` INTEGER
- `modified_unix_seconds` INTEGER
- `permissions` INTEGER
- `owner_id` INTEGER
- `group_id` INTEGER
- `content_hash` TEXT
- `content_version` INTEGER
- `last_editor_device_id` TEXT
- `deleted` INTEGER
- `rename_from_path` TEXT
- `mtime_source` TEXT

#### `blocks`

- `block_id` INTEGER PRIMARY KEY
- `entry_id` INTEGER
- `block_index` INTEGER
- `block_offset` INTEGER
- `block_size` INTEGER
- `block_hash` TEXT
- `compressed_size` INTEGER
- `compression_type` TEXT
- `present_locally` INTEGER

#### `transfer_sessions`

- `session_id` TEXT PRIMARY KEY
- `peer_device_id` TEXT
- `folder_id` TEXT
- `entry_id` INTEGER
- `state` TEXT
- `resume_offset` INTEGER
- `expected_hash` TEXT
- `updated_unix_seconds` INTEGER

#### `block_availability`

Optional cache of remote block ownership.

- `peer_device_id` TEXT
- `content_hash` TEXT
- `block_index` INTEGER
- `available` INTEGER
- `last_queried_unix_seconds` INTEGER

### Indexing requirements

Indexes should exist on:

- `entries(folder_id, relative_path)`
- `entries(content_hash)`
- `blocks(entry_id, block_index)`
- `transfer_sessions(peer_device_id, state)`
- `devices(last_seen_unix_seconds)`

## Discovery Model

Discovery is multi-path:

1. **LAN broadcast/multicast discovery** for local networks.
2. **Optional bootstrap/discovery servers** to exchange reachable peer endpoints.
3. **Manual peer entry** when the user specifies an address, port, or QR code.
4. **Relay-assisted discovery** if NAT traversal fails.

Discovery payloads should include:

- device ID,
- device name,
- listen port,
- advertised capabilities,
- protocol version,
- optional public endpoint summary,
- optional folder labels or sync intents.

Discovery traffic itself must not be trusted. It only suggests candidates for a secure connection.

## Identity and Authentication

Each device owns a unique cryptographic identity.

### Device identity

- A stable device ID is generated from a private key or certificate fingerprint.
- The private key never leaves the device.
- The public key or certificate fingerprint is shared during handshake.

### Peer trust

A peer is trusted if one of the following is true:

- it is pre-registered by fingerprint,
- it is signed by a trusted bootstrap authority,
- it is manually approved by the user,
- it matches an existing persisted trust record.

### TLS session establishment

All peer connections use TLS.
Requirements:

- mutual authentication if both sides have registered identities,
- certificate or public-key pinning,
- protocol version negotiation,
- replay-resistant handshake,
- application-level device ID verification after TLS setup.

The `TcpHandshake` layer is the natural place to enforce:

- trusted peer validation,
- device ID matching,
- connection role selection to prevent duplicate peer links,
- heartbeat/ping and reconnect logic.

## Connection Establishment Flow

1. Discover candidate peer endpoint.
2. Open TCP connection.
3. Perform TLS handshake.
4. Validate peer certificate/fingerprint.
5. Exchange device IDs and capabilities.
6. Negotiate protocol version and feature flags.
7. Exchange folder indexes.
8. Compute delta plan.
9. Transfer only required blocks and metadata.

## Protocol Layout

A versioned framing protocol should sit on top of TLS.

### Control messages

- `HELLO`
- `HELLO_ACK`
- `INDEX_SUMMARY`
- `INDEX_REQUEST`
- `INDEX_RESPONSE`
- `BLOCK_REQUEST`
- `BLOCK_RESPONSE`
- `DIRECTORY_CREATE`
- `FILE_COMMIT`
- `DELETE`
- `RENAME`
- `CONFLICT`
- `PING`
- `PONG`
- `ERROR`

### Capability flags

- block sync supported,
- resumable transfer supported,
- compression supported,
- checksum algorithm supported,
- watcher events supported,
- relay fallback supported.

### Delta exchange

The important optimization is to exchange summaries, not full file contents.
Each side sends:

- path,
- size,
- modification timestamp,
- directory/file type,
- content hash,
- block-hash summary.

Then each peer computes:

- which files are missing,
- which blocks are missing within files,
- which directories need structure replication,
- whether the local copy is stale, newer, deleted, or conflicted.

## Directory Synchronization Rules

Directory sync happens in two phases.

### Phase 1: structure replication

Before content transfer:

- create root directory if missing,
- create subdirectories recursively,
- apply permissions/timestamps where supported,
- record tombstones for deletions.

### Phase 2: content replication

For files:

- fetch or push missing blocks,
- write blocks to a temporary file or sparse target,
- verify block hashes as they arrive,
- assemble in correct order,
- atomically rename into final location.

This preserves tree structure even for partial transfers.

## Parallelism Model

The sync engine should support parallel downloads.

### Parallel dimensions

- multiple files at once,
- multiple blocks per file,
- multiple peers contributing blocks,
- upload/download overlap,
- scanning and transfer in parallel.

### Scheduling policy

Prefer:

1. directory structure creation,
2. small files,
3. missing blocks for open transfers,
4. high-priority user-modified paths,
5. large files in background.

### Multi-source block selection

If multiple peers have the same block:

- prefer the fastest/most recent peer,
- prefer local LAN over relay,
- prefer peers with lower RTT and fewer retries,
- distribute block requests to avoid hotspot peers.

## File Watching and Reconciliation

Scanning alone is not enough for near-real-time sync.

### Watcher sources

- Linux: inotify,
- macOS: FSEvents or kqueue-based fallback,
- Windows: ReadDirectoryChangesW.

### Watcher strategy

- use OS watcher events as primary trigger,
- use periodic full scans as correctness fallback,
- debounce bursts of changes,
- coalesce rename and move events,
- ensure scan cannot miss a change after watcher overflow.

### Reconciling watcher events

Watcher events should update the local index and enqueue sync work:

- file created,
- file modified,
- file deleted,
- file renamed,
- directory created or removed.

## Deletions and Renames

### Deletions

A deletion is represented as a tombstone.
That tombstone propagates to peers so a deleted file is not resurrected by stale state.

### Renames

Renames should be treated as:

- old path tombstone or rename source,
- new path create with same content identity when possible.

Peers should preserve rename intent instead of converting every rename into delete plus recreate when metadata allows.

## Conflict Handling

Conflicts occur when two devices modify the same path independently before convergence.

### Conflict policy

Default strategy:

- keep both copies,
- preserve both versions,
- generate a conflict filename suffix,
- record the winning and losing device IDs in metadata.

### Example conflict naming

- `file.txt`
- `file.syncflow-conflict-<device>-<timestamp>.txt`

### Conflict detection rules

A conflict exists when:

- both sides changed since the common baseline,
- hashes differ,
- timestamps alone are not sufficient to declare a safe overwrite.

## Resumable Transfers

Transfers must survive interruption.

### State tracked per transfer

- file path,
- block offset,
- completed blocks,
- expected total size,
- expected full content hash,
- source peer(s),
- current temporary file location.

### Resume behavior

On reconnect:

- query transfer state,
- request only remaining blocks,
- verify previously received blocks remain valid,
- continue writing to the existing temp file.

The existing `ResumableTransferManager` and `FileTransfer` layers already map well to this design.

## Compression

Compression is optional and negotiated per transfer.

Recommended policy:

- compress text-heavy or repetitive blocks,
- skip compression for already compressed formats,
- include content type or heuristic detection,
- never sacrifice resumability or block verification.

Compression should happen per block, not only per whole file, so peers can resume and parallelize cleanly.

## NAT Traversal and Reachability

To work across different networks, peers need connectivity options in this order:

1. direct LAN connection,
2. direct WAN connection to discovered endpoint,
3. NAT hole punching,
4. UPnP/NAT-PMP port mapping,
5. relay fallback.

### Relay role

A relay should only forward encrypted peer traffic when direct connectivity fails.
It must not decrypt or reindex content.

### Manual override

Advanced users should be able to pin:

- listen port,
- advertised endpoint,
- relay preference,
- trusted fingerprints,
- folder IDs.

## Performance Requirements

The system should avoid full-file copies whenever possible.

### Key optimizations

- block-level hashing and delta computation,
- local index persistence,
- incremental watcher-driven updates,
- resumable temp-file writes,
- parallel requests to multiple peers,
- optional compression,
- metadata-only sync for directories when content is already present.

### Efficient scheduling

A scheduler should consider:

- file size,
- user activity,
- network RTT,
- peer availability,
- queue backlog,
- remaining block count,
- recent failures.

## Security Considerations

- Never trust discovery packets.
- Never accept unauthenticated metadata as authoritative.
- Verify both TLS identity and application-level device ID.
- Pin or persist trusted keys.
- Reject malformed protocol frames.
- Validate block and full-file hashes before commit.
- Treat relay endpoints as transport only.

## Suggested Implementation Phases

### Phase 1: local block index

- add SQLite index,
- scan folders into metadata and block records,
- compute file and block hashes,
- persist snapshot state.

### Phase 2: secure peer exchange

- extend TLS handshake,
- exchange folder summaries,
- compute delta plans,
- request missing blocks.

### Phase 3: directory sync

- replicate directories and metadata,
- preserve permissions/timestamps,
- implement tombstones and rename detection.

### Phase 4: multi-source scheduling

- parallel block downloads,
- source selection across peers,
- backpressure and retry logic.

### Phase 5: watcher integration

- add OS watchers,
- debounce and reconcile scans,
- improve latency for local edits.

### Phase 6: reachability hardening

- NAT traversal,
- relay fallback,
- better peer selection and health tracking.

## Success Criteria

The architecture is complete when:

- two devices can discover each other without a central data server,
- they authenticate securely,
- they exchange indexes and compute exact deltas,
- directories are recreated with metadata intact,
- files transfer by missing blocks only,
- interrupted transfers resume,
- renames and deletions propagate,
- conflicts are preserved rather than silently overwritten.

## Notes for Current Repository Evolution

The current repository already contains many of the required primitives. The next most valuable code changes would be:

1. add a persistent SQLite index layer,
2. extend metadata records to include block lists,
3. define a versioned wire protocol for index exchange and block requests,
4. connect `DeviceDiscovery` and `TcpHandshake` to that protocol,
5. integrate watcher events into `SyncEngine`,
6. make transfer scheduling multi-peer aware.
