#!/bin/bash
# run-syncflow.sh - Run Syncflow with appropriate privilege escalation

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BINARY="$PROJECT_DIR/build/syncflow"

# Function to print help
show_help() {
    cat << 'EOF'
Syncflow Termux Runner

Usage: ./run-syncflow.sh [OPTIONS] [COMMAND] [ARGS]

Options:
    -h, --help          Show this help message
    -r, --require-root  Require root access (fail if not available)
    -b, --build         Build before running (if binary missing)

Commands are passed directly to syncflow. Examples:
    ./run-syncflow.sh list-devices
    ./run-syncflow.sh send /path/to/file device-id
    ./run-syncflow.sh add-folder /path/to/folder

For more information, run:
    ./run-syncflow.sh --help

EOF
}

# Parse options
REQUIRE_ROOT=0
BUILD_IF_MISSING=0

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -r|--require-root)
            REQUIRE_ROOT=1
            shift
            ;;
        -b|--build)
            BUILD_IF_MISSING=1
            shift
            ;;
        *)
            break
            ;;
    esac
done

# Check if binary exists
if [ ! -f "$BINARY" ]; then
    echo "[!] Syncflow binary not found at: $BINARY"
    
    if [ $BUILD_IF_MISSING -eq 1 ]; then
        echo "[*] Building now..."
        bash "$PROJECT_DIR/scripts/build-termux.sh" "$PROJECT_DIR"
    else
        echo "[*] Build it with: bash $PROJECT_DIR/scripts/build-termux.sh"
        exit 1
    fi
fi

# Determine privilege escalation method
if command -v sudo &> /dev/null; then
    ESCALATE_CMD="sudo"
    echo "[*] Using sudo for privilege escalation"
elif command -v su &> /dev/null; then
    # Test if su works without password (Termux with Termux:Boot)
    if su -c "true" 2>/dev/null; then
        ESCALATE_CMD="su"
        echo "[*] Using su for privilege escalation"
    else
        ESCALATE_CMD=""
        if [ $REQUIRE_ROOT -eq 1 ]; then
            echo "[!] Root access required but not available"
            echo "[*] Install sudo or enable su with Termux:Boot"
            exit 1
        fi
        echo "[!] Warning: No root access. Some features may not work."
        echo "[*] Install termux-boot or sudo for full functionality"
    fi
else
    ESCALATE_CMD=""
    if [ $REQUIRE_ROOT -eq 1 ]; then
        echo "[!] Root access required but su/sudo not found"
        exit 1
    fi
fi

# Execute syncflow
if [ -n "$ESCALATE_CMD" ]; then
    if [ "$ESCALATE_CMD" = "sudo" ]; then
        exec sudo "$BINARY" "$@"
    else
        exec su -c "$BINARY $*"
    fi
else
    exec "$BINARY" "$@"
fi
