# syncflow

Cross-platform UDP device discovery for a mini AirDrop / Nearby Share style project.

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

## CMake run targets

```bash
cmake --build build --target run_cli_start
cmake --build build --target run_cli_list_devices
cmake --build build --target run_cli_stop
```
