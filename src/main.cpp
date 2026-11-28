#include <LittleFS.h>
#include <WiFi.h>
#include "core.h"

LGFX lcd;
LGFX_Sprite canvas(&lcd);
Preferences prefs;

static int cur = 0;
static uint32_t qrUntil = 0;
static uint32_t lastDraw = 0;
int shiftX = LCD_SHIFT_X, shiftY = LCD_SHIFT_Y;

int activeApp() { return cur; }

void setShift(int x, int y) {
  shiftX = constrain(x, -LCD_SHIFT_MAX, LCD_SHIFT_MAX);
  shiftY = constrain(y, -LCD_SHIFT_MAX, LCD_SHIFT_MAX);
  prefs.putShort("sx", shiftX);
  prefs.putShort("sy", shiftY);
  showAlign();
}

void showAlign() {
  qrUntil = millis() + QR_SHOW_MS;
  int w = canvas.width(), h = canvas.height(), cx = w / 2, cy = h / 2;
  canvas.fillScreen(TFT_BLACK);
  for (int i = 0; i < 3; i++) canvas.drawRect(i, i, w - 2 * i, h - 2 * i, i ? TFT_DARKGREY : TFT_WHITE);
  for (int r = 20; r <= 100; r += 20) canvas.drawCircle(cx, cy, r, r == 100 ? TFT_CYAN : 0x2945);
  canvas.drawFastHLine(0, cy, w, TFT_RED);
  canvas.drawFastVLine(cx, 0, h, TFT_RED);
  canvas.setTextColor(TFT_YELLOW);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setFont(&fonts::Font2);
  canvas.drawString("ALIGN", cx, cy - 34);
  char s[32]; snprintf(s, sizeof s, "X %+d  Y %+d", shiftX, shiftY);
  canvas.drawString(s, cx, cy + 34);
  lcd.fillScreen(TFT_BLACK);   // clear the strip the shift exposes
  canvas.pushSprite(shiftX, shiftY);
}

static void recreateCanvas() {
  canvas.deleteSprite();
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  canvas.createSprite(lcd.width(), lcd.height());
}

void setRotationCfg(uint8_t r) {
  lcd.setRotation(r & 7);
  recreateCanvas();
  lcd.fillScreen(TFT_BLACK);
  APPS[cur]->forceRedraw();
}

void switchApp(int i) {
  i = (i % APP_COUNT + APP_COUNT) % APP_COUNT;
  qrUntil = 0;
  APPS[cur]->exit();
  cur = i;
  prefs.putUChar("app", cur);
  lcd.fillScreen(TFT_BLACK);
  APPS[cur]->enter();
  APPS[cur]->forceRedraw();
  Serial.printf("app -> %s\n", APPS[cur]->name());
  linkBroadcastState();
}

void showQr() {
  qrUntil = millis() + QR_SHOW_MS;
  String url = prefs.getString("url", DEFAULT_URL);
  canvas.fillScreen(TFT_WHITE);
  int s = min(canvas.width(), canvas.height()) - 50;
  canvas.qrcode(url.c_str(), (canvas.width() - s) / 2, 8, s, 6);
  canvas.setTextColor(TFT_BLACK);
  canvas.setTextDatum(textdatum_t::bottom_center);
  canvas.setFont(&fonts::Font2);
  canvas.drawString("Scan for control page", canvas.width() / 2, canvas.height() - 22);
  canvas.drawString(WiFi.isConnected() ? WiFi.localIP().toString() : "MAC " + WiFi.macAddress(),
                    canvas.width() / 2, canvas.height() - 4);
  canvas.pushSprite(shiftX, shiftY);
  Serial.println("pickup -> QR");
}

void setup() {
  Serial.setRxBufferSize(8192);
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  // Must be > 0: in this core a 0 timeout underflows a retry counter and write() hangs forever
  // once the PC stops reading. 20 ms lets it give up quickly and drop output instead.
  Serial.setTxTimeoutMs(20);
  prefs.begin("cube", false);
  shiftX = prefs.getShort("sx", LCD_SHIFT_X);
  shiftY = prefs.getShort("sy", LCD_SHIFT_Y);
  lcd.init();
  lcd.setBrightness(255);
  setRotationCfg(prefs.getUChar("rot", DEFAULT_ROT));
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.drawString("HoloCube", lcd.width() / 2, lcd.height() / 2, &fonts::FreeSansBold12pt7b);

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  imuBegin();   // prints its own status and keeps retrying in the background

  netBegin();   // prints Wi-Fi MAC for NC State Nomad registration
  linkBegin();
  for (int i = 0; i < APP_COUNT; i++) APPS[i]->begin();
  cur = prefs.getUChar("app", 0) % APP_COUNT;
  lcd.fillScreen(TFT_BLACK);
  APPS[cur]->enter();
}

void loop() {
  uint32_t now = millis();
  linkPoll();

  switch (imuNextGesture()) {   // IMU runs in its own 100 Hz task, independent of drawing
    case G_RIGHT:  switchApp(cur + 1); break;
    case G_LEFT:   switchApp(cur - 1); break;
    case G_PICKUP: showQr(); break;
    default: break;
  }

  if (qrUntil) {
    if ((int32_t)(now - qrUntil) < 0) return;
    qrUntil = 0;
    lcd.fillScreen(TFT_BLACK);
    APPS[cur]->forceRedraw();
  }

  App* a = APPS[cur];
  a->update(now);
  if (!a->direct() && now - lastDraw >= 33) {
    lastDraw = now;
    if (a->draw(canvas)) canvas.pushSprite(shiftX, shiftY);
  }
}
