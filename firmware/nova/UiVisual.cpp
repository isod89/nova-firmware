//
// UiVisual.cpp - full-screen visualizers while listening
//
// Classic and light: plain shapes redrawn each frame, nothing that makes
// the processor work hard (it stays at 80 MHz). The receiver does not pass
// audio to the processor, so the pictures follow what the SI4732 measures
// 20 times a second: signal level, quality (SNR), multipath, stereo, RDS.
//
//   Turn: next style   Press + turn: volume   Click: station info on/off
//   Hold: back
//
#include "Ui.h"

static const char *const styleNames[] = { "Meters", "LED bars", "VU", "Trace", "Waterfall", "Big display" };
#define STYLE_COUNT 6

static uint8_t from = SCR_STATIONS;
static bool pinned = false;
static uint32_t infoUntil = 0, lastFrame = 0, lastSample = 0, lastHist = 0;
static SignalFull cur;
static float L = 0, Q = 0, M = 0;                 // smoothed level, quality, multipath (0..1)
static float jumpE = 0;                           // fading: how much the signal moves
static uint8_t lastR = 0, lastS = 0;
static bool firstFrame = true;

#define HIST 160
static uint8_t histR[HIST], histS[HIST];          // 8 s of level and quality
static uint8_t histPos = 0, histN = 0;
#define COLS 32
static uint8_t colR[COLS];                        // LED bars: one column every 125 ms
static uint8_t colPos = 0;
static float needle[2] = { 0, 0 }, needleV[2] = { 0, 0 };
static float vuPeak[2] = { 0, 0 };
static uint32_t vuPeakAt[2] = { 0, 0 };

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

const char *visualStyleName(uint8_t s) { return styleNames[s % STYLE_COUNT]; }

void visualOpen(uint8_t back)
{
  from = back == SCR_TUNE ? SCR_TUNE : SCR_STATIONS;
  firstFrame = true;
  infoUntil = millis() + 4000;
  pinned = false;
  uiShow(SCR_VISUAL);
}

bool visualActive() { return uiScreen() == SCR_VISUAL; }

static void sample()
{
  cur = radioSignalFull();
  float jump = fabsf((float)cur.rssi - lastR) / 6.0f + fabsf((float)cur.snr - lastS) / 6.0f;
  lastR = cur.rssi; lastS = cur.snr;
  L += (clamp01(cur.rssi / 60.0f) - L) * 0.3f;
  Q += (clamp01(cur.snr / 30.0f) - Q) * 0.3f;
  M += (clamp01(cur.mult / 60.0f) - M) * 0.3f;
  jumpE = jumpE * 0.85f + clamp01(jump);
  histR[histPos] = cur.rssi; histS[histPos] = cur.snr;
  histPos = (histPos + 1) % HIST;
  if(histN < HIST) histN++;
  if(millis() - lastHist >= 125)
  {
    lastHist = millis();
    colR[colPos] = cur.rssi;
    colPos = (colPos + 1) % COLS;
  }
}

void visualInput(const Input &in)
{
  if(volumeActive()) { volumeInput(in); return; }
  if(in.hold) { uiShow(from); return; }
  if(in.pushTurn) { volumeOpen(); Input t = in; t.pushTurn = false; volumeInput(t); return; }
  if(in.turn)
  {
    cfg.visStyle = (cfg.visStyle + STYLE_COUNT + (in.turn > 0 ? 1 : -1)) % STYLE_COUNT;
    configSave();
    firstFrame = true;
    infoUntil = millis() + 2500;
  }
  if(in.click) { pinned = !pinned; infoUntil = pinned ? 0 : millis(); }
}

bool visualTick()
{
  if(!screenIsOff()) screenWake();               // keep the screen on while watching
  uint32_t now = millis();
  if(now - lastSample >= 50) { lastSample = now; sample(); }
  return now - lastFrame >= (cfg.visEco ? 100u : 50u);
}

static uint16_t bandColor() { return uiGroupColor(groupOf(radioFreq(), radioMode())); }

static void stationName(char *name, size_t n)
{
  name[0] = '\0';
  int16_t st = stationTuned();
  if(st >= 0) stationLabel(st, name, n, true);
  else if(radioMode() == MODE_FM && rdsName()[0]) snprintf(name, n, "%s", rdsName());
  if(!name[0]) snprintf(name, n, "%s", bandLabel(radioFreq(), radioMode()));
}

// LED colour by height: green, yellow, red
static uint16_t ledColor(int seg, int segs) { return seg >= segs * 85 / 100 ? 0xF904 : seg >= segs * 65 / 100 ? 0xFE60 : 0x3666; }

//
// Styles
//
static void meter(int cx, int cy, int r, float value, float &pos, float &vel, const char *label)
{
  vel += (value - pos) * 0.35f;       // a little spring: overshoots like a real needle
  vel *= 0.62f;
  pos += vel;
  float p = clamp01(pos);
  for(int i = 0; i <= 10; i++)
  {
    float a = 3.1416f * (1.08f - 1.16f * i / 10.0f);
    int in = i % 5 ? 6 : 11;
    spr.drawLine(cx + (int)((r - in) * cosf(a)), cy - (int)((r - in) * sinf(a)), cx + (int)(r * cosf(a)), cy - (int)(r * sinf(a)), i >= 8 ? 0xF904 : 0xBDF7);
  }
  float a = 3.1416f * (1.08f - 1.16f * p);
  spr.drawWedgeLine(cx, cy, cx + (int)((r - 4) * cosf(a)), cy - (int)((r - 4) * sinf(a)), 2.0f, 0.7f, 0xFFFF);
  spr.fillCircle(cx, cy, 5, 0xBDF7);
  uiText(label, cx, cy + 16, F_CAP, 0x9CF3, MC_DATUM);
}

static void lamps(int y)
{
  struct Lamp { const char *t; bool on; uint16_t c; } list[] =
  {
    { "STEREO", cur.pilot, 0x3666 }, { "RDS", radioMode() == MODE_FM && rdsName()[0], 0x0C3F },
    { "MULTIPATH", M > 0.2f, 0xFCE1 }, { "FADING", jumpE > 0.5f, 0xFEA1 },
  };
  int x = 14;
  for(auto &l : list)
  {
    int w = uiTextWidth(l.t, F_CAP) + 14;
    uiRRect(x, y, w, 16, 8, l.on ? l.c : 0x2945);
    uiText(l.t, x + w / 2, y + 8, F_CAP, l.on ? 0xFFFF : 0x6B4D, MC_DATUM);
    x += w + 6;
  }
}

static void drawMeters()
{
  spr.fillSprite(0x0000);
  uiRRect(10, 22, 145, 104, 14, 0x2124);
  uiRRect(165, 22, 145, 104, 14, 0x2124);
  meter(82, 104, 64, L, needle[0], needleV[0], "Signal");
  meter(237, 104, 64, Q, needle[1], needleV[1], "Quality");
  char buf[24];
  snprintf(buf, sizeof(buf), "%u dBuV", cur.rssi); uiText(buf, 82, 34, F_SMB, 0xFFFF, MC_DATUM);
  snprintf(buf, sizeof(buf), "SNR %u dB", cur.snr); uiText(buf, 237, 34, F_SMB, 0xFFFF, MC_DATUM);
  lamps(134);
}

static void drawBars()
{
  spr.fillSprite(0x0000);
  const int segs = 16, top = 30, bottom = 150, sh = (bottom - top) / segs;
  int w = SCREEN_W / COLS;
  for(int k = 0; k < COLS; k++)
  {
    int c = (colPos + k) % COLS;                 // oldest on the left
    int lit = colR[c] * segs / 60; if(lit > segs) lit = segs;
    for(int s = 0; s < segs; s++)
    {
      int y = bottom - (s + 1) * sh;
      bool on = s < lit;
      uint16_t col = on ? ledColor(s, segs) : 0x18C3;
      if(on && s == lit - 1) col = uiMix(col, 0xFFFF, 110);   // brighter top
      spr.fillRect(k * w + 1, y + 1, w - 2, sh - 2, col);
    }
  }
  uiText("SIGNAL  \xC2\xB7  LAST 4 s", 8, 160, F_CAP, 0x6B4D, ML_DATUM);
}

static void vuBar(int y, float v, float &peak, uint32_t &peakAt, const char *label, const char *value)
{
  const int segs = 40, x0 = 86, w = 224;
  int lit = (int)(clamp01(v) * segs);
  if(v >= peak || millis() - peakAt > 1500) { peak = v; peakAt = millis(); }
  int pk = (int)(clamp01(peak) * segs);
  for(int s = 0; s < segs; s++)
  {
    int x = x0 + s * w / segs;
    uint16_t c = s < lit ? ledColor(s, segs) : (s == pk - 1 ? 0xFFFF : 0x18C3);
    spr.fillRect(x, y, w / segs - 2, 18, c);
  }
  uiText(label, 10, y + 9, F_SMB, 0xDEFB, ML_DATUM);
  uiText(value, 306, y + 29, F_CAP, 0x9CF3, MR_DATUM);
}

static void drawVU()
{
  spr.fillSprite(0x0000);
  char a[24], b[24], name[64], f[24];
  stationName(name, sizeof(name));
  uiFormatFreq(f, sizeof(f), radioFreq(), radioMode(), true);
  char fit[64];
  uiFit(fit, sizeof(fit), name, F_MD, 200);
  uiText(fit, 10, 14, F_MD, 0xFFFF, ML_DATUM);
  uiText(f, 310, 14, F_SMB, bandColor(), MR_DATUM);
  snprintf(a, sizeof(a), "%u dBuV", cur.rssi);
  snprintf(b, sizeof(b), "SNR %u dB", cur.snr);
  vuBar(42, L, vuPeak[0], vuPeakAt[0], "SIGNAL", a);
  vuBar(92, Q, vuPeak[1], vuPeakAt[1], "QUALITY", b);
  lamps(142);
}

static void drawTrace()
{
  spr.fillSprite(0x0000);
  const int x0 = 4, y0 = 22, w = 312, h = 128;
  for(int i = 0; i <= 8; i++) spr.drawFastVLine(x0 + i * w / 8, y0, h, 0x0120);
  for(int i = 0; i <= 4; i++) spr.drawFastHLine(x0, y0 + i * h / 4, w, 0x0120);
  int n = histN;
  for(int pass = 0; pass < 2; pass++)
  {
    uint16_t c = pass ? 0xFEA1 : 0x47E8;
    float scale = pass ? h / 40.0f : h / 64.0f;
    int px = 0, py = 0;
    for(int k = 0; k < n; k++)
    {
      int i = (histPos + HIST - n + k) % HIST;
      int v = pass ? histS[i] : histR[i];
      int x = x0 + (HIST - n + k) * w / (HIST - 1);
      int y = y0 + h - 1 - (int)(v * scale); if(y < y0) y = y0;
      if(k) { spr.drawLine(px, py, x, y, c); spr.drawLine(px, py + 1, x, y + 1, c); }
      px = x; py = y;
    }
  }
  char buf[48];
  snprintf(buf, sizeof(buf), "SIGNAL %u dBuV", cur.rssi); uiText(buf, 8, 160, F_CAP, 0x47E8, ML_DATUM);
  snprintf(buf, sizeof(buf), "SNR %u dB", cur.snr); uiText(buf, 160, 160, F_CAP, 0xFEA1, ML_DATUM);
  uiText("8 s", 312, 160, F_CAP, 0x6B4D, MR_DATUM);
}

// Waterfall keeps its past on its own canvas
static LGFX_Sprite *canvas = nullptr;
static uint16_t lut[64];
static uint8_t lutGroup = 255;
static uint32_t rnd = 12345;

static void buildLut()
{
  uint16_t b = bandColor(), a = uiMix(0x0000, uiMix(b, 0xB81F, 120), 140);
  for(int i = 0; i < 64; i++)
  {
    float v = i / 63.0f;
    lut[i] = v < 0.45f ? uiMix(0x0000, a, (uint8_t)(v / 0.45f * 255)) : v < 0.85f ? uiMix(a, b, (uint8_t)((v - 0.45f) / 0.4f * 255)) : uiMix(b, 0xFFFF, (uint8_t)((v - 0.85f) / 0.15f * 200));
  }
  lutGroup = groupOf(radioFreq(), radioMode());
}

static void drawWaterfall(float t)
{
  if(!canvas)
  {
    canvas = new LGFX_Sprite(&spr);
    canvas->setPsram(true);
    canvas->setColorDepth(16);
    if(!canvas->createSprite(SCREEN_W, SCREEN_H)) { delete canvas; canvas = nullptr; }
  }
  if(!canvas) { spr.fillSprite(0); uiText("Not enough memory", 160, 85, F_SMB, 0xFFFF, MC_DATUM); return; }
  if(firstFrame || lutGroup != groupOf(radioFreq(), radioMode())) { canvas->fillSprite(0); buildLut(); }
  uint16_t *b = static_cast<uint16_t *>(canvas->getBuffer());
  memmove(b + SCREEN_W, b, SCREEN_W * (SCREEN_H - 1) * 2);
  float centre = 160 + 25 * sinf(t * 0.13f);
  float width = 8 + 45 * Q;
  float noiseAmp = 0.06f + 0.22f * (1 - Q) + M * 0.3f;
  for(int x = 0; x < SCREEN_W; x++)
  {
    float d = (x - centre) / width;
    float v = (0.35f + 0.55f * L) * expf(-d * d);
    rnd = rnd * 1103515245u + 12345u;
    v += noiseAmp * ((rnd >> 16) & 255) / 255.0f + jumpE * 0.12f;
    int i = (int)(v * 63); if(i > 63) i = 63; if(i < 0) i = 0;
    uint16_t c = lut[i];
    b[x] = (c >> 8) | (c << 8);                 // the sprite stores pixels byte-swapped
  }
  memcpy(spr.getBuffer(), b, SCREEN_W * SCREEN_H * 2);
}

static void drawBig()
{
  spr.fillSprite(0x0000);
  char name[64], f[24], fit[64];
  stationName(name, sizeof(name));
  const char *clk = clockText();
  if(clk) uiText(clk, 310, 16, F_MD, 0x9CF3, MR_DATUM);
  uiFit(fit, sizeof(fit), name, F_LG, 230);
  uiText(fit, 10, 16, F_LG, 0xFFFF, ML_DATUM);
  uiFormatFreq(f, sizeof(f), radioFreq(), radioMode(), false);
  uiText(f, 250, 78, F_XL, bandColor(), MR_DATUM);
  uiText(radioMode() == MODE_FM ? "MHz" : "kHz", 256, 88, F_SMB, 0x9CF3, ML_DATUM);
  // Two slim bars
  struct Row { const char *t; float v; uint16_t c; } rows[] = { { "SIGNAL", L, bandColor() }, { "QUALITY", Q, 0xFEA1 } };
  for(int i = 0; i < 2; i++)
  {
    int y = 116 + i * 18;
    uiText(rows[i].t, 10, y + 4, F_CAP, 0x9CF3, ML_DATUM);
    uiRRect(76, y, 234, 8, 4, 0x2124);
    int w = (int)(234 * clamp01(rows[i].v));
    if(w > 8) uiRRect(76, y, w, 8, 4, rows[i].c);
  }
  const char *rt = radioMode() == MODE_FM ? rdsText() : "";
  if(*rt) { uiFit(fit, sizeof(fit), rt, F_SM, 300); uiText(fit, 10, 160, F_SM, 0xBDF7, ML_DATUM); }
  else if(cur.pilot) uiText("STEREO", 10, 160, F_SM, 0x3666, ML_DATUM);
}

//
// Frame
//
void visualDraw()
{
  lastFrame = millis();
  float t = millis() / 1000.0f;
  uint8_t s = cfg.visStyle % STYLE_COUNT;
  switch(s)
  {
    case 0: drawMeters(); break;
    case 1: drawBars(); break;
    case 2: drawVU(); break;
    case 3: drawTrace(); break;
    case 4: drawWaterfall(t); break;
    default: drawBig(); break;
  }
  firstFrame = false;

  // Station name and RDS on entry, after a change of style, or pinned with a click
  bool info = pinned || millis() < infoUntil;
  bool ownText = s == 2 || s == 5;
  if(s == 0)
  {
    char name[64], f[24], buf[96], fit[96];
    stationName(name, sizeof(name));
    uiFormatFreq(f, sizeof(f), radioFreq(), radioMode(), true);
    snprintf(buf, sizeof(buf), "%s  \xC2\xB7  %s", name, f);
    uiFit(fit, sizeof(fit), buf, F_SMB, 300);
    uiText(fit, 160, 10, F_SMB, 0xFFFF, MC_DATUM);
  }
  else if(info && !ownText)
  {
    char name[64], f[24], fit[64];
    stationName(name, sizeof(name));
    uiFormatFreq(f, sizeof(f), radioFreq(), radioMode(), true);
    spr.fillRect(0, 0, SCREEN_W, 22, 0x0000);
    uiFit(fit, sizeof(fit), name, F_SMB, 200);
    uiText(fit, 8, 10, F_SMB, 0xFFFF, ML_DATUM);
    uiText(f, 312, 10, F_SMB, bandColor(), MR_DATUM);
  }
  if(info && s != 0 && s != 3 && s != 5)
  {
    uiText(visualStyleName(s), 310, 160, F_CAP, 0x6B4D, MR_DATUM);
  }
  if(volumeActive()) volumeDraw();
}
