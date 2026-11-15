#include <LittleFS.h>
#include <WiFi.h>
#include "core.h"

LGFX lcd;
LGFX_Sprite canvas(&lcd);
Preferences prefs;

static int cur = 0;
static uint32_t qrUntil = 0;
static uint32_t lastImu = 0, lastDraw = 0;

int activeApp() { return cur; }

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
  canvas.pushSprite(0, 0);
  Serial.println("pickup -> QR");
}

void setup() {
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never block when no USB host
  prefs.begin("cube", false);
  lcd.init();
  lcd.setBrightness(255);
  setRotationCfg(prefs.getUChar("rot", 0));
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.drawString("HoloCube", lcd.width() / 2, lcd.height() / 2, &fonts::FreeSansBold12pt7b);

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  if (!imuBegin()) Serial.println("MPU6050 not found");

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

  if (now - lastImu >= 10) {
    lastImu = now;
    switch (imuPoll(now)) {
      case G_RIGHT:  switchApp(cur + 1); break;
      case G_LEFT:   switchApp(cur - 1); break;
      case G_PICKUP: showQr(); break;
      default: break;
    }
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
    if (a->draw(canvas)) canvas.pushSprite(0, 0);
  }
}
