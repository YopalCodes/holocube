// JSON-lines control protocol shared by USB serial (Web Serial) and BLE UART (Web Bluetooth).
// Device -> page lines always start with '{'; log lines never do.
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <mbedtls/base64.h>
#include <freertos/stream_buffer.h>
#include "core.h"

#define SVC_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_UUID  "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define TX_UUID  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

enum Port { P_SERIAL, P_BLE };
static NimBLECharacteristic* txChr;
static volatile bool bleConnected = false;
static volatile bool stateDirty = false;
static StreamBufferHandle_t bleRx;
static String serialLine, bleLine;
static File upload;
static size_t uploadSize = 0, uploadGot = 0;

struct RxCb : NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string v = c->getValue();
    xStreamBufferSend(bleRx, v.data(), v.size(), pdMS_TO_TICKS(50));
  }
};
struct SrvCb : NimBLEServerCallbacks {
  void onConnect(NimBLEServer*) override { bleConnected = true; stateDirty = true; }
  void onDisconnect(NimBLEServer*) override { bleConnected = false; NimBLEDevice::startAdvertising(); }
};

static void send(Port p, const String& s) {
  if (p == P_SERIAL) { Serial.println(s); return; }
  if (!bleConnected) return;
  String line = s + "\n";
  for (size_t i = 0; i < line.length(); i += 180) {
    size_t n = min((size_t)180, line.length() - i);
    txChr->setValue((const uint8_t*)line.c_str() + i, n);
    txChr->notify();
    delay(4);
  }
}

static String stateJson() {
  JsonDocument d;
  d["type"] = "state";
  JsonArray a = d["apps"].to<JsonArray>();
  for (int i = 0; i < APP_COUNT; i++) a.add(APPS[i]->name());
  d["active"] = activeApp();
  d["wifi"] = wifiStatus();
  d["ssid"] = prefs.getString("ssid", "");
  d["mac"] = WiFi.macAddress();
  d["rot"] = prefs.getUChar("rot", 0);
  d["url"] = prefs.getString("url", DEFAULT_URL);
  d["lat"] = prefs.getFloat("lat", DEFAULT_LAT);
  d["lon"] = prefs.getFloat("lon", DEFAULT_LON);
  d["evn"] = prefs.getString("evn", "TXT");
  d["evt"] = prefs.getString("evt", "");
  d["gif"] = LittleFS.exists(GIF_PATH) ? (int)LittleFS.open(GIF_PATH).size() : 0;
  d["fsFree"] = (int)(LittleFS.totalBytes() - LittleFS.usedBytes());
  String s; serializeJson(d, s);
  return s;
}

void linkBroadcastState() { stateDirty = true; }

static String reply(const char* type, const char* msg, long n = -1) {
  JsonDocument d; d["type"] = type; d["msg"] = msg; if (n >= 0) d["n"] = n;
  String s; serializeJson(d, s); return s;
}

static void handle(Port p, String& line) {
  JsonDocument d;
  if (deserializeJson(d, line)) { send(p, reply("err", "bad json")); return; }
  String cmd = d["cmd"] | "";

  if (cmd == "info") { send(p, stateJson()); return; }
  if (cmd == "app") { switchApp(d["i"] | 0); send(p, stateJson()); return; }
  if (cmd == "qr") { showQr(); send(p, reply("ok", "qr")); return; }
  if (cmd == "imu_stream") { extern bool imuStream; imuStream = d["on"] | false; send(p, reply("ok", "imu_stream")); return; }

  if (cmd == "wifi") {
    prefs.putString("ssid", d["ssid"] | "");
    prefs.putString("pass", d["pass"] | "");
    prefs.putString("euser", d["euser"] | "");
    prefs.putString("eid", d["eid"] | "");
    netReconnect();
    send(p, reply("ok", "wifi saved"));
    return;
  }

  if (cmd == "cfg") {
    if (d["lat"].is<float>()) prefs.putFloat("lat", d["lat"]);
    if (d["lon"].is<float>()) prefs.putFloat("lon", d["lon"]);
    if (d["url"].is<const char*>()) prefs.putString("url", d["url"].as<const char*>());
    if (d["evn"].is<const char*>()) prefs.putString("evn", d["evn"].as<const char*>());
    if (d["evt"].is<const char*>()) prefs.putString("evt", d["evt"].as<const char*>());
    if (d["rot"].is<int>()) { prefs.putUChar("rot", d["rot"].as<int>() & 7); setRotationCfg(d["rot"].as<int>()); }
    APPS[activeApp()]->forceRedraw();
    send(p, stateJson());
    return;
  }

  if (cmd == "gif_begin") {
    uploadSize = d["size"] | 0;
    if (!uploadSize || uploadSize > GIF_MAX_BYTES) { send(p, reply("err", "size")); return; }
    if (upload) upload.close();
    upload = LittleFS.open("/gif.tmp", "w");
    uploadGot = 0;
    send(p, upload ? reply("ack", "begin", 0) : reply("err", "fs"));
    return;
  }
  if (cmd == "gif_chunk") {
    const char* b64 = d["d"] | "";
    size_t inLen = strlen(b64), outLen = 0;
    static uint8_t buf[3072];
    if (!upload || mbedtls_base64_decode(buf, sizeof buf, &outLen, (const uint8_t*)b64, inLen) != 0 ||
        upload.write(buf, outLen) != outLen) {
      send(p, reply("err", "chunk"));
      return;
    }
    uploadGot += outLen;
    send(p, reply("ack", "chunk", uploadGot));
    return;
  }
  if (cmd == "gif_end") {
    if (!upload) { send(p, reply("err", "no upload")); return; }
    upload.close();
    if (uploadGot != uploadSize) { LittleFS.remove("/gif.tmp"); send(p, reply("err", "size mismatch")); return; }
    LittleFS.remove(GIF_PATH);
    LittleFS.rename("/gif.tmp", GIF_PATH);
    gifReload();
    send(p, reply("ok", "gif saved", uploadGot));
    stateDirty = true;
    return;
  }
  send(p, reply("err", "unknown cmd"));
}

static void feed(Port p, String& buf, char c) {
  if (c == '\n') { buf.trim(); if (buf.length()) handle(p, buf); buf = ""; }
  else if (buf.length() < 6000) buf += c;
}

void linkBegin() {
  bleRx = xStreamBufferCreate(8192, 1);
  serialLine.reserve(4096);
  bleLine.reserve(4096);
  NimBLEDevice::init(BLE_NAME);
  NimBLEDevice::setMTU(247);
  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(new SrvCb);
  NimBLEService* svc = srv->createService(SVC_UUID);
  NimBLECharacteristic* rx = svc->createCharacteristic(RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR, 512);
  rx->setCallbacks(new RxCb);
  txChr = svc->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID);
  adv->setScanResponse(true);
  adv->start();
}

void linkPoll() {
  int budget = 4096;
  while (Serial.available() && budget--) feed(P_SERIAL, serialLine, (char)Serial.read());
  uint8_t tmp[256];
  size_t n;
  while ((n = xStreamBufferReceive(bleRx, tmp, sizeof tmp, 0)) > 0)
    for (size_t i = 0; i < n; i++) feed(P_BLE, bleLine, (char)tmp[i]);
  if (stateDirty) {
    stateDirty = false;
    String s = stateJson();
    send(P_SERIAL, s);
    send(P_BLE, s);
  }
}
