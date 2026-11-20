#include "ui.h"
#include "display.h"
#include "config.h"
#include "flights.h"
#include "basemap.h"
#include "fmt.h"
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
  char lines[3][20];    // label text, up to 3 lines
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

  // the street map fills the whole screen
  static uint8_t *map = (uint8_t *)heap_caps_malloc(MAP_BYTES, MALLOC_CAP_SPIRAM);
  if (s.mapStyle != MAP_BLANK && mapCopy(map)) {
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
  const float rangeDisp = nmTo(s.units, s.rangeNm);   // range in the user's units
  auto ringLabel = [&](float rr, float d, bool unit) {
    char b[16];
    fmtRange(b, sizeof b, d);
    if (unit) { strlcat(b, " ", sizeof b); strlcat(b, unitDist(s.units), sizeof b); }
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
    float step = niceStep(rangeDisp);
    for (float d = step; d / rangeDisp * R < reach; d += step) {
      float rr = d / rangeDisp * R;
      if (fabsf(rr - R) < 4) continue;   // the range ring itself is drawn solid
      ringPx(rr, 3);
      ringLabel(rr, d, false);
    }
  }

  // the selected range: a solid ring with a compass rose on it
  base->drawCircle(cx, cy, R, 1);
  ringLabel(R, rangeDisp, true);
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
  // home: a little house
  base->fillRect(cx - 7, cy - 8, 15, 15, 0);
  base->fillTriangle(cx - 6, cy - 1, cx, cy - 7, cx + 6, cy - 1, 1);
  base->drawRect(cx - 4, cy - 1, 9, 7, 1);
  base->fillRect(cx - 1, cy + 2, 3, 4, 1);

  // map credit, required by OpenStreetMap
  if (s.mapStyle != MAP_BLANK) {
    const char *credit = "(c) OpenStreetMap";
    int y = SCREEN_H - 9, x = s.layout == LAYOUT_FULL ? SCREEN_W / 2 - 51 : 2;
    base->fillRect(x - 1, y - 1, strlen(credit) * 6 + 1, 9, 0);
    base->setCursor(x, y);
    base->print(credit);
  }
}

static uint32_t baseKeyFor(const Settings &s) {
  uint32_t k = mapVersion() * 2654435761u;
  k ^= (uint32_t)(s.rangeNm * 100) * 40503u;
  k ^= (s.layout << 1) ^ (s.rings << 2) ^ (s.mapStyle << 4) ^ (s.units << 7);
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
// Adds a token if the line stays short enough to read; whole tokens only.
static const size_t LABEL_MAX = 18;
static bool appendTok(char *line, size_t n, const char *tok) {
  if (!tok || !tok[0]) return true;
  size_t len = strlen(line), add = strlen(tok) + (len ? 1 : 0);
  if (len + add > LABEL_MAX) return false;
  if (len) strlcat(line, " ", n);
  strlcat(line, tok, n);
  return true;
}

// Builds up to three short lines, e.g.
//   Delta 1234
//   Boeing 737
//   35,000 ft ^ 520 mph
static void buildLabel(const Settings &s, const Aircraft &a, Track &t) {
  for (auto &l : t.lines) l[0] = 0;
  const bool *f = s.labelFields;
  char tok[24];
  int li = 0;
  auto put = [&](const char *tk) {
    if (!tk || !tk[0]) return;
    while (li < 3 && !appendTok(t.lines[li], sizeof t.lines[li], tk)) li++;
  };
  auto newLine = [&]() { if (li < 3 && t.lines[li][0]) li++; };

  if (f[LF_FLIGHT]) { flightName(tok, sizeof tok, a); put(tok); }
  if (f[LF_CODE] && a.call[0]) put(a.call);
  if (f[LF_REG] && a.reg[0] && strcmp(a.reg, a.call)) put(a.reg);
  newLine();
  if (f[LF_MODEL]) put(modelName(a));
  if (f[LF_TYPE] && a.type[0]) put(a.type);
  newLine();
  if (a.emergency) { snprintf(tok, sizeof tok, "EMERGENCY"); put(tok); }
  if (f[LF_ALT]) {
    fmtAltitude(tok, sizeof tok, a, s.units);
    if (f[LF_VS] && a.vsValid && abs(a.vs) >= 300 && !a.ground) {
      size_t l = strlen(tok);
      if (l + 2 < sizeof tok) { tok[l] = ' '; tok[l + 1] = a.vs > 0 ? 0x18 : 0x19; tok[l + 2] = 0; }
    }
    put(tok);
  } else if (f[LF_VS] && a.vsValid && abs(a.vs) >= 300) {
    put(a.vs > 0 ? "climbing" : "descending");
  }
  if (f[LF_SPEED]) { fmtSpeed(tok, sizeof tok, a, s.units); put(tok); }
  if (f[LF_DIR] && a.trackValid) { snprintf(tok, sizeof tok, "to %s", compass8(a.track)); put(tok); }
  if (f[LF_DIST]) { fmtDist(tok, sizeof tok, a.distNm, s.units); put(tok); }
  if (f[LF_SQUAWK] && a.squawk[0]) { snprintf(tok, sizeof tok, "sq %s", a.squawk); put(tok); }
}

static void projectXY(const Settings &s, int cx, int cy, double lat, double lon, float &x, float &y) {
  const float pxPerM = R / (s.rangeNm * 1852.0f);
  double dE = (lon - s.lon) * 111320.0 * cos(radians(s.lat));
  double dN = (lat - s.lat) * 111320.0;
  x = cx + dE * pxPerM;
  y = cy - dN * pxPerM;
}

// Rotated polygon helpers. Shapes are defined pointing north (+y = forward).
struct Pt { float f, s; };   // forward, sideways (+ = right wing)
static void fillQuad(int x, int y, float sx, float sy, float k, Pt a, Pt b, Pt c, Pt d) {
  const float px = -sy, py = sx;
  auto X = [&](Pt p) { return (int)lroundf(x + (sx * p.f + px * p.s) * k); };
  auto Y = [&](Pt p) { return (int)lroundf(y + (sy * p.f + py * p.s) * k); };
  gfx->fillTriangle(X(a), Y(a), X(b), Y(b), X(c), Y(c), 1);
  gfx->fillTriangle(X(a), Y(a), X(c), Y(c), X(d), Y(d), 1);
}

// A little airliner silhouette: fuselage, swept wings, tailplane.
static void drawPlane(int x, int y, float trackDeg, float k) {
  const float tr = radians(trackDeg), sx = sinf(tr), sy = -cosf(tr);
  fillQuad(x, y, sx, sy, k, {7.5f, 0}, {5.5f, 1.3f}, {-6, 1.0f}, {-6, -1.0f});        // nose + body (right half)
  fillQuad(x, y, sx, sy, k, {7.5f, 0}, {5.5f, -1.3f}, {-6, -1.0f}, {-6, 1.0f});       // body (left half)
  fillQuad(x, y, sx, sy, k, {2.5f, 0.8f}, {-1.5f, 7.5f}, {-3, 7.5f}, {-1.5f, 0.8f});  // right wing
  fillQuad(x, y, sx, sy, k, {2.5f, -0.8f}, {-1.5f, -7.5f}, {-3, -7.5f}, {-1.5f, -0.8f});
  fillQuad(x, y, sx, sy, k, {-4.5f, 0.6f}, {-6.5f, 3.5f}, {-7.5f, 3.5f}, {-6.5f, 0.6f});  // tailplane
  fillQuad(x, y, sx, sy, k, {-4.5f, -0.6f}, {-6.5f, -3.5f}, {-7.5f, -3.5f}, {-6.5f, -0.6f});
}

// Helicopter: round cabin, tail boom, tail rotor and a spinning main rotor.
static void drawHeli(int x, int y, float trackDeg, uint32_t now) {
  const float tr = radians(trackDeg), sx = sinf(tr), sy = -cosf(tr), px = -sy, py = sx;
  auto P = [&](float f, float s, int &ox, int &oy) { ox = lroundf(x + sx * f + px * s); oy = lroundf(y + sy * f + py * s); };
  int ax, ay, bx, by;
  gfx->fillCircle(lroundf(x + sx * 1.5f), lroundf(y + sy * 1.5f), 3, 1);   // cabin
  P(-1, 0, ax, ay); P(-8, 0, bx, by);
  gfx->drawLine(ax, ay, bx, by, 1);                                          // tail boom
  P(-8, -2, ax, ay); P(-8, 2, bx, by);
  gfx->drawLine(ax, ay, bx, by, 1);                                          // tail rotor
  const float rot = (now % 1000) / 1000.0f * PI;                             // rotor spins
  for (int i = 0; i < 2; i++) {
    float r = rot + i * PI / 2;
    int ex = lroundf(8 * cosf(r)), ey = lroundf(8 * sinf(r));
    gfx->drawLine(x - ex, y - ey, x + ex, y + ey, 1);
  }
}

static void drawSymbol(const Track &t, float pxPerNm, bool leaders, uint32_t now) {
  const int x = lroundf(t.x), y = lroundf(t.y);
  const float tr = radians(t.track);
  const float sx = sinf(tr), sy = -cosf(tr);

  if (leaders && t.trackValid && t.gs > 30) {
    float len = constrain(t.gs / 60.0f * pxPerNm, 4.0f, 36.0f);
    gfx->drawLine(lroundf(x + sx * 8), lroundf(y + sy * 8), lroundf(x + sx * (len + 9)), lroundf(y + sy * (len + 9)), 1);
  }
  gfx->fillCircle(x, y, 9, 0);   // clear a halo so the icon reads over the map
  const float heading = t.trackValid ? t.track : 0;
  if (t.cls == CLS_HELI) {
    drawHeli(x, y, heading, now);
  } else {
    float k = (t.cls == CLS_LIGHT || t.cls == CLS_GLIDER || t.cls == CLS_DRONE) ? 0.8f : 1.0f;
    drawPlane(x, y, heading, k);
  }
  if (t.emergency && (now / 400) % 2) gfx->drawCircle(x, y, 11, 1);

  // radar "ping": a ring that expands for a moment after the beam passes
  uint32_t age = now - t.paintMs;
  if (age < 350) gfx->drawCircle(x, y, 10 + age / 35, 1);
}

struct Rect { int x, y, w, h; };
static bool overlaps(const Rect &a, const Rect &b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void drawLabels(const Settings &s, int cx, int cy) {
  std::vector<Rect> placed;
  placed.reserve(tracks.size() * 2);
  // labels must not cover other aircraft
  for (const Track &t : tracks) placed.push_back({(int)lroundf(t.x) - 8, (int)lroundf(t.y) - 8, 17, 17});
  if (s.layout == LAYOUT_FULL) {   // corner info boxes
    placed.push_back({0, 0, 110, 27});
    placed.push_back({SCREEN_W - 110, 0, 110, 27});
    placed.push_back({0, SCREEN_H - 16, 130, 16});
    placed.push_back({SCREEN_W - 80, SCREEN_H - 16, 80, 16});
  }
  const int maxX = s.layout == LAYOUT_PANEL ? 298 : SCREEN_W - 1;
  for (size_t i = 0; i < tracks.size(); i++) {
    const Track &t = tracks[i];
    int lines = 0, w = 0;
    for (auto &l : t.lines) if (l[0]) { lines++; w = max(w, (int)strlen(l) * 6); }
    if (!lines) continue;
    w += 2;
    const int h = lines * 9 + 1;
    const int x = lroundf(t.x), y = lroundf(t.y);
    const int cand[8][2] = {{11, -h / 2},  {-11 - w, -h / 2}, {10, -h - 2},  {10, 3},
                            {-10 - w, -h - 2}, {-10 - w, 3}, {-w / 2, -h - 11}, {-w / 2, 11}};
    for (int k = 0; k < 8; k++) {
      Rect r = {x + cand[k][0], y + cand[k][1], w, h};
      if (r.x < 1 || r.y < 1 || r.x + r.w > maxX || r.y + r.h > SCREEN_H - 1) continue;
      bool clash = false;
      for (const Rect &p : placed) if (overlaps(r, p)) { clash = true; break; }
      if (clash) continue;
      placed.push_back(r);
      gfx->fillRect(r.x, r.y, r.w, r.h, t.emergency ? 1 : 0);
      int ty = r.y + 1;
      for (auto &l : t.lines)
        if (l[0]) { text(r.x + 1, ty, l, nullptr, t.emergency ? 0 : 1); ty += 9; }
      break;
    }
  }
}

// ---------- side panel / corner info
static void clockStr(const Settings &s, char *b, size_t n, char *date, size_t dn) {
  struct tm tm;
  if (getLocalTime(&tm, 0)) {
    if (s.units == UNITS_METRIC) strftime(b, n, "%H:%M", &tm);
    else {
      int h = tm.tm_hour % 12;
      snprintf(b, n, "%d:%02d", h ? h : 12, tm.tm_min);
    }
    if (date) strftime(date, dn, "%a %b %d", &tm);
    if (s.units != UNITS_METRIC && n > 8) strlcat(b, tm.tm_hour < 12 ? "a" : "p", n);   // marker for AM/PM
  } else {
    snprintf(b, n, "--:--");
    if (date) date[0] = 0;
  }
}

// Clock strings end in 'a'/'p' for AM/PM: draw the time big, "AM"/"PM" small.
static int clockW(const char *b) {
  char t[12];
  strlcpy(t, b, sizeof t);
  size_t l = strlen(t);
  bool ap = l && (t[l - 1] == 'a' || t[l - 1] == 'p');
  if (ap) t[l - 1] = 0;
  return textW(t, &FreeSansBold12pt7b) + (ap ? 16 : 0);
}
static void drawClock(int x, int y, const char *b) {
  char t[12];
  strlcpy(t, b, sizeof t);
  size_t l = strlen(t);
  char ap = l ? t[l - 1] : 0;
  if (ap == 'a' || ap == 'p') t[l - 1] = 0; else ap = 0;
  text(x, y, t, &FreeSansBold12pt7b);
  if (ap) text(x + textW(t, &FreeSansBold12pt7b) + 3, y - 7, ap == 'a' ? "AM" : "PM");
}

static void fitText(char *s, int maxChars) {
  if ((int)strlen(s) > maxChars) s[maxChars] = 0;
}

static void drawPanel(const Settings &s, const std::vector<Aircraft> &list, const FlightStatus &fs) {
  const int X = 306, CH = 15;   // 15 characters fit across the panel
  char b[48], d[32];
  // the panel sits on top of the map
  gfx->fillRect(300, 0, SCREEN_W - 300, SCREEN_H, 0);
  gfx->drawFastVLine(300, 0, SCREEN_H, 1);
  gfx->drawFastVLine(302, 0, SCREEN_H, 1);
  clockStr(s, b, sizeof b, d, sizeof d);
  drawClock(X, 22, b);
  text(X, 28, d);

  snprintf(b, sizeof b, "%d", fs.shown);
  text(X, 60, b, &FreeSansBold12pt7b);
  int nx = X + textW(b, &FreeSansBold12pt7b) + 6;
  text(nx, 45, fs.shown == 1 ? "plane" : "planes");
  char r[12];
  fmtRange(r, sizeof r, nmTo(s.units, s.rangeNm));
  snprintf(b, sizeof b, "in %s %s", r, unitDist(s.units));
  text(nx, 54, b);
  gfx->drawFastHLine(302, 67, 98, 1);

  // the closest plane, spelled out
  const Aircraft *first = nullptr;
  for (const Aircraft &a : list) if (a.visible) { first = &a; break; }
  int y = 72;
  if (first) {
    const Aircraft &a = *first;
    text(X, y, a.emergency ? "! EMERGENCY !" : "CLOSEST");
    y += 11;
    flightName(b, sizeof b, a); fitText(b, CH);
    text(X, y, b); y += 9;
    snprintf(b, sizeof b, "%s", modelName(a)); fitText(b, CH);
    text(X, y, b); y += 9;
    fmtAltitude(b, sizeof b, a, s.units); text(X, y, b); y += 9;
    char dist[16];
    fmtDist(dist, sizeof dist, a.distNm, s.units);
    snprintf(b, sizeof b, "%s %s", dist, compass8(a.bearing)); fitText(b, CH);
    text(X, y, b); y += 9;
    gfx->drawFastHLine(302, y + 2, 98, 1);
    y += 7;
  }

  // the next few, one per two lines
  int shown = 0;
  for (const Aircraft &a : list) {
    if (!a.visible || &a == first) continue;
    if (y > SCREEN_H - 50) break;
    flightName(b, sizeof b, a); fitText(b, CH);
    if (a.emergency) gfx->fillRect(303, y - 1, 97, 19, 1);
    text(X, y, b, nullptr, a.emergency ? 0 : 1);
    char alt[20], dist[16];
    fmtAltitude(alt, sizeof alt, a, s.units);
    fmtDist(dist, sizeof dist, a.distNm, s.units);
    snprintf(b, sizeof b, "%s", alt);
    if ((int)(strlen(alt) + strlen(dist) + 1) <= CH) snprintf(b, sizeof b, "%-*s%s", CH - (int)strlen(dist), alt, dist);
    text(X, y + 9, b, nullptr, a.emergency ? 0 : 1);
    y += 21;
    shown++;
  }
  if (!first) {
    text(X, y, fs.lastOkMs ? "No planes" : "Looking for");
    text(X, y + 9, fs.lastOkMs ? "nearby now" : "planes...");
  }

  // footer: plain-English status
  gfx->drawFastHLine(302, SCREEN_H - 30, 98, 1);
  String ms = mapStatus();
  snprintf(b, sizeof b, "Seen today: %d", flightsSeenToday());
  text(X, SCREEN_H - 26, b);
  if (fs.failures > 0 && !fs.lastOkMs) text(X, SCREEN_H - 17, "No internet?");
  else if (fs.failures > 2) text(X, SCREEN_H - 17, "Data delayed");
  else if (ms.startsWith("downloading")) text(X, SCREEN_H - 17, ("Map " + ms.substring(ms.lastIndexOf(' ') + 1)).c_str());
  else if (ms.startsWith("drawing")) text(X, SCREEN_H - 17, "Drawing map");
  else if (ms.startsWith("error")) text(X, SCREEN_H - 17, "Map unavailable");
  else text(X, SCREEN_H - 17, fs.lastOkMs ? "Live" : "Starting...");
  text(X, SCREEN_H - 8, netIp().c_str());
}

// a framed white box, so overlay text reads cleanly over the map
static void box(int x, int y, int w, int h) {
  gfx->fillRect(x, y, w, h, 0);
  gfx->drawRect(x, y, w, h, 1);
}

static void drawCorners(const Settings &s, const FlightStatus &fs) {
  char b[32];
  clockStr(s, b, sizeof b, nullptr, 0);
  box(0, 0, clockW(b) + 12, 26);
  drawClock(6, 20, b);

  snprintf(b, sizeof b, "%d", fs.shown);
  int w = textW(b, &FreeSansBold12pt7b);
  const char *word = fs.shown == 1 ? "plane" : "planes";
  int bw = w + 8 + strlen(word) * 6 + 10;
  box(SCREEN_W - bw, 0, bw, 26);
  text(SCREEN_W - bw + 5, 20, b, &FreeSansBold12pt7b);
  text(SCREEN_W - strlen(word) * 6 - 5, 10, word);

  char r[12];
  fmtRange(r, sizeof r, nmTo(s.units, s.rangeNm));
  snprintf(b, sizeof b, "Ring: %s %s", r, unitDist(s.units));
  box(0, SCREEN_H - 15, strlen(b) * 6 + 8, 15);
  text(4, SCREEN_H - 11, b);
  const char *st = fs.failures > 2 ? "Data delayed" : (fs.lastOkMs ? "Live" : "Starting");
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
  text(16, 222, " KEY = zoom in      BOOT = zoom out");
  text(16, 234, " hold BOOT = show/hide plane names");
  text(16, 246, " hold KEY = this screen (10 s: reset)");
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
  if (!mapReady() && s.mapStyle != MAP_BLANK) {
    String ts = mapStatus();
    if (ts.startsWith("downloading") || ts.startsWith("drawing")) label(cx - 51, cy + R - 40, "Drawing the map...");
  }
  if (!fs.lastOkMs) {
    label(cx - 69, cy + 22, fs.failures ? "Can't reach plane data yet" : "Looking for planes...");
    if (fs.failures && fs.error.length()) {
      char e[40];
      snprintf(e, sizeof e, "%s", fs.error.c_str());
      label(cx - (int)strlen(e) * 3, cy + 34, e);   // the reason, for troubleshooting
    }
  }

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
