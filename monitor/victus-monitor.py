#!/usr/bin/env python3
"""Victus Control temperature monitor.

Runs as a per-user systemd service inside the desktop session and watches the
backend's temperature / fan / mode state over the control socket. Fires desktop
notifications when the machine is hot but cooling is not engaged, or when a
temperature is critically high.

It intentionally lives in the user session (not the privileged backend):
- notify-send needs the session D-Bus, which the user already has.
- The backend only samples temperatures while Better Auto is running, so it
  cannot observe the "hot while in MANUAL mode" case this monitor is meant to
  catch. Polling here covers every fan mode.

No root, no sudo, no desktop-specific assumptions beyond a running notification
daemon (KDE, GNOME, etc. all provide one).
"""

import shutil
import socket
import struct
import subprocess
import sys
import time

SOCKET_PATH = "/run/victus-control/victus_backend.sock"
POLL_INTERVAL_S = 3.0

# Per-component thresholds (°C). GPU numbers are lower than CPU because a mobile
# dGPU thermal-throttles far earlier (~87°C on an RTX 4060), so a 100°C GPU
# "critical" would never fire.
CPU_HOT_C = 83.0        # cooling-not-engaged warning trigger
GPU_HOT_C = 83.0

# A fan is considered "not spinning up" below this RPM.
FAN_IDLE_RPM = 2200

# Hysteresis: after a category fires, it re-arms only once the driving
# temperature falls this far below the threshold, so readings that hover near a
# limit don't produce a stream of notifications.
REARM_MARGIN_C = 5.0

# Hard rate limit per category regardless of hysteresis (seconds).
RATE_LIMIT_S = 60.0

# Sustained-temperature alert thresholds: fire if temp stays above threshold
# for the given duration continuously.
CPU_SUSTAINED_85_SECS = 12.0
CPU_SUSTAINED_90_SECS = 9.0
CPU_SUSTAINED_95_SECS = 6.0
CPU_SUSTAINED_100_SECS = 3.0
GPU_SUSTAINED_80_SECS = 12.0
GPU_SUSTAINED_85_SECS = 9.0

APP_NAME = "Victus Control"


def _recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("socket closed")
        buf += chunk
    return buf


def query(command):
    """Send one command over the control socket and return the trimmed reply.

    Returns None on any I/O error (backend down, socket missing, etc.) so the
    caller can simply skip a tick rather than crash the service.
    """
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(4.0)
            s.connect(SOCKET_PATH)
            payload = command.encode()
            s.sendall(struct.pack("<I", len(payload)) + payload)
            (resp_len,) = struct.unpack("<I", _recv_exact(s, 4))
            if resp_len == 0 or resp_len > 4096:
                return None
            return _recv_exact(s, resp_len).decode(errors="replace").strip()
    except (OSError, ConnectionError, struct.error):
        return None


def parse_temp(reply):
    """Backend returns an integer, 'IDLE' (suspended GPU), or 'ERROR: ...'."""
    if reply is None or reply == "IDLE" or reply.startswith("ERROR"):
        return None
    try:
        return float(reply)
    except ValueError:
        return None


def parse_rpm(reply):
    if reply is None or reply.startswith("ERROR"):
        return None
    try:
        return int(reply)
    except ValueError:
        return None


def notify(summary, body):
    # Always urgency=normal: most notification daemons (GNOME's Shell included)
    # ignore -t on critical-urgency notifications and leave them on screen
    # until dismissed, defeating the timeout below.
    subprocess.Popen(
        ["notify-send", "-a", APP_NAME, "-u", "normal", "-t", "5000",
         "-i", "sensors-temperature-symbolic", summary, body],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )


class Category:
    """Tracks per-alert hysteresis + rate limiting."""

    def __init__(self, threshold_c):
        self.threshold_c = threshold_c
        self.armed = True
        self.last_fired = 0.0

    def should_fire(self, driving_temp_c, now):
        if driving_temp_c is None:
            return False
        # Re-arm once we've cooled well below the threshold.
        if not self.armed and driving_temp_c <= self.threshold_c - REARM_MARGIN_C:
            self.armed = True
        if not self.armed:
            return False
        if driving_temp_c < self.threshold_c:
            return False
        if now - self.last_fired < RATE_LIMIT_S:
            return False
        self.armed = False
        self.last_fired = now
        return True


class SustainedAlert:
    """Fires when temp stays above threshold continuously for required_secs.

    Resets the timer as soon as temp drops below (threshold - REARM_MARGIN_C),
    so a brief dip prevents a re-fire until the temp climbs and holds again.
    Rate-limited independently per instance.
    """

    def __init__(self, threshold_c, required_secs):
        self.threshold_c = threshold_c
        self.required_secs = required_secs
        self.above_since = None   # monotonic timestamp when streak started
        self.last_fired = 0.0
        self.armed = True

    def update(self, temp_c, now):
        """Call every poll tick. Returns True exactly when alert should fire."""
        if temp_c is None:
            self.above_since = None
            return False

        if not self.armed:
            if temp_c <= self.threshold_c - REARM_MARGIN_C:
                self.armed = True
            return False

        if temp_c >= self.threshold_c:
            if self.above_since is None:
                self.above_since = now
            elapsed = now - self.above_since
            if elapsed >= self.required_secs and now - self.last_fired >= RATE_LIMIT_S:
                self.last_fired = now
                self.above_since = None
                self.armed = False
                return True
        elif temp_c <= self.threshold_c - REARM_MARGIN_C:
            self.above_since = None

        return False


def main():
    if shutil.which("notify-send") is None:
        print("victus-monitor: notify-send not found (install libnotify); exiting",
              file=sys.stderr)
        return 1

    cooling = Category(min(CPU_HOT_C, GPU_HOT_C))

    cpu_85 = SustainedAlert(85.0, CPU_SUSTAINED_85_SECS)
    cpu_90 = SustainedAlert(90.0, CPU_SUSTAINED_90_SECS)
    cpu_95 = SustainedAlert(95.0, CPU_SUSTAINED_95_SECS)
    cpu_100 = SustainedAlert(100.0, CPU_SUSTAINED_100_SECS)
    gpu_80 = SustainedAlert(80.0, GPU_SUSTAINED_80_SECS)
    gpu_85 = SustainedAlert(85.0, GPU_SUSTAINED_85_SECS)

    print("victus-monitor: watching temperatures", file=sys.stderr)

    while True:
        now = time.monotonic()

        cpu = parse_temp(query("GET_CPU_TEMP"))
        gpu = parse_temp(query("GET_GPU_TEMP"))
        mode = query("GET_FAN_MODE")
        fan1 = parse_rpm(query("GET_FAN_SPEED 1"))
        fan2 = parse_rpm(query("GET_FAN_SPEED 2"))

        # Sustained-temperature alerts (fire regardless of fan/mode state).
        if cpu_85.update(cpu, now):
            notify("CPU running hot",
                   f"CPU above 85 °C for {CPU_SUSTAINED_85_SECS:.0f} s (now {cpu:.0f} °C). "
                   f"Check active workloads.")

        if cpu_90.update(cpu, now):
            notify("CPU very hot",
                   f"CPU above 90 °C for {CPU_SUSTAINED_90_SECS:.0f} s (now {cpu:.0f} °C). "
                   f"Close heavy workloads.")

        if cpu_95.update(cpu, now):
            notify("CPU critical temperature",
                   f"CPU above 95 °C for {CPU_SUSTAINED_95_SECS:.0f} s (now {cpu:.0f} °C). "
                   f"Close heavy workloads immediately.")

        if cpu_100.update(cpu, now):
            notify("CPU dangerously hot",
                   f"CPU above 100 °C for {CPU_SUSTAINED_100_SECS:.0f} s (now {cpu:.0f} °C). "
                   f"System may throttle or shut down.")

        if gpu_80.update(gpu, now):
            notify("GPU running hot",
                   f"GPU above 80 °C for {GPU_SUSTAINED_80_SECS:.0f} s (now {gpu:.0f} °C). "
                   f"Check active workloads.")

        if gpu_85.update(gpu, now):
            notify("GPU critical temperature",
                   f"GPU above 85 °C for {GPU_SUSTAINED_85_SECS:.0f} s (now {gpu:.0f} °C). "
                   f"Close heavy workloads immediately.")

        # Cooling-not-engaged warning: hot, but the fans aren't ramping — either
        # they're idling or the user is in MANUAL mode with low fan speeds.
        hottest = max([t for t in (cpu, gpu) if t is not None], default=None)
        fans_idle = (
            (fan1 is not None and fan1 < FAN_IDLE_RPM) and
            (fan2 is not None and fan2 < FAN_IDLE_RPM)
        )
        cooling_not_engaged = fans_idle or (mode == "MANUAL")

        if hottest is not None and cooling_not_engaged and cooling.should_fire(hottest, now):
            reason = "fan mode is MANUAL" if mode == "MANUAL" else "the fans are barely spinning"
            notify("Cooling may not keep up",
                   f"{'CPU' if hottest == cpu else 'GPU'} is at {hottest:.0f} °C "
                   f"but {reason}. Consider Better Auto or a higher fan speed.")

        time.sleep(POLL_INTERVAL_S)

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        pass
