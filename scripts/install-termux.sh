#!/bin/bash
# install-termux.sh - Quick installer for Termux environment

set -e

INSTALL_DIR="${1:-.}"
REPO_NAME="syncflow"

echo "=========================================="
echo "Syncflow Termux Installer"
echo "=========================================="
echo ""

# Check Termux
if [ ! -d "$PREFIX" ]; then
    echo "[!] Not running in Termux"
    exit 1
fi

echo "[*] Termux detected at: $PREFIX"
echo ""

# Step 1: Run setup
echo "[*] Running setup..."
bash "$INSTALL_DIR/scripts/setup-termux.sh"

echo ""
echo "[*] Setup complete!"
echo ""

# Step 2: Build
echo "[*] Building Syncflow..."
bash "$INSTALL_DIR/scripts/build-termux.sh" "$INSTALL_DIR"

echo ""
echo "=========================================="
echo "Installation Complete!"
echo "=========================================="
echo ""
echo "Quick start:"
echo "  syncflow list-devices          # Discover peers"
echo "  syncflow send <file> <device>  # Send a file"
echo "  syncflow add-folder <path>     # Add sync folder"
echo ""
echo "With root access:"
echo "  su -c 'syncflow list-devices'"
echo ""
echo "For more help:"
echo "  syncflow --help"
echo "  cat $INSTALL_DIR/docs/TERMUX_BUILD.md"
echo ""
