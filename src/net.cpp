#include "net.h"
#include "admin_page.h"
#include "config.h"
#include "display.h"
#include "weather.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
using fs::FS;
#include <ESP8266HTTPClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiManager.h>

volatile bool faceDirty = true;

static ESP8266WebServer server(80);
static ESP8266HTTPUpdateServer updater;

static const size_t MAX_BODY = 512;
static const int GEO_MAX = 5;
static const size_t GEO_MAX_SIZE = 8192;

static void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

static void sendError(int code, const char* msg) {
  String s = "{\"error\":\"";
  s += msg;
  s += "\"}";
  sendJson(code, s);
}

static void sendConfig() {
  char buf[256];
  if (serializeSettings(settings, buf, sizeof(buf)) == 0) {
    sendError(500, "serialize failed");
    return;
  }
  sendJson(200, buf);
}

static void handleConfigGet() { sendConfig(); }

static void handleConfigPost() {
  const String& body = server.arg("plain");
  if (body.length() == 0 || body.length() > MAX_BODY) {
    sendError(400, "bad body size");
    return;
  }
  JsonDocument req;
  if (deserializeJson(req, body) || !req.is<JsonObject>()) {
    sendError(400, "invalid json");
    return;
  }

  char buf[256];
  if (serializeSettings(settings, buf, sizeof(buf)) == 0) {
    sendError(500, "serialize failed");
    return;
  }
  JsonDocument merged;
  deserializeJson(merged, buf);
  for (JsonPair kv : req.as<JsonObject>()) merged[kv.key()] = kv.value();

  char mbuf[MAX_BODY + 256];
  size_t n = serializeJson(merged, mbuf, sizeof(mbuf));
  if (n == 0 || n >= sizeof(mbuf)) {
    sendError(400, "too large");
    return;
  }
  Settings next;
  if (!parseSettings(mbuf, next)) {
    sendError(400, "invalid config");
    return;
  }

  bool locChanged = next.lat != settings.lat || next.lon != settings.lon;
  bool faceChanged = locChanged || next.face != settings.face || next.unit != settings.unit ||
                     strcmp(next.city, settings.city) != 0;
  bool brightChanged = next.brightness != settings.brightness;
  settings = next;
  configSave();
  if (brightChanged) setBrightness(settings.brightness);
  if (faceChanged) faceDirty = true;
  if (locChanged) weatherRequestRefresh();
  sendConfig();
}

static void handleGeocode() {
  String q = server.arg("q");
  q.trim();
  JsonDocument out;
  JsonArray arr = out.to<JsonArray>();

  if (q.length() > 0 && q.length() <= 64) {
    char enc[200];
    urlEncode(q.c_str(), enc, sizeof(enc));
    String url = "http://geocoding-api.open-meteo.com/v1/search?name=";
    url += enc;
    url += "&count=5";

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(5000);
    if (http.begin(client, url)) {
      if (http.GET() == HTTP_CODE_OK) {
        int size = http.getSize();
        if (size <= (int)GEO_MAX_SIZE) {
          String resp = http.getString();
          GeoResult res[GEO_MAX];
          int n = parseGeocode(resp.c_str(), res, GEO_MAX);
          for (int i = 0; i < n; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["name"] = res[i].name;
            o["admin1"] = res[i].admin1;
            o["country"] = res[i].country;
            o["lat"] = res[i].lat;
            o["lon"] = res[i].lon;
          }
        }
      }
      http.end();
    }
  }
  String s;
  serializeJson(out, s);
  sendJson(200, s);
}

static void handleStatus() {
  JsonDocument doc;
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  JsonObject w = doc["weather"].to<JsonObject>();
  w["valid"] = weather.valid;
  w["tempC"] = weather.tempC;
  w["code"] = weather.code;
  w["age"] = weatherFetchedAt == 0 ? -1L : (long)((millis() - weatherFetchedAt) / 1000);
  String s;
  serializeJson(doc, s);
  sendJson(200, s);
}

static void onApMode(WiFiManager*) { showMessage("CYBERCLOCK-SETUP", "192.168.4.1"); }

static bool waitConnected(uint32_t ms) {
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < ms) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

void netBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname("cyberclock");
  showMessage("CONNECTING", "");
  WiFi.begin();
  bool ok = waitConnected(20000);
#ifdef WIFI_SSID
  if (!ok) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    ok = waitConnected(20000);
  }
#endif
  if (!ok) {
    WiFiManager wm;
    wm.setConfigPortalTimeout(0);
    wm.setAPCallback(onApMode);
    wm.autoConnect("CYBERCLOCK-SETUP");
  }

  showMessage("ONLINE", WiFi.localIP().toString().c_str());
  delay(3000);

  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", ADMIN_PAGE); });
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/geocode", HTTP_GET, handleGeocode);
  server.on("/api/status", HTTP_GET, handleStatus);
  updater.setup(&server, "/update");
  server.begin();
}

void netLoop() { server.handleClient(); }
