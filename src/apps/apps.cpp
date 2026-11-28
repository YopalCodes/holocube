// Built-in apps + registry. To add an app: write a class deriving from App, add it to APPS below.
#include <WiFi.h>
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include "../core.h"

static void centerText(LGFX_Sprite& c, const String& s, int y, const lgfx::IFont* f, uint16_t col) {
  c.setFont(f);
  c.setTextColor(col);
  c.setTextDatum(textdatum_t::middle_center);
  c.drawString(s, c.width() / 2, y);
}

// Clock & Weather lives in clock.cpp
extern App* const CLOCK_APP;

// ---------------- Countdown (Eastern time) ----------------
struct CountdownApp : App {
  time_t shown = 0;
  bool dirty = true;
  const char* name() const override { return "TXT Countdown"; }
  void forceRedraw() override { dirty = true; }
  bool draw(LGFX_Sprite& c) override {
    time_t now = time(nullptr);
    if (now == shown && !dirty) return false;
    shown = now; dirty = false;
    c.fillScreen(TFT_BLACK);
    int h = c.height();
    String name = prefs.getString("evn", "TXT");
    String evt = prefs.getString("evt", "");   // "YYYY-MM-DDTHH:MM" Eastern
    centerText(c, name, 28, &fonts::FreeSansBold12pt7b, 0x9CDF);
    struct tm tm = {};
    if (evt.length() < 16 || sscanf(evt.c_str(), "%d-%d-%dT%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                                    &tm.tm_hour, &tm.tm_min) != 5) {
      if (prefs.getString("art", "").length()) {   // tracking an artist, but no upcoming show (yet)
        const char* st = concertStatus();
        centerText(c, *st ? st : "No dates announced yet", h / 2, &fonts::Font2, TFT_LIGHTGREY);
        centerText(c, "Checks for new dates every hour", h / 2 + 22, &fonts::Font2, TFT_DARKGREY);
      } else {
        centerText(c, "Set concert date", h / 2, &fonts::Font4, TFT_LIGHTGREY);
        centerText(c, "on the control page", h / 2 + 26, &fonts::Font2, TFT_DARKGREY);
      }
      return true;
    }
    if (!timeValid()) { centerText(c, "Waiting for time sync", h / 2, &fonts::Font2, TFT_DARKGREY); return true; }
    tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_isdst = -1;
    long left = (long)difftime(mktime(&tm), now);   // TZ is America/New_York
    char buf[32];
    if (left <= 0) {
      centerText(c, left > -6 * 3600 ? "SHOW TIME!" : "Update the date", h / 2, &fonts::FreeSansBold18pt7b, TFT_MAGENTA);
      return true;
    }
    snprintf(buf, sizeof buf, "%ld", left / 86400);
    centerText(c, buf, h / 2 - 22, &fonts::Font7, TFT_WHITE);
    centerText(c, left / 86400 == 1 ? "day" : "days", h / 2 + 22, &fonts::Font4, 0x9CDF);
    snprintf(buf, sizeof buf, "%02ld:%02ld:%02ld", (left % 86400) / 3600, (left % 3600) / 60, left % 60);
    centerText(c, buf, h / 2 + 60, &fonts::Font4, TFT_CYAN);
    centerText(c, evt.substring(0, 10) + " " + evt.substring(11) + " ET", h - 18, &fonts::Font2, TFT_DARKGREY);
    return true;
  }
};

// ---------------- GIF player (plays from PSRAM, draws straight to LCD) ----------------
static AnimatedGIF gif;
static int gifX, gifY;
static void gifDraw(GIFDRAW* p) {
  static uint16_t line[480];
  int w = min(p->iWidth, (int)min(lcd.width(), 480));
  int y = gifY + shiftY + p->iY + p->y;
  if (y < 0 || y >= lcd.height()) return;
  uint8_t* s = p->pPixels;
  uint16_t* pal = p->pPalette;
  if (p->ucDisposalMethod == 2) {
    for (int x = 0; x < w; x++) if (s[x] == p->ucTransparent) s[x] = p->ucBackground;
    p->ucHasTransparency = 0;
  }
  int x0 = gifX + shiftX + p->iX;
  if (!p->ucHasTransparency) {
    for (int x = 0; x < w; x++) line[x] = pal[s[x]];
    lcd.pushImage(x0, y, w, 1, line);
    return;
  }
  int x = 0;
  while (x < w) {  // push runs of opaque pixels
    while (x < w && s[x] == p->ucTransparent) x++;
    int st = x, n = 0;
    while (x < w && s[x] != p->ucTransparent) line[n++] = pal[s[x++]];
    if (n) lcd.pushImage(x0 + st, y, n, 1, line);
  }
}

struct GifApp : App {
  uint8_t* buf = nullptr;
  size_t len = 0;
  bool opened = false, stale = true, active = false;
  uint32_t nextAt = 0;
  const char* name() const override { return "GIF Player"; }
  bool direct() const override { return true; }
  void begin() override { gif.begin(BIG_ENDIAN_PIXELS); }  // LovyanGFX treats uint16_t* pixels as byte-swapped RGB565
  void reload() { stale = true; if (active) { lcd.fillScreen(TFT_BLACK); load(); forceRedraw(); } }
  void load() {
    stale = false;
    if (opened) { gif.close(); opened = false; }
    free(buf); buf = nullptr; len = 0;
    File f = LittleFS.open(GIF_PATH, "r");
    if (!f) return;
    len = f.size();
    buf = (uint8_t*)ps_malloc(len);
    if (!buf || f.read(buf, len) != len) { free(buf); buf = nullptr; len = 0; return; }
    if (gif.open(buf, len, gifDraw)) {
      opened = true;
      gifX = (lcd.width() - gif.getCanvasWidth()) / 2;
      gifY = (lcd.height() - gif.getCanvasHeight()) / 2;
    }
  }
  void enter() override {
    active = true;
    if (stale || !opened) load();
    else gif.reset();
    nextAt = 0;
    forceRedraw();
  }
  void exit() override { active = false; }
  void forceRedraw() override {
    if (opened) { gif.reset(); nextAt = 0; return; }
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_LIGHTGREY);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.drawString("No GIF yet", lcd.width() / 2, lcd.height() / 2 - 12, &fonts::Font4);
    lcd.drawString("Upload one from the control page", lcd.width() / 2, lcd.height() / 2 + 16, &fonts::Font2);
  }
  void update(uint32_t now) override {
    if (stale) { lcd.fillScreen(TFT_BLACK); load(); forceRedraw(); }
    if (!opened || (int32_t)(now - nextAt) < 0) return;
    int d = 0;
    lcd.startWrite();
    int r = gif.playFrame(false, &d);
    lcd.endWrite();
    if (r <= 0) gif.reset();
    nextAt = now + max(d, 20);
  }
};

static GifApp gifApp;
static CountdownApp countdownApp;

void gifReload() { gifApp.reload(); }
void gifUnload() {
  if (gifApp.opened) { gif.close(); gifApp.opened = false; }
  free(gifApp.buf); gifApp.buf = nullptr; gifApp.len = 0;
  gifApp.stale = true;
}

App* const APPS[] = {CLOCK_APP,&gifApp, &countdownApp};
const int APP_COUNT = sizeof(APPS) / sizeof(APPS[0]);
