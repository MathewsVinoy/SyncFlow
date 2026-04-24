# Build and Run

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

## Run

```bash
./build/app
```

Keep `config.json` in the project root when running.
