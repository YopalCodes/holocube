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
static volatile uint32_t streamUntil = 0;   // raw stream for tools/calibrate_*.py
static QueueHandle_t gestures;
static Gesture imuPoll(uint32_t now);

void imuSetStream(bool on) { streamUntil = on ? millis() + 3000 : 0; }

Gesture imuNextGesture() {
  Gesture g = G_NONE;
  if (gestures) xQueueReceive(gestures, &g, 0);
  return g;
}

static uint8_t addr = IMU_ADDR;
static int failures = 0;

static void wr(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr); Wire.write(reg); Wire.write(v); Wire.endTransmission();
}

static bool probe(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }

// Finds and configures the MPU6050. On failure prints an I2C scan (once per outage).
static bool connectImu(bool report) {
  Wire.end();
  Wire.begin(IMU_SDA, IMU_SCL, 400000);
  Wire.setTimeOut(20);
  for (uint8_t a : {(uint8_t)IMU_ADDR, (uint8_t)0x69}) {
    if (!probe(a)) continue;
    addr = a;
    wr(0x6B, 0x01);  // wake, PLL gyro clock
    wr(0x1A, 0x03);  // DLPF ~44 Hz
    wr(0x1B, 0x08);  // gyro ±500 dps
    wr(0x1C, 0x08);  // accel ±4 g
    Serial.printf("MPU6050 connected at 0x%02X\n", addr);
    return true;
  }
  if (report) {
    Serial.printf("MPU6050 not found. I2C scan (SDA=GPIO%d, SCL=GPIO%d):", IMU_SDA, IMU_SCL);
    int n = 0;
    for (uint8_t a = 1; a < 127; a++) if (probe(a)) { Serial.printf(" 0x%02X", a); n++; }
    Serial.println(n ? "" : " nothing. Check SDA/SCL wiring and 3V3/GND to the IMU.");
  }
  return false;
}

static volatile bool diagReq = false;
void imuDiag() { diagReq = true; }

// Wiring diagnostic: line levels with pull-ups, then look for the IMU on every free pin pair.
static void runDiag() {
  Wire.end();
  const int pins[] = {1, 5, 6, 8, 43, 44};   // XIAO pins not used by the LCD
  Serial.print("DIAG line levels with pull-up (1 = idle high, 0 = stuck low/shorted):");
  for (int p : pins) { pinMode(p, INPUT_PULLUP); delay(2); Serial.printf(" GPIO%d=%d", p, digitalRead(p)); }
  Serial.println();
  int hits = 0;
  for (int sda : pins) for (int scl : pins) {
    if (sda == scl) continue;
    Wire.begin(sda, scl, 100000);
    Wire.setTimeOut(10);
    for (uint8_t a = 1; a < 127; a++)
      if (probe(a)) { Serial.printf("DIAG found 0x%02X with SDA=GPIO%d SCL=GPIO%d\n", a, sda, scl); hits++; }
    Wire.end();
  }
  Serial.println(hits ? "DIAG done" : "DIAG done: no I2C device on any pin pair");
}

static void imuTask(void*) {
  TickType_t last = xTaskGetTickCount();
  bool reported = false;
  for (;;) {
    if (diagReq) { diagReq = false; runDiag(); ok = false; reported = true; }
    if (!ok) {                     // keep retrying once a second, so a reseated wire just works
      ok = connectImu(!reported);
      reported = !ok;
      failures = 0;
      if (!ok) { vTaskDelay(pdMS_TO_TICKS(1000)); last = xTaskGetTickCount(); continue; }
    }
    Gesture g = imuPoll(millis());
    if (g != G_NONE) xQueueSend(gestures, &g, 0);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
  }
}

bool imuBegin() {
  gestures = xQueueCreate(4, sizeof(Gesture));
  ok = connectImu(true);
  xTaskCreatePinnedToCore(imuTask, "imu", 4096, nullptr, 3, nullptr, 0);
  return ok;
}

static Gesture imuPoll(uint32_t now) {
  Wire.beginTransmission(addr); Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(addr, (uint8_t)14) != 14) {
    if (++failures >= 20) { ok = false; Serial.println("MPU6050 lost, reconnecting..."); }
    return G_NONE;
  }
  failures = 0;
  int16_t raw[7];
  for (int i = 0; i < 7; i++) raw[i] = (Wire.read() << 8) | Wire.read();
  float a[3] = {raw[0] / 8192.0f, raw[1] / 8192.0f, raw[2] / 8192.0f};
  float gyroMax = max(abs(raw[4]), max(abs(raw[5]), abs(raw[6]))) / 65.5f;

  if ((int32_t)(streamUntil - now) > 0) {   // raw stream for calibration; gestures are paused meanwhile
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
