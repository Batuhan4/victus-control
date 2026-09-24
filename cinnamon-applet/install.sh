#!/bin/bash
# Victus Control Cinnamon Applet Installer
# Installs the applet into the user's local Cinnamon applets directory and adds
# it to the panel.

set -euo pipefail

APPLET_UUID="victus-control@hjlabs.in"
APPLET_DIR="$HOME/.local/share/cinnamon/applets/$APPLET_UUID"
SOURCE_DIR="$(cd "$(dirname "$0")" && pwd)/$APPLET_UUID"

echo "🌀 Installing Victus Control Cinnamon applet..."

if [ ! -d "$SOURCE_DIR" ]; then
    echo "Error: applet source not found at $SOURCE_DIR" >&2
    exit 1
fi

mkdir -p "$HOME/.local/share/cinnamon/applets"
rm -rf "$APPLET_DIR"
mkdir -p "$APPLET_DIR"

install -m 0644 "$SOURCE_DIR/applet.js"             "$APPLET_DIR/applet.js"
install -m 0644 "$SOURCE_DIR/metadata.json"         "$APPLET_DIR/metadata.json"
install -m 0644 "$SOURCE_DIR/settings-schema.json"  "$APPLET_DIR/settings-schema.json"

echo "✅ Applet installed at: $APPLET_DIR"

# Add it to the panel unless it is already there. Cinnamon keeps the panel
# layout in a gsettings list, so this appends one entry with a fresh instance id.
if command -v gsettings >/dev/null 2>&1 && [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    python3 - "$APPLET_UUID" <<'PY' || echo "Note: could not add the applet to the panel automatically."
import ast, subprocess, sys

uuid = sys.argv[1]
current = subprocess.check_output(
    ["gsettings", "get", "org.cinnamon", "enabled-applets"], text=True).strip()
applets = ast.literal_eval(current)

if any(uuid in entry for entry in applets):
    print(f"--> {uuid} is already on the panel.")
else:
    ids = [int(e.rsplit(":", 1)[-1]) for e in applets if e.rsplit(":", 1)[-1].isdigit()]
    applets.append(f"panel1:right:3:{uuid}:{max(ids) + 1 if ids else 1}")
    subprocess.run(["gsettings", "set", "org.cinnamon", "enabled-applets", str(applets)],
                   check=True)
    print(f"--> Added {uuid} to the panel.")
PY
else
    echo ""
    echo "Add it to your panel with:"
    echo "  right-click the panel -> Applets -> Victus Control -> +"
fi

echo ""
echo "If it does not appear, restart Cinnamon: Alt+F2, type 'r', press Enter."
