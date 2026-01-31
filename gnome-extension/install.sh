#!/bin/bash
# Victus Control GNOME Shell Extension Installer
# This script installs the extension by creating a symlink

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

# Create symlink
ln -s "$SOURCE_DIR" "$EXTENSION_DIR"
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
