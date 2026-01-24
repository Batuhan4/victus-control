#!/bin/bash
# Helper script to set RGB zone color with root privileges

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <zone_number> <hex_color>"
    exit 1
fi

ZONE="$1"
COLOR="$2"

# Validate zone number (0-3)
if ! [[ "$ZONE" =~ ^[0-3]$ ]]; then
    echo "Error: Zone must be 0, 1, 2, or 3"
    exit 1
fi

# Validate hex color (6 hex digits)
if ! [[ "$COLOR" =~ ^[0-9A-Fa-f]{6}$ ]]; then
    echo "Error: Color must be 6 hex digits (e.g., FFFFFF)"
    exit 1
fi

# Convert to uppercase
COLOR=$(echo "$COLOR" | tr '[:lower:]' '[:upper:]')

# Write to zone file
ZONE_FILE="/sys/devices/platform/hp-wmi/rgb_zones/zone0${ZONE}"

if [ ! -f "$ZONE_FILE" ]; then
    echo "Error: Zone file $ZONE_FILE not found"
    exit 1
fi

echo "$COLOR" > "$ZONE_FILE"

if [ $? -eq 0 ]; then
    exit 0
else
    echo "Error: Failed to write to $ZONE_FILE"
    exit 1
fi
