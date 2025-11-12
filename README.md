# victus-control

Fan control for HP Victus / Omen laptops on Linux. Stock firmware keeps both fans near **2000 RPM** in AUTO—even while the CPU cooks. victus-control delivers a real “Better Auto” curve, manual RPM control, and keyboard lighting support via a patched `hp-wmi` driver, a privileged backend, and a GTK4 desktop client.

> [!WARNING]
> Validated primarily on **HP Victus 16-s00xxxx** running CachyOS (Arch). Other models may work but are unverified—monitor thermals carefully. On **hp victus 15 fa0xxx**, manual fan speeds are not supported, only MAX and AUTO works, better auto can be toggled too, but there is no difference i noticed. 

## Why victus-control
- **Better Auto mode** (selectable from the GTK UI or CLI) samples CPU/GPU temps & utilisation every ~2 s, clamps to each fan’s hardware max, and reapplies targets every 90 s with the firmware-required 10 s stagger. Result: fans climb smoothly with load instead of idling at 2000 RPM like HP’s AUTO.
- **Manual mode** exposes eight RPM steps (~2000 ➜ 5800/6100 RPM) with per-fan precision and watchdog refreshes that keep settings alive through firmware quirks.
- **Keyboard lighting** toggles single-zone RGB colour and brightness.

## System Requirements
- 64-bit Linux with `systemd` (Arch-based distros confirmed).
- Packages installed automatically: `meson`, `ninja`, `gtk4`, `git`, `dkms`, `linux-headers`.
- Root privileges for installing the DKMS module, sudoers rules, and systemd units.

## Install & Update
```bash
git clone https://github.com/Batuhan4/victus-control.git
cd victus-control
sudo ./install.sh
```
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

## Developing
```bash
meson setup build --prefix=/usr
meson compile -C build
sudo meson install -C build
```
- Smoke test (requires backend running): `python test_backend.py`.
- The installer fetches `hp-wmi-fan-and-backlight-control`; it’s git-ignored to keep the repo lean.

## Troubleshooting
- **Fans ignore commands**: ensure the DKMS module is loaded (`dkms status | grep hp-wmi-fan-and-backlight-control`, `sudo modprobe hp_wmi`).
- **Permission errors**: confirm `victus` group membership (`groups $USER`), then re-run the installer or `sudo usermod -aG victus $USER`.
- **Socket missing**: `sudo systemd-tmpfiles --create`; `sudo systemctl restart victus-backend.service`.
- **Uninstall**: `sudo systemctl disable --now victus-backend` and `sudo dkms remove hp-wmi-fan-and-backlight-control/0.0.2 --all`.

## Contributing
See `AGENTS.md` for coding style, testing, and PR expectations. Hardware validation notes are welcome in PR descriptions.

## License
GPLv3. See `LICENSE` for the full text.
