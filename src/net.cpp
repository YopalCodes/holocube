// Wi-Fi (WPA2-PSK or optional WPA2-Enterprise PEAP), NTP, Open-Meteo weather (no API key)
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
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
      linkBroadcastState();   // only sets a flag; the main loop sends it
    }
  }
  http.end();
}

// ---- Auto concert dates: pick the next show for the tracked artist (pref "art") ----
static volatile bool concertReq = true;
static const char* volatile cStatus = "";
void concertRefresh() { concertReq = true; }
const char* concertStatus() { return cStatus; }

static time_t etEpoch(const char* s) {   // "YYYY-MM-DDTHH:MM" in Eastern (the device TZ)
  struct tm tm = {};
  if (sscanf(s, "%d-%d-%dT%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min) != 5) return 0;
  tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_isdst = -1;
  return mktime(&tm);
}

static bool fetchConcert() {   // returns false on a network error (retry soon)
  String art = prefs.getString("art", "");
  if (!art.length()) { cStatus = ""; return true; }
  if (!*cStatus) cStatus = prefs.getString("evt", "").length() ? "" : "Looking up concert dates";
  WiFiClientSecure tls;
  tls.setInsecure();   // public, non-secret data; skips certificate pinning
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(tls, String(CONCERTS_BASE) + art + ".json")) { cStatus = "Can't reach the concert list"; return false; }
  int code = http.GET();
  bool ok = code == 200 || code == 404;
  if (code == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getString())) ok = false;
    else {
      time_t now = time(nullptr);
      const char* artist = doc["artist"] | "";
      String evt, evn = artist;
      for (JsonObject e : doc["events"].as<JsonArray>()) {
        const char* et = e["et"] | "";
        time_t t = etEpoch(et);
        if (t && t > now - 6 * 3600) {   // show "SHOW TIME!" for 6 h, then move on
          evt = et;
          const char* city = e["city"] | "";
          if (*city && strlen(artist) + strlen(city) + 3 <= 20) evn = String(artist) + " - " + city;
          break;
        }
      }
      if (evt != prefs.getString("evt", "") || evn != prefs.getString("evn", "")) {
        prefs.putString("evt", evt);
        prefs.putString("evn", evn);
        Serial.printf("Concert: %s %s\n", evn.c_str(), evt.length() ? evt.c_str() : "(none announced)");
        linkBroadcastState();
      }
      cStatus = evt.length() ? "" : "No dates announced yet";
    }
  } else if (code == 404) {
    cStatus = "Artist isn't in the concert list";
  }
  if (!ok) cStatus = prefs.getString("evt", "").length() ? "" : "Can't reach the concert list";
  http.end();
  return ok;
}

static void netTask(void*) {
  uint32_t last = 0, lastConcert = 0;
  bool concertOk = false;
  for (;;) {
    if (WiFi.isConnected() && timeValid() &&
        (concertReq || millis() - lastConcert > (concertOk ? 60 * 60 * 1000UL : 2 * 60 * 1000UL))) {
      concertReq = false;
      concertOk = fetchConcert();
      lastConcert = millis();
    }
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
  xTaskCreate(netTask, "net", 12288, nullptr, 1, nullptr);   // TLS needs the extra stack
}
