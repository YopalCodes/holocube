"""Guided roll calibration for HoloCube.

Measures which IMU axis is "up", which axis is roll and in which direction, then how far
and how long you tilt, and how much the cube wobbles in normal handling. Prints recommended
config.h values and checks them by replaying your recordings through the firmware's detector.

    python tools/calibrate_roll.py            # show recommendations
    python tools/calibrate_roll.py --write    # also save them into src/config.h
"""
import argparse
from imu_common import (Imu, bar, step, mean_vec, roll_deg, filtered, percentile, clamp,
                        apply_config, fmt_float, RATE_HZ)

ap = argparse.ArgumentParser()
ap.add_argument("--port")
ap.add_argument("--write", action="store_true")
args = ap.parse_args()

AXES = "xyz"


def detect_rolls(rolls, trig, rearm, hold_ms, cooldown_ms=500):
    """Replays the firmware's roll detector over a filtered roll series. Returns list of +1/-1."""
    events, armed, tilt_dir, since, cool = [], True, 0, 0, 0
    for i, r in enumerate(rolls):
        t = i * 1000 / RATE_HZ
        if not armed and abs(r) < rearm:
            armed = True
        d = 1 if r > trig else -1 if r < -trig else 0
        if d != tilt_dir:
            tilt_dir, since = d, t
        if armed and d and t - since >= hold_ms and t >= cool:
            armed, cool = False, t + cooldown_ms
            events.append(d)
    return events


imu = Imu(args.port)
try:
    # 1. Level rest -> which axis points up
    step("1/6  Hold level", "Set the cube down flat, display facing up. Don't touch it.", 3)
    rest = imu.record(3)
    g = mean_vec(rest)
    up = max(range(3), key=lambda i: abs(g[i]))
    up_sign = 1 if g[up] > 0 else -1
    print(f"  gravity = ({g[0]:+.2f}, {g[1]:+.2f}, {g[2]:+.2f}) g  ->  up axis {AXES[up]}, sign {up_sign:+d}")
    horiz = [i for i in range(3) if i != up]

    def live_axes(s, left):
        a = s[1]
        r = [roll_deg(a, h, up, up_sign, 1) for h in horiz]
        print(f"  {AXES[horiz[0]]}:{r[0]:+6.1f}°  {AXES[horiz[1]]}:{r[1]:+6.1f}°  ({left:3.1f}s left)", end="\r")

    # 2. Tilt right -> which axis is roll and its sign
    step("2/6  Tilt RIGHT", "Tilt the cube to the RIGHT the way you would to switch apps, hold ~1 s, then return to level.", 4)
    right = imu.record(4, live_axes)
    peaks = {h: max((roll_deg(s[1], h, up, up_sign, 1) for s in right), key=abs) for h in horiz}
    roll_axis = max(horiz, key=lambda h: abs(peaks[h]))
    roll_sign = 1 if peaks[roll_axis] > 0 else -1
    print(f"  biggest change on axis {AXES[roll_axis]} ({peaks[roll_axis]:+.0f}°)  ->  roll axis {AXES[roll_axis]}, sign {roll_sign:+d}")

    def series(samples):
        return filtered([roll_deg(s[1], roll_axis, up, up_sign, roll_sign) for s in samples])

    def live_roll(s, left):
        r = roll_deg(s[1], roll_axis, up, up_sign, roll_sign)
        print(f"  L {bar(r, -90, 90, marks=[(-30, '|'), (30, '|')])} R  {r:+6.1f}°  ({left:3.1f}s)", end="\r")

    # 3. Tilt left -> confirm
    step("3/6  Tilt LEFT", "Tilt the cube to the LEFT, hold ~1 s, then return to level.", 4)
    left = imu.record(4, live_roll)
    lpeak = min(series(left))
    if lpeak > -10:
        print(f"  warning: left tilt only reached {lpeak:+.0f}°. Expected a clearly negative angle; redo with a bigger tilt.")
    else:
        print(f"  left peak {lpeak:+.0f}°  (correctly negative)")

    # 4 + 5. Sequences
    step("4/6  RIGHT, then LEFT", "Tilt right, back to level, then tilt left, back to level. Normal speed.", 5)
    rl = imu.record(5, live_roll)
    step("5/6  LEFT, then RIGHT", "Tilt left, back to level, then tilt right, back to level. Normal speed.", 5)
    lr = imu.record(5, live_roll)

    # 6. Casual handling
    step("6/6  Normal handling", "Pick the cube up, look at it, move it around casually, and set it down. Do NOT do any gestures.", 8)
    casual = imu.record(8, live_roll)

    # ---- analysis ----
    deliberate = [series(right), series(left), series(rl), series(lr)]
    tilt_peaks = [max(series(right)), -min(series(left)),
                  max(series(rl)), -min(series(rl)), max(series(lr)), -min(series(lr))]
    rest_max = max(abs(r) for r in series(rest))
    casual_s = series(casual)
    casual_max = max(abs(r) for r in casual_s)
    casual_p95 = percentile([abs(r) for r in casual_s], 95)
    print(f"\n  deliberate tilt peaks: {', '.join(f'{p:.0f}°' for p in tilt_peaks)}")
    print(f"  resting noise: {rest_max:.1f}°   casual handling: 95% under {casual_p95:.0f}°, max {casual_max:.0f}°")

    weakest = min(tilt_peaks)
    trig = 0.6 * weakest
    if casual_max < 0.85 * weakest:
        trig = max(trig, casual_max * 1.15)
    trig = round(clamp(min(trig, 0.85 * weakest), 15, 60))
    if casual_max >= trig:
        print(f"  warning: casual handling reached {casual_max:.0f}°, above the {trig}° trigger. "
              "Tilt more deliberately, or expect some accidental switches.")
    rearm = round(clamp(max(rest_max * 2 + 3, 6), 6, trig * 0.5))

    durations = []
    for s in deliberate:
        above, run = 0, 0
        for r in s:
            run = run + 1 if abs(r) > trig else 0
            above = max(above, run)
        if above:
            durations.append(above * 1000 / RATE_HZ)
    hold = int(clamp(0.4 * min(durations) if durations else 120, 60, 300) // 10 * 10)

    # ---- verify with the firmware's detector ----
    expect = [[1], [-1], [1, -1], [-1, 1]]
    names = ["right", "left", "right-left", "left-right"]
    print(f"\n  Replaying with trigger {trig}°, rearm {rearm}°, hold {hold} ms:")
    ok = 0
    for name, s, want in zip(names, deliberate, expect):
        got = detect_rolls(s, trig, rearm, hold)
        good = got == want
        ok += good
        show = " ".join("R" if d > 0 else "L" for d in got) or "nothing"
        print(f"    {name:<11} detected {show:<8} {'OK' if good else 'expected ' + ' '.join('R' if d > 0 else 'L' for d in want)}")
    false = detect_rolls(casual_s, trig, rearm, hold)
    print(f"    casual      {len(false)} accidental switch(es)")
    print(f"  {ok}/4 sequences correct.")

    apply_config({
        "IMU_UP_AXIS": up, "IMU_UP_SIGN": up_sign, "IMU_ROLL_AXIS": roll_axis, "IMU_ROLL_SIGN": roll_sign,
        "ROLL_TRIGGER_DEG": fmt_float(trig), "ROLL_HOLD_MS": hold, "ROLL_REARM_DEG": fmt_float(rearm),
    }, args.write)
except KeyboardInterrupt:
    print("\nCancelled.")
finally:
    imu.close()
