# victus-control

## Quick Install
```bash
curl -fsSL https://raw.githubusercontent.com/Batuhan4/victus-control/main/bootstrap.sh | bash
```
The bootstrap script downloads the current `main` branch into a temporary directory and runs `install.sh`.
Do not pipe it into `sudo`; `install.sh` elevates itself and preserves the desktop user for GNOME extension setup.

Fan control and keyboard lighting for HP Victus / Omen laptops on Linux. Stock firmware keeps both fans near **2000 RPM** in AUTO even while the CPU cooks. `victus-control` adds a real Better Auto curve, manual RPM control, a privileged backend, a GTK4 desktop app, and a GNOME Shell extension.

> [!WARNING]
> Validated primarily on **HP Victus 16-s00xxxx** and contributor-tested on Fedora/Arch variants listed in PRs and issues. Other models may work but are not guaranteed. Monitor thermals carefully. On **HP Victus 15 fa0xxx**, manual fan speeds appear unsupported; only `MAX`, `AUTO`, and Better Auto are known to behave.

## Why victus-control
- **Better Auto mode** (selectable from the GTK UI or CLI) samples CPU/GPU temps & utilisation every ~2 s, clamps to each fan’s hardware max, and reapplies targets every 90 s with the firmware-required 10 s stagger. Result: fans climb smoothly with load instead of idling at 2000 RPM like HP’s AUTO.
- **Manual mode** exposes eight RPM steps (~2000 ➜ 5800/6100 RPM) with per-fan precision and watchdog refreshes that keep settings alive through firmware quirks.
- **Keyboard lighting** supports single-zone RGB colour and brightness on compatible hardware, plus animated rainbow / breathe / flow effects.
- **GNOME Shell integration** exposes fan and keyboard controls from the top panel.

## Support Matrix
- **Main installer**: Arch-based distros, Fedora, and Ubuntu/Debian-based distros.
- **Desktop app**: GTK4 app installed by the main project installer.
- **GNOME Shell extension**: supported on GNOME Shell 45+ and auto-installed by `install.sh` when GNOME is present.
- **Ubuntu / Debian**: contributor-tested on Ubuntu 24.04 LTS (GNOME 46). Other Debian-based distros use the same installer path but are less tested.

## Ubuntu / Secure Boot (userspace, no kernel module)

If you can't load the patched `hp-wmi` DKMS module — most notably on **Ubuntu with Secure Boot enabled**, where unsigned out-of-tree modules are rejected — see [`victus-fan/`](victus-fan/). It is a self-contained **userspace** controller that drives the **stock** in-tree `hp-wmi` driver (two-state `pwm1_enable`, no kernel module), tying fan speed to your power profile and CPU / iGPU / NVIDIA temperature via a small daemon + CLI (no GUI). Tested on an HP Victus 15-fb0xxx running Ubuntu 26.04 — see [`victus-fan/README.md`](victus-fan/README.md).

> **Pick one fan controller, not both.** `victus-fan` and the main `victus-backend` (installed by the DKMS installers above) both write the same `pwm1_enable` knob; running both at once makes them fight over the fans. Use `victus-fan` **instead of** the DKMS stack on machines where the patched module can't load — not alongside it.

## System Requirements
- 64-bit Linux with `systemd`.
- Supported installer targets:
  - Arch-based distros via `pacman`
  - Fedora via `dnf`
  - Ubuntu/Debian-based distros via `apt-get`
- GNOME Shell 45+ if you want the panel extension.
- Root privileges for installing the DKMS module, sudoers rules, and systemd units.

## Install & Update
### Bootstrap one-liner
```bash
curl -fsSL https://raw.githubusercontent.com/Batuhan4/victus-control/main/bootstrap.sh | bash
```
Use this if you want a temporary checkout and the shortest install path.

### Git clone installer
```bash
git clone https://github.com/Batuhan4/victus-control.git
cd victus-control
sudo ./install.sh
```
The wrapper routes to `arch-install.sh`, `fedora-install.sh`, or `ubuntu-install.sh` based on your OS.
On GNOME systems, it also installs the panel extension for the desktop user automatically.

### Fedora notes
- Validated by contributors on `HP Victus 16-S0046NT` with Fedora 43.
- The Fedora installer now verifies that the patched `hp_wmi` module is actually active before starting the backend.
- If you recently updated the kernel, reboot first so the running kernel matches the installed `kernel-devel` package.

### Ubuntu / Debian notes
- Contributor-tested on Ubuntu 24.04 LTS (GNOME 46) with an HP Victus 16.
- The installer accepts DKMS-managed `hp_wmi` module layouts used by both `/extra` and `/updates/dkms`.
- Secure Boot can block the DKMS module from loading. If install succeeds but `hp_wmi` still does not load, enroll the MOK key with `sudo mokutil --import /var/lib/shim-signed/mok/MOK.der` or disable Secure Boot, then reboot.

The installer handles dependency install, user/group creation, DKMS module registration, build + install, and restarts `victus-backend.service`. Log out/in afterwards so your user joins the `victus` group.

### Background services
- `victus-healthcheck.service` runs during boot to ensure the patched `hp-wmi` DKMS module is built for the current kernel and that `hp_wmi` is loaded before the backend starts.
- `victus-backend.service` launches automatically at boot, stays active 24/7, and keeps Better Auto applied even when no UI client is connected—so fan tweaks persist without needing to open the app.

## Daily Usage
- Launch the GTK app (`victus-control`) or use the CLI client (`test_backend.py`).
- Mode dropdown offers `AUTO`, `Better Auto`, `MANUAL`, `MAX`:
  - *Better Auto* is enforced by the background service on each boot, keeps fans in manual PWM, and dynamically adjusts RPMs based on temps/utilisation—ideal for gaming or heavy workloads.
  - *Manual* maps slider positions to calibrated RPM steps; fan 2 honours the 10 s offset automatically.
- Keyboard tab exposes RGB colour + brightness controls, plus **animated lighting effects**:
  - *Static colour* — one fixed colour (the classic behaviour).
  - *Rainbow cycle* — the whole keyboard walks the hue wheel.
  - *Breathe* — the current colour fades in and out.
  - *Flow (river)* — on four-zone Omen keyboards the hue travels left-to-right
    across the zones so the colour appears to flow along the board. Single-zone
    backlights have no geometry for a wave to travel across, so this behaves
    like *Rainbow* there and the dropdown says so.
  - The *Speed* slider (1-100) sets the cycle rate. Picking a static colour
    stops the animation, and the chosen effect is restored after a reboot.
  - The animation runs in the backend, so the lighting keeps going after the
    GUI is closed. It pauses while the backlight is switched off.
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
- **Fans ignore commands**: ensure the DKMS module is loaded (`dkms status | grep hp-wmi-fan-and-backlight-control`, `modprobe --show-depends hp_wmi | tail -n1` should point at the DKMS-built `hp-wmi.ko` under `/updates/dkms/` on Arch or `/extra/` on other distros).
- **Permission errors**: confirm `victus` group membership (`groups $USER`), then re-run the installer or `sudo usermod -aG victus $USER`.
- **Socket missing**: `sudo systemd-tmpfiles --create`; `sudo systemctl restart victus-backend.service`.
- **Fans stuck at one speed after the service starts**: a few boards report
  software fan support but have a BIOS that ignores per-RPM targets. The Better
  Auto loop then switches the fans to MANUAL and cannot set a speed, leaving
  them pinned with the firmware curve disabled. Check with
  `cat /sys/devices/platform/hp-wmi/hwmon/hwmon*/fan1_target` — if that returns
  `Invalid argument` while writes appear to succeed, your board is affected.
  Keep the keyboard lighting and leave the fans to the firmware with:
  ```bash
  sudo mkdir -p /etc/systemd/system/victus-backend.service.d
  printf '[Service]\nEnvironment=VICTUS_NO_FAN_CONTROL=1\n' | \
    sudo tee /etc/systemd/system/victus-backend.service.d/no-fan-control.conf
  sudo systemctl daemon-reload && sudo systemctl restart victus-backend
  ```
- **GNOME extension missing after install**: log out/in once, then run `gnome-extensions enable victus-control@victus`.
- **Uninstall**: `sudo systemctl disable --now victus-backend` and `sudo dkms remove hp-wmi-fan-and-backlight-control/$(dkms status -m hp-wmi-fan-and-backlight-control | sed -n 's#.*/\([^,]*\),.*#\1#p' | head -n1) --all` (or substitute the version shown by `dkms status`).

## Contributing
See `AGENTS.md` for coding style, testing, and PR expectations. Hardware validation notes are welcome in PR descriptions.

## License
GPLv3. See `LICENSE` for the full text.
