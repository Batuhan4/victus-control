#!/bin/bash

# Sets all four RGB zones in one invocation.
#
# The animation engine repaints every zone on each frame; going through
# set-rgb-zone.sh would mean four sudo executions (and four journal entries)
# per frame, so the whole frame is pushed here instead.
#
# It is intended to be called by the victus-control backend service with sudo.

set -euo pipefail

if [ "$#" -ne 4 ]; then
    echo "Usage: $0 <zone0_hex> <zone1_hex> <zone2_hex> <zone3_hex>" >&2
    exit 1
fi

ZONE_DIR="/sys/devices/platform/hp-wmi/rgb_zones"
COLORS=("$@")

# Validate every zone before writing any of them, so a bad colour in the last
# argument cannot leave the keyboard showing a half-applied frame.
for zone in 0 1 2 3; do
    color="${COLORS[zone]}"

    if ! [[ "$color" =~ ^[0-9A-Fa-f]{6}$ ]]; then
        echo "Error: Colour for zone ${zone} must be 6 hex digits" >&2
        exit 1
    fi

    if [ ! -f "${ZONE_DIR}/zone0${zone}" ]; then
        echo "Error: Zone file ${ZONE_DIR}/zone0${zone} not found" >&2
        exit 1
    fi
done

for zone in 0 1 2 3; do
    color="${COLORS[zone]}"
    printf '%s\n' "${color^^}" > "${ZONE_DIR}/zone0${zone}"
done
