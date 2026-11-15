#pragma once
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Preferences.h>
#include "config.h"

// ---------- Display ----------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;
 public:
  LGFX() {
    auto b = bus_.config();
    b.spi_host = SPI2_HOST;
    b.spi_mode = (LCD_CS < 0) ? 3 : 0;
    b.freq_write = LCD_SPI_HZ;
    b.pin_sclk = LCD_SCK; b.pin_mosi = LCD_MOSI; b.pin_miso = -1; b.pin_dc = LCD_DC;
    b.dma_channel = SPI_DMA_CH_AUTO;
    bus_.config(b);
    panel_.setBus(&bus_);
    auto p = panel_.config();
    p.pin_cs = LCD_CS; p.pin_rst = LCD_RST; p.pin_busy = -1;
    p.panel_width = LCD_W; p.panel_height = LCD_H;
    p.memory_width = 240; p.memory_height = 320;
    p.offset_y = LCD_OFFSET_Y;
    p.invert = LCD_INVERT; p.readable = false; p.bus_shared = false;
    panel_.config(p);
    if (LCD_BL >= 0) {
      auto l = light_.config();
      l.pin_bl = LCD_BL; l.freq = 12000; l.pwm_channel = 7;
      light_.config(l);
      panel_.setLight(&light_);
    }
    setPanel(&panel_);
  }
};

extern LGFX lcd;
extern LGFX_Sprite canvas;   // full-screen PSRAM frame buffer for flicker-free apps
extern Preferences prefs;    // NVS, namespace "cube"

void setRotationCfg(uint8_t r);  // 0-3 normal, 4-7 mirrored (for beam-splitter reflection)

// ---------- App framework ----------
// To add an app: subclass App in src/apps/, then add it to the APPS list in apps.cpp.
struct App {
  virtual const char* name() const = 0;
  virtual void begin() {}                    // once at boot
  virtual void enter() {}                    // became active
  virtual void exit() {}                     // leaving
  virtual void update(uint32_t now) {}       // every loop
  virtual bool draw(LGFX_Sprite& c) { return false; }  // draw to canvas; return true to push
  virtual bool direct() const { return false; }        // true: app draws straight to lcd in update()
  virtual void forceRedraw() {}
};
extern App* const APPS[];
extern const int APP_COUNT;
int activeApp();
void switchApp(int i);
void showQr();

// ---------- Networking ----------
struct Weather { bool ok; float tempF; float windMph; int code; uint32_t at; };
extern Weather weather;
void netBegin();
void netReconnect();
bool timeValid();
String wifiStatus();
const char* weatherText(int code);

// ---------- Link (USB serial + BLE) ----------
void linkBegin();
void linkPoll();
void linkBroadcastState();

// ---------- IMU ----------
enum Gesture { G_NONE, G_RIGHT, G_LEFT, G_PICKUP };
bool imuBegin();                 // also starts the 100 Hz IMU task
Gesture imuNextGesture();        // non-blocking; G_NONE if nothing new
void imuSetStream(bool on);      // raw stream for calibration; auto-expires 3 s after last refresh

// ---------- GIF ----------
void gifReload();
