# Victus Control GNOME Shell Extension

A GNOME Shell extension for controlling HP Victus laptop fans and keyboard RGB lighting directly from the top panel.

## Features

- **Fan Mode Control**: Switch between AUTO, Better Auto, MANUAL, and MAX modes
- **Manual Fan Speed**: Per-fan speed control with 8 RPM steps (when in MANUAL mode)
- **Keyboard RGB**: Color presets and brightness control
- **Live Status**: Real-time CPU temperature and fan RPM display

## Requirements

- **victus-control backend**: This extension requires the `victus-backend.service` to be running
- **GNOME Shell**: Version 45 or later
- **HP Victus Laptop**: The patched hp-wmi driver must be installed

## Installation

1. Ensure victus-control is installed and the backend service is running:
   ```bash
   sudo systemctl status victus-backend.service
   ```

2. Install the extension:
   ```bash
   cd gnome-extension
   ./install.sh
   ```

3. Enable the extension:
   ```bash
   gnome-extensions enable victus-control@victus
   ```

   Or use the GNOME Extensions app.

## Usage

Click on the fan icon (🌀) in the top panel to access:

- **Fan Mode**: Select operating mode from the dropdown
- **Fan Speed Sliders**: Adjust individual fan speeds (visible in MANUAL mode only)
- **Keyboard RGB**: Choose color presets and adjust brightness
- **Status**: View current CPU temperature and fan RPM

## Development

To test changes without restarting GNOME Shell:
1. Save your changes
2. Disable and re-enable the extension:
   ```bash
   gnome-extensions disable victus-control@victus
   gnome-extensions enable victus-control@victus
   ```

View extension logs:
```bash
journalctl -f -o cat /usr/bin/gnome-shell | grep -i victus
```

## License

GPLv3 - See the main [LICENSE](../LICENSE) file.
