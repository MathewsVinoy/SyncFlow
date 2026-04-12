#!/bin/bash
# setup-termux.sh - Quick setup script for Termux environment

set -e

echo "=========================================="
echo "Syncflow Termux Setup Script"
echo "=========================================="
echo ""

# Check if running in Termux
if [ ! -d "$PREFIX" ]; then
    echo "[!] This script is designed for Termux"
    echo "[*] PREFIX environment variable not found"
    exit 1
fi

echo "[*] Detected Termux environment: $PREFIX"
echo ""

# Step 1: Update packages
echo "[*] Updating Termux packages..."
pkg update -y
pkg upgrade -y

# Step 2: Install required packages
echo ""
echo "[*] Installing build tools..."
REQUIRED_PACKAGES="build-essential cmake clang binutils git"

for pkg in $REQUIRED_PACKAGES; do
    if pkg list-installed 2>/dev/null | grep -q "^$pkg/"; then
        echo "  ✓ $pkg already installed"
    else
        echo "  - Installing $pkg..."
        pkg install -y "$pkg"
    fi
done

# Step 3: Optional - Install root access tools
echo ""
echo "[*] Optional: Setting up root access..."
read -p "  Do you want to install root access tools? (y/n) " -n 1 -r
echo ""

if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "  Installing sudo..."
    pkg install -y sudo
    
    echo "  Installing termux-boot..."
    pkg install -y termux-boot
    
    echo ""
    echo "  [*] Root setup complete"
    echo "  [*] Enable Termux:Boot in System Settings > Accessibility"
    echo "  [*] Then start a root session with: su"
else
    echo "  [*] Skipped root access setup"
fi

# Step 4: Create convenience symlinks
echo ""
echo "[*] Creating convenience symlinks..."

mkdir -p "$HOME/bin"

# Create wrapper script
cat > "$HOME/bin/syncflow" << 'EOF'
#!/bin/bash
if [ -f "$HOME/syncflow/build/syncflow" ]; then
    exec "$HOME/syncflow/build/syncflow" "$@"
else
    echo "[!] Syncflow not built. Run: bash ~/syncflow/scripts/build-termux.sh ~/syncflow"
    exit 1
fi
EOF

chmod +x "$HOME/bin/syncflow"
echo "  ✓ Created ~/bin/syncflow wrapper"

# Ensure ~/bin is in PATH
if ! echo "$PATH" | grep -q "$HOME/bin"; then
    echo ""
    echo "[!] $HOME/bin not in PATH"
    echo "[*] Add this to ~/.bashrc or ~/.profile:"
    echo "  export PATH=\"\$HOME/bin:\$PATH\""
fi

# Step 5: Verify installation
echo ""
echo "=========================================="
echo "Verification"
echo "=========================================="

for tool in cmake gcc g++ clang; do
    if command -v $tool &> /dev/null; then
        VERSION=$($tool --version | head -n 1)
        echo "  ✓ $tool: $VERSION"
    fi
done

echo ""
echo "=========================================="
echo "Setup Complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "  1. Clone or copy Syncflow repository to ~/syncflow"
echo "  2. Build: bash ~/syncflow/scripts/build-termux.sh ~/syncflow"
echo "  3. Run: ~/bin/syncflow list-devices"
echo ""
echo "For full documentation, see:"
echo "  ~/syncflow/docs/TERMUX_BUILD.md"
echo ""
