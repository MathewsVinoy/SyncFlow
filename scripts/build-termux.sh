#!/bin/bash
# build-termux.sh - Build Syncflow for Termux with root-compatible configuration

set -e

REPO_DIR="${1:-.}"
BUILD_DIR="$REPO_DIR/build"
INSTALL_PREFIX="${2:-$PREFIX}"

echo "=========================================="
echo "Syncflow Termux Build Script"
echo "=========================================="
echo "Repository: $REPO_DIR"
echo "Build directory: $BUILD_DIR"
echo "Install prefix: $INSTALL_PREFIX"
echo ""

# Check if repo exists
if [ ! -d "$REPO_DIR" ]; then
    echo "[!] Repository directory not found: $REPO_DIR"
    exit 1
fi

# Check for required tools
for tool in cmake gcc g++ make; do
    if ! command -v $tool &> /dev/null; then
        echo "[!] Required tool not found: $tool"
        echo "[*] Run: pkg install build-essential cmake"
        exit 1
    fi
done

echo "[*] All required tools found"
echo ""

# Create build directory
echo "[*] Creating build directory..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure CMake
echo "[*] Configuring CMake for Termux..."
cmake "$REPO_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=17 \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_CXX_FLAGS="-O2 -march=native -ftree-vectorize"

echo "[*] CMake configuration complete"
echo ""

# Build
echo "[*] Building Syncflow (using $(nproc) cores)..."
make -j$(nproc)

echo "[*] Build successful!"
echo ""
echo "=========================================="
echo "Build Summary"
echo "=========================================="
echo "Binary location: $BUILD_DIR/syncflow"
echo "Libraries:"
ls -lh "$BUILD_DIR/CMakeFiles" 2>/dev/null | grep -E '\.(a|so)' || echo "  (built as part of executable)"
echo ""
echo "To verify:"
echo "  $BUILD_DIR/syncflow --help"
echo ""
echo "To install:"
echo "  cd $BUILD_DIR && make install"
echo ""
echo "To run with root:"
echo "  su -c '$BUILD_DIR/syncflow list-devices'"
echo ""
