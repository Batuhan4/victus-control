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
pacman -S --needed --noconfirm meson ninja gtk4 git dkms linux-headers

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

# --- 3. Install Patched HP-WMI Kernel Module ---
echo "--> Installing patched hp-wmi kernel module..."
if [ -d "hp-wmi-fan-and-backlight-control" ]; then
    echo "Kernel module source directory already exists. Skipping clone."
else
    git clone https://github.com/Vilez0/hp-wmi-fan-and-backlight-control.git
fi
cd hp-wmi-fan-and-backlight-control

# Check if the module is already installed with DKMS
if dkms status | grep -q 'hp-wmi-fan-and-backlight-control/0.0.2,.*, installed'; then
    echo "hp-wmi module is already installed via DKMS."
else
    # If the module is registered but not installed (e.g., from a failed run), remove it first.
    if dkms status | grep -q 'hp-wmi-fan-and-backlight-control/0.0.2'; then
        echo "Removing existing (but not installed) DKMS module registration."
        dkms remove hp-wmi-fan-and-backlight-control/0.0.2 --all
    fi
    echo "Registering and installing the DKMS module..."
    dkms add .
    dkms install hp-wmi-fan-and-backlight-control/0.0.2
fi

# Ensure the new module is loaded
if lsmod | grep -q "hp_wmi"; then
  rmmod hp_wmi
fi
modprobe hp_wmi
cd ..
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
systemctl enable --now victus-backend.service || {
    echo "Error: Failed to enable/start victus-backend service"
    exit 1
}
echo "Backend service enabled and started."

echo ""
echo "--- Installation Complete! ---"
echo ""
echo "IMPORTANT: For the group changes to take full effect, please log out and log back in."
echo "After logging back in, you can launch the application from your desktop menu or by running 'victus-control' in the terminal."
echo ""
