#!/bin/bash
set -euo pipefail

module_name="hp-wmi-fan-and-backlight-control"
module_version="0.0.2"
log_prefix="[victus-healthcheck]"

hp_wmi_fan_interface_ready() {
    local hwmon_path

    hwmon_path="$(find /sys/devices/platform/hp-wmi/hwmon -mindepth 1 -maxdepth 1 -type d -name 'hwmon*' | head -n 1 || true)"
    [[ -n "${hwmon_path}" ]] || return 1
    [[ -e "${hwmon_path}/fan1_target" ]] || return 1
    [[ -e "${hwmon_path}/fan2_target" ]] || return 1

    return 0
}

warn_if_keyboard_interface_missing() {
    if [[ ! -e /sys/class/leds/hp::kbd_backlight && ! -e /sys/devices/platform/hp-wmi/rgb_zones/zone00 ]]; then
        echo "$log_prefix warning: keyboard lighting interface was not detected; fan control may still be available on this model" >&2
    fi
}

resolve_hp_wmi_module_path() {
    modprobe --show-depends hp_wmi 2>/dev/null \
        | awk '$1 == "insmod" && $2 ~ /\/hp-wmi\.ko($|\.(gz|xz|zst)$)/ { print $2 }' \
        | tail -n 1
}

hp_wmi_module_path_is_dkms() {
    local module_path="${1:-}"

    case "${module_path}" in
        */extra/hp-wmi.ko|*/extra/hp-wmi.ko.*|*/updates/hp-wmi.ko|*/updates/hp-wmi.ko.*|*/updates/dkms/hp-wmi.ko|*/updates/dkms/hp-wmi.ko.*)
            return 0
            ;;
    esac

    return 1
}

if ! command -v dkms >/dev/null 2>&1; then
    echo "$log_prefix dkms command not found; skipping kernel module verification" >&2
    exit 0
fi

current_kernel="$(uname -r)"

status_output="$(dkms status -m "${module_name}" -v "${module_version}" || true)"
if [[ ! "$status_output" =~ ${current_kernel}.*installed ]]; then
    echo "$log_prefix module not built for ${current_kernel}; attempting dkms autoinstall" >&2
    if ! dkms autoinstall -m "${module_name}" -v "${module_version}" -k "${current_kernel}" >/dev/null 2>&1; then
        echo "$log_prefix dkms autoinstall failed; retrying targeted install" >&2
        dkms install "${module_name}/${module_version}" -k "${current_kernel}" >/dev/null 2>&1 || true
    fi
fi

module_path="$(resolve_hp_wmi_module_path || true)"
if ! hp_wmi_module_path_is_dkms "${module_path}"; then
    if [[ -n "${module_path}" ]]; then
        echo "$log_prefix warning: modprobe hp_wmi resolved to an unexpected module path: ${module_path}" >&2
    else
        echo "$log_prefix warning: unable to determine which hp_wmi module modprobe would load" >&2
    fi
fi

if ! lsmod | grep -q '^hp_wmi' || ! hp_wmi_fan_interface_ready; then
    echo "$log_prefix reloading hp_wmi kernel module" >&2
    modprobe led_class_multicolor >/dev/null 2>&1 || true
    if lsmod | grep -q '^hp_wmi'; then
        modprobe -r hp_wmi >/dev/null 2>&1 || rmmod hp_wmi >/dev/null 2>&1 || true
    fi
    modprobe hp_wmi || echo "$log_prefix warning: failed to load hp_wmi module" >&2
fi

if ! hp_wmi_fan_interface_ready; then
    echo "$log_prefix warning: patched hp_wmi interface is still incomplete after reload" >&2
else
    warn_if_keyboard_interface_missing
fi

exit 0
