#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# --- Check for root privileges ---
if [ "$EUID" -ne 0 ]; then
  echo "This script requires root privileges. Re-running with sudo..."
  sudo "$0" "$@"
  exit
fi

echo "--- Starting Victus Control Installation ---"

# --- 1. Install Dependencies ---
echo "--> Installing required packages..."

packages=(meson ninja gtk4 git dkms)
declare -A header_packages=()

for module_dir in /usr/lib/modules/*; do
    [[ -d "${module_dir}" ]] || continue

    pkgbase_path="${module_dir}/pkgbase"
    kernel_release=$(basename "${module_dir}")

    if [[ -r "${pkgbase_path}" ]]; then
        kernel_pkg=$(<"${pkgbase_path}")
        kernel_pkg=${kernel_pkg//[[:space:]]/}
        header_pkg="${kernel_pkg}-headers"

        if pacman -Si "${header_pkg}" > /dev/null 2>&1; then
            header_packages["${header_pkg}"]=1
            echo "Detected kernel '${kernel_pkg}' (${kernel_release}); queued '${header_pkg}'."
            continue
        fi

        echo "Warning: Unable to find package '${header_pkg}' for kernel '${kernel_pkg}' (${kernel_release})."
    else
        echo "Warning: Unable to read kernel package info at '${pkgbase_path}'."
    fi

    header_packages[linux-headers]=1
done

if [[ ${#header_packages[@]} -eq 0 ]]; then
    echo "Warning: No kernel headers detected; defaulting to 'linux-headers'."
    header_packages[linux-headers]=1
fi

for header_pkg in "${!header_packages[@]}"; do
    packages+=("${header_pkg}")
done

pacman -S --needed --noconfirm "${packages[@]}"

# --- 2. Create Users and Groups ---
echo "--> Creating secure users and groups..."

# Create the victus-backend group if it doesn't exist
if ! getent group victus-backend > /dev/null; then
    groupadd --system victus-backend
    echo "Group 'victus-backend' created."
else
    echo "Group 'victus-backend' already exists."
fi

# Create the victus-backend user if it doesn't exist
if ! id -u victus-backend > /dev/null 2>&1; then
    useradd --system -g victus-backend -s /usr/bin/nologin victus-backend
    echo "User 'victus-backend' created."
else
    echo "User 'victus-backend' already exists."
fi

# Add victus-backend to the victus group
if ! groups victus-backend | grep -q '\bvictus\b'; then
    usermod -aG victus victus-backend
    echo "User 'victus-backend' added to the 'victus' group."
else
    echo "User 'victus-backend' is already in the 'victus' group."
fi

# Create the victus user group if it doesn't exist
if ! getent group victus > /dev/null; then
    groupadd victus
    echo "Group 'victus' created."
else
    echo "Group 'victus' already exists."
fi

# Add the user who invoked sudo to the 'victus' group
# SUDO_USER is set by sudo to the username of the original user.
if [ -n "$SUDO_USER" ]; then
    if ! groups "$SUDO_USER" | grep -q '\bvictus\b'; then
        usermod -aG victus "$SUDO_USER"
        echo "User '$SUDO_USER' added to the 'victus' group."
    else
        echo "User '$SUDO_USER' is already in the 'victus' group."
    fi
else
    echo "Warning: Could not determine the original user. Please add your user to the 'victus' group manually with: sudo usermod -aG victus \$USER"
fi

# --- 2.5. Clean up old configuration files ---
echo "--> Cleaning up old configuration files..."
# Remove any old sudoers files and scripts
rm -f /etc/sudoers.d/victus-fan-sudoers
rm -f /etc/sudoers.d/victus-control-sudoers
rm -f /usr/bin/set-fan-speed.sh
echo "Old configuration files cleaned up."

# --- 3. Install Patched HP-WMI Kernel Module ---
echo "--> Installing patched hp-wmi kernel module..."
wmi_root="wmi-project"
wmi_repo="${wmi_root}/hp-wmi-fan-and-backlight-control"

mkdir -p "${wmi_root}"

if [ -d "${wmi_repo}/.git" ]; then
    echo "Kernel module source directory already exists. Updating repository..."
    if ! git -C "${wmi_repo}" fetch origin master; then
        echo "Warning: Failed to fetch latest hp-wmi-fan-and-backlight-control changes."
    else
        git -C "${wmi_repo}" reset --hard origin/master || {
            echo "Warning: Failed to reset hp-wmi-fan-and-backlight-control repository to origin/master."
            echo "         Remove the directory manually if the local clone is corrupted."
        }
    fi
else
    git clone https://github.com/Batuhan4/hp-wmi-fan-and-backlight-control.git "${wmi_repo}"
fi
pushd "${wmi_repo}" >/dev/null

# Check if the module is already installed with DKMS
module_name="hp-wmi-fan-and-backlight-control"
module_version="0.0.2"

if dkms status -m "${module_name}" -v "${module_version}" >/dev/null 2>&1; then
    echo "Removing existing DKMS registration for ${module_name}/${module_version}..."
    dkms remove "${module_name}/${module_version}" --all || true
fi

echo "Registering DKMS module ${module_name}/${module_version}..."
dkms add .

declare -a kernels_needing_install=()

for module_dir in /usr/lib/modules/*; do
    [[ -d "${module_dir}" ]] || continue
    kernel_release=$(basename "${module_dir}")

    kernel_status=$(dkms status -m "${module_name}" -v "${module_version}" -k "${kernel_release}" || true)

    if [[ "${kernel_status}" != *"installed"* ]]; then
        kernels_needing_install+=("${kernel_release}")
    fi
done

if [[ ${#kernels_needing_install[@]} -eq 0 ]]; then
    echo "hp-wmi module is already installed via DKMS for all detected kernels."
else
    echo "Installing hp-wmi module for kernels: ${kernels_needing_install[*]}"
    for kernel_release in "${kernels_needing_install[@]}"; do
        dkms install "${module_name}/${module_version}" -k "${kernel_release}"
    done
fi

# Ensure the new module is loaded
if lsmod | grep -q "hp_wmi"; then
  rmmod hp_wmi
fi
modprobe hp_wmi
popd >/dev/null
echo "Kernel module installed and loaded."

# --- 4. Build and Install victus-control ---
echo "--> Building and installing the application..."
meson setup build --prefix=/usr
ninja -C build
ninja -C build install
echo "Application built and installed."

# --- 5. Enable Backend Service ---
echo "--> Configuring and starting backend service..."
# Ensure the tmpfiles.d config is applied immediately to create the socket directory
systemd-tmpfiles --create || {
    echo "Warning: Failed to create tmpfiles, continuing..."
}
systemctl daemon-reload || {
    echo "Error: Failed to reload systemd daemon"
    exit 1
}
udevadm control --reload-rules && udevadm trigger || {
    echo "Warning: Failed to reload udev rules, continuing..."
}

if systemctl list-unit-files | grep -q '^victus-backend.service'; then
    echo "--> Refreshing victus-backend.service state..."
    if systemctl is-enabled victus-backend.service >/dev/null 2>&1; then
        systemctl restart victus-backend.service || {
            echo "Error: Failed to restart victus-backend service"
            exit 1
        }
        echo "Backend service restarted."
    else
        systemctl enable --now victus-backend.service || {
            echo "Error: Failed to enable/start victus-backend service"
            exit 1
        }
        echo "Backend service enabled and started."
    fi
else
    systemctl enable --now victus-backend.service || {
        echo "Error: Failed to enable/start victus-backend service"
        exit 1
    }
    echo "Backend service enabled and started."
fi

echo ""
echo "--- Installation Complete! ---"
echo ""
echo "IMPORTANT: For the group changes to take full effect, please log out and log back in."
echo "After logging back in, you can launch the application from your desktop menu or by running 'victus-control' in the terminal."
echo ""
