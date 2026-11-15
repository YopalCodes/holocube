// MPU6050 over raw I2C + gesture detection (roll left/right, double bob = pick-up)
#include <Wire.h>
#include "core.h"

static bool ok = false;
static float rollF = 0, baseMag = 1.0f;
static bool armed = true;
static int tiltDir = 0;
static uint32_t tiltSince = 0, cooldownUntil = 0, lastPeak = 0, firstPeak = 0;
static int peaks = 0;
static bool aboveTh = false;
bool imuStream = false;   // set by {"cmd":"imu_stream"}; prints raw data for tools/calibrate_*.py

static void wr(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(IMU_ADDR); Wire.write(reg); Wire.write(v); Wire.endTransmission();
}

bool imuBegin() {
  Wire.begin(IMU_SDA, IMU_SCL, 400000);
  Wire.beginTransmission(IMU_ADDR);
  if (Wire.endTransmission() != 0) return false;
  wr(0x6B, 0x01);  // wake, PLL gyro clock
  wr(0x1A, 0x03);  // DLPF ~44 Hz
  wr(0x1B, 0x08);  // gyro ±500 dps
  wr(0x1C, 0x08);  // accel ±4 g
  ok = true;
  return true;
}

Gesture imuPoll(uint32_t now) {
  if (!ok) return G_NONE;
  Wire.beginTransmission(IMU_ADDR); Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(IMU_ADDR, 14) != 14) return G_NONE;
  int16_t raw[7];
  for (int i = 0; i < 7; i++) raw[i] = (Wire.read() << 8) | Wire.read();
  float a[3] = {raw[0] / 8192.0f, raw[1] / 8192.0f, raw[2] / 8192.0f};
  float gyroMax = max(abs(raw[4]), max(abs(raw[5]), abs(raw[6]))) / 65.5f;

  if (imuStream) {   // raw stream for calibration; gestures are paused meanwhile
    Serial.printf("I %lu %.3f %.3f %.3f %.1f %.1f %.1f\n", (unsigned long)now, a[0], a[1], a[2],
                  raw[4] / 65.5f, raw[5] / 65.5f, raw[6] / 65.5f);
    return G_NONE;
  }

  float roll = atan2f(a[IMU_ROLL_AXIS], a[IMU_UP_AXIS] * IMU_UP_SIGN) * 57.2958f * IMU_ROLL_SIGN;
  rollF += 0.25f * (roll - rollF);

  // ---- Roll left/right ----
  if (!armed && fabsf(rollF) < ROLL_REARM_DEG) armed = true;
  int dir = rollF > ROLL_TRIGGER_DEG ? 1 : rollF < -ROLL_TRIGGER_DEG ? -1 : 0;
  if (dir != tiltDir) { tiltDir = dir; tiltSince = now; }
  if (armed && dir && now - tiltSince >= ROLL_HOLD_MS && (int32_t)(now - cooldownUntil) >= 0) {
    armed = false;
    cooldownUntil = now + GESTURE_COOLDOWN;
    peaks = 0;
    return dir > 0 ? G_RIGHT : G_LEFT;
  }

  // ---- Double bob (vertical spikes while level) ----
  float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  float dyn = mag - baseMag;
  if (fabsf(dyn) < 0.1f) baseMag += 0.01f * dyn;   // slow gravity baseline
  if (peaks && now - firstPeak > BOB_WINDOW_MS) peaks = 0;
  bool level = fabsf(rollF) < BOB_MAX_TILT_DEG && gyroMax < 200;
  bool above = dyn > BOB_G;
  if (above && !aboveTh && level && now - lastPeak > BOB_MIN_GAP_MS && (int32_t)(now - cooldownUntil) >= 0) {
    lastPeak = now;
    if (peaks++ == 0) firstPeak = now;
    else {
      peaks = 0;
      cooldownUntil = now + GESTURE_COOLDOWN * 2;
      return G_PICKUP;
    }
  }
  aboveTh = above;
  return G_NONE;
}
