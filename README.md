# victus-control

Fan control for HP Victus / Omen laptops on Linux. Stock firmware keeps both fans near **2000 RPM** in AUTO—even while the CPU cooks. victus-control delivers a real “Better Auto” curve, manual RPM control, and keyboard lighting support via a patched `hp-wmi` driver, a privileged backend, and a GTK4 desktop client.

> [!WARNING]
> Validated primarily on **HP Victus 16-s00xxxx** running CachyOS (Arch). Other models may work but are unverified—monitor thermals carefully. On **hp victus 15 fa0xxx**, manual fan speeds are not supported, only MAX and AUTO works, better auto can be toggled too, but there is no difference i noticed. 

## Why victus-control
- **Better Auto mode** (selectable from the GTK UI or CLI) samples CPU/GPU temps & utilisation every ~2 s, clamps to each fan’s hardware max, and reapplies targets every 90 s with the firmware-required 10 s stagger. Result: fans climb smoothly with load instead of idling at 2000 RPM like HP’s AUTO.
- **Manual mode** exposes eight RPM steps (~2000 ➜ 5800/6100 RPM) with per-fan precision and watchdog refreshes that keep settings alive through firmware quirks.
- **Keyboard lighting** toggles single-zone RGB colour and brightness.

## System Requirements
- 64-bit Linux with `systemd`.
- Supported installer targets:
  - Arch-based distros via `pacman`
  - Fedora via `dnf`
- Root privileges for installing the DKMS module, sudoers rules, and systemd units.

## Install & Update
### Auto-detecting installer
```bash
git clone https://github.com/Batuhan4/victus-control.git
cd victus-control
sudo ./install.sh
```
The wrapper routes to `arch-install.sh` or `fedora-install.sh` based on your OS.

### Fedora notes
- Validated by contributors on `HP Victus 16-S0046NT` with Fedora 43.
- The Fedora installer now verifies that the patched `hp_wmi` module is actually active before starting the backend.
- If you recently updated the kernel, reboot first so the running kernel matches the installed `kernel-devel` package.

The installer handles dependency install, user/group creation, DKMS module registration, build + install, and restarts `victus-backend.service`. Log out/in afterwards so your user joins the `victus` group.

### Background services
- `victus-healthcheck.service` runs during boot to ensure the patched `hp-wmi` DKMS module is built for the current kernel and that `hp_wmi` is loaded before the backend starts.
- `victus-backend.service` launches automatically at boot, stays active 24/7, and keeps Better Auto applied even when no UI client is connected—so fan tweaks persist without needing to open the app.

## Daily Usage
- Launch the GTK app (`victus-control`).
- Mode dropdown offers `AUTO`, `Better Auto`, `MANUAL`, `MAX`:
  - *Better Auto* is enforced by the background service on each boot, keeps fans in manual PWM, and dynamically adjusts RPMs based on temps/utilisation—ideal for gaming or heavy workloads.
  - *Manual* maps slider positions to calibrated RPM steps; fan 2 honours the 10 s offset automatically.
- Keyboard tab exposes RGB colour + brightness controls.
- Backend status: `systemctl status victus-backend.service` (logs via `journalctl -u victus-backend`).

## Developing
```bash
meson setup build --prefix=/usr
meson compile -C build
sudo meson install -C build
```
- Run unit tests: `meson test -C build`.
- Manual smoke test (requires backend running): launch `victus-control`, switch modes, and confirm fan RPM and keyboard changes are reflected in the UI.
- The installer fetches `hp-wmi-fan-and-backlight-control`; it’s git-ignored to keep the repo lean.

## Troubleshooting
- **Fans ignore commands**: ensure the DKMS module is loaded (`dkms status | grep hp-wmi-fan-and-backlight-control`, `modprobe --show-depends hp_wmi | tail -n1` should point at `/extra/hp-wmi.ko.xz`).
- **Permission errors**: confirm `victus` group membership (`groups $USER`), then re-run the installer or `sudo usermod -aG victus $USER`.
- **Socket missing**: `sudo systemd-tmpfiles --create`; `sudo systemctl restart victus-backend.service`.
- **Uninstall**: `sudo systemctl disable --now victus-backend` and `sudo dkms remove hp-wmi-fan-and-backlight-control/0.0.2 --all`.

## Contributing
Follow the existing Meson/C++ style, run `meson test -C build`, and include hardware validation notes in PR descriptions when you have access to supported laptops.

## License
GPLv3. See `LICENSE` for the full text.
