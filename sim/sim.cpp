// Desktop simulator: runs the real faces and the real TFT_eSPI against an emulated
// ST7789, and serves the screen to a browser. Build with sim/build.sh, then open
// http://localhost:8240
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <random>
#include <string>
#include "../src/config.h"
#include "../src/display.h"
#include "../src/faces/face.h"
#include "../src/weather.h"
#include "ESP8266WiFi.h"

#undef time
#undef gettimeofday

// ---- Arduino platform ----
static const auto t0 = std::chrono::steady_clock::now();
uint32_t millis() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}
uint32_t micros() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
}
void delay(uint32_t) {}
void delayMicroseconds(uint32_t) {}
static std::mt19937 rng(1234);
long random(long hi) { return hi <= 0 ? 0 : (long)(rng() % (uint32_t)hi); }
long random(long lo, long hi) { return hi <= lo ? lo : lo + random(hi - lo); }
void randomSeed(unsigned long s) { rng.seed(s); }
void pinMode(uint8_t, uint8_t) {}
SimEsp ESP;
SimWiFi WiFi;
SPIClass SPI;

// ---- emulated ST7789: decodes the command stream TFT_eSPI sends over SPI ----
static uint16_t fb[240 * 240];
static bool dcData = true;
static uint8_t cmd = 0, args[4];
static int nargs = 0, wx0, wx1, wy0, wy1, cx, cy;
static bool hiByte = true;
static uint8_t hi;

void digitalWrite(uint8_t pin, uint8_t val) {
  if (pin == TFT_DC) dcData = val;
}

void st7789Byte(uint8_t b) {
  if (!dcData) {
    cmd = b;
    nargs = 0;
    hiByte = true;
    if (cmd == 0x2C) { cx = wx0; cy = wy0; }
    return;
  }
  if (cmd == 0x2A || cmd == 0x2B) {  // column / row address set
    if (nargs < 4) args[nargs++] = b;
    if (nargs == 4) {
      int a = args[0] << 8 | args[1], z = args[2] << 8 | args[3];
      if (cmd == 0x2A) { wx0 = a; wx1 = z; } else { wy0 = a; wy1 = z; }
    }
  } else if (cmd == 0x2C) {  // memory write: big-endian RGB565 pixels
    if (hiByte) { hi = b; hiByte = false; return; }
    hiByte = true;
    if (cx < 240 && cy < 240) fb[cy * 240 + cx] = hi << 8 | b;
    if (++cx > wx1) { cx = wx0; if (++cy > wy1) cy = wy0; }
  }
}

// ---- simulated clock, weather and settings ----
static long timeOffset = 0;  // seconds added to the real clock
int sim_gettimeofday(struct timeval* tv, void*) {
  int r = gettimeofday(tv, nullptr);
  tv->tv_sec += timeOffset;
  return r;
}
time_t sim_time(time_t* t) {
  time_t now = time(nullptr) + timeOffset;
  if (t) *t = now;
  return now;
}

Settings settings;
WeatherData weather;
uint32_t weatherFetchedAt = 0;
void weatherRequestRefresh() {}
void weatherLoop() {}
bool weatherStale() { return false; }
bool timeSynced() { return true; }

static void setWeather(int code, float tempC) {
  weather.valid = true;
  weather.code = weather.dayCode = code;
  weather.tempC = tempC;
  weather.hiC = tempC + 4;
  weather.loC = tempC - 3;
  weather.humidity = code >= 51 ? 88 : 61;
  bool wet = code >= 51;
  weather.precipProb = wet ? 80 : 5;
  weather.precipMm = wet ? 6.5f : 0;
  weather.windKmh = 14;
  weather.windDir = 135;
}

// ---- tiny HTTP server ----
static int listenFd = -1;
static std::string pageDir;

static std::string param(const std::string& q, const char* key) {
  std::string k = std::string(key) + "=";
  size_t p = q.find(k);
  if (p == std::string::npos || (p > 0 && q[p - 1] != '&' && q[p - 1] != '?')) return "";
  size_t e = q.find('&', p);
  std::string v = q.substr(p + k.size(), e == std::string::npos ? std::string::npos : e - p - k.size());
  std::string out;
  for (size_t i = 0; i < v.size(); i++) {
    if (v[i] == '+') out += ' ';
    else if (v[i] == '%' && i + 2 < v.size()) { out += (char)strtol(v.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
    else out += v[i];
  }
  return out;
}

static void reply(int fd, const char* type, const void* body, size_t n) {
  char head[160];
  int h = snprintf(head, sizeof(head),
                   "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n"
                   "Connection: close\r\n\r\n", type, n);
  write(fd, head, h);
  const char* p = (const char*)body;
  while (n) {
    ssize_t w = write(fd, p, n);
    if (w <= 0) break;
    p += w;
    n -= w;
  }
}

static std::string stateJson() {
  struct timeval tv;
  sim_gettimeofday(&tv, nullptr);
  char buf[256];
  snprintf(buf, sizeof(buf), "{\"face\":%d,\"unit\":\"%c\",\"code\":%d,\"tempC\":%.0f,\"city\":\"%s\",\"second\":%ld}",
           settings.face, settings.unit, weather.code, weather.tempC, settings.city, (long)(tv.tv_sec % 60));
  return buf;
}

static void handle(int fd) {
  char req[2048];
  ssize_t n = read(fd, req, sizeof(req) - 1);
  if (n <= 0) return;
  req[n] = '\0';
  std::string line(req, strcspn(req, "\r\n"));
  size_t a = line.find(' '), b = line.find(' ', a + 1);
  std::string path = line.substr(a + 1, b - a - 1), query;
  if (size_t q = path.find('?'); q != std::string::npos) { query = path.substr(q + 1); path = path.substr(0, q); }

  if (path == "/frame") {
    reply(fd, "application/octet-stream", fb, sizeof(fb));
  } else if (path == "/set") {
    std::string v;
    if (!(v = param(query, "face")).empty()) settings.face = (uint8_t)atoi(v.c_str());
    if (!(v = param(query, "unit")).empty()) settings.unit = v[0];
    if (!(v = param(query, "city")).empty()) snprintf(settings.city, sizeof(settings.city), "%s", v.c_str());
    int code = weather.code;
    float temp = weather.tempC;
    if (!(v = param(query, "code")).empty()) code = atoi(v.c_str());
    if (!(v = param(query, "tempC")).empty()) temp = (float)atof(v.c_str());
    setWeather(code, temp);
    if (!(v = param(query, "second")).empty()) {  // jump so the clock reads this second
      long now = (long)sim_time(nullptr), want = atol(v.c_str());
      timeOffset += ((want - now % 60) + 60) % 60;  // keeps any hour shift
    }
    if (!(v = param(query, "hours")).empty()) timeOffset += atol(v.c_str()) * 3600;  // shift the clock by whole hours
    if (!(v = param(query, "resetTime")).empty()) timeOffset = 0;
    std::string j = stateJson();
    reply(fd, "application/json", j.data(), j.size());
  } else if (path == "/state") {
    std::string j = stateJson();
    reply(fd, "application/json", j.data(), j.size());
  } else {
    std::string file = pageDir + "/index.html", body;
    if (FILE* f = fopen(file.c_str(), "rb")) {
      char chunk[4096];
      size_t k;
      while ((k = fread(chunk, 1, sizeof(chunk), f)) > 0) body.append(chunk, k);
      fclose(f);
    }
    reply(fd, "text/html; charset=utf-8", body.data(), body.size());
  }
}

static void serve() {
  for (;;) {
    int fd = accept(listenFd, nullptr, nullptr);
    if (fd < 0) return;
    fcntl(fd, F_SETFL, 0);  // blocking for this one short exchange
    struct timeval to = {0, 200000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
    handle(fd);
    close(fd);
  }
}

static Face* pick(int i) {
  switch (i) {  // same order as src/main.cpp
    case 1: return faceTerminal();
    case 2: return faceVitals();
    case 3: return faceCorpo();
    case 4: return faceRadar();
    case 5: return faceChongqing();
    case 6: return faceHongKong();
    case 7: return faceShibuya();
    case 8: return faceOrbit();
    case 9: return faceNeoLA();
    default: return faceNetrunner();
  }
}

int main(int argc, char** argv) {
  int port = argc > 1 ? atoi(argv[1]) : 8240;
  std::string self = argv[0];
  pageDir = self.substr(0, self.find_last_of('/') == std::string::npos ? 0 : self.find_last_of('/'));
  if (pageDir.empty()) pageDir = ".";

  listenFd = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(listenFd, (sockaddr*)&addr, sizeof(addr)) < 0 || listen(listenFd, 8) < 0) {
    perror("listen");
    return 1;
  }
  fcntl(listenFd, F_SETFL, O_NONBLOCK);
  printf("CYBERCLOCK simulator on http://localhost:%d\n", port);
  fflush(stdout);

  settings = defaultSettings();
  setWeather(61, 12);
  displayBegin();

  Face* face = nullptr;
  int shownFace = -1;
  FaceState cur, prev;
  uint32_t lastState = 0, lastTick = 0;
  for (;;) {
    serve();
    uint32_t now = millis();
    if (settings.face != shownFace || !face) {
      if (face) face->end();
      shownFace = settings.face;
      face = pick(shownFace);
      face->begin();
      memset(&prev, 0, sizeof(prev));
      buildFaceState(cur);
      face->update(cur, prev);
      prev = cur;
      lastState = now;
    }
    if (now - lastState >= 50) {
      lastState = now;
      buildFaceState(cur);
      if (memcmp(&cur, &prev, sizeof(cur)) != 0) {
        face->update(cur, prev);
        prev = cur;
      }
    }
    if (now - lastTick >= 50) {
      lastTick = now;
      face->tick(now);
    }
    usleep(2000);
  }
}
