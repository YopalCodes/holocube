"""Shared helpers for the HoloCube IMU calibration scripts.

Needs pyserial:  pip install pyserial
The board must run firmware that supports {"cmd":"imu_stream"}.
"""
import math
import re
import sys
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is missing. Install it with:  pip install pyserial")

CONFIG_H = Path(__file__).resolve().parent.parent / "src" / "config.h"
RATE_HZ = 100  # firmware samples the IMU every 10 ms


def find_port():
    for p in list_ports.comports():
        if p.vid == 0x303A:  # Espressif USB
            return p.device
    ports = [p.device for p in list_ports.comports()]
    sys.exit(f"No XIAO ESP32-S3 found. Ports seen: {ports or 'none'}. Pass --port COMx.")


class Imu:
    def __init__(self, port=None):
        self.port = port or find_port()
        self.ser = serial.Serial(self.port, 115200, timeout=0.2, write_timeout=1)
        self._send('{"cmd":"imu_stream","on":true}')
        time.sleep(0.3)
        self.ser.reset_input_buffer()
        print(f"Streaming IMU data from {self.port}. Gestures on the device are paused.\n")

    def _send(self, line):
        self.ser.write((line + "\n").encode())

    def close(self):
        try:
            self._send('{"cmd":"imu_stream","on":false}')
            self.ser.close()
        except Exception:
            pass

    def read(self):
        """Return one sample (t_ms, [ax, ay, az] in g, [gx, gy, gz] in dps), or None on timeout."""
        line = self.ser.readline().decode(errors="replace").strip()
        if not line.startswith("I "):
            return None
        try:
            v = line.split()
            return int(v[1]), [float(x) for x in v[2:5]], [float(x) for x in v[5:8]]
        except (ValueError, IndexError):
            return None

    def record(self, seconds, live=None):
        """Collect samples for `seconds`, calling live(sample) about 25x/s for a live display."""
        self.ser.reset_input_buffer()
        out, end, last_draw = [], time.time() + seconds, 0
        while time.time() < end:
            s = self.read()
            if not s:
                continue
            out.append(s)
            if live and time.time() - last_draw > 0.04:
                last_draw = time.time()
                live(s, end - time.time())
        if live:
            print()
        if len(out) < seconds * RATE_HZ * 0.5:
            print(f"  warning: only {len(out)} samples in {seconds}s; is the USB link busy?")
        return out


# ---------- math ----------
def mag(a):
    return math.sqrt(a[0] ** 2 + a[1] ** 2 + a[2] ** 2)


def mean_vec(samples):
    n = len(samples)
    return [sum(s[1][i] for s in samples) / n for i in range(3)]


def angle_between(a, b):
    dot = sum(x * y for x, y in zip(a, b))
    return math.degrees(math.acos(max(-1.0, min(1.0, dot / (mag(a) * mag(b) + 1e-9)))))


def roll_deg(a, roll_axis, up_axis, up_sign, roll_sign):
    return math.degrees(math.atan2(a[roll_axis], a[up_axis] * up_sign)) * roll_sign


def filtered(values, k=0.25):
    """Same low-pass the firmware uses: x += 0.25 * (new - x)."""
    out, x = [], values[0] if values else 0.0
    for v in values:
        x += k * (v - x)
        out.append(x)
    return out


def percentile(vals, p):
    if not vals:
        return 0.0
    s = sorted(vals)
    return s[min(len(s) - 1, int(p / 100 * len(s)))]


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


# ---------- terminal UI ----------
def bar(value, lo, hi, width=61, marks=()):
    """One-line gauge. marks: list of (value, char) drawn as reference ticks."""
    cells = [" "] * width
    pos = lambda v: int(round((clamp(v, lo, hi) - lo) / (hi - lo) * (width - 1)))
    for mv, ch in marks:
        cells[pos(mv)] = ch
    p = pos(value)
    zero = pos(0) if lo < 0 < hi else 0
    for i in range(min(p, zero), max(p, zero) + 1):
        if cells[i] == " ":
            cells[i] = "="
    cells[p] = "#"
    return "[" + "".join(cells) + "]"


def step(title, instructions, seconds):
    print(f"\n=== {title} ===")
    print(instructions)
    input(f"Press Enter, then do it ({seconds} s of recording)... ")
    for n in (3, 2, 1):
        print(f"  {n}...", end="\r", flush=True)
        time.sleep(0.6)
    print("  GO!      ")


# ---------- config.h ----------
def read_config():
    vals = {}
    for m in re.finditer(r"^#define\s+(\w+)\s+([-\d.]+f?)\b", CONFIG_H.read_text(), re.M):
        vals[m.group(1)] = float(m.group(2).rstrip("f"))
    return vals


def apply_config(updates, write):
    print("\nRecommended src/config.h values:")
    for k, v in updates.items():
        print(f"  #define {k:<18} {v}")
    if not write:
        print("\nRun again with --write to save these into src/config.h, then rebuild and flash.")
        return
    text = CONFIG_H.read_text()
    for k, v in updates.items():
        text, n = re.subn(rf"^(#define\s+{k}\s+)[-\d.]+f?", rf"\g<1>{v}", text, flags=re.M)
        if not n:
            print(f"  warning: {k} not found in config.h")
    CONFIG_H.write_text(text)
    print(f"\nSaved to {CONFIG_H}. Rebuild and flash:  pio run -t upload")


def fmt_float(v):
    return f"{v:.2f}f"
