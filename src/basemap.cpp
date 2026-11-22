#include "basemap.h"
#include "config.h"
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold9pt7b.h>

// ---------------- pure part (also built on the desktop for tests)

static const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// Colours of the OpenStreetMap "Carto" style
MapClass mapClassify(uint8_t r, uint8_t g, uint8_t b) {
  int R = r, G = g, B = b;
  if (R > 190 && R - G > 45 && B < 200) return MC_MOTOR;                    // motorway / trunk
  if (R > 235 && G > 190 && G < 232 && B < 190) return MC_MAJOR;            // primary (orange)
  if (R > 235 && G > 238 && B < 215 && B < G - 25) return MC_MID;           // secondary (yellow)
  if (B > R + 25 && G > R + 15 && B >= G - 8) return MC_WATER;              // water (blue)
  if (G > R + 12 && G > B + 8) return MC_PARK;                              // parks, woods
  if (R >= 250 && G >= 250 && B >= 250) return MC_MINOR;                    // small streets
  return MC_NONE;
}

static inline void setBit(uint8_t *bits, int x, int y) {
  bits[y * MAP_ROW_BYTES + (x >> 3)] |= 0x80 >> (x & 7);
}

void mapRender(const uint8_t *cls, uint8_t style, uint8_t *bits) {
  memset(bits, 0, MAP_BYTES);
  if (style == MAP_BLANK) return;
  const int W = MAP_W, H = MAP_H;
  auto at = [&](int x, int y) -> uint8_t {
    x = x < 0 ? 0 : (x >= W ? W - 1 : x);
    y = y < 0 ? 0 : (y >= H ? H - 1 : y);
    return cls[y * W + x];
  };
  auto isWaterish = [&](int x, int y) {
    uint8_t c = at(x, y);
    return c == MC_WATER || c == MC_MINOR || c == MC_MID;   // bridges/ferry lines inside water
  };
  // water with a 3x3 majority vote, so ferry routes and labels don't
  // shred rivers and harbours
  static uint8_t *water = nullptr;
  if (!water) water = (uint8_t *)malloc(W * H);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int n = 0, w = 0;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          uint8_t c = at(x + dx, y + dy);
          w += c == MC_WATER;
          n += isWaterish(x + dx, y + dy) ? 1 : 0;
        }
      uint8_t c = cls[y * W + x];
      water[y * W + x] = w >= 3 && n >= 5 && c != MC_MOTOR && c != MC_MAJOR;
    }

  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const uint8_t c = cls[y * W + x];
      const int thr = BAYER4[y & 3][x & 3];
      if (water[y * W + x]) {
        bool shore = false;
        if (x > 0 && !water[y * W + x - 1]) shore = true;
        if (x < W - 1 && !water[y * W + x + 1]) shore = true;
        if (y > 0 && !water[(y - 1) * W + x]) shore = true;
        if (y < H - 1 && !water[(y + 1) * W + x]) shore = true;
        if (shore || thr < 2) setBit(bits, x, y);   // outline + light tint
        continue;
      }
      switch (c) {
        case MC_MOTOR:
        case MC_MAJOR: setBit(bits, x, y); break;
        case MC_MID: if (style == 1) setBit(bits, x, y); break;
        case MC_PARK: if (thr < 1) setBit(bits, x, y); break;
        default: break;
      }
    }
}


// ---------------- place names and airports

namespace {
struct LRect { int x, y, w, h; };
bool hit(const LRect &a, const LRect &b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
// GFXcanvas1 that draws straight into our map buffer (same 1-bit layout)
struct MapCanvas : public GFXcanvas1 {
  uint8_t *target;
  MapCanvas(uint8_t *t) : GFXcanvas1(MAP_W, MAP_H), target(t) { memcpy(getBuffer(), t, MAP_BYTES); }
  void done() { memcpy(target, getBuffer(), MAP_BYTES); }
};
int32_t rd32(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
}

// text with a white outline, so it reads over roads and water
static void haloText(GFXcanvas1 &g, int x, int y, const char *s, const GFXfont *f, bool bold) {
  g.setFont(f);
  g.setTextWrap(false);
  g.setTextColor(0);
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= (bold ? 2 : 1); dx++) {
      if (!dx && !dy) continue;
      g.setCursor(x + dx, y + dy);
      g.print(s);
    }
  g.setTextColor(1);
  g.setCursor(x, y);
  g.print(s);
  if (bold) { g.setCursor(x + 1, y); g.print(s); }
}

void mapDrawLabels(uint8_t *bits, double lat0, double lon0, float mpp, int cx, int cy,
                   uint8_t layout, const uint8_t *places, size_t plen,
                   const uint8_t *aps, size_t alen, bool towns) {
  MapCanvas g(bits);
  std::vector<LRect> used;
  // keep clear of the house, the side panel / corner boxes and the map credit
  used.push_back({cx - 12, cy - 12, 25, 25});
  if (layout == 0) {
    used.push_back({296, 0, MAP_W - 296, MAP_H});
    used.push_back({0, MAP_H - 11, 106, 11});
  } else {
    used.push_back({0, 0, 112, 28});
    used.push_back({MAP_W - 112, 0, 112, 28});
    used.push_back({0, MAP_H - 17, 132, 17});
    used.push_back({MAP_W - 82, MAP_H - 17, 82, 17});
    used.push_back({MAP_W / 2 - 54, MAP_H - 11, 108, 11});
  }
  const double kx = 111320.0 * cos(lat0 * 0.017453292519943295) / mpp, ky = 111320.0 / mpp;
  auto project = [&](int32_t la, int32_t lo, int &x, int &y) {
    x = (int)lround(cx + (lo / 1e5 - lon0) * kx);
    y = (int)lround(cy - (la / 1e5 - lat0) * ky);
    return x >= 2 && y >= 2 && x < MAP_W - 2 && y < MAP_H - 2;
  };
  auto clear = [&](const LRect &r) {
    if (r.x < 1 || r.y < 1 || r.x + r.w > MAP_W - 1 || r.y + r.h > MAP_H - 1) return false;
    for (const LRect &u : used) if (hit(r, u)) return false;
    return true;
  };

  // airports first: they matter most on a flight radar
  const float rangeNm = mpp * SCOPE_R / 1852.0f;
  int nAirports = 0;
  for (size_t off = 0; off + 12 <= alen && nAirports < 8; off += 12) {
    const uint8_t *r = aps + off;
    int kind = r[8];
    if (kind < 2 && rangeNm > 60) continue;    // only big airports when zoomed far out
    int x, y;
    if (!project(rd32(r), rd32(r + 4), x, y)) continue;
    char code[4] = {(char)r[9], (char)r[10], (char)r[11], 0};
    LRect sym = {x - 7, y - 7, 15, 15};
    if (!clear(sym)) continue;
    const int tw = 3 * 7 + 2, th = 10;
    const LRect cand[4] = {{x + 9, y - 5, tw, th}, {x - 9 - tw, y - 5, tw, th}, {x - tw / 2, y - 18, tw, th}, {x - tw / 2, y + 9, tw, th}};
    for (const LRect &c : cand) {
      if (!clear(c)) continue;
      // symbol: ringed circle with a runway through it
      g.fillCircle(x, y, 6, 0);
      g.drawCircle(x, y, 6, 1);
      g.drawCircle(x, y, 5, 1);
      g.drawLine(x - 4, y + 4, x + 4, y - 4, 1);
      g.drawLine(x - 3, y + 4, x + 4, y - 3, 1);
      g.drawLine(x - 4, y + 3, x + 3, y - 4, 1);
      haloText(g, c.x + 1, c.y + 1, code, nullptr, true);
      used.push_back(sym);
      used.push_back({c.x - 1, c.y - 1, c.w + 2, c.h + 2});
      nAirports++;
      break;
    }
  }

  // towns and cities, biggest first
  if (towns) {
    int nPlaces = 0;
    for (size_t off = 0; off + 10 <= plen && nPlaces < 14;) {
      const uint8_t *r = places + off;
      int len = r[9];
      off += 10 + len;
      int x, y;
      if (!project(rd32(r), rd32(r + 4), x, y)) continue;
      char name[44];
      memcpy(name, r + 10, len);
      name[len] = 0;
      const bool big = r[8] >= 160;   // about 1 million people or more
      const GFXfont *f = big ? &FreeSansBold9pt7b : nullptr;
      int16_t bx, by;
      uint16_t w, h;
      g.setFont(f);
      g.getTextBounds(name, 0, 0, &bx, &by, &w, &h);
      if (!big) { w = len * 6; h = 8; by = 0; }
      LRect dot = {x - 2, y - 2, 5, 5};
      if (!clear(dot)) continue;
      const LRect cand[4] = {{x + 5, y - (int)h / 2 - 1, (int)w + 3, (int)h + 3},
                             {x - 5 - (int)w - 3, y - (int)h / 2 - 1, (int)w + 3, (int)h + 3},
                             {x - (int)w / 2 - 1, y - (int)h - 6, (int)w + 3, (int)h + 3},
                             {x - (int)w / 2 - 1, y + 4, (int)w + 3, (int)h + 3}};
      for (const LRect &c : cand) {
        if (!clear(c)) continue;
        g.fillRect(x - 2, y - 2, 5, 5, 0);
        g.fillRect(x - 1, y - 1, 3, 3, 1);
        if (big) g.drawRect(x - 3, y - 3, 7, 7, 1);
        haloText(g, c.x + 1 - (big ? bx : 0), c.y + 1 - (big ? by : 0), name, f, false);
        used.push_back(dot);
        used.push_back({c.x - 2, c.y - 2, c.w + 4, c.h + 4});
        nPlaces++;
        break;
      }
    }
  }
  g.done();
}

// ---------------- device part
#ifndef HOST_TEST
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <PNGdec.h>
#include <atomic>

// data/places.bin and data/airports.bin, embedded by platformio.ini
extern const uint8_t places_start[] asm("_binary_data_places_bin_start");
extern const uint8_t places_end[] asm("_binary_data_places_bin_end");
extern const uint8_t airports_start[] asm("_binary_data_airports_bin_start");
extern const uint8_t airports_end[] asm("_binary_data_airports_bin_end");

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
static const uint32_t MAGIC = 0x4F534D32;  // "OSM2" (adds place names)

static void setStatus(const String &s) {
  xSemaphoreTake(lock, portMAX_DELAY);
  statusText = s;
  xSemaphoreGive(lock);
}

String mapStatus() {
  xSemaphoreTake(lock, portMAX_DELAY);
  String s = statusText;
  xSemaphoreGive(lock);
  return s;
}
bool mapReady() { return ready; }
uint32_t mapVersion() { return ver; }

bool mapCopy(uint8_t *dst) {
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

// One cached image per zoom level / style / layout, so stepping through
// zoom levels with the buttons only downloads each once.
static String cachePath(const Settings &s) {
  return "/m_" + String((int)lroundf(s.rangeNm * 10)) + "_" + String(s.mapStyle) + "_" + String(s.layout) + (s.placeNames ? "" : "n") + ".bin";
}

static bool loadCache(const Settings &s) {
  File f = LittleFS.open(cachePath(s), "r");
  if (!f) return false;
  MapHeader h;
  bool ok = f.read((uint8_t *)&h, sizeof h) == sizeof h && h.magic == MAGIC &&
            fabsf(h.lat - (float)s.lat) < 1e-4f && fabsf(h.lon - (float)s.lon) < 1e-4f &&
            fabsf(h.range - s.rangeNm) < 1e-3f && h.style == s.mapStyle && h.layout == s.layout &&
            h.pad[0] == (uint8_t)s.placeNames;
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
  MapHeader h = {MAGIC, (float)s.lat, (float)s.lon, s.rangeNm, s.mapStyle, s.layout, {(uint8_t)s.placeNames, 0}};
  f.write((uint8_t *)&h, sizeof h);
  f.write(bits, MAP_BYTES);
  f.close();
}

// ---- tile download + decode into per-pixel classes
struct TileCtx { uint8_t *dst; uint8_t palCls[256]; bool havePal; PNG *png; };

static int pngDraw(PNGDRAW *d) {
  TileCtx *ctx = (TileCtx *)d->pUser;
  uint8_t *row = ctx->dst + d->y * 256;
  const uint8_t *p = d->pPixels;
  const int w = min(d->iWidth, 256);
  switch (d->iPixelType) {
    case PNG_PIXEL_INDEXED: {
      if (!ctx->havePal) {
        // classify each palette colour once
        for (int i = 0; i < 256; i++)
          ctx->palCls[i] = mapClassify(d->pPalette[i * 3], d->pPalette[i * 3 + 1], d->pPalette[i * 3 + 2]);
        ctx->havePal = true;
      }
      const int bpp = d->iBpp, perByte = 8 / bpp, mask = (1 << bpp) - 1;
      for (int x = 0; x < w; x++) {
        int idx = bpp == 8 ? p[x] : (p[x / perByte] >> ((perByte - 1 - x % perByte) * bpp)) & mask;
        row[x] = ctx->palCls[idx];
      }
      break;
    }
    case PNG_PIXEL_TRUECOLOR:
    case PNG_PIXEL_TRUECOLOR_ALPHA: {
      const int step = d->iPixelType == PNG_PIXEL_TRUECOLOR ? 3 : 4;
      for (int x = 0; x < w; x++, p += step) row[x] = mapClassify(p[0], p[1], p[2]);
      break;
    }
    case PNG_PIXEL_GRAYSCALE:
      for (int x = 0; x < w; x++) row[x] = mapClassify(p[x], p[x], p[x]);
      break;
    default:
      memset(row, MC_NONE, w);
  }
  return 1;
}

static bool fetchTile(int z, int x, int y, uint8_t *dst, uint8_t *dl, size_t dlCap, String &err) {
  char url[96];
  snprintf(url, sizeof url, "https://tile.openstreetmap.org/%d/%d/%d.png", z, x, y);
  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt) delay(1500 * attempt);
    WiFiClientSecure c;
    c.setInsecure();   // public map images
    HTTPClient http;
    if (!http.begin(c, url)) { err = "bad URL"; continue; }
    http.useHTTP10(true);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    // OSM's tile policy asks apps to identify themselves
    http.setUserAgent("FlightRadar-ESP32/" FW_VERSION " (personal flight radar display)");
    int code = http.GET();
    if (code != 200) {
      err = code < 0 ? "no connection to map server" : "map server said " + String(code);
      http.end();
      continue;
    }
    WiFiClient *s = http.getStreamPtr();
    int len = http.getSize();
    size_t got = 0;
    uint32_t t0 = millis();
    while ((len < 0 || (int)got < len) && got < dlCap && millis() - t0 < 20000) {
      int avail = s->available();
      if (avail > 0) got += s->readBytes(dl + got, min((size_t)avail, dlCap - got));
      else if (!s->connected()) break;
      else delay(2);
    }
    http.end();
    if (len > 0 && (int)got != len) { err = "map download cut off"; continue; }

    static PNG *png = new PNG();   // large decoder state; lands in PSRAM
    static TileCtx ctx;
    ctx.dst = dst;
    ctx.havePal = false;
    if (png->openRAM(dl, got, pngDraw) != PNG_SUCCESS) { err = "bad map image"; continue; }
    int rc = png->decode(&ctx, 0);
    png->close();
    if (rc == PNG_SUCCESS) return true;
    err = "map decode " + String(rc);
  }
  return false;
}

static double mercY(double lat) {
  double r = radians(lat);
  return (1.0 - log(tan(r) + 1.0 / cos(r)) / PI) / 2.0;
}

static bool build(const Settings s) {
  const int W = MAP_W, H = MAP_H, CX = scopeCx(s.layout), CY = scopeCy(s.layout);
  const float mpp = s.rangeNm * 1852.0f / SCOPE_R;   // meters per screen pixel
  uint8_t *bits = (uint8_t *)heap_caps_calloc(1, MAP_BYTES, MALLOC_CAP_SPIRAM);
  if (s.mapStyle == MAP_BLANK) {
    publish(bits);
    heap_caps_free(bits);
    setStatus("ready");
    return true;
  }

  const double cosLat = cos(radians(s.lat));
  const double degPerM_lat = 1.0 / 111320.0;
  const double degPerM_lon = 1.0 / (111320.0 * cosLat);
  // tile zoom whose pixels are about the size of a screen pixel (or a bit bigger)
  int z = (int)floor(log2(156543.034 * cosLat / mpp));
  z = constrain(z, 3, 16);
  const double world = 256.0 * (1 << z);

  double latN = s.lat + CY * mpp * degPerM_lat, latS = s.lat - (H - CY) * mpp * degPerM_lat;
  double lonW = s.lon - CX * mpp * degPerM_lon, lonE = s.lon + (W - CX) * mpp * degPerM_lon;
  int tx0 = (int)floor((lonW + 180) / 360 * world / 256), tx1 = (int)floor((lonE + 180) / 360 * world / 256);
  int ty0 = (int)floor(mercY(latN) * world / 256), ty1 = (int)floor(mercY(latS) * world / 256);
  int tw = tx1 - tx0 + 1, th = ty1 - ty0 + 1;
  if (tw * th > 16) { setStatus("error: map too large"); heap_caps_free(bits); return false; }

  uint8_t *tiles = (uint8_t *)heap_caps_malloc((size_t)tw * th * 65536, MALLOC_CAP_SPIRAM);
  bool *have = (bool *)calloc(tw * th, 1);
  const size_t dlCap = 256 * 1024;
  uint8_t *dl = (uint8_t *)heap_caps_malloc(dlCap, MALLOC_CAP_SPIRAM);
  uint8_t *cls = (uint8_t *)heap_caps_malloc(W * H, MALLOC_CAP_SPIRAM);
  if (!tiles || !have || !dl || !cls) {
    setStatus("error: out of memory");
    heap_caps_free(tiles); free(have); heap_caps_free(dl); heap_caps_free(cls); heap_caps_free(bits);
    return false;
  }

  int okCount = 0;
  String err;
  const int n = 1 << z;
  for (int j = 0; j < th; j++)
    for (int i = 0; i < tw; i++) {
      setStatus("downloading map " + String(j * tw + i + 1) + "/" + String(tw * th));
      int tx = ((tx0 + i) % n + n) % n, ty = constrain(ty0 + j, 0, n - 1);
      have[j * tw + i] = fetchTile(z, tx, ty, tiles + (size_t)(j * tw + i) * 65536, dl, dlCap, err);
      okCount += have[j * tw + i];
    }
  heap_caps_free(dl);
  if (okCount == 0) {
    setStatus("error: " + err);
    heap_caps_free(tiles); free(have); heap_caps_free(cls); heap_caps_free(bits);
    return false;
  }
  setStatus("drawing map");

  for (int py = 0; py < H; py++) {
    double lat = s.lat - (py - CY) * mpp * degPerM_lat;
    int gy = (int)floor(mercY(lat) * world - ty0 * 256.0);
    for (int px = 0; px < W; px++) {
      double lon = s.lon + (px - CX) * mpp * degPerM_lon;
      int gx = (int)floor((lon + 180) / 360 * world - tx0 * 256.0);
      int ti = gx >> 8, tj = gy >> 8;
      uint8_t c = MC_NONE;
      if (ti >= 0 && tj >= 0 && ti < tw && tj < th && have[tj * tw + ti])
        c = tiles[(size_t)(tj * tw + ti) * 65536 + (gy & 255) * 256 + (gx & 255)];
      cls[py * W + px] = c;
    }
  }
  heap_caps_free(tiles);
  free(have);

  mapRender(cls, s.mapStyle, bits);
  heap_caps_free(cls);
  setStatus("adding place names");
  mapDrawLabels(bits, s.lat, s.lon, mpp, CX, CY, s.layout, places_start, places_end - places_start,
                airports_start, airports_end - airports_start, s.placeNames);
  publish(bits);
  saveCache(s, bits);
  heap_caps_free(bits);
  setStatus(okCount < tw * th ? "ready (part of the map is missing)" : "ready");
  return true;
}

static void mapTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1500)) > 0) {}   // debounce
    Settings snap = cfgSnapshot();
    if (!snap.locSet) { setStatus("waiting for location"); continue; }
    if (loadCache(snap)) { setStatus("ready"); continue; }
    while (WiFi.status() != WL_CONNECTED) delay(1000);
    for (int attempt = 0; attempt < 3; attempt++) {
      if (build(snap)) break;
      // a newer request (different zoom/location) wins over retrying this one
      if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(15000 * (attempt + 1))) > 0) {
        xTaskNotifyGive(task);
        break;
      }
    }
  }
}

void mapStart() {
  lock = xSemaphoreCreateMutex();
  mapBits = (uint8_t *)heap_caps_calloc(1, MAP_BYTES, MALLOC_CAP_SPIRAM);
  LittleFS.begin(true);
  xTaskCreatePinnedToCore(mapTask, "map", 12 * 1024, nullptr, 1, &task, 0);
  mapRequest();
}

void mapRequest() {
  ready = false;   // old image no longer matches the scale/location
  ver++;
  if (task) xTaskNotifyGive(task);
}
#endif
