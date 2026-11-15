// Wi-Fi (WPA2-PSK or optional WPA2-Enterprise PEAP), NTP, Open-Meteo weather (no API key)
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "core.h"

Weather weather = {};

bool timeValid() { return time(nullptr) > 1700000000; }

String wifiStatus() {
  if (WiFi.isConnected()) return "connected " + WiFi.localIP().toString();
  return prefs.getString("ssid", "").length() ? "connecting" : "not configured";
}

const char* weatherText(int c) {
  if (c == 0) return "Clear";
  if (c <= 2) return "Partly cloudy";
  if (c == 3) return "Overcast";
  if (c <= 48) return "Fog";
  if (c <= 57) return "Drizzle";
  if (c <= 67) return "Rain";
  if (c <= 77) return "Snow";
  if (c <= 82) return "Showers";
  if (c <= 86) return "Snow showers";
  return "Thunderstorm";
}

void netReconnect() {
  String ssid = prefs.getString("ssid", "");
  WiFi.disconnect();
  if (!ssid.length()) return;
  String user = prefs.getString("euser", "");
  if (user.length()) {  // WPA2-Enterprise (e.g. eduroam) - prefer NC State Nomad + "ncsu" instead
    String id = prefs.getString("eid", user);
    WiFi.begin(ssid.c_str(), WPA2_AUTH_PEAP, id.c_str(), user.c_str(), prefs.getString("pass", "").c_str());
  } else {
    WiFi.begin(ssid.c_str(), prefs.getString("pass", "").c_str());
  }
  Serial.printf("Wi-Fi: joining '%s'\n", ssid.c_str());
}

static void fetchWeather() {
  char url[200];
  snprintf(url, sizeof url,
           "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code,wind_speed_10m&temperature_unit=fahrenheit&wind_speed_unit=mph",
           prefs.getFloat("lat", DEFAULT_LAT), prefs.getFloat("lon", DEFAULT_LON));
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(url)) return;
  if (http.GET() == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) {
      weather.tempF = doc["current"]["temperature_2m"] | 0.0f;
      weather.windMph = doc["current"]["wind_speed_10m"] | 0.0f;
      weather.code = doc["current"]["weather_code"] | 0;
      weather.at = millis();
      weather.ok = true;
    }
  }
  http.end();
}

static void netTask(void*) {
  uint32_t last = 0;
  for (;;) {
    if (WiFi.isConnected() && (!weather.ok || millis() - last > 15 * 60 * 1000UL)) {
      fetchWeather();
      last = millis();
      if (!weather.ok) last -= 14 * 60 * 1000UL;  // retry in ~1 min
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void netBegin() {
  setenv("TZ", TZ_EASTERN, 1);
  tzset();
  WiFi.mode(WIFI_STA);   // factory eFuse MAC is used; never override or randomize it (NC State Nomad)
  WiFi.setAutoReconnect(true);
  Serial.printf("\n=== Wi-Fi MAC (register on NC State Nomad): %s ===\n", WiFi.macAddress().c_str());
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) {
    Serial.printf("Wi-Fi connected, IP %s\n", WiFi.localIP().toString().c_str());
    configTzTime(TZ_EASTERN, "pool.ntp.org", "time.nist.gov");
    linkBroadcastState();
  }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  netReconnect();
  xTaskCreate(netTask, "net", 8192, nullptr, 1, nullptr);
}
