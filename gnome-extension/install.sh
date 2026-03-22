#!/bin/bash
# Victus Control GNOME Shell Extension Installer
# Installs the extension into the user's local GNOME extensions directory.

set -euo pipefail

EXTENSION_UUID="victus-control@victus"
EXTENSION_DIR="$HOME/.local/share/gnome-shell/extensions/$EXTENSION_UUID"
SOURCE_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "🌀 Installing Victus Control GNOME Extension..."

# Create extensions directory if it doesn't exist
mkdir -p "$HOME/.local/share/gnome-shell/extensions"

# Remove existing installation
if [ -L "$EXTENSION_DIR" ] || [ -d "$EXTENSION_DIR" ]; then
    echo "Removing existing installation..."
    rm -rf "$EXTENSION_DIR"
fi

# Install the runtime files GNOME Shell expects
mkdir -p "$EXTENSION_DIR"
install -m 0644 "$SOURCE_DIR/extension.js" "$EXTENSION_DIR/extension.js"
install -m 0644 "$SOURCE_DIR/metadata.json" "$EXTENSION_DIR/metadata.json"
if [ -f "$SOURCE_DIR/stylesheet.css" ]; then
    install -m 0644 "$SOURCE_DIR/stylesheet.css" "$EXTENSION_DIR/stylesheet.css"
fi

echo "✅ Extension installed at: $EXTENSION_DIR"

echo ""
echo "To enable the extension:"
echo "  1. Log out and log back in, OR"
echo "  2. Press Alt+F2, type 'r' and press Enter (X11 only)"
echo ""
echo "Then run:"
echo "  gnome-extensions enable $EXTENSION_UUID"
echo ""
echo "Or use GNOME Extensions app to enable it."
