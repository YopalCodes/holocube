// Clock & Weather: weather strip on top, big glowing time in the middle, date at the bottom,
// over a seasonal particle layer. Draw order: particles -> lights -> time -> weather strip.
// Everything goes into the full-screen PSRAM canvas and is pushed in one go (no flicker).
#include "../core.h"

static constexpr int W = 240, H = 240;
static constexpr float TAU = 6.2831853f;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return lgfx::color565(r, g, b); }
static uint16_t dim(uint16_t c, float k) {   // scale an RGB565 colour by 0..1
  k = k < 0 ? 0 : k > 1 ? 1 : k;
  return (uint16_t)(((int)(((c >> 11) & 31) * k) << 11) | ((int)(((c >> 5) & 63) * k) << 5) | (int)((c & 31) * k));
}
static float frand(float a, float b) { return a + (b - a) * (esp_random() / 4294967295.0f); }

// ---------------- Weather icons (drawn from shapes, ~30x30, centred on x,y) ----------------
enum Sky { SKY_CLEAR, SKY_PARTLY, SKY_OVERCAST, SKY_FOG, SKY_DRIZZLE, SKY_RAIN, SKY_SNOW, SKY_SHOWERS, SKY_SNOWSHOWERS, SKY_THUNDER };
static Sky skyOf(int c) {   // Open-Meteo WMO weather codes
  if (c == 0) return SKY_CLEAR;
  if (c <= 2) return SKY_PARTLY;
  if (c == 45 || c == 48) return SKY_FOG;
  if (c >= 51 && c <= 57) return SKY_DRIZZLE;
  if (c >= 61 && c <= 67) return SKY_RAIN;
  if (c >= 71 && c <= 77) return SKY_SNOW;
  if (c >= 80 && c <= 82) return SKY_SHOWERS;
  if (c == 85 || c == 86) return SKY_SNOWSHOWERS;
  if (c >= 95) return SKY_THUNDER;
  return SKY_OVERCAST;
}

static void sun(LGFX_Sprite& c, int x, int y, int r) {
  uint16_t col = rgb(255, 196, 40);
  for (int i = 0; i < 8; i++) {
    float a = i * TAU / 8, ca = cosf(a), sa = sinf(a);
    c.drawWideLine(x + ca * (r + 3), y + sa * (r + 3), x + ca * (r + 6), y + sa * (r + 6), 1.0f, col);
  }
  c.fillSmoothCircle(x, y, r, col);
}
static void cloud(LGFX_Sprite& c, int x, int y, uint16_t col) {   // ~26x16, flat bottom at y+7
  c.fillCircle(x - 7, y + 2, 5, col);
  c.fillCircle(x + 1, y - 2, 7, col);
  c.fillCircle(x + 8, y + 2, 5, col);
  c.fillRect(x - 7, y + 2, 16, 6, col);
}
static void drawIcon(LGFX_Sprite& c, Sky s, int x, int y) {
  const uint16_t white = rgb(225, 230, 240), grey = rgb(130, 135, 150), blue = rgb(80, 160, 255);
  switch (s) {
    case SKY_CLEAR: sun(c, x, y, 7); break;
    case SKY_PARTLY: sun(c, x - 5, y - 5, 5); cloud(c, x + 3, y + 3, white); break;
    case SKY_OVERCAST: cloud(c, x + 5, y - 4, grey); cloud(c, x - 2, y + 3, white); break;
    case SKY_FOG:
      for (int i = 0; i < 4; i++) c.drawWideLine(x - 12 + (i & 1) * 4, y - 7 + i * 5, x + 12 - (i & 1) * 4, y - 7 + i * 5, 1.0f, grey);
      break;
    case SKY_DRIZZLE:
      cloud(c, x, y - 5, white);
      for (int i = -1; i <= 1; i++) c.fillCircle(x + i * 6, y + 7 + (i == 0) * 4, 1, blue);
      break;
    case SKY_RAIN: case SKY_SHOWERS:
      if (s == SKY_SHOWERS) sun(c, x - 7, y - 9, 4);
      cloud(c, x + (s == SKY_SHOWERS) * 2, y - 5, white);
      for (int i = -1; i <= 1; i++) c.drawWideLine(x + i * 7 + 2, y + 6, x + i * 7 - 1, y + 13, 1.0f, blue);
      break;
    case SKY_SNOW: case SKY_SNOWSHOWERS:
      if (s == SKY_SNOWSHOWERS) sun(c, x - 7, y - 9, 4);
      cloud(c, x + (s == SKY_SNOWSHOWERS) * 2, y - 5, white);
      for (int i = -1; i <= 1; i++) c.fillCircle(x + i * 7, y + 8 + (i == 0) * 5, 2, white);
      break;
    case SKY_THUNDER: {
      cloud(c, x, y - 6, grey);
      uint16_t bolt = rgb(255, 210, 50);
      c.fillTriangle(x + 2, y + 2, x - 4, y + 10, x + 1, y + 10, bolt);
      c.fillTriangle(x - 1, y + 8, x + 4, y + 8, x - 3, y + 16, bolt);
      break;
    }
  }
}

// ---------------- Seasonal particles ----------------
enum Fx : uint8_t { FX_NONE, FX_SNOW, FX_RAIN, FX_PETALS, FX_FIREFLIES, FX_LEAVES, FX_SPARKLE };
struct Particle { float x, y, v, ph, s; uint16_t col; float vx, vy; };
static constexpr int NP_MAX = 24;
static Particle ps[NP_MAX];
static int np = 0;
static Fx fx = FX_NONE;
static bool lights = false;   // winter Christmas lights
static float snowLayer[W / 4];   // settled snow height per 4-px column; melts slowly
static float gx = 0, gy = 1;     // gravity direction in screen space (from the IMU roll)

static void spawn(Particle& p, bool anywhere) {
  p.ph = frand(0, TAU);
  p.vx = p.vy = 0;
  switch (fx) {
    case FX_FIREFLIES:
      p.x = frand(10, W - 10); p.y = frand(40, H - 10); p.s = frand(3, 5);
      p.vx = frand(-10, 10); p.vy = frand(-10, 10); p.col = rgb(200, 255, 90);
      return;
    case FX_SPARKLE:
      p.x = frand(8, W - 8); p.y = frand(8, H - 8); p.s = frand(5, 10);
      p.v = frand(0.35f, 0.8f); p.ph = anywhere ? frand(0, 1) : 0; p.col = rgb(190, 220, 255);
      return;
    case FX_SNOW:
      p.s = frand(2, 5); p.v = frand(14, 24) * (0.5f + p.s * 0.2f);
      p.col = dim(rgb(235, 242, 255), frand(0.65f, 1.0f));
      break;
    case FX_RAIN:
      p.s = frand(12, 20); p.v = frand(170, 230);
      p.col = dim(rgb(110, 170, 255), frand(0.6f, 0.95f));
      break;
    case FX_PETALS:
      p.s = frand(4, 7); p.v = frand(16, 28);
      p.col = dim(frand(0, 1) < 0.5f ? rgb(255, 140, 190) : rgb(255, 200, 225), frand(0.7f, 1.0f));
      break;
    case FX_LEAVES: {
      static const uint16_t pal[] = {rgb(230, 120, 30), rgb(200, 60, 40), rgb(220, 175, 40), rgb(170, 90, 40)};
      p.s = frand(5, 8.5f); p.v = frand(20, 32);
      p.col = dim(pal[esp_random() % 4], frand(0.75f, 1.0f));
      break;
    }
    default: return;
  }
  if (anywhere) { p.x = frand(0, W); p.y = frand(0, H); return; }
  // Enter from the "uphill" edge so particles keep falling toward real gravity when tilted.
  float along = frand(-175, 175);
  p.x = W / 2 - gx * 175 - gy * along;
  p.y = H / 2 - gy * 175 + gx * along;
}

static void setFx(Fx want) {
  if (want == fx) return;
  fx = want;
  np = fx == FX_SNOW ? 22 : fx == FX_RAIN ? 20 : fx == FX_PETALS ? 14 : fx == FX_LEAVES ? 12 :
       fx == FX_FIREFLIES ? 12 : fx == FX_SPARKLE ? 10 : 0;
  for (int i = 0; i < np; i++) spawn(ps[i], true);
}

// pref "fx": 0 auto (season from the date, weather overrides), 1 off, 2 winter, 3 rain, 4 spring,
// 5 summer, 6 fall, 7 winter without snow.
static void chooseFx(uint8_t pref, const struct tm* tm) {
  Fx want = FX_NONE;
  bool orn = false;
  switch (pref) {
    case 1: break;
    case 2: want = FX_SNOW; orn = true; break;
    case 3: want = FX_RAIN; break;
    case 4: want = FX_PETALS; break;
    case 5: want = FX_FIREFLIES; break;
    case 6: want = FX_LEAVES; break;
    case 7: want = FX_SPARKLE; orn = true; break;
    default:
      if (!tm) break;
      int m = tm->tm_mon;
      bool winter = m == 11 || m <= 1;
      want = winter ? FX_SPARKLE : m <= 4 ? FX_PETALS : m <= 7 ? FX_FIREFLIES : FX_LEAVES;
      orn = winter;
      if (weather.ok) {   // real snow only when it's actually snowing
        Sky s = skyOf(weather.code);
        if (s == SKY_SNOW || s == SKY_SNOWSHOWERS) want = FX_SNOW;
        else if (s == SKY_RAIN || s == SKY_SHOWERS || s == SKY_DRIZZLE || s == SKY_THUNDER) want = FX_RAIN;
      }
  }
  if (!CLOCK_FX) { want = FX_NONE; orn = false; }
  lights = orn;
  setFx(want);
}

static void stepParticles(LGFX_Sprite& c, float dt, float t) {
  for (int i = 0; i < np; i++) {
    Particle& p = ps[i];
    switch (fx) {
      case FX_SNOW: case FX_PETALS: case FX_LEAVES: {
        float sway = sinf(t * (fx == FX_SNOW ? 1.3f : 1.0f) + p.ph) * (fx == FX_SNOW ? 14 : 28);
        p.x += (gx * p.v + gy * sway) * dt;   // fall along gravity, sway across it
        p.y += (gy * p.v - gx * sway) * dt;
        if (fx == FX_SNOW && p.x >= 0 && p.x < W) {
          int col = (int)p.x / 4;
          if (p.y >= H - snowLayer[col]) {   // landed: pile up, spilling into lower neighbours
            snowLayer[col] = min(snowLayer[col] + p.s * 0.3f, 12.0f);
            for (int n : {col - 1, col + 1})
              if (n >= 0 && n < W / 4 && snowLayer[col] - snowLayer[n] > 2) { snowLayer[col] -= 0.5f; snowLayer[n] += 0.5f; }
            spawn(p, false);
            continue;
          }
        }
        break;
      }
      case FX_RAIN:
        p.x += gx * p.v * dt; p.y += gy * p.v * dt;
        break;
      case FX_FIREFLIES:
        p.vx += frand(-30, 30) * dt + gx * 2 * dt; p.vy += frand(-30, 30) * dt;
        p.vx = constrain(p.vx, -14.0f, 14.0f); p.vy = constrain(p.vy, -14.0f, 14.0f);
        p.x += p.vx * dt; p.y += p.vy * dt;
        if (p.x < 6 || p.x > W - 6) p.vx = -p.vx;
        if (p.y < 36 || p.y > H - 6) p.vy = -p.vy;
        p.x = constrain(p.x, 6.0f, W - 6.0f); p.y = constrain(p.y, 36.0f, H - 6.0f);
        break;
      case FX_SPARKLE:
        p.ph += p.v * dt;
        if (p.ph >= 1) spawn(p, false);
        break;
      default: break;
    }
    if (fx != FX_FIREFLIES && fx != FX_SPARKLE && (p.x < -80 || p.x > W + 80 || p.y < -90 || p.y > H + 90)) spawn(p, false);

    int x = (int)p.x, y = (int)p.y;
    switch (fx) {
      case FX_SNOW:
        if (p.s > 3.5f) c.fillSmoothCircle(x, y, (int)p.s + 2, dim(p.col, 0.18f));   // soft halo on big flakes
        c.fillSmoothCircle(x, y, (int)(p.s * 0.7f + 0.5f), p.col);
        break;
      case FX_RAIN:
        c.drawWideLine(x, y, (int)(p.x - gx * p.s), (int)(p.y - gy * p.s), 1.0f, p.col);
        break;
      case FX_PETALS: {
        float a = t * 2.2f + p.ph;
        c.fillEllipse(x, y, max(1, (int)(p.s * (0.35f + 0.65f * fabsf(cosf(a))) + 0.5f)), max(1, (int)(p.s * 0.7f + 0.5f)), p.col);
        break;
      }
      case FX_LEAVES: {
        float a = t * 1.6f + p.ph, ax = cosf(a) * p.s * 1.7f, ay = sinf(a) * p.s * 1.7f;
        float wx = -sinf(a) * p.s * 0.75f, wy = cosf(a) * p.s * 0.75f;
        c.fillTriangle(x + ax, y + ay, x + wx, y + wy, x - ax, y - ay, p.col);
        c.fillTriangle(x + ax, y + ay, x - wx, y - wy, x - ax, y - ay, p.col);
        c.drawLine(x + ax, y + ay, x - ax, y - ay, dim(p.col, 0.55f));
        break;
      }
      case FX_FIREFLIES: {
        float b = sinf(t * 1.8f + p.ph);
        b = b > 0 ? b * b * b : 0;
        if (b < 0.05f) break;
        if (b > 0.25f) {
          c.fillSmoothCircle(x, y, (int)(p.s * 2.6f), dim(p.col, b * 0.18f));
          c.fillSmoothCircle(x, y, (int)(p.s * 1.6f), dim(p.col, b * 0.35f));
        }
        c.fillSmoothCircle(x, y, (int)(p.s * 0.8f + 0.5f), dim(p.col, b));
        break;
      }
      case FX_SPARKLE: {
        float b = sinf(p.ph * 3.14159f);
        int l = (int)(p.s * b);
        uint16_t col = dim(p.col, b);
        c.drawWideLine(x - l, y, x + l, y, 1.0f, col);
        c.drawWideLine(x, y - l, x, y + l, 1.0f, col);
        int d = l * 2 / 5;   // short diagonals for a star shape
        c.drawLine(x - d, y - d, x + d, y + d, dim(col, 0.6f));
        c.drawLine(x - d, y + d, x + d, y - d, dim(col, 0.6f));
        c.fillSmoothCircle(x, y, 2, dim(rgb(255, 255, 255), b));
        break;
      }
      default: break;
    }
  }

  bool any = false;
  for (float& h : snowLayer) {
    h = max(0.0f, h - dt * (fx == FX_SNOW ? 0.03f : 0.09f));   // melts slowly, faster once it stops snowing
    any |= h >= 1;
  }
  if (any) {
    uint16_t col = rgb(185, 195, 215);
    for (int i = 0; i < W / 4; i++) {
      int h = (int)snowLayer[i];
      if (h > 0) c.fillRect(i * 4, H - h, 4, h, col);
    }
  }
}

// ---------------- Christmas lights (winter) ----------------
// A wire drooping in two swoops across the top; bulbs hang off it toward real gravity and flash
// in a pattern that changes every 6 s: twinkle, chase, alternate, all pulsing together.
static constexpr int NB = 13;
static const uint16_t BULB[] = {rgb(255, 40, 50), rgb(40, 230, 80), rgb(60, 120, 255), rgb(255, 190, 30), rgb(230, 60, 220)};
static float bulbOn[NB];

static float wireY(float x) { return 3 + 13 * fabsf(sinf(x * 3.14159f / 120)); }   // two swoops, 13 px sag

static void stepLights(LGFX_Sprite& c, float dt, float t) {
  const uint16_t wire = rgb(30, 80, 40);
  for (int x = 0; x < W; x += 4) c.drawLine(x, wireY(x), x + 4, wireY(x + 4), wire);
  int mode = (int)(t / 6) % 4, step = (int)(t * 6);
  for (int i = 0; i < NB; i++) {
    float target;
    switch (mode) {
      case 0: target = sinf(t * 2.3f + i * 2.17f) > 0.2f; break;      // twinkle
      case 1: target = (step - i + 400) % 4 == 0; break;              // chase
      case 2: target = ((int)(t * 1.5f) + i) % 2; break;              // alternate
      default: target = 0.55f + 0.45f * sinf(t * 3); break;          // pulse together
    }
    bulbOn[i] += (target - bulbOn[i]) * min(1.0f, dt * 14);           // quick fade, not a hard cut
    float x = 9 + i * (W - 18) / (NB - 1.0f), y = wireY(x);
    int bx = x + gx * 7, by = y + gy * 7;                             // bulb hangs off the wire
    uint16_t col = BULB[i % 5];
    float on = bulbOn[i];
    c.drawLine(x, y, bx, by, wire);
    if (on > 0.3f) {
      c.fillSmoothCircle(bx, by, 13, dim(col, 0.10f * on));
      c.fillSmoothCircle(bx, by, 9, dim(col, 0.22f * on));
    }
    c.fillEllipse(bx, by + (int)(gy * 2), 4, 5, dim(col, 0.18f + 0.82f * on));
    c.fillSmoothCircle(bx - 1, by - 1, 1, dim(rgb(255, 255, 255), 0.7f * on));
  }
}

// ---------------- The clock ----------------
static LGFX_Sprite tsp;                  // glowing time, re-rendered only when the minute changes
static constexpr int TS_Y = 66, TS_H = 84, TS_BASE = 68;   // sprite top on screen, height, text baseline
static const uint16_t C_TIME = rgb(225, 242, 255), C_GLOW = rgb(0, 150, 255), C_ACC = rgb(0, 175, 255);

struct ClockApp : App {
  bool dirty = true;
  int shownMin = -1, lastSec = -1, colonX = W / 2;
  uint32_t secAt = 0, lastMs = 0;
  uint8_t fxPref = 0;
  char date[24] = "", ampm[3] = "";

  const char* name() const override { return "Clock & Weather"; }
  void begin() override { fxPref = prefs.getUChar("fx", 0); }
  void enter() override { lastMs = 0; dirty = true; }
  void forceRedraw() override { dirty = true; fxPref = prefs.getUChar("fx", 0); }

  void renderTime(const struct tm& tm) {
    if (!tsp.getBuffer()) { tsp.setColorDepth(16); tsp.setPsram(true); tsp.createSprite(W, TS_H); }
    char hs[3], ms[3];
    snprintf(hs, sizeof hs, "%d", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12);
    snprintf(ms, sizeof ms, "%02d", tm.tm_min);
    tsp.fillScreen(TFT_BLACK);
    tsp.setFont(&fonts::DejaVu72);
    tsp.setTextDatum(textdatum_t::baseline_left);
    const int gap = 24, wh = tsp.textWidth(hs), wm = tsp.textWidth(ms), x0 = (W - (wh + gap + wm)) / 2;
    colonX = x0 + wh + gap / 2;
    // Glow: the digits drawn dim and fat underneath, then bright on top.
    for (int pass = 0; pass < 3; pass++) {
      int r = 2 - pass;
      tsp.setTextColor(pass == 0 ? dim(C_GLOW, 0.22f) : pass == 1 ? dim(C_GLOW, 0.45f) : C_TIME);
      for (int dx = -r; dx <= r; dx++)
        for (int dy = -r; dy <= r; dy++) {
          if (r && abs(dx) != r && abs(dy) != r) continue;   // ring only
          tsp.drawString(hs, x0 + dx, TS_BASE + dy);
          tsp.drawString(ms, colonX + gap / 2 + dx, TS_BASE + dy);
        }
    }
    size_t n = strftime(date, sizeof date, "%a, %b ", &tm);
    snprintf(date + n, sizeof date - n, "%d", tm.tm_mday);
    strcpy(ampm, tm.tm_hour < 12 ? "AM" : "PM");
    shownMin = tm.tm_min;
  }

  void drawTime(LGFX_Sprite& c, const struct tm& tm, uint32_t ms) {
    if (dirty || tm.tm_min != shownMin) renderTime(tm);
    if (tm.tm_sec != lastSec) { lastSec = tm.tm_sec; secAt = ms; }
    float frac = min((ms - secAt) / 1000.0f, 0.999f);
    tsp.pushSprite(&c, 0, TS_Y, (uint16_t)TFT_BLACK);   // black = transparent, particles show through

    // Colon: two dots that flash on each second and fade.
    float k = 1.0f - 0.65f * frac;
    int cy = TS_Y + TS_BASE - 26;
    for (int s : {-11, 11}) {
      c.fillSmoothCircle(colonX, cy + s, 6, dim(C_GLOW, 0.3f * k));
      c.fillSmoothCircle(colonX, cy + s, 4, dim(C_TIME, k));
    }
    // Seconds: a slow sweep under the time.
    const int bx = 50, bw = 140, by = TS_Y + TS_H + 6;
    int fill = (int)(bw * (tm.tm_sec + frac) / 60.0f);
    c.drawFastHLine(bx, by, bw, rgb(35, 45, 60));
    c.fillRect(bx, by - 1, fill, 3, dim(C_ACC, 0.8f));
    c.fillSmoothCircle(bx + fill, by, 3, C_TIME);

    c.setFont(&fonts::DejaVu18);
    c.setTextDatum(textdatum_t::middle_center);
    c.setTextColor(rgb(110, 150, 200));
    c.drawString(ampm, W / 2, by + 20);
    c.setTextColor(rgb(175, 178, 190));
    c.drawString(date, W / 2, 214);
  }

  void drawNoTime(LGFX_Sprite& c) {
    c.setTextDatum(textdatum_t::middle_center);
    c.setFont(&fonts::DejaVu56);
    c.setTextColor(rgb(70, 75, 90));
    c.drawString("--:--", W / 2, 108);
    c.setFont(&fonts::DejaVu18);
    c.setTextColor(rgb(170, 175, 190));
    c.drawString(wifiStatus(), W / 2, 162);
    c.setTextColor(rgb(110, 115, 130));
    c.drawString("Set Wi-Fi on the", W / 2, 192);
    c.drawString("control page", W / 2, 212);
    shownMin = -1;
  }

  void drawStrip(LGFX_Sprite& c) {
    const int y = 34;
    char buf[8];
    c.setFont(&fonts::DejaVu24);
    c.setTextDatum(textdatum_t::middle_left);
    bool ok = weather.ok;
    if (ok) snprintf(buf, sizeof buf, "%d", (int)lroundf(weather.tempF));
    else strcpy(buf, "--");
    uint16_t col = ok ? rgb(255, 190, 90) : rgb(90, 95, 110);
    int tw = c.textWidth(buf), iconW = ok ? 38 : 0, gw = iconW + tw + 10, x0 = (W - gw) / 2;
    if (ok) drawIcon(c, skyOf(weather.code), x0 + 15, y);
    c.setTextColor(col);
    c.drawString(buf, x0 + iconW, y + 1);
    int dx = x0 + iconW + tw + 5;   // degree sign (not in the font)
    c.drawCircle(dx, y - 7, 3, col);
    c.drawCircle(dx, y - 7, 2, col);
  }

  bool draw(LGFX_Sprite& c) override {
    uint32_t ms = millis();
    float dt = lastMs ? min((ms - lastMs) / 1000.0f, 0.1f) : 0;
    lastMs = ms;
    float t = (ms % 3600000) / 1000.0f;
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    bool tv = timeValid();

    float r = imuRollDeg() * CLOCK_TILT_SIGN * DEG_TO_RAD;
    gx = sinf(r); gy = cosf(r);
    chooseFx(fxPref, tv ? &tm : nullptr);

    c.fillScreen(TFT_BLACK);
    stepParticles(c, dt, t);
    if (lights) stepLights(c, dt, t);
    if (tv) drawTime(c, tm, ms); else drawNoTime(c);
    drawStrip(c);
    dirty = false;
    return true;
  }
};

static ClockApp clockAppInst;
extern App* const CLOCK_APP = &clockAppInst;
