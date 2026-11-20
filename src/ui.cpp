#include "ui.h"
#include "display.h"
#include "config.h"
#include "flights.h"
#include "terrain.h"
#include "net.h"
#include <qrcode.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <time.h>
#include <vector>
#include <algorithm>

static const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static const int R = SCOPE_R;

// ---------- state
static GFXcanvas1 *base = nullptr;       // static scope: map, rings, bezel
static uint16_t *angLut = nullptr;       // angle of each screen pixel from the center, 0..65535
static int lutCx = -1, lutCy = -1;
static uint32_t baseKey = 0;             // what the base layer was built for
static uint32_t infoUntil = 0;
static int holdLeft = -1;
static AlertEvent banner;
static bool bannerOn = false;
static float lastBeam = 0;
static uint32_t lastFlightVer = 0;
static uint32_t lastCalmDraw = 0;

struct Track {
  char hex[7];
  float x, y;           // painted screen position
  float track, gs, dist;
  bool trackValid, emergency;
  uint8_t cls;
  char l1[20], l2[20];
  uint32_t paintMs;
};
static std::vector<Track> tracks;

// ---------- small drawing helpers
static void text(int x, int y, const char *s, const GFXfont *f = nullptr, uint16_t col = 1) {
  gfx->setFont(f);
  gfx->setTextColor(col);
  gfx->setCursor(x, y);
  gfx->print(s);
}

static int textW(const char *s, const GFXfont *f = nullptr) {
  int16_t x1, y1;
  uint16_t w, h;
  gfx->setFont(f);
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w;
}

static void textCentered(int cx, int y, const char *s, const GFXfont *f = nullptr) {
  text(cx - textW(s, f) / 2, y, s, f);
}

// classic 6x8 font with a white box behind it, for legibility over the map
static void label(int x, int y, const char *s, bool invert = false) {
  int w = strlen(s) * 6;
  gfx->fillRect(x - 1, y - 1, w + 1, 9, invert ? 1 : 0);
  text(x, y, s, nullptr, invert ? 0 : 1);
}

static void drawQr(int x, int y, int scale, const char *data) {
  QRCode qr;
  uint8_t buf[qrcode_getBufferSize(4)];
  if (qrcode_initText(&qr, buf, 4, ECC_LOW, data) != 0) return;
  gfx->fillRect(x - scale * 2, y - scale * 2, (qr.size + 4) * scale, (qr.size + 4) * scale, 0);
  for (int j = 0; j < qr.size; j++)
    for (int i = 0; i < qr.size; i++)
      if (qrcode_getModule(&qr, i, j)) gfx->fillRect(x + i * scale, y + j * scale, scale, scale, 1);
}

static void scopeCenter(const Settings &s, int &cx, int &cy) {
  cx = scopeCx(s.layout);
  cy = scopeCy(s.layout);
}

// distance from the center to the farthest screen corner
static float reachPx(int cx, int cy) {
  float dx = max(cx, SCREEN_W - 1 - cx), dy = max(cy, SCREEN_H - 1 - cy);
  return sqrtf(dx * dx + dy * dy);
}

static void buildAngleLut(int cx, int cy) {
  if (cx == lutCx && cy == lutCy) return;
  for (int y = 0; y < SCREEN_H; y++)
    for (int x = 0; x < SCREEN_W; x++) {
      float a = atan2f((float)(x - cx), (float)(cy - y));   // clockwise from north
      if (a < 0) a += 2 * PI;
      angLut[y * SCREEN_W + x] = (uint16_t)(a / (2 * PI) * 65536);
    }
  lutCx = cx;
  lutCy = cy;
}

static float niceStep(float range) {
  static const float steps[] = {0.5f, 1, 2, 2.5f, 5, 10, 20, 25, 50, 100};
  float raw = range / 3.2f;
  for (float st : steps)
    if (st >= raw) return st;
  return 100;
}

static void fmtRange(char *b, size_t n, float v) {
  if (v < 1) snprintf(b, n, "%.1f", v);
  else snprintf(b, n, "%d", (int)v);
}

// ---------- base layer
static void buildBase(const Settings &s) {
  int cx, cy;
  scopeCenter(s, cx, cy);
  base->fillScreen(0);

  // terrain fills the whole screen
  static uint8_t *map = (uint8_t *)heap_caps_malloc(MAP_BYTES, MALLOC_CAP_SPIRAM);
  if (s.mapStyle != MAP_BLANK && terrainCopy(map)) {
    uint8_t *dst = base->getBuffer();
    for (int y = 0; y < SCREEN_H; y++) memcpy(dst + y * (SCREEN_W / 8), map + y * MAP_ROW_BYTES, SCREEN_W / 8);
  }

  auto ringPx = [&](float rr, int every) {
    for (int a = 0; a < 720; a++) {
      if (a % every) continue;
      float t = radians(a / 2.0f);
      base->drawPixel(cx + lroundf(rr * sinf(t)), cy - lroundf(rr * cosf(t)), 1);
    }
  };
  auto ringLabel = [&](float rr, float d) {
    char b[12];
    fmtRange(b, sizeof b, d);
    int lx = cx + (int)(rr * 0.7071f) + 2, ly = cy + (int)(rr * 0.7071f) - 3;
    int w = strlen(b) * 6;
    base->fillRect(lx - 1, ly - 1, w + 1, 9, 0);
    base->setFont(nullptr);
    base->setTextColor(1);
    base->setCursor(lx, ly);
    base->print(b);
  };

  // dotted range rings all the way out to the corners
  const float reach = reachPx(cx, cy);
  if (s.rings) {
    float step = niceStep(s.rangeNm);
    for (float d = step; d / s.rangeNm * R < reach; d += step) {
      float rr = d / s.rangeNm * R;
      if (fabsf(rr - R) < 4) continue;   // the range ring itself is drawn solid
      ringPx(rr, 3);
      ringLabel(rr, d);
    }
  }

  // the selected range: a solid ring with a compass rose on it
  base->drawCircle(cx, cy, R, 1);
  ringLabel(R, s.rangeNm);
  for (int a = 0; a < 360; a += 10) {
    float t = radians(a);
    int len = a % 30 == 0 ? 7 : 3;
    for (int k = -len / 2; k <= len / 2 + (a % 30 == 0 ? 0 : 0); k++)
      base->drawPixel(cx + lroundf((R + k) * sinf(t)), cy - lroundf((R + k) * cosf(t)), 1);
  }
  base->setFont(nullptr);
  const char *card[4] = {"N", "E", "S", "W"};
  for (int i = 0; i < 4; i++) {
    float t = radians(i * 90);
    int x = cx + lroundf((R - 14) * sinf(t)) - 2, y = cy - lroundf((R - 14) * cosf(t)) - 3;
    base->fillRect(x - 2, y - 2, 9, 11, 0);
    base->setCursor(x, y);
    base->print(card[i]);
  }
  // home marker
  base->drawFastHLine(cx - 4, cy, 9, 1);
  base->drawFastVLine(cx, cy - 4, 9, 1);
  base->drawCircle(cx, cy, 2, 0);
  base->drawPixel(cx, cy, 1);
}

static uint32_t baseKeyFor(const Settings &s) {
  uint32_t k = terrainVersion() * 2654435761u;
  k ^= (uint32_t)(s.rangeNm * 100) * 40503u;
  k ^= (s.layout << 1) ^ (s.rings << 2) ^ (s.mapStyle << 4);
  return k;
}

// ---------- sweep
static void drawSweep(int cx, int cy, float beam) {
  const float trail = 70.0f;
  const float reach = reachPx(cx, cy);
  // bounding box of the wedge, clipped to the screen
  float xs[8], ys[8];
  int n = 0;
  xs[n] = 0; ys[n++] = 0;
  for (float a : {beam, beam - trail}) {
    xs[n] = sinf(radians(a)); ys[n++] = -cosf(radians(a));
  }
  for (int c = 0; c < 360; c += 90) {
    float d = fmodf(beam - c + 720, 360);
    if (d <= trail) { xs[n] = sinf(radians(c)); ys[n++] = -cosf(radians(c)); }
  }
  float x0 = 1, x1 = -1, y0 = 1, y1 = -1;
  for (int i = 0; i < n; i++) {
    x0 = min(x0, xs[i]); x1 = max(x1, xs[i]);
    y0 = min(y0, ys[i]); y1 = max(y1, ys[i]);
  }
  int px0 = max(0, cx + (int)floorf(x0 * reach)), px1 = min(SCREEN_W - 1, cx + (int)ceilf(x1 * reach));
  int py0 = max(0, cy + (int)floorf(y0 * reach)), py1 = min(SCREEN_H - 1, cy + (int)ceilf(y1 * reach));

  const uint16_t beamA = (uint16_t)(beam / 360.0f * 65536);
  const uint16_t trailA = (uint16_t)(trail / 360.0f * 65536);
  for (int y = py0; y <= py1; y++) {
    const uint16_t *row = angLut + y * SCREEN_W;
    for (int x = px0; x <= px1; x++) {
      uint16_t d = beamA - row[x];          // wraps naturally
      if (d > trailA) continue;
      float f = 1.0f - (float)d / trailA;   // 1 at the beam, 0 at the tail
      float dens = 0.55f * f * sqrtf(f);
      if (dens * 16 > BAYER4[y & 3][x & 3] + 0.5f) gfx->drawPixel(x, y, 1);
    }
  }
  // the beam itself, 2 px wide, out to the screen edge
  float t = radians(beam);
  int ex = cx + lroundf(reach * sinf(t)), ey = cy - lroundf(reach * cosf(t));
  gfx->drawLine(cx, cy, ex, ey, 1);
  int ox = fabsf(cosf(t)) > fabsf(sinf(t)) ? 1 : 0, oy = 1 - ox;
  gfx->drawLine(cx + ox, cy + oy, ex + ox, ey + oy, 1);
}

// ---------- aircraft
static void fmtAlt(char *b, size_t n, const Aircraft &a) {
  if (a.ground) snprintf(b, n, "GND");
  else if (!a.altValid) snprintf(b, n, "---");
  else if (a.alt >= 18000) snprintf(b, n, "FL%03d", (int)((a.alt + 50) / 100));
  else snprintf(b, n, "%d", (int)((a.alt + 50) / 100 * 100));
}

// Adds a token if the line stays short enough to read; whole tokens only.
static const size_t LABEL_MAX = 17;
static void appendTok(char *line, size_t n, const char *tok) {
  if (!tok[0]) return;
  size_t len = strlen(line), add = strlen(tok) + (len ? 1 : 0);
  if (len + add > LABEL_MAX) return;
  if (len) strlcat(line, " ", n);
  strlcat(line, tok, n);
}

static void buildLabel(const Settings &s, const Aircraft &a, Track &t) {
  char tok[16];
  t.l1[0] = t.l2[0] = 0;
  const bool *f = s.labelFields;
  if (f[LF_CALLSIGN]) appendTok(t.l1, sizeof t.l1, a.call[0] ? a.call : (a.reg[0] ? a.reg : a.hex));
  if (f[LF_REG] && a.reg[0] && strcmp(a.reg, a.call)) appendTok(t.l1, sizeof t.l1, a.reg);
  if (f[LF_TYPE]) appendTok(t.l1, sizeof t.l1, a.type);
  if (a.emergency) appendTok(t.l2, sizeof t.l2, a.squawk[0] ? a.squawk : "EMRG");
  if (f[LF_ALT]) { fmtAlt(tok, sizeof tok, a); appendTok(t.l2, sizeof t.l2, tok); }
  if (f[LF_VS] && a.vsValid && abs(a.vs) >= 300) {
    snprintf(tok, sizeof tok, "%c%d", a.vs > 0 ? 0x18 : 0x19, abs(a.vs) / 100 * 100);
    appendTok(t.l2, sizeof t.l2, tok);
  }
  if (f[LF_SPEED] && a.gsValid) { snprintf(tok, sizeof tok, "%dkt", (int)a.gs); appendTok(t.l2, sizeof t.l2, tok); }
  if (f[LF_HEADING] && a.trackValid) { snprintf(tok, sizeof tok, "%03d%c", (int)a.track, 0xF8); appendTok(t.l2, sizeof t.l2, tok); }
  if (f[LF_DIST]) { snprintf(tok, sizeof tok, "%.1fnm", a.distNm); appendTok(t.l2, sizeof t.l2, tok); }
  if (f[LF_SQUAWK] && a.squawk[0] && !a.emergency) appendTok(t.l2, sizeof t.l2, a.squawk);
}

static void projectXY(const Settings &s, int cx, int cy, double lat, double lon, float &x, float &y) {
  const float pxPerM = R / (s.rangeNm * 1852.0f);
  double dE = (lon - s.lon) * 111320.0 * cos(radians(s.lat));
  double dN = (lat - s.lat) * 111320.0;
  x = cx + dE * pxPerM;
  y = cy - dN * pxPerM;
}

static void drawSymbol(const Track &t, float pxPerNm, bool leaders, uint32_t now) {
  const int x = lroundf(t.x), y = lroundf(t.y);
  const float tr = radians(t.track);
  const float sx = sinf(tr), sy = -cosf(tr);

  if (leaders && t.trackValid && t.gs > 30) {
    float len = constrain(t.gs / 60.0f * pxPerNm, 4.0f, 36.0f);
    gfx->drawLine(x, y, lroundf(x + sx * (len + 4)), lroundf(y + sy * (len + 4)), 1);
  }
  gfx->fillCircle(x, y, 6, 0);   // clear a halo so the symbol reads over the map
  if (t.cls == CLS_HELI) {
    gfx->fillCircle(x, y, 3, 1);
    gfx->drawFastHLine(x - 5, y, 11, 1);
  } else if (!t.trackValid) {
    gfx->fillRect(x - 2, y - 2, 5, 5, 1);
  } else {
    // notched dart pointing along the track
    const float px = -sy, py = sx;   // perpendicular
    auto P = [&](float fwd, float side, int &ox, int &oy) {
      ox = lroundf(x + sx * fwd + px * side);
      oy = lroundf(y + sy * fwd + py * side);
    };
    int tx, ty, lx, ly, nx, ny, rx, ry;
    P(6, 0, tx, ty);
    P(-4, 4.5f, lx, ly);
    P(-1.5f, 0, nx, ny);
    P(-4, -4.5f, rx, ry);
    if (t.cls == CLS_MILITARY) {
      gfx->drawLine(tx, ty, lx, ly, 1); gfx->drawLine(lx, ly, nx, ny, 1);
      gfx->drawLine(nx, ny, rx, ry, 1); gfx->drawLine(rx, ry, tx, ty, 1);
    } else {
      gfx->fillTriangle(tx, ty, lx, ly, nx, ny, 1);
      gfx->fillTriangle(tx, ty, nx, ny, rx, ry, 1);
    }
  }
  if (t.emergency && (now / 400) % 2) gfx->drawCircle(x, y, 9, 1);

  // radar "ping": a ring that expands for a moment after the beam passes
  uint32_t age = now - t.paintMs;
  if (age < 350) gfx->drawCircle(x, y, 7 + age / 35, 1);
}

struct Rect { int x, y, w, h; };
static bool overlaps(const Rect &a, const Rect &b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void drawLabels(const Settings &s, int cx, int cy) {
  std::vector<Rect> placed;
  placed.reserve(tracks.size() * 2);
  // labels must not cover other aircraft
  for (const Track &t : tracks) placed.push_back({(int)lroundf(t.x) - 6, (int)lroundf(t.y) - 6, 13, 13});
  if (s.layout == LAYOUT_FULL) {   // corner info boxes
    placed.push_back({0, 0, 84, 27});
    placed.push_back({SCREEN_W - 100, 0, 100, 27});
    placed.push_back({0, SCREEN_H - 16, 110, 16});
    placed.push_back({SCREEN_W - 60, SCREEN_H - 16, 60, 16});
  }
  const int maxX = s.layout == LAYOUT_PANEL ? 298 : SCREEN_W - 1;
  for (size_t i = 0; i < tracks.size(); i++) {
    const Track &t = tracks[i];
    int lines = (t.l1[0] ? 1 : 0) + (t.l2[0] ? 1 : 0);
    if (!lines) continue;
    int w = max(strlen(t.l1), strlen(t.l2)) * 6 + 2, h = lines * 9 + 1;
    const int x = lroundf(t.x), y = lroundf(t.y);
    const int cand[8][2] = {{9, -h - 1}, {9, 3},        {-9 - w, -h - 1}, {-9 - w, 3},
                            {-w / 2, -h - 9}, {-w / 2, 9}, {14, -h / 2},     {-14 - w, -h / 2}};
    bool done = false;
    for (int k = 0; k < 8 && !done; k++) {
      Rect r = {x + cand[k][0], y + cand[k][1], w, h};
      if (r.x < 1 || r.y < 1 || r.x + r.w > maxX || r.y + r.h > SCREEN_H - 1) continue;
      bool clash = false;
      for (const Rect &p : placed) if (overlaps(r, p)) { clash = true; break; }
      if (clash) continue;
      placed.push_back(r);
      gfx->fillRect(r.x, r.y, r.w, r.h, t.emergency ? 1 : 0);
      int ty = r.y + 1;
      if (t.l1[0]) { text(r.x + 1, ty, t.l1, nullptr, t.emergency ? 0 : 1); ty += 9; }
      if (t.l2[0]) text(r.x + 1, ty, t.l2, nullptr, t.emergency ? 0 : 1);
      done = true;
    }
  }
}

// ---------- side panel / corner info
static void clockStr(char *b, size_t n, char *date, size_t dn) {
  struct tm tm;
  if (getLocalTime(&tm, 0)) {
    strftime(b, n, "%H:%M", &tm);
    if (date) strftime(date, dn, "%a %d %b", &tm);
  } else {
    snprintf(b, n, "--:--");
    if (date) date[0] = 0;
  }
}

static void drawPanel(const Settings &s, const std::vector<Aircraft> &list, const FlightStatus &fs) {
  const int X = 306;
  char b[40], d[24];
  // the panel sits on top of the map
  gfx->fillRect(300, 0, SCREEN_W - 300, SCREEN_H, 0);
  gfx->drawFastVLine(300, 0, SCREEN_H, 1);
  gfx->drawFastVLine(302, 0, SCREEN_H, 1);
  clockStr(b, sizeof b, d, sizeof d);
  text(X, 22, b, &FreeSansBold12pt7b);
  text(X, 36, d);

  snprintf(b, sizeof b, "%d", fs.shown);
  text(X, 62, b, &FreeSansBold12pt7b);
  text(X + textW(b, &FreeSansBold12pt7b) + 8, 55, "AIRCRAFT");
  char r[12];
  fmtRange(r, sizeof r, s.rangeNm);
  snprintf(b, sizeof b, "RANGE %s NM", r);
  text(X, 68, b);
  gfx->drawFastHLine(302, 80, 98, 1);

  // nearest traffic
  int y = 86, shown = 0;
  for (const Aircraft &a : list) {
    if (!a.visible) continue;
    if (y > SCREEN_H - 40) break;
    const char *id = a.call[0] ? a.call : (a.reg[0] ? a.reg : a.hex);
    if (a.emergency) {
      gfx->fillRect(303, y - 1, 97, 19, 1);
      text(X, y, id, nullptr, 0);
      snprintf(b, sizeof b, "SQK %s", a.squawk[0] ? a.squawk : "EMRG");
      text(X, y + 9, b, nullptr, 0);
    } else {
      text(X, y, id);
      if (a.type[0]) text(399 - strlen(a.type) * 6, y, a.type);
      char alt[10];
      fmtAlt(alt, sizeof alt, a);
      snprintf(b, sizeof b, "%s %4.1fnm", alt, a.distNm);
      text(X, y + 9, b);
    }
    y += 22;
    shown++;
  }
  if (!shown) text(X, y, fs.lastOkMs ? "no traffic" : "");

  // status footer
  gfx->drawFastHLine(302, SCREEN_H - 30, 98, 1);
  String ts = terrainStatus();
  if (!fs.error.isEmpty() && fs.failures > 0) {
    text(X, SCREEN_H - 26, "NO DATA");
    text(X, SCREEN_H - 17, "retrying...");
  } else if (fs.lastOkMs) {
    uint32_t age = (millis() - fs.lastOkMs) / 1000;
    snprintf(b, sizeof b, "LIVE %lus", (unsigned long)age);
    text(X, SCREEN_H - 26, b);
    text(X, SCREEN_H - 17, sourceName(fs.activeSource));
  } else {
    text(X, SCREEN_H - 26, "CONNECTING");
  }
  if (ts.startsWith("downloading")) text(X, SCREEN_H - 8, ("map " + ts.substring(ts.lastIndexOf(' ') + 1)).c_str());
  else if (ts.startsWith("drawing")) text(X, SCREEN_H - 8, "drawing map");
  else text(X, SCREEN_H - 8, netIp().c_str());
}

// a framed white box, so overlay text reads cleanly over the map
static void box(int x, int y, int w, int h) {
  gfx->fillRect(x, y, w, h, 0);
  gfx->drawRect(x, y, w, h, 1);
}

static void drawCorners(const Settings &s, const FlightStatus &fs) {
  char b[32];
  clockStr(b, sizeof b, nullptr, 0);
  int w = textW(b, &FreeSansBold12pt7b);
  box(0, 0, w + 12, 26);
  text(6, 20, b, &FreeSansBold12pt7b);

  snprintf(b, sizeof b, "%d", fs.shown);
  w = textW(b, &FreeSansBold12pt7b);
  int bw = w + 8 + 48 + 10;
  box(SCREEN_W - bw, 0, bw, 26);
  text(SCREEN_W - bw + 5, 20, b, &FreeSansBold12pt7b);
  text(SCREEN_W - 53, 10, "AIRCRAFT");

  char r[12];
  fmtRange(r, sizeof r, s.rangeNm);
  snprintf(b, sizeof b, "RANGE %s NM", r);
  box(0, SCREEN_H - 15, strlen(b) * 6 + 8, 15);
  text(4, SCREEN_H - 11, b);
  const char *st = fs.failures > 0 ? "NO DATA" : (fs.lastOkMs ? "LIVE" : "WAIT");
  int sw = strlen(st) * 6 + 8;
  box(SCREEN_W - sw, SCREEN_H - 15, sw, 15);
  text(SCREEN_W - sw + 4, SCREEN_H - 11, st);
}

// ---------- full-screen pages
static void pageSetup(bool fallback) {
  gfx->fillScreen(0);
  text(16, 30, "FlightRadar", &FreeSansBold12pt7b);
  if (fallback) {
    text(16, 52, "Can't reach your Wi-Fi", &FreeSansBold9pt7b);
    String s = "\"" + netSsid() + "\" - retrying. To change it:";
    text(16, 68, s.c_str());
  } else {
    text(16, 52, "Let's get connected", &FreeSansBold9pt7b);
  }
  text(16, 92, "1. On your phone, join the Wi-Fi", &FreeSans9pt7b);
  text(30, 112, netApName().c_str(), &FreeSansBold9pt7b);
  text(16, 136, "   (or scan the code)", &FreeSans9pt7b);
  text(16, 168, "2. A setup page opens. Pick", &FreeSans9pt7b);
  text(16, 188, "   your home Wi-Fi.", &FreeSans9pt7b);
  text(16, 220, "If it doesn't open, browse to", &FreeSans9pt7b);
  text(16, 240, "   192.168.4.1", &FreeSansBold9pt7b);
  String wifiQr = "WIFI:T:nopass;S:" + netApName() + ";;";
  drawQr(282, 96, 3, wifiQr.c_str());
}

static void pageConnecting() {
  gfx->fillScreen(0);
  text(16, 30, "FlightRadar", &FreeSansBold12pt7b);
  text(16, 70, "Connecting to Wi-Fi...", &FreeSansBold9pt7b);
  text(16, 92, netSsid().c_str(), &FreeSans9pt7b);
  int dots = (millis() / 400) % 4;
  for (int i = 0; i < dots; i++) gfx->fillCircle(20 + i * 14, 120, 4, 1);
}

static void pageInfo(const Settings &s, bool locating) {
  gfx->fillScreen(0);
  text(16, 30, "FlightRadar", &FreeSansBold12pt7b);
  if (locating) text(16, 54, "Finding your location...", &FreeSansBold9pt7b);
  text(16, 84, "Settings page:", &FreeSans9pt7b);
  String url = "http://" + netHost();
  text(16, 106, url.c_str(), &FreeSansBold9pt7b);
  String ip = "or http://" + netIp();
  text(16, 128, ip.c_str(), &FreeSans9pt7b);
  text(16, 158, ("Wi-Fi: " + netSsid()).c_str());
  char b[64];
  snprintf(b, sizeof b, "Signal: %d dBm   Firmware %s", netRssi(), FW_VERSION);
  text(16, 170, b);
  if (s.locSet) {
    snprintf(b, sizeof b, "Location: %.4f, %.4f %s", s.lat, s.lon, s.locApprox ? "(approx.)" : "");
    text(16, 182, b);
  }
  text(16, 210, "Buttons:");
  text(16, 222, " KEY  = zoom in    BOOT = zoom out");
  text(16, 234, " hold BOOT = labels on/off");
  text(16, 246, " hold KEY 10 s = factory reset");
  drawQr(292, 60, 3, ("http://" + netIp() + "/").c_str());
}

static void overlayHold() {
  gfx->fillRect(40, 100, 320, 100, 0);
  gfx->drawRect(40, 100, 320, 100, 1);
  gfx->drawRect(42, 102, 316, 96, 1);
  textCentered(200, 135, "Factory reset", &FreeSansBold12pt7b);
  char b[48];
  snprintf(b, sizeof b, "Keep holding KEY: %d", holdLeft);
  textCentered(200, 165, b, &FreeSans9pt7b);
  textCentered(200, 185, "Release to cancel");
}

void uiMessage(const char *title, const char *line) {
  gfx->fillScreen(0);
  textCentered(200, 135, title, &FreeSansBold12pt7b);
  textCentered(200, 165, line, &FreeSans9pt7b);
  displayPush();
}

// ---------- public
void uiInit() {
  base = new GFXcanvas1(SCREEN_W, SCREEN_H);
  angLut = (uint16_t *)heap_caps_malloc(SCREEN_W * SCREEN_H * 2, MALLOC_CAP_SPIRAM);
}

void uiShowInfo(uint32_t ms) { infoUntil = millis() + ms; }
void uiSetHoldProgress(int s) { holdLeft = s; }

static bool isNight(const Settings &s) {
  if (s.nightMode == NIGHT_OFF) return false;
  struct tm tm;
  if (!getLocalTime(&tm, 0)) return false;
  int h = tm.tm_hour;
  return s.nightStart > s.nightEnd ? (h >= s.nightStart || h < s.nightEnd)
                                   : (h >= s.nightStart && h < s.nightEnd);
}

bool uiFrame() {
  const Settings s = cfgSnapshot();
  const uint32_t now = millis();
  static bool panelOff = false;

  NetState ns = netState();
  if (holdLeft >= 0) {
    if (panelOff) { displayPower(true); panelOff = false; }
    overlayHold();
    return true;
  }
  if (ns == NET_SETUP || ns == NET_FALLBACK) {
    if (panelOff) { displayPower(true); panelOff = false; }
    pageSetup(ns == NET_FALLBACK);
    return true;
  }
  if (ns == NET_CONNECTING) { pageConnecting(); return true; }
  if (now < infoUntil || !s.locSet) {
    if (panelOff) { displayPower(true); panelOff = false; }
    pageInfo(s, !s.locSet);
    return true;
  }

  // night handling
  const bool night = isNight(s);
  if (night && s.nightMode == NIGHT_BLANK) {
    if (!panelOff) { displayPower(false); panelOff = true; }
    return false;
  }
  if (panelOff) { displayPower(true); panelOff = false; }
  const bool calm = night && s.nightMode == NIGHT_CALM;
  const uint32_t fv = flightsVersion();
  if (calm && fv == lastFlightVer && now - lastCalmDraw < 30000) return false;
  lastCalmDraw = now;

  // static layer
  uint32_t key = baseKeyFor(s);
  if (key != baseKey) {
    buildBase(s);
    baseKey = key;
  }
  memcpy(gfx->getBuffer(), base->getBuffer(), SCREEN_W * SCREEN_H / 8);

  int cx, cy;
  scopeCenter(s, cx, cy);
  buildAngleLut(cx, cy);
  const int visMaxX = s.layout == LAYOUT_PANEL ? 296 : SCREEN_W - 4;
  const float period = s.sweepSec * 1000.0f;
  const float beam = fmodf(now, period) / period * 360.0f;

  std::vector<Aircraft> list;
  flightsCopy(list);
  FlightStatus fs = flightsStatus();

  // paint aircraft the beam passed since the last frame
  const float pxPerNm = R / s.rangeNm;
  const bool newData = fv != lastFlightVer;
  lastFlightVer = fv;
  float swept = fmodf(beam - lastBeam + 360.0f, 360.0f);
  for (const Aircraft &a : list) {
    if (!a.visible) continue;
    float since = fmodf(beam - a.bearing + 360.0f, 360.0f);
    bool crossed = calm || since <= swept;
    Track *t = nullptr;
    for (Track &tr : tracks) if (!strcmp(tr.hex, a.hex)) { t = &tr; break; }
    if (!t && !crossed) continue;
    if (!t) {
      tracks.push_back(Track{});
      t = &tracks.back();
      strlcpy(t->hex, a.hex, sizeof t->hex);
    }
    if (crossed || (newData && calm)) {
      float x, y;
      projectXY(s, cx, cy, a.lat, a.lon, x, y);
      if (x < 3 || y < 3 || x > visMaxX || y > SCREEN_H - 4) { t->paintMs = 0; continue; }
      t->x = x; t->y = y;
      t->track = a.track; t->trackValid = a.trackValid;
      t->gs = a.gs; t->dist = a.distNm;
      t->cls = a.cls; t->emergency = a.emergency;
      buildLabel(s, a, *t);
      t->paintMs = calm ? now - 10000 : now;   // no ping animation at night
    }
  }
  lastBeam = beam;

  // forget aircraft not repainted for 1.5 revolutions (left the area/filter)
  const uint32_t ttl = calm ? 120000 : (uint32_t)(period * 1.5f);
  tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                              [&](const Track &t) { return t.paintMs == 0 || now - t.paintMs > ttl; }),
               tracks.end());
  std::sort(tracks.begin(), tracks.end(), [](const Track &a, const Track &b) {
    if (a.emergency != b.emergency) return a.emergency;
    return a.dist < b.dist;
  });

  if (!calm) drawSweep(cx, cy, beam);
  for (const Track &t : tracks) drawSymbol(t, pxPerNm, s.leaders, now);
  if (s.labels) drawLabels(s, cx, cy);

  if (s.layout == LAYOUT_PANEL) drawPanel(s, list, fs);
  else drawCorners(s, fs);

  // map / data notices inside the scope
  if (!terrainReady() && s.mapStyle != MAP_BLANK) {
    String ts = terrainStatus();
    if (ts.startsWith("downloading") || ts.startsWith("drawing")) label(cx - 45, cy + R - 40, "drawing map...");
  }
  if (!fs.lastOkMs && fs.failures == 0) label(cx - 48, cy + 20, "waiting for data");

  // alert banner
  AlertEvent ev;
  if (alertsPending(ev)) { banner = ev; bannerOn = true; }
  if (bannerOn) {
    if (now - banner.ms > 10000) bannerOn = false;
    else {
      gfx->fillRect(0, SCREEN_H - 20, SCREEN_W, 20, 1);
      gfx->setFont(&FreeSansBold9pt7b);
      gfx->setTextColor(0);
      gfx->setCursor(8, SCREEN_H - 6);
      gfx->print(banner.text);
    }
  }
  return true;
}
