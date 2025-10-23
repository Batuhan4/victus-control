#!/bin/bash

# This script sets the fan mode via the hp-wmi hwmon interface.
# It must be executed with root privileges (victus-backend uses sudo).

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <AUTO|MANUAL|MAX>" >&2
    exit 1
fi

mode="${1^^}"
case "$mode" in
    AUTO) value="2" ;;
    MANUAL) value="1" ;;
    MAX) value="0" ;;
    BETTER_AUTO)
        # Better auto uses manual mode for the underlying control loop.
        value="1"
        ;;
    *)
        echo "Error: Unsupported fan mode '$1'." >&2
        exit 2
        ;;
esac

HWMON_BASE="/sys/devices/platform/hp-wmi/hwmon"
HWMON_PATH=$(find "$HWMON_BASE" -mindepth 1 -maxdepth 1 -type d -name "hwmon*" | head -n 1 || true)

if [[ -z "${HWMON_PATH}" ]]; then
    echo "Error: Hwmon directory not found under $HWMON_BASE." >&2
    exit 3
fi

CONTROL_FILE="${HWMON_PATH}/pwm1_enable"
if [[ ! -w "${CONTROL_FILE}" ]]; then
    # Attempt to adjust permissions for diagnostics, but continue even if it fails.
    chmod 664 "${CONTROL_FILE}" 2>/dev/null || true
fi

echo "${value}" > "${CONTROL_FILE}"
