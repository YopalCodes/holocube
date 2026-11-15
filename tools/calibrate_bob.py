"""Guided pick-up (double bob) calibration for HoloCube, with a live view of each bob.

Measures how hard normal handling jolts the cube versus a deliberate up-down-up-down,
and the timing between your two bobs. Prints recommended config.h values and checks them
by replaying your recordings through the firmware's detector.

    python tools/calibrate_bob.py             # show recommendations
    python tools/calibrate_bob.py --write     # also save them into src/config.h
    python tools/calibrate_bob.py --live      # just watch the live bob gauge (Ctrl+C to stop)
"""
import argparse
from imu_common import (Imu, bar, step, mean_vec, mag, angle_between, clamp, read_config,
                        apply_config, fmt_float, RATE_HZ)

ap = argparse.ArgumentParser()
ap.add_argument("--port")
ap.add_argument("--write", action="store_true")
ap.add_argument("--live", action="store_true")
args = ap.parse_args()

cfg = read_config()
MAX_TILT = cfg.get("BOB_MAX_TILT_DEG", 18.0)
TRIES = 3


def dyn_series(samples, base):
    """Vertical jolt in g, using the firmware's slowly adapting gravity baseline."""
    out = []
    for _, a, _ in samples:
        d = mag(a) - base
        if abs(d) < 0.1:
            base += 0.01 * d
        out.append(d)
    return out


def level_mask(samples, g0):
    return [angle_between(a, g0) < MAX_TILT and max(abs(x) for x in gy) < 200 for _, a, gy in samples]


def detect_bobs(dyn, level, th, min_gap, window, cooldown=1000):
    """Replays the firmware's double-bob detector. Returns sample indexes where pick-up fired."""
    fired, peaks, last, first, above_prev, cool = [], 0, -1e9, 0, False, 0
    for i, (d, lv) in enumerate(zip(dyn, level)):
        t = i * 1000 / RATE_HZ
        if peaks and t - first > window:
            peaks = 0
        above = d > th
        if above and not above_prev and lv and t - last > min_gap and t >= cool:
            last = t
            if peaks == 0:
                peaks, first = 1, t
            else:
                peaks, cool = 0, t + cooldown
                fired.append(i)
        above_prev = above
    return fired


def spikes(dyn, level, floor=0.2, gap_ms=120):
    """Local maxima above `floor` while level, at least gap_ms apart: [(index, height)]."""
    out, i, n = [], 0, len(dyn)
    while i < n:
        if dyn[i] > floor and level[i]:
            j = i
            while j < n and dyn[j] > floor:
                j += 1
            k = max(range(i, j), key=lambda x: dyn[x])
            if not out or (k - out[-1][0]) * 1000 / RATE_HZ > gap_ms:
                out.append((k, dyn[k]))
            elif dyn[k] > out[-1][1]:
                out[-1] = (k, dyn[k])
            i = j
        i += 1
    return out


imu = Imu(args.port)
try:
    step("Rest", "Set the cube down flat and don't touch it.", 2)
    rest = imu.record(2)
    g0 = mean_vec(rest)
    base = mag(g0)
    noise = max(abs(mag(s[1]) - base) for s in rest)
    print(f"  gravity {base:.3f} g, resting noise ±{noise:.3f} g")
    th_now = cfg.get("BOB_G", 0.45)

    def live(s, left):
        d = mag(s[1]) - base
        tilt = angle_between(s[1], g0)
        flag = "level" if tilt < MAX_TILT else "TILTED"
        print(f"  down {bar(d, -1.0, 2.0, marks=[(th_now, '|')])} up  {d:+5.2f} g  {flag:<6} ({left:3.1f}s)", end="\r")

    if args.live:
        print(f"Live view. '|' marks the current BOB_G threshold ({th_now} g). Ctrl+C to stop.")
        imu.record(10 ** 6, live)

    step("Normal handling", "Pick the cube up, carry it around, set it down a few times. Do NOT bob it.", 8)
    casual = imu.record(8, live)

    bobs = []
    for n in range(1, TRIES + 1):
        step(f"Double bob {n}/{TRIES}", "Hold it level and bob it UP-DOWN, UP-DOWN quickly, the way you'll do it for real.", 3)
        bobs.append(imu.record(3, live))

    # ---- analysis ----
    c_dyn, c_lvl = dyn_series(casual, base), level_mask(casual, g0)
    casual_peak = max([h for _, h in spikes(c_dyn, c_lvl)] or [0.0])
    print(f"\n  normal handling: biggest level jolt {casual_peak:.2f} g")

    heights, gaps, series = [], [], []
    for n, rec in enumerate(bobs, 1):
        d, lv = dyn_series(rec, base), level_mask(rec, g0)
        series.append((d, lv))
        sp = sorted(sorted(spikes(d, lv), key=lambda x: -x[1])[:2])
        if len(sp) < 2:
            print(f"  bob {n}: found {len(sp)} spike(s). Bob harder or keep it level.")
            continue
        gap = (sp[1][0] - sp[0][0]) * 1000 / RATE_HZ
        heights += [sp[0][1], sp[1][1]]
        gaps.append(gap)
        print(f"  bob {n}: spikes {sp[0][1]:.2f} g and {sp[1][1]:.2f} g, {gap:.0f} ms apart")

    if not heights:
        raise SystemExit("\nNo usable double bobs recorded. Run again.")

    weakest = min(heights)
    if weakest > casual_peak * 1.15:
        th = (weakest + casual_peak) / 2
    else:
        th = weakest * 0.8
        print(f"  warning: your bobs ({weakest:.2f} g) are not clearly stronger than normal handling "
              f"({casual_peak:.2f} g). Bob more sharply, or expect accidental QR pop-ups.")
    th = round(clamp(th, 0.2, 1.5), 2)
    min_gap = int(clamp(min(gaps) * 0.5, 100, 300) // 10 * 10)
    window = int(clamp(max(gaps) * 1.5, 400, 1500) // 10 * 10)

    print(f"\n  Replaying with BOB_G {th} g, min gap {min_gap} ms, window {window} ms:")
    hits = sum(1 for d, lv in series if detect_bobs(d, lv, th, min_gap, window))
    false = len(detect_bobs(c_dyn, c_lvl, th, min_gap, window))
    print(f"    double bobs detected: {hits}/{TRIES}")
    print(f"    accidental pick-ups during normal handling: {false}")

    apply_config({"BOB_G": fmt_float(th), "BOB_MIN_GAP_MS": min_gap, "BOB_WINDOW_MS": window}, args.write)
except KeyboardInterrupt:
    print("\nCancelled.")
finally:
    imu.close()
