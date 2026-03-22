# victus-control

Fan control and keyboard lighting for HP Victus / Omen laptops on Linux. Stock firmware keeps both fans near **2000 RPM** in AUTO even while the CPU cooks. `victus-control` adds a real Better Auto curve, manual RPM control, a privileged backend, a GTK4 desktop app, and a GNOME Shell extension.

> [!WARNING]
> Validated primarily on **HP Victus 16-s00xxxx** and contributor-tested on Fedora/Arch variants listed in PRs and issues. Other models may work but are not guaranteed. Monitor thermals carefully. On **HP Victus 15 fa0xxx**, manual fan speeds appear unsupported; only `MAX`, `AUTO`, and Better Auto are known to behave.

## Why victus-control
- **Better Auto mode** (selectable from the GTK UI or CLI) samples CPU/GPU temps & utilisation every ~2 s, clamps to each fan’s hardware max, and reapplies targets every 90 s with the firmware-required 10 s stagger. Result: fans climb smoothly with load instead of idling at 2000 RPM like HP’s AUTO.
- **Manual mode** exposes eight RPM steps (~2000 ➜ 5800/6100 RPM) with per-fan precision and watchdog refreshes that keep settings alive through firmware quirks.
- **Keyboard lighting** supports single-zone RGB colour and brightness on compatible hardware.
- **GNOME Shell integration** exposes fan and keyboard controls from the top panel.

## Support Matrix
- **Main installer**: Arch-based distros and Fedora.
- **Desktop app**: GTK4 app installed by the main project installer.
- **GNOME Shell extension**: supported on GNOME Shell 45+ and auto-installed by `install.sh` when GNOME is present.
- **Ubuntu GNOME**: the extension should work on GNOME-based Ubuntu setups if `victus-backend.service` is already installed and reachable, but the project does not currently ship a full Ubuntu installer.

## System Requirements
- 64-bit Linux with `systemd`.
- Supported installer targets:
  - Arch-based distros via `pacman`
  - Fedora via `dnf`
- GNOME Shell 45+ if you want the panel extension.
- Root privileges for installing the DKMS module, sudoers rules, and systemd units.

## Install & Update
### Auto-detecting installer
```bash
git clone https://github.com/Batuhan4/victus-control.git
cd victus-control
sudo ./install.sh
```
The wrapper routes to `arch-install.sh` or `fedora-install.sh` based on your OS.
On GNOME systems, it also installs the panel extension for the desktop user automatically.

### Fedora notes
- Validated by contributors on `HP Victus 16-S0046NT` with Fedora 43.
- The Fedora installer now verifies that the patched `hp_wmi` module is actually active before starting the backend.
- If you recently updated the kernel, reboot first so the running kernel matches the installed `kernel-devel` package.

The installer handles dependency install, user/group creation, DKMS module registration, build + install, and restarts `victus-backend.service`. Log out/in afterwards so your user joins the `victus` group.

### Background services
- `victus-healthcheck.service` runs during boot to ensure the patched `hp-wmi` DKMS module is built for the current kernel and that `hp_wmi` is loaded before the backend starts.
- `victus-backend.service` launches automatically at boot, stays active 24/7, and keeps Better Auto applied even when no UI client is connected—so fan tweaks persist without needing to open the app.

## Daily Usage
- Launch the GTK app (`victus-control`) or use the CLI client (`test_backend.py`).
- Mode dropdown offers `AUTO`, `Better Auto`, `MANUAL`, `MAX`:
  - *Better Auto* is enforced by the background service on each boot, keeps fans in manual PWM, and dynamically adjusts RPMs based on temps/utilisation—ideal for gaming or heavy workloads.
  - *Manual* maps slider positions to calibrated RPM steps; fan 2 honours the 10 s offset automatically.
- Keyboard tab exposes RGB colour + brightness controls.
- Backend status: `systemctl status victus-backend.service` (logs via `journalctl -u victus-backend`).

## GNOME Shell Extension

A GNOME Shell extension is available for quick access to fan and keyboard controls from the top panel.

### Features
- 🌀 **Fan Mode Control**: Switch between AUTO, Better Auto, MANUAL, and MAX
- 📊 **Manual Fan Speed**: Per-fan sliders with 8 RPM steps (visible in MANUAL mode)
- ⌨️ **Keyboard RGB**: 10 color presets and brightness slider
- 🌡️ **Live Status**: Real-time CPU temperature and fan RPM display

### Installation
```bash
sudo ./install.sh
gnome-extensions enable victus-control@victus
```
`install.sh` installs the extension automatically on GNOME systems. You only need the manual `gnome-extensions enable ...` step after install or after logging back in.

If you want to install the extension by itself:
```bash
cd gnome-extension
bash ./install.sh
gnome-extensions enable victus-control@victus
```

### Requirements
- GNOME Shell 45 or later
- `victus-backend.service` must be running
- On Ubuntu GNOME and other GNOME desktops, the extension can be installed separately as long as the backend socket is available.

See [gnome-extension/README.md](gnome-extension/README.md) for detailed documentation.

## Developing
```bash
meson setup build --prefix=/usr
meson compile -C build
sudo meson install -C build
```
- Smoke test (requires backend running): `python test_backend.py`.
- The installer fetches `hp-wmi-fan-and-backlight-control`; it’s git-ignored to keep the repo lean.

## Troubleshooting
- **Fans ignore commands**: ensure the DKMS module is loaded (`dkms status | grep hp-wmi-fan-and-backlight-control`, `modprobe --show-depends hp_wmi | tail -n1` should point at `/extra/hp-wmi.ko.xz`).
- **Permission errors**: confirm `victus` group membership (`groups $USER`), then re-run the installer or `sudo usermod -aG victus $USER`.
- **Socket missing**: `sudo systemd-tmpfiles --create`; `sudo systemctl restart victus-backend.service`.
- **GNOME extension missing after install**: log out/in once, then run `gnome-extensions enable victus-control@victus`.
- **Uninstall**: `sudo systemctl disable --now victus-backend` and `sudo dkms remove hp-wmi-fan-and-backlight-control/0.0.2 --all`.

## Contributing
See `AGENTS.md` for coding style, testing, and PR expectations. Hardware validation notes are welcome in PR descriptions.

## License
GPLv3. See `LICENSE` for the full text.
