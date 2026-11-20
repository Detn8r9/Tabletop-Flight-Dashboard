#include "terrain.h"
#include "config.h"
#include <math.h>
#include <string.h>
#include <stdint.h>

// The renderer is plain C++ so it can be unit-tested on a desktop.
static const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static inline void setBit(uint8_t *bits, int x, int y) {
  bits[y * MAP_ROW_BYTES + (x >> 3)] |= 0x80 >> (x & 7);
}

static int niceInterval(int range) {
  static const int steps[] = {10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000};
  int raw = range / 8;
  for (int s : steps)
    if (s >= raw) return s;
  return 2000;
}

void terrainRender(const int16_t *E, float mpp, uint8_t style, uint8_t *bits) {
  memset(bits, 0, MAP_BYTES);
  if (style == MAP_BLANK) return;
  const int W = MAP_W, H = MAP_H;

  // Elevation range over land on screen -> contour interval
  int lo = 100000, hi = -100000;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int16_t e = E[y * W + x];
      if (e == INT16_MIN || e <= 0) continue;
      lo = e < lo ? e : lo;
      hi = e > hi ? e : hi;
    }
  int interval = hi > lo ? niceInterval(hi - lo) : 100;

  // Vertical exaggeration grows with scale so hills stay visible zoomed out
  float zf = 1.0f + mpp / 40.0f;
  if (zf > 8) zf = 8;
  const float Lx = -0.5f, Ly = 0.5f, Lz = 0.70710678f;   // light from NW, 45 deg

  auto at = [&](int x, int y) -> int16_t {
    x = x < 0 ? 0 : (x >= W ? W - 1 : x);
    y = y < 0 ? 0 : (y >= H ? H - 1 : y);
    return E[y * W + x];
  };
  auto band = [&](int16_t e) -> int { return (int)floorf((float)e / interval); };

  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      int16_t e = E[y * W + x];
      if (e == INT16_MIN) continue;
      bool water = e <= 0;

      if (water) {
        // coastline: water pixel touching land
        bool coast = false;
        const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4 && !coast; k++) {
          int16_t n = at(x + dx[k], y + dy[k]);
          coast = n != INT16_MIN && n > 0;
        }
        // water: a light, staggered dot grid so it reads as "sea" without clutter
        if (coast || ((y & 3) == 0 && ((x + (y & 4)) & 7) == 0)) setBit(bits, x, y);
        continue;
      }

      // contour lines where the band changes to the right or below
      int b = band(e);
      int16_t er = at(x + 1, y), ed = at(x, y + 1);
      bool line = false, index = false;
      if (er > 0 && er != INT16_MIN && band(er) != b) {
        line = true;
        index |= (b > band(er) ? b : band(er)) % 5 == 0;
      }
      if (ed > 0 && ed != INT16_MIN && band(ed) != b) {
        line = true;
        index |= (b > band(ed) ? b : band(ed)) % 5 == 0;
      }
      if (line) {
        setBit(bits, x, y);
        if (index && x + 1 < W) setBit(bits, x + 1, y);   // bold index contour
        continue;
      }

      if (style == MAP_TERRAIN) {
        float dzdx = (at(x + 1, y) - at(x - 1, y)) / (2 * mpp) * zf;
        float dzdn = (at(x, y - 1) - at(x, y + 1)) / (2 * mpp) * zf;   // north is up
        float nx = -dzdx, ny = -dzdn, nz = 1;
        float inv = 1.0f / sqrtf(nx * nx + ny * ny + nz * nz);
        float shade = (nx * Lx + ny * Ly + nz * Lz) * inv;
        float dark = (0.7071f - shade) * 2.2f;
        if (dark > 1) dark = 1;
        dark *= 0.5f;   // keep the background light so traffic stands out
        if (dark > 0 && dark * 16 > BAYER4[y & 3][x & 3] + 0.5f) setBit(bits, x, y);
      }
    }
  }
}

// ======================= device-only part =======================
#ifndef HOST_TEST
#include <WiFi.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <PNGdec.h>
#include <atomic>

static SemaphoreHandle_t lock;
static uint8_t *mapBits = nullptr;
static bool ready = false;
static std::atomic<uint32_t> ver{0};
static String statusText = "waiting for location";
static TaskHandle_t task = nullptr;

struct MapHeader {
  uint32_t magic;
  float lat, lon, range;
  uint8_t style, layout, pad[2];
};
static const uint32_t MAGIC = 0x4D415033;  // "MAP3"

static void setStatus(const String &s) {
  xSemaphoreTake(lock, portMAX_DELAY);
  statusText = s;
  xSemaphoreGive(lock);
}

String terrainStatus() {
  xSemaphoreTake(lock, portMAX_DELAY);
  String s = statusText;
  xSemaphoreGive(lock);
  return s;
}
bool terrainReady() { return ready; }
uint32_t terrainVersion() { return ver; }

bool terrainCopy(uint8_t *dst) {
  if (!ready) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  memcpy(dst, mapBits, MAP_BYTES);
  xSemaphoreGive(lock);
  return true;
}

static void publish(const uint8_t *bits) {
  xSemaphoreTake(lock, portMAX_DELAY);
  memcpy(mapBits, bits, MAP_BYTES);
  ready = true;
  ver++;
  xSemaphoreGive(lock);
}

// one cached image per zoom level/style, so stepping through zoom levels
// with the buttons only downloads each once
static String cachePath(const Settings &s) {
  return "/m_" + String((int)lroundf(s.rangeNm * 10)) + "_" + String(s.mapStyle) + "_" + String(s.layout) + ".bin";
}

static bool loadCache(const Settings &s) {
  File f = LittleFS.open(cachePath(s), "r");
  if (!f) return false;
  MapHeader h;
  bool ok = f.read((uint8_t *)&h, sizeof h) == sizeof h && h.magic == MAGIC &&
            fabsf(h.lat - (float)s.lat) < 1e-4f && fabsf(h.lon - (float)s.lon) < 1e-4f &&
            fabsf(h.range - s.rangeNm) < 1e-3f && h.style == s.mapStyle && h.layout == s.layout;
  if (ok) {
    uint8_t *tmp = (uint8_t *)heap_caps_malloc(MAP_BYTES, MALLOC_CAP_SPIRAM);
    ok = f.read(tmp, MAP_BYTES) == MAP_BYTES;
    if (ok) publish(tmp);
    heap_caps_free(tmp);
  }
  f.close();
  return ok;
}

static void saveCache(const Settings &s, const uint8_t *bits) {
  File f = LittleFS.open(cachePath(s), "w");
  if (!f) return;
  MapHeader h = {MAGIC, (float)s.lat, (float)s.lon, s.rangeNm, s.mapStyle, s.layout, {0, 0}};
  f.write((uint8_t *)&h, sizeof h);
  f.write(bits, MAP_BYTES);
  f.close();
}

// ---- tile download + decode
struct TileCtx { int16_t *dst; };
static int pngDraw(PNGDRAW *d) {
  TileCtx *ctx = (TileCtx *)d->pUser;
  int16_t *row = ctx->dst + d->y * 256;
  const uint8_t *p = d->pPixels;
  int bpp = d->iPixelType == PNG_PIXEL_TRUECOLOR_ALPHA ? 4 : 3;
  if (d->iPixelType != PNG_PIXEL_TRUECOLOR && d->iPixelType != PNG_PIXEL_TRUECOLOR_ALPHA) return 0;
  for (int x = 0; x < d->iWidth && x < 256; x++, p += bpp) {
    float e = (p[0] * 256.0f + p[1] + p[2] / 256.0f) - 32768.0f;
    row[x] = (int16_t)constrain(e, -11000.0f, 9000.0f);
  }
  return 1;
}

static bool fetchTile(int z, int x, int y, int16_t *dst, uint8_t *dl, size_t dlCap, String &err) {
  char url[120];
  snprintf(url, sizeof url, "http://s3.amazonaws.com/elevation-tiles-prod/terrarium/%d/%d/%d.png", z, x, y);
  for (int attempt = 0; attempt < 2; attempt++) {
    HTTPClient http;
    WiFiClient c;
    http.begin(c, url);
    http.useHTTP10(true);
    http.setTimeout(15000);
    http.setUserAgent("FlightRadar-ESP32/" FW_VERSION);
    int code = http.GET();
    if (code != 200) { err = "tile HTTP " + String(code); http.end(); delay(500); continue; }
    WiFiClient *s = http.getStreamPtr();
    int len = http.getSize();
    size_t got = 0;
    uint32_t t0 = millis();
    while ((len < 0 || (int)got < len) && got < dlCap && millis() - t0 < 20000) {
      int avail = s->available();
      if (avail > 0) {
        got += s->readBytes(dl + got, min((size_t)avail, dlCap - got));
      } else if (!s->connected()) {
        break;
      } else {
        delay(2);
      }
    }
    http.end();
    if (len > 0 && (int)got != len) { err = "tile truncated"; continue; }

    static PNG *png = new PNG();   // large decoder state; lands in PSRAM
    TileCtx ctx = {dst};
    if (png->openRAM(dl, got, pngDraw) != PNG_SUCCESS) { err = "tile not PNG"; continue; }
    int rc = png->decode(&ctx, 0);
    png->close();
    if (rc == PNG_SUCCESS) return true;
    err = "tile decode " + String(rc);
  }
  return false;
}

static double mercY(double lat) {
  double r = radians(lat);
  return (1.0 - log(tan(r) + 1.0 / cos(r)) / PI) / 2.0;
}

static bool build(const Settings s) {
  const int W = MAP_W, H = MAP_H, CX = scopeCx(s.layout), CY = scopeCy(s.layout);
  const float mpp = s.rangeNm * 1852.0f / SCOPE_R;
  uint8_t *bits = (uint8_t *)heap_caps_calloc(1, MAP_BYTES, MALLOC_CAP_SPIRAM);
  if (s.mapStyle == MAP_BLANK) {
    publish(bits);
    saveCache(s, bits);
    heap_caps_free(bits);
    setStatus("ready");
    return true;
  }

  const double cosLat = cos(radians(s.lat));
  const double degPerM_lat = 1.0 / 111320.0;
  const double degPerM_lon = 1.0 / (111320.0 * cosLat);
  int z = (int)floor(log2(156543.034 * cosLat / mpp));
  z = constrain(z, 1, 12);
  const double world = 256.0 * (1 << z);

  // tile span covering the screen
  double latN = s.lat + CY * mpp * degPerM_lat, latS = s.lat - (H - CY) * mpp * degPerM_lat;
  double lonW = s.lon - CX * mpp * degPerM_lon, lonE = s.lon + (W - CX) * mpp * degPerM_lon;
  int tx0 = (int)floor((lonW + 180) / 360 * world / 256), tx1 = (int)floor((lonE + 180) / 360 * world / 256);
  int ty0 = (int)floor(mercY(latN) * world / 256), ty1 = (int)floor(mercY(latS) * world / 256);
  int tw = tx1 - tx0 + 1, th = ty1 - ty0 + 1;
  if (tw * th > 16) { setStatus("error: too many tiles"); heap_caps_free(bits); return false; }

  int16_t *tiles = (int16_t *)heap_caps_malloc((size_t)tw * th * 256 * 256 * 2, MALLOC_CAP_SPIRAM);
  bool *have = (bool *)calloc(tw * th, 1);
  const size_t dlCap = 400 * 1024;
  uint8_t *dl = (uint8_t *)heap_caps_malloc(dlCap, MALLOC_CAP_SPIRAM);
  int16_t *E = (int16_t *)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
  if (!tiles || !have || !dl || !E) {
    setStatus("error: out of memory");
    heap_caps_free(tiles); free(have); heap_caps_free(dl); heap_caps_free(E); heap_caps_free(bits);
    return false;
  }

  int okCount = 0;
  String err;
  const int n = 1 << z;
  for (int j = 0; j < th; j++)
    for (int i = 0; i < tw; i++) {
      setStatus("downloading terrain " + String(j * tw + i + 1) + "/" + String(tw * th));
      int tx = ((tx0 + i) % n + n) % n, ty = constrain(ty0 + j, 0, n - 1);
      int16_t *dst = tiles + (size_t)(j * tw + i) * 65536;
      have[j * tw + i] = fetchTile(z, tx, ty, dst, dl, dlCap, err);
      okCount += have[j * tw + i];
    }
  heap_caps_free(dl);

  if (okCount == 0) {
    setStatus("error: " + err);
    heap_caps_free(tiles); free(have); heap_caps_free(E); heap_caps_free(bits);
    return false;
  }
  setStatus("drawing terrain");

  // sample elevation for every map pixel (bilinear)
  for (int py = 0; py < H; py++) {
    double lat = s.lat - (py - CY) * mpp * degPerM_lat;
    double gy = mercY(lat) * world - ty0 * 256.0;
    for (int px = 0; px < W; px++) {
      double lon = s.lon + (px - CX) * mpp * degPerM_lon;
      double gx = (lon + 180) / 360 * world - tx0 * 256.0;
      auto sample = [&](int X, int Y) -> int {
        int ti = X >> 8, tj = Y >> 8;
        if (ti < 0 || tj < 0 || ti >= tw || tj >= th || !have[tj * tw + ti]) return INT16_MIN;
        return tiles[(size_t)(tj * tw + ti) * 65536 + (Y & 255) * 256 + (X & 255)];
      };
      int X = (int)floor(gx - 0.5), Y = (int)floor(gy - 0.5);
      float fx = gx - 0.5 - X, fy = gy - 0.5 - Y;
      int a = sample(X, Y), b = sample(X + 1, Y), c = sample(X, Y + 1), d = sample(X + 1, Y + 1);
      int16_t v;
      if (a == INT16_MIN || b == INT16_MIN || c == INT16_MIN || d == INT16_MIN) {
        v = a != INT16_MIN ? a : INT16_MIN;
      } else {
        v = (int16_t)lroundf((a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy);
      }
      E[py * W + px] = v;
    }
  }
  heap_caps_free(tiles);
  free(have);

  terrainRender(E, mpp, s.mapStyle, bits);
  heap_caps_free(E);
  publish(bits);
  saveCache(s, bits);
  heap_caps_free(bits);
  setStatus(okCount < tw * th ? "ready (some tiles missing)" : "ready");
  return true;
}

static void terrainTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // debounce: settle for 1.5 s after the last change
    while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1500)) > 0) {}
    Settings snap = cfgSnapshot();
    if (!snap.locSet) { setStatus("waiting for location"); continue; }
    if (loadCache(snap)) { setStatus("ready"); continue; }
    while (WiFi.status() != WL_CONNECTED) delay(1000);
    for (int attempt = 0; attempt < 3; attempt++) {
      if (build(snap)) break;
      delay(10000 * (attempt + 1));
    }
  }
}

void terrainStart() {
  lock = xSemaphoreCreateMutex();
  mapBits = (uint8_t *)heap_caps_calloc(1, MAP_BYTES, MALLOC_CAP_SPIRAM);
  LittleFS.begin(true);
  xTaskCreatePinnedToCore(terrainTask, "terrain", 12 * 1024, nullptr, 1, &task, 0);
  terrainRequest();
}

void terrainRequest() {
  // the old image no longer matches the scale/location: hide it until rebuilt
  ready = false;
  ver++;
  if (task) xTaskNotifyGive(task);
}
#endif
