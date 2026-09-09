<div align="center">

<img src="frontend/victus-icon.svg" width="96" alt="victus-control logo">

# victus-control

**Fan control and keyboard lighting for HP Victus / Omen laptops on Linux.**

Stock firmware parks both fans near **2000 RPM** in AUTO while the CPU cooks.
`victus-control` gives you a real fan curve, animated RGB backlighting, a
privileged backend, a GTK4 desktop app, and a GNOME Shell extension.

<br>

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-00d9ff?style=for-the-badge&logo=gnu&logoColor=white)](LICENSE)
[![Platform](https://img.shields.io/badge/Linux-systemd-00d9ff?style=for-the-badge&logo=linux&logoColor=white)](#system-requirements)
[![GTK4](https://img.shields.io/badge/GTK-4-00d9ff?style=for-the-badge&logo=gtk&logoColor=white)](https://www.gtk.org/)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-00e676?style=for-the-badge)](#contributing)

[![Arch](https://img.shields.io/badge/Arch-supported-1793d1?style=flat-square&logo=archlinux&logoColor=white)](#install--update)
[![Fedora](https://img.shields.io/badge/Fedora-supported-51a2da?style=flat-square&logo=fedora&logoColor=white)](#fedora-notes)
[![Ubuntu](https://img.shields.io/badge/Ubuntu-supported-e95420?style=flat-square&logo=ubuntu&logoColor=white)](#ubuntu--debian-notes)
[![GNOME](https://img.shields.io/badge/GNOME-45%2B-4a86cf?style=flat-square&logo=gnome&logoColor=white)](#gnome-shell-extension)

</div>

---

<div align="center">

<img src="docs/images/screenshot-dashboard.png" width="620" alt="victus-control dashboard">

*One page: animated backlight preview above, analog fan dials and thermometers below.*

</div>

---

## Contents

- [Why victus-control](#why-victus-control)
- [Quick install](#quick-install)
- [Support matrix](#support-matrix)
- [Secure Boot / userspace alternative](#secure-boot--userspace-alternative)
- [System requirements](#system-requirements)
- [Install & update](#install--update)
- [Daily usage](#daily-usage)
- [Lighting effects](#lighting-effects)
- [Cinnamon applet](#cinnamon-applet)
- [GNOME Shell extension](#gnome-shell-extension)
- [Developing](#developing)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)
- [License](#license)

---

## Why victus-control

| | Feature | What it does |
| :-: | --- | --- |
| 🌀 | **Better Auto** | Samples CPU/GPU temperature and utilisation every ~2 s, clamps to each fan's hardware maximum, and reapplies targets every 90 s with the firmware-required 10 s stagger. Fans climb smoothly with load instead of idling at 2000 RPM like HP's AUTO. |
| 🎚️ | **Manual mode** | Eight RPM steps (~2000 ➜ 5800/6100 RPM) with per-fan precision and watchdog refreshes that keep settings alive through firmware quirks. |
| 🌈 | **Animated lighting** | Single-zone and four-zone RGB, brightness, and rainbow / breathe / flow animations that keep running after the app is closed. |
| 📊 | **Analog telemetry** | Fan dials whose blades turn with the real RPM, and thermometers that shift colour as they heat. |
| 🖥️ | **Panel integration** | Fan and keyboard controls from the panel — a Cinnamon applet and a GNOME Shell extension. |

> [!WARNING]
> Validated primarily on **HP Victus 16-s00xxxx** and contributor-tested on the Fedora/Arch variants listed in PRs and issues. Other models may work but are not guaranteed — **monitor your thermals**. On **HP Victus 15 fa0xxx**, manual fan speeds appear unsupported; only `MAX`, `AUTO`, and Better Auto are known to behave.

---

## Quick install

```bash
curl -fsSL https://raw.githubusercontent.com/Batuhan4/victus-control/main/bootstrap.sh | bash
```

The bootstrap script downloads the current `main` branch into a temporary directory and runs `install.sh`.

> [!CAUTION]
> Do **not** pipe this into `sudo`. `install.sh` elevates itself and needs to know the original desktop user to set up the GNOME extension.

---

## Support matrix

| Component | Status |
| --- | --- |
| **Main installer** | Arch-based, Fedora, and Ubuntu/Debian-based distros |
| **Desktop app** | GTK4, installed by the main project installer |
| **GNOME Shell extension** | GNOME Shell 45+, auto-installed by `install.sh` when GNOME is present |
| **Ubuntu / Debian** | Contributor-tested on Ubuntu 24.04 LTS (GNOME 46); other Debian-based distros use the same path but are less tested |

---

## Secure Boot / userspace alternative

If you can't load the patched `hp-wmi` DKMS module — most notably on **Ubuntu with Secure Boot enabled**, where unsigned out-of-tree modules are rejected — see [`victus-fan/`](victus-fan/).

It is a self-contained **userspace** controller that drives the **stock** in-tree `hp-wmi` driver (two-state `pwm1_enable`, no kernel module), tying fan speed to your power profile and CPU / iGPU / NVIDIA temperature via a small daemon + CLI (no GUI). Tested on an HP Victus 15-fb0xxx running Ubuntu 26.04 — see [`victus-fan/README.md`](victus-fan/README.md).

> [!IMPORTANT]
> **Pick one fan controller, not both.** `victus-fan` and the main `victus-backend` both write the same `pwm1_enable` knob; running both at once makes them fight over the fans. Use `victus-fan` **instead of** the DKMS stack on machines where the patched module can't load — not alongside it.

---

## System requirements

| Requirement | Detail |
| --- | --- |
| OS | 64-bit Linux with `systemd` |
| Package manager | `pacman` (Arch), `dnf` (Fedora), or `apt-get` (Ubuntu/Debian) |
| Desktop | GNOME Shell 45+ for the panel extension (optional) |
| Privileges | Root, for the DKMS module, sudoers rules, and systemd units |

---

## Install & update

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

The wrapper routes to `arch-install.sh`, `fedora-install.sh`, or `ubuntu-install.sh` based on your OS. On GNOME systems it also installs the panel extension for the desktop user automatically.

The installer handles dependency install, user/group creation, DKMS module registration, build + install, and restarts `victus-backend.service`.

> [!NOTE]
> Log out and back in afterwards so your user joins the `victus` group.

### Fedora notes

- Validated by contributors on `HP Victus 16-S0046NT` with Fedora 43.
- The Fedora installer verifies that the patched `hp_wmi` module is actually active before starting the backend.
- If you recently updated the kernel, reboot first so the running kernel matches the installed `kernel-devel` package.

### Ubuntu / Debian notes

- Contributor-tested on Ubuntu 24.04 LTS (GNOME 46) with an HP Victus 16.
- The installer accepts DKMS-managed `hp_wmi` module layouts used by both `/extra` and `/updates/dkms`.
- Secure Boot can block the DKMS module from loading. If install succeeds but `hp_wmi` still does not load, enroll the MOK key with `sudo mokutil --import /var/lib/shim-signed/mok/MOK.der` or disable Secure Boot, then reboot.

### Background services

| Unit | Role |
| --- | --- |
| `victus-healthcheck.service` | Runs at boot to ensure the patched `hp-wmi` DKMS module is built for the current kernel and `hp_wmi` is loaded before the backend starts |
| `victus-backend.service` | Starts at boot and stays active, keeping Better Auto and the lighting applied even with no UI client connected |

---

## Daily usage

Launch the GTK app (`victus-control`) or use the CLI client (`test_backend.py`).

Everything lives on one page, with a card per subsystem.

**Cooling card** — the profile dropdown offers `AUTO`, `Better Auto`, `MANUAL`, `MAX`:

- *Better Auto* is enforced by the background service on each boot, keeps fans in manual PWM, and adjusts RPM from temperature and utilisation — ideal for gaming or heavy workloads.
- *Manual* maps slider positions to calibrated RPM steps; fan 2 honours the 10 s offset automatically.

Fan speed and temperature are shown as analog dials with the digital value under each. The rotor blades turn at a rate derived from the measured RPM, and a stopped fan renders grey rather than merely still. Thermometers and readouts shift cyan → amber → red, crossing at 70 °C and 85 °C.

> [!NOTE]
> On boards whose firmware exposes no fan speed targets, manual speed is removed from the card entirely and both `MANUAL` and `Better Auto` are dropped from the profile list, since both steer the fans through those targets. `AUTO` and `MAX` remain, and the backend leaves such boards on the firmware curve at start-up.

**Keyboard card** — a switch turns the backlight on and off, a row of style radios picks the lighting mode, and the speed slider or colour picker appears depending on which style is selected. There is no Apply step; changing anything applies it.

Backend status: `systemctl status victus-backend.service` (logs via `journalctl -u victus-backend`).

---

## Lighting effects

| Effect | Behaviour |
| --- | --- |
| **Static colour** | One fixed colour — the classic behaviour |
| **Rainbow cycle** | The whole keyboard walks the hue wheel |
| **Breathe** | The current colour fades in and out |
| **Flow (river)** | On four-zone Omen keyboards the hue travels left-to-right across the zones, so the colour flows along the board. Not offered on single-zone hardware, which has no geometry for it |

<div align="center">
<img src="docs/images/screenshot-solid.png" width="560" alt="Solid style showing the colour picker">
<br>
<em>Picking <b>Solid</b> swaps the speed slider for a colour picker — the card only shows the control that applies.</em>
</div>

- The **Speed** slider (1–100) sets the cycle rate.
- Picking a static colour stops the animation. Whichever you chose last, an effect or a solid colour, is restored after a reboot.
- The animation runs in the backend, so lighting keeps going after the GUI is closed. It pauses while the backlight is switched off and resumes when it is switched back on.

> [!TIP]
> Controls that cannot do anything are not shown: **Flow** is absent on single-zone boards, the colour picker only appears for **Solid**, and the speed slider only for the animated styles.

---

## Cinnamon applet

For Cinnamon desktops (Linux Mint, and Cinnamon on Ubuntu/Fedora/Arch). Fan mode,
keyboard backlight and lighting effects from the panel, next to the battery and
volume icons — the same idea as the brightness slider in the battery menu, so the
desktop app is not needed for everyday adjustments.

<div align="center">
<img src="docs/images/cinnamon-applet.png" width="420" alt="Victus Control Cinnamon applet menu">
</div>

| | In the menu |
| :-: | --- |
| 🌀 | **Fan mode** — Auto, Better Auto, Max (and Manual where the firmware supports it) |
| 📊 | **Live telemetry** — both fan RPMs and CPU/GPU temperature |
| ⌨️ | **Keyboard backlight** — on/off switch and a brightness slider |
| 🌈 | **Lighting effect** — Static, Rainbow, Breathe (and Flow on four-zone boards) with a speed slider |

The panel itself shows CPU temperature by default; right-click → *Configure* to
show fan RPM instead, or just the icon.

```bash
bash cinnamon-applet/install.sh
```

`install.sh` runs this automatically when Cinnamon is detected. If the applet does
not appear, restart Cinnamon with <kbd>Alt</kbd>+<kbd>F2</kbd>, then `r`, Enter.

> [!NOTE]
> Controls that the hardware cannot honour are hidden rather than shown inert:
> **Manual** fan mode is absent where the firmware refuses fan speed targets, and
> **Flow** is absent on single-zone keyboards.

> [!TIP]
> The applet polls slowly (10 s) while its menu is closed and only speeds up to
> 2 s while you have it open, so it costs very little when you are not using it.
> The interval is configurable in its settings.

## GNOME Shell extension

Quick access to fan and keyboard controls from the top panel.

| | Feature |
| :-: | --- |
| 🌀 | **Fan mode control** — AUTO, Better Auto, MANUAL, MAX |
| 📊 | **Manual fan speed** — per-fan sliders with 8 RPM steps (visible in MANUAL mode) |
| ⌨️ | **Keyboard RGB** — 10 colour presets and a brightness slider |
| 🌡️ | **Live status** — real-time CPU temperature and fan RPM |

```bash
sudo ./install.sh
gnome-extensions enable victus-control@victus
```

`install.sh` installs the extension automatically on GNOME systems. You only need the manual `gnome-extensions enable ...` step after install or after logging back in.

<details>
<summary>Install the extension by itself</summary>

```bash
cd gnome-extension
bash ./install.sh
gnome-extensions enable victus-control@victus
```

</details>

**Requirements:** GNOME Shell 45+, `victus-backend.service` running. On Ubuntu GNOME and other GNOME desktops the extension can be installed separately as long as the backend socket is available.

See [gnome-extension/README.md](gnome-extension/README.md) for detailed documentation.

---

## Developing

```bash
meson setup build --prefix=/usr
meson compile -C build
sudo meson install -C build
```

- Smoke test (requires the backend running): `python test_backend.py`
- The installer fetches `hp-wmi-fan-and-backlight-control`; it is git-ignored to keep the repo lean.

---

## Troubleshooting

<details>
<summary><b>Fans ignore commands</b></summary>

Ensure the DKMS module is loaded:

```bash
dkms status | grep hp-wmi-fan-and-backlight-control
modprobe --show-depends hp_wmi | tail -n1
```

The last command should point at the DKMS-built `hp-wmi.ko` under `/updates/dkms/` on Arch or `/extra/` on other distros.

</details>

<details>
<summary><b>Fans stuck at one speed after the service starts</b></summary>

A few boards report software fan support but have a BIOS that ignores per-RPM targets. Better Auto then switches the fans to MANUAL and cannot steer them, leaving them pinned with the firmware curve disabled.

Check with the backend in Better Auto and the machine under load:

```bash
grep . /sys/devices/platform/hp-wmi/hwmon/hwmon*/fan*_input
journalctl -u victus-backend -n 20
```

If the RPM readings sit at one value while the `better-auto:` lines in the log keep raising the level, your board is affected. Keep the keyboard lighting and leave the fans to the firmware:

```bash
sudo mkdir -p /etc/systemd/system/victus-backend.service.d
printf '[Service]\nEnvironment=VICTUS_NO_FAN_CONTROL=1\n' | \
  sudo tee /etc/systemd/system/victus-backend.service.d/no-fan-control.conf
sudo systemctl daemon-reload && sudo systemctl restart victus-backend
```

With that set the backend puts the driver back into AUTO on every start, refuses fan commands from any client, and the app's cooling card shows fan speeds and temperatures only.

</details>

<details>
<summary><b>Permission errors</b></summary>

Confirm `victus` group membership, then re-run the installer:

```bash
groups $USER
sudo usermod -aG victus $USER
```

Log out and back in for the group change to take effect.

</details>

<details>
<summary><b>Socket missing</b></summary>

```bash
sudo systemd-tmpfiles --create
sudo systemctl restart victus-backend.service
```

</details>

<details>
<summary><b>GNOME extension missing after install</b></summary>

Log out and back in once, then:

```bash
gnome-extensions enable victus-control@victus
```

</details>

<details>
<summary><b>Uninstall</b></summary>

```bash
sudo systemctl disable --now victus-backend
sudo dkms remove hp-wmi-fan-and-backlight-control/$(dkms status -m hp-wmi-fan-and-backlight-control | sed -n 's#.*/\([^,]*\),.*#\1#p' | head -n1) --all
```

Substitute the version shown by `dkms status` if the command above does not resolve it.

</details>

---

## Contributing

See `AGENTS.md` for coding style, testing, and PR expectations. Hardware validation notes are very welcome in PR descriptions — this project lives on contributors reporting what does and does not work on their board.

## License

GPLv3. See [`LICENSE`](LICENSE) for the full text.
