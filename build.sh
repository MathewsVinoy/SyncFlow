#!/bin/bash
# Platform detection and build script for Syncflow
# Supports Linux, macOS, and Windows (MinGW)

set -e

echo "=== Syncflow Cross-Platform Build Script ==="

# Detect OS
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "Detected: Linux"
    PLATFORM="Linux"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Detected: macOS"
    PLATFORM="macOS"
elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]]; then
    echo "Detected: Windows (MinGW/Cygwin)"
    PLATFORM="Windows"
else
    echo "Unknown platform: $OSTYPE"
    exit 1
fi

# Create build directory
BUILD_DIR="build_${PLATFORM,,}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "Building for $PLATFORM in $BUILD_DIR..."

# Configure
cmake -S .. -B . \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON

# Build
cmake --build . -j "$(nproc || sysctl -n hw.ncpu || echo 4)"

echo "Build completed successfully!"

# Show binaries
if [[ "$PLATFORM" == "Windows" ]]; then
    echo "Executables:"
    find . -name "*.exe" -type f
else
    echo "Executables:"
    find bin -type f -executable 2>/dev/null || find . -name "app" -o -name "syncflow_tests"
fi

echo ""
echo "To run tests:"
if [[ "$PLATFORM" == "Windows" ]]; then
    echo "  $BUILD_DIR\\bin\\syncflow_tests"
else
    echo "  ./$BUILD_DIR/bin/syncflow_tests"
fi

echo ""
echo "To create package:"
if [[ "$PLATFORM" == "Windows" ]]; then
    echo "  cd $BUILD_DIR && cpack -G NSIS"
    echo "  cd $BUILD_DIR && cpack -G ZIP"
elif [[ "$PLATFORM" == "macOS" ]]; then
    echo "  cd $BUILD_DIR && cpack -G DragNDrop"
    echo "  cd $BUILD_DIR && cpack -G TGZ"
else
    echo "  cd $BUILD_DIR && cpack -G DEB"
    echo "  cd $BUILD_DIR && cpack -G RPM"
    echo "  cd $BUILD_DIR && cpack -G TGZ"
fi
