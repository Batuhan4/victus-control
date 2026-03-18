#!/bin/bash

set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
    echo "This script requires root privileges. Re-running with sudo..."
    exec sudo "$0" "$@"
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${script_dir}"

module_name="hp-wmi-fan-and-backlight-control"
module_version="0.0.2"

verify_hp_wmi_interface() {
    local hwmon_path

    hwmon_path="$(find /sys/devices/platform/hp-wmi/hwmon -mindepth 1 -maxdepth 1 -type d -name 'hwmon*' | head -n 1 || true)"
    if [[ -z "${hwmon_path}" ]]; then
        echo "Error: hp_wmi hwmon directory not found." >&2
        return 1
    fi

    if [[ ! -e "${hwmon_path}/fan1_target" || ! -e "${hwmon_path}/fan2_target" ]]; then
        echo "Error: Patched hp_wmi fan target controls are missing under ${hwmon_path}." >&2
        return 1
    fi

    if [[ ! -e /sys/class/leds/hp::kbd_backlight && ! -e /sys/devices/platform/hp-wmi/rgb_zones/zone00 ]]; then
        echo "Error: Patched hp_wmi keyboard lighting interface was not detected." >&2
        return 1
    fi

    return 0
}

reload_patched_hp_wmi() {
    local attempts=0

    echo "--> Reloading patched hp-wmi module..."
    modprobe led_class_multicolor >/dev/null 2>&1 || true

    if ! modprobe --show-depends hp_wmi | grep -q '/extra/hp-wmi\.ko'; then
        echo "Error: modprobe hp_wmi is not resolving to the DKMS-installed module in /extra." >&2
        return 1
    fi

    if lsmod | grep -q '^hp_wmi\b'; then
        modprobe -r hp_wmi || rmmod hp_wmi
    fi

    modprobe hp_wmi

    until verify_hp_wmi_interface; do
        attempts=$((attempts + 1))
        if (( attempts >= 3 )); then
            return 1
        fi
        sleep 1
    done

    return 0
}

install_fedora_dependencies() {
    local current_kernel
    local packages=(
        meson
        ninja-build
        gtk4-devel
        git
        dkms
        gcc-c++
        policycoreutils-python-utils
        sudo
    )

    dnf install -y "${packages[@]}"

    current_kernel="$(uname -r)"
    if ! dnf install -y "kernel-devel-${current_kernel}"; then
        echo "Warning: Could not install kernel-devel-${current_kernel}; falling back to kernel-devel." >&2
        dnf install -y kernel-devel
    fi
}

ensure_users_and_groups() {
    echo "--> Creating secure users and groups..."

    if ! getent group victus >/dev/null; then
        groupadd --system victus
        echo "Group 'victus' created."
    else
        echo "Group 'victus' already exists."
    fi

    if ! getent group victus-backend >/dev/null; then
        groupadd --system victus-backend
        echo "Group 'victus-backend' created."
    else
        echo "Group 'victus-backend' already exists."
    fi

    if ! id -u victus-backend >/dev/null 2>&1; then
        useradd --system -g victus-backend -s /usr/sbin/nologin victus-backend
        echo "User 'victus-backend' created."
    else
        echo "User 'victus-backend' already exists."
    fi

    if ! groups victus-backend | grep -q '\bvictus\b'; then
        usermod -aG victus victus-backend
        echo "User 'victus-backend' added to the 'victus' group."
    fi

    if [[ -n "${SUDO_USER:-}" ]] && ! groups "${SUDO_USER}" | grep -q '\bvictus\b'; then
        usermod -aG victus "${SUDO_USER}"
        echo "User '${SUDO_USER}' added to the 'victus' group."
    fi
}

install_helpers_and_sudoers() {
    echo "--> Installing helper scripts and sudoers..."
    install -m 0755 backend/src/set-fan-speed.sh /usr/bin/set-fan-speed.sh
    install -m 0755 backend/src/set-fan-mode.sh /usr/bin/set-fan-mode.sh
    install -m 0755 backend/src/set-rgb-zone.sh /usr/bin/set-rgb-zone.sh
    rm -f /etc/sudoers.d/victus-fan-sudoers
    install -m 0440 victus-control-sudoers /etc/sudoers.d/victus-control-sudoers
    if command -v visudo >/dev/null 2>&1; then
        visudo -cf /etc/sudoers.d/victus-control-sudoers >/dev/null
    fi
}

install_hp_wmi_dkms() {
    local wmi_root="wmi-project"
    local wmi_repo="${wmi_root}/hp-wmi-fan-and-backlight-control"
    local current_kernel

    echo "--> Installing patched hp-wmi kernel module..."
    mkdir -p "${wmi_root}"

    if [[ -d "${wmi_repo}/.git" ]]; then
        git -C "${wmi_repo}" fetch origin master
        git -C "${wmi_repo}" reset --hard origin/master
    else
        git clone https://github.com/Batuhan4/hp-wmi-fan-and-backlight-control.git "${wmi_repo}"
    fi

    pushd "${wmi_repo}" >/dev/null

    if dkms status -m "${module_name}" -v "${module_version}" >/dev/null 2>&1; then
        dkms remove "${module_name}/${module_version}" --all || true
    fi

    dkms add .
    current_kernel="$(uname -r)"
    dkms install "${module_name}/${module_version}" -k "${current_kernel}"

    popd >/dev/null

    reload_patched_hp_wmi
}

build_and_install_app() {
    echo "--> Building and installing the application..."
    meson setup build --wipe --prefix=/usr
    meson compile -C build
    meson install -C build
}

install_fedora_udev_rules() {
    echo "--> Installing Fedora-specific udev rules..."
    install -m 0644 99-hp-wmi-permissions.rules /etc/udev/rules.d/99-hp-wmi-permissions.rules
    rm -f /etc/udev/rules.d/victus-control.rules
}

start_services() {
    echo "--> Configuring and starting backend service..."

    systemd-tmpfiles --create || echo "Warning: Failed to create tmpfiles, continuing..."
    systemctl daemon-reload
    udevadm control --reload-rules
    udevadm trigger --subsystem-match=hwmon --subsystem-match=leds || true
    udevadm settle || true

    systemctl enable --now victus-healthcheck.service || true
    systemctl enable --now victus-backend.service
    sleep 2
    systemctl is-active --quiet victus-backend.service
}

echo "--- Starting Victus Control Installation (Fedora) ---"
echo "--> Installing required packages..."
install_fedora_dependencies
ensure_users_and_groups
install_helpers_and_sudoers
install_hp_wmi_dkms
build_and_install_app
install_fedora_udev_rules
start_services

if command -v getenforce >/dev/null 2>&1 && [[ "$(getenforce)" == "Enforcing" ]]; then
    echo
    echo "SELinux is enforcing. That configuration was validated for basic fan control on Fedora,"
    echo "but if you see permission denials, check 'journalctl -t setroubleshoot' and 'ausearch -m avc'."
fi

echo
echo "--- Installation Complete! ---"
echo
echo "IMPORTANT: For the group changes to take full effect, please log out and log back in."
echo "After logging back in, you can launch the application from your desktop menu or by running 'victus-control'."
