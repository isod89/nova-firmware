//
// UiTune.cpp - free tuning, complete receiver control
//
//   Turn          tune by the step (accelerated)
//   Push + turn   fast tuning
//   Click         controls: Mode, Band, Step, Filter, AGC, Volume, Seek,
//                 Scope, Keypad (direct frequency), Save
//   Hold          menu
//
#include "Ui.h"

enum Chip : uint8_t { CH_MODE, CH_BAND, CH_STEP, CH_BW, CH_AGC, CH_VOL, CH_SEEK, CH_SCOPE, CH_KEYPAD, CH_SAVE, CH_COUNT };
static const char *const chipName[CH_COUNT] = { "Mode", "Band", "Step", "Filter", "AGC", "Volume", "Seek", "Scope", "Keypad", "Save" };
static const char *const chipHint[CH_COUNT] =
{
  "Turn: AM, FM, LSB, USB, CW", "Click: choose a band", "Turn: tuning step", "Turn: receiver bandwidth",
  "Turn: automatic gain or attenuation", "Turn: volume", "Turn: search down or up", "Click: see the signals around",
  "Click: type a frequency", "Click: add to Favorites"
};

#define CHIPS_VISIBLE 6

static int8_t focus = -1;
static bool editing = false;
static uint32_t focusAt = 0;
static uint8_t chipFirst = 0;
static uint32_t lastFM = 99100000, lastAM = 9650000;

// Overlays
enum Overlay : uint8_t { OV_NONE, OV_BANDS, OV_KEYPAD, OV_SCOPE };
static uint8_t overlay = OV_NONE;

void tuneEnter() { focus = -1; editing = false; overlay = OV_NONE; }

static uint8_t agcIndex(uint8_t m) { return m == MODE_FM ? 0 : m == MODE_AM ? 1 : 2; }
static uint8_t agcMax(uint8_t m) { return m == MODE_FM ? 27 : m == MODE_AM ? 37 : 1; }

static void tuneBy(int32_t steps, uint32_t st)
{
  uint32_t f = radioFreq();
  uint8_t m = radioMode();
  uint32_t rem = modeIsSSB(m) ? 0 : f % st;
  int64_t nf;
  if(rem) nf = steps > 0 ? (int64_t)f - rem + (int64_t)st * steps : (int64_t)f - rem + (int64_t)st * (steps + 1);
  else nf = (int64_t)f + (int64_t)st * steps;
  if(nf < (int64_t)radioMinFreq(m)) nf = radioMinFreq(m);
  if(nf > (int64_t)radioMaxFreq(m)) nf = radioMaxFreq(m);
  radioSetFreq((uint32_t)nf);
  if(m == MODE_FM) lastFM = nf; else lastAM = nf;
}

static void setMode(uint8_t next)
{
  uint8_t cur = radioMode();
  if(cur == MODE_FM) lastFM = radioFreq(); else lastAM = radioFreq();
  uint32_t f = next == MODE_FM ? lastFM : (cur == MODE_FM ? lastAM : radioFreq());
  radioTune(f, next);
}

static void changeMode(int dir)
{
  static const uint8_t order[] = { MODE_FM, MODE_AM, MODE_LSB, MODE_USB, MODE_CW };
  uint8_t cur = radioMode(), k = 0;
  for(uint8_t i = 0; i < ITEM_COUNT(order); i++) if(order[i] == cur) k = i;
  setMode(order[(k + (dir > 0 ? 1 : ITEM_COUNT(order) - 1)) % ITEM_COUNT(order)]);
}

//
// Band picker
//
struct BandDef { const char *name; uint32_t lo, hi, start; uint8_t mode; };
static const BandDef bandDefs[] =
{
  { "FM Broadcast",   87500000, 108000000,  99100000, MODE_FM },
  { "FM Japan",       76000000,  90000000,  80000000, MODE_FM },
  { "OIRT FM",        65800000,  74000000,  70000000, MODE_FM },
  { "Longwave",         153000,    279000,    198000, MODE_AM },
  { "Medium wave",      520000,   1710000,   1000000, MODE_AM },
  { "120m Broadcast",  2300000,   2495000,   2400000, MODE_AM },
  { "90m Broadcast",   3200000,   3400000,   3300000, MODE_AM },
  { "75m Broadcast",   3900000,   4000000,   3950000, MODE_AM },
  { "60m Broadcast",   4750000,   5060000,   4900000, MODE_AM },
  { "49m Broadcast",   5900000,   6200000,   6000000, MODE_AM },
  { "41m Broadcast",   7200000,   7600000,   7300000, MODE_AM },
  { "31m Broadcast",   9400000,   9900000,   9650000, MODE_AM },
  { "25m Broadcast",  11600000,  12100000,  11850000, MODE_AM },
  { "22m Broadcast",  13570000,  13870000,  13700000, MODE_AM },
  { "19m Broadcast",  15100000,  15800000,  15400000, MODE_AM },
  { "16m Broadcast",  17480000,  17900000,  17700000, MODE_AM },
  { "15m Broadcast",  18900000,  19020000,  18950000, MODE_AM },
  { "13m Broadcast",  21450000,  21850000,  21650000, MODE_AM },
  { "11m Broadcast",  25670000,  26100000,  25850000, MODE_AM },
  { "160m Ham",        1800000,   2000000,   1900000, MODE_LSB },
  { "80m Ham",         3500000,   4000000,   3750000, MODE_LSB },
  { "40m Ham",         7000000,   7300000,   7150000, MODE_LSB },
  { "30m Ham (CW)",   10100000,  10150000,  10120000, MODE_CW },
  { "20m Ham",        14000000,  14350000,  14200000, MODE_USB },
  { "17m Ham",        18068000,  18168000,  18120000, MODE_USB },
  { "15m Ham",        21000000,  21450000,  21250000, MODE_USB },
  { "12m Ham",        24890000,  24990000,  24950000, MODE_USB },
  { "10m Ham",        28000000,  29700000,  28500000, MODE_USB },
  { "CB",             26965000,  27405000,  27185000, MODE_AM },
  { "Time: WWV",      10000000,  10000000,  10000000, MODE_AM },
  { "Time: CHU",       7850000,   7850000,   7850000, MODE_AM },
};
static uint8_t bandSel = 0;

static void openBands()
{
  uint32_t f = radioFreq();
  bandSel = 0;
  for(uint8_t i = 0; i < ITEM_COUNT(bandDefs); i++)
    if(f >= bandDefs[i].lo && f <= bandDefs[i].hi && (bandDefs[i].mode == MODE_FM) == (radioMode() == MODE_FM)) { bandSel = i; break; }
  overlay = OV_BANDS;
}

static void drawBands()
{
  uiStatusBar("Choose a band", T.text);
  const int rows = 5, rh = 29, y0 = 22;
  int n = ITEM_COUNT(bandDefs);
  int top = bandSel - 2;
  if(top > n - rows) top = n - rows;
  if(top < 0) top = 0;
  char buf[48], a[16], b[16];
  for(int r = 0; r < rows && top + r < n; r++)
  {
    int i = top + r, y = y0 + r * rh;
    const BandDef &d = bandDefs[i];
    bool hl = i == bandSel;
    if(hl) uiGloss(6, y, 302, rh - 3, 10, T.sel); else uiRRect(6, y, 302, rh - 3, 10, T.surface);
    uint16_t gc = uiGroupColor(groupOf(d.start, d.mode));
    spr.fillCircle(20, y + 13, 4, hl ? T.selText : gc);
    uiText(d.name, 32, y + 13, F_SMB, hl ? T.selText : T.text, ML_DATUM);
    uiFormatFreq(a, sizeof(a), d.lo, d.mode == MODE_FM ? MODE_FM : MODE_AM, false);
    uiFormatFreq(b, sizeof(b), d.hi, d.mode == MODE_FM ? MODE_FM : MODE_AM, false);
    if(d.lo == d.hi) snprintf(buf, sizeof(buf), "%s %s", a, d.mode == MODE_FM ? "MHz" : "kHz");
    else snprintf(buf, sizeof(buf), "%s-%s  %s", a, b, modeName(d.mode));
    uiText(buf, 300, y + 13, F_CAP, hl ? T.selText : T.dim, MR_DATUM);
  }
  uiScrollbar(312, y0, rows * rh - 3, n, rows, top);
}

//
// Keypad: direct frequency entry, digit by digit
//
static uint8_t digits[6], digitCount = 5, digitPos = 0;
static bool keypadFM = false;

static void openKeypad()
{
  keypadFM = radioMode() == MODE_FM;
  uint32_t v = keypadFM ? radioFreq() / 10000 : radioFreq() / 1000;   // FM: 10 kHz units, else kHz
  digitCount = 5;
  for(int i = digitCount - 1; i >= 0; i--) { digits[i] = v % 10; v /= 10; }
  digitPos = 0;
  overlay = OV_KEYPAD;
}

static uint32_t keypadValue()
{
  uint32_t v = 0;
  for(int i = 0; i < digitCount; i++) v = v * 10 + digits[i];
  return keypadFM ? v * 10000 : v * 1000;
}

static void keypadApply()
{
  uint32_t hz = keypadValue();
  uint8_t m = radioMode();
  if(hz < radioMinFreq(m) || hz > radioMaxFreq(m)) { uiToast(keypadFM ? "FM: 64.00 to 108.00 MHz" : "150 to 30000 kHz"); return; }
  radioSetFreq(hz);
  overlay = OV_NONE;
}

static void drawKeypad()
{
  uiStatusBar("Keypad", T.text);
  uiLine(keypadFM ? "Frequency in MHz" : "Frequency in kHz", 160, 34, 304, F_SM, T.dim, MC_DATUM);
  int cw = 40, gap = 6, dot = keypadFM ? 14 : 0;
  int total = digitCount * cw + (digitCount - 1) * gap + dot;
  int x = 160 - total / 2;
  for(int i = 0; i < digitCount; i++)
  {
    if(keypadFM && i == 3) { uiText(".", x + 3, 92, F_XLS, T.text, MC_DATUM); x += dot; }
    bool on = i == digitPos;
    if(on) uiGloss(x, 56, cw, 58, 12, T.sel); else uiRRect(x, 56, cw, 58, 12, T.surface);
    char d[2] = { (char)('0' + digits[i]), 0 };
    uiText(d, x + cw / 2, 86, F_XLS, on ? T.selText : T.text, MC_DATUM);
    x += cw + gap;
  }
  char buf[24];
  uiFormatFreq(buf, sizeof(buf), keypadValue(), keypadFM ? MODE_FM : MODE_AM, true);
  uiText(buf, 160, 130, F_SMB, T.accent, MC_DATUM);
  uiText("Turn: digit  \xC2\xB7  Click: next  \xC2\xB7  Hold: cancel", 160, 156, F_CAP, T.dim, MC_DATUM);
}

//
// Scope: the signals around the current frequency
//
#define SCOPE_N 101
static uint8_t scopeR[SCOPE_N], scopeS[SCOPE_N];
static int16_t scopeCursor = SCOPE_N / 2;
static uint32_t scopeCenter = 0, scopeStep = 0;
static bool scopeAbort() { return inputAbort(); }

static void openScope()
{
  uint8_t m = radioMode();
  uint32_t f = radioFreq();
  scopeStep = m == MODE_FM ? 100000 : f < 1711000 ? (cfg.region == 0 ? 10000 : 9000) : modeIsSSB(m) ? 1000 : 5000;
  scopeCenter = modeIsSSB(m) ? (f / 1000) * 1000 : f - f % scopeStep;
  uiMessage("Scope", "Measuring the signals around...");
  radioScope(scopeCenter, scopeStep, SCOPE_N, scopeR, scopeS, scopeAbort);
  scopeCursor = SCOPE_N / 2;
  overlay = OV_SCOPE;
}

static uint32_t scopeFreq(int i) { return scopeCenter + (int32_t)(i - SCOPE_N / 2) * (int32_t)scopeStep; }

static void drawScope()
{
  uint8_t m = radioMode();
  char buf[48], f[24];
  uiFormatFreq(f, sizeof(f), scopeStep * (SCOPE_N / 2), m == MODE_FM ? MODE_FM : MODE_AM, true);
  snprintf(buf, sizeof(buf), "Scope  \xC2\xB1 %s", f);
  uiStatusBar(buf, T.text);

  int x0 = 8, y0 = 24, h = 96;
  uiRRect(4, y0 - 2, 312, h + 6, 10, T.surface);
  uint8_t maxR = 1;
  for(int i = 0; i < SCOPE_N; i++) if(scopeR[i] > maxR) maxR = scopeR[i];
  for(int i = 0; i < SCOPE_N; i++)
  {
    int bh = scopeR[i] * (h - 8) / (maxR < 30 ? 30 : maxR);
    uint16_t c = scopeS[i] >= 12 ? T.good : scopeS[i] >= 5 ? T.accent : T.dim;
    spr.fillRect(x0 + i * 3, y0 + h - bh, 2, bh, c);
  }
  int cx = x0 + scopeCursor * 3;
  spr.fillRect(cx, y0, 1, h, T.warn);
  spr.fillTriangle(cx - 4, y0, cx + 5, y0, cx, y0 + 5, T.warn);
  spr.fillRect(x0 + (SCOPE_N / 2) * 3, y0 + h + 1, 2, 3, T.text);

  uint32_t cf = scopeFreq(scopeCursor);
  uiFormatFreq(f, sizeof(f), cf, m == MODE_FM ? MODE_FM : MODE_AM, true);
  uiText(f, 10, 138, F_MD, T.text, ML_DATUM);
  snprintf(buf, sizeof(buf), "%u dBuV  \xC2\xB7  SNR %u", scopeR[scopeCursor], scopeS[scopeCursor]);
  uiText(buf, 310, 138, F_SM, T.dim, MR_DATUM);
  uiText("Turn: move  \xC2\xB7  Click: listen  \xC2\xB7  Hold: back", 160, 160, F_CAP, T.dim, MC_DATUM);
}

//
// Controls
//
static void adjustChip(int8_t c, int16_t turn, int16_t fast)
{
  uint8_t m = radioMode();
  int d = turn > 0 ? 1 : -1;
  switch(c)
  {
    case CH_MODE: changeMode(d); break;
    case CH_STEP:
    {
      uint8_t n; stepList(m, n);
      cfg.stepIdx[m] = (cfg.stepIdx[m] + n + d) % n;
      radioApplyStep();
      break;
    }
    case CH_BW:
    {
      uint8_t n = bandwidthCount(m);
      cfg.bwIdx[m] = (cfg.bwIdx[m] + n + d) % n;
      radioApplyAudio();
      break;
    }
    case CH_AGC:
    {
      uint8_t a = agcIndex(m), mx = agcMax(m);
      int v = cfg.agc[a] + d;
      cfg.agc[a] = v < 0 ? 0 : v > mx ? mx : v;
      radioApplyAudio();
      break;
    }
    case CH_VOL:
    {
      int v = (int)cfg.volume + fast;
      radioSetVolume(v < 0 ? 0 : v > 63 ? 63 : v);
      break;
    }
    case CH_SEEK:
      if(modeIsSSB(m)) uiToast("Seek works in AM and FM");
      else radioSeek(d);
      break;
  }
  configSave();
}

static void chipValue(int8_t c, char *buf, size_t size)
{
  uint8_t m = radioMode();
  switch(c)
  {
    case CH_MODE: snprintf(buf, size, "%s", modeName(m)); break;
    case CH_BAND: snprintf(buf, size, "%s", uiShortBand(radioFreq(), m)); break;
    case CH_STEP:
    {
      uint32_t hz = currentStep();
      if(hz >= 1000000) snprintf(buf, size, "%lu MHz", (unsigned long)(hz / 1000000));
      else if(hz >= 1000) snprintf(buf, size, "%lu k", (unsigned long)(hz / 1000));
      else snprintf(buf, size, "%luHz", (unsigned long)hz);
      break;
    }
    case CH_BW:   snprintf(buf, size, "%s", bandwidthLabel(m, cfg.bwIdx[m])); break;
    case CH_AGC:
    {
      uint8_t a = cfg.agc[agcIndex(m)];
      if(!a) snprintf(buf, size, "Auto"); else if(a == 1) snprintf(buf, size, "Off"); else snprintf(buf, size, "-%u dB", a - 1);
      break;
    }
    case CH_VOL:  snprintf(buf, size, "%u", cfg.volume); break;
    default:      buf[0] = '\0'; break;
  }
}

void tuneInput(const Input &in)
{
  focusAt = millis();

  if(overlay == OV_BANDS)
  {
    int n = ITEM_COUNT(bandDefs);
    if(in.turn) bandSel = (bandSel + n + (in.turn > 0 ? 1 : -1)) % n;
    if(in.click)
    {
      const BandDef &d = bandDefs[bandSel];
      overlay = OV_NONE;
      radioTune(d.start, d.mode);
      if(d.mode == MODE_FM) lastFM = d.start; else lastAM = d.start;
    }
    if(in.hold) overlay = OV_NONE;
    return;
  }
  if(overlay == OV_KEYPAD)
  {
    if(in.turn) digits[digitPos] = (digits[digitPos] + 10 + (in.turn > 0 ? 1 : -1)) % 10;
    if(in.click) { if(digitPos + 1 < digitCount) digitPos++; else keypadApply(); }
    if(in.hold) overlay = OV_NONE;
    return;
  }
  if(overlay == OV_SCOPE)
  {
    if(in.turn) { int c = scopeCursor + in.turnFast; scopeCursor = c < 0 ? 0 : c >= SCOPE_N ? SCOPE_N - 1 : c; }
    if(in.click)
    {
      uint32_t f = scopeFreq(scopeCursor);
      overlay = OV_NONE;
      radioSetFreq(f);
    }
    if(in.hold) overlay = OV_NONE;
    return;
  }

  if(in.hold) { if(focus >= 0) { focus = -1; editing = false; } else menuOpen(SCR_TUNE); return; }

  if(focus < 0)
  {
    if(in.click) { focus = CH_MODE; editing = false; chipFirst = 0; return; }
    if(in.turn)
    {
      uint32_t st = currentStep();
      uint8_t m = radioMode();
      if(in.pushTurn)
      {
        uint32_t big = m == MODE_FM ? 1000000 : modeIsSSB(m) ? 10000 : 100000;
        tuneBy(in.turn, big > st ? big : st * 10);
      }
      else tuneBy(modeIsSSB(m) ? in.turnFast : (abs(in.turnFast) > 4 ? in.turnFast / 2 : in.turn), st);
    }
    return;
  }

  if(editing)
  {
    if(in.turn) adjustChip(focus, in.turn, in.turnFast);
    if(in.click) editing = false;
    return;
  }

  if(in.turn)
  {
    focus = (focus + CH_COUNT + (in.turn > 0 ? 1 : -1)) % CH_COUNT;
    if(focus < chipFirst) chipFirst = focus;
    if(focus >= chipFirst + CHIPS_VISIBLE) chipFirst = focus - CHIPS_VISIBLE + 1;
  }
  if(in.click)
  {
    switch(focus)
    {
      case CH_BAND:   openBands(); break;
      case CH_SCOPE:  openScope(); break;
      case CH_KEYPAD: openKeypad(); break;
      case CH_SAVE:
      {
        int16_t i = stationAddManual(radioFreq(), radioMode());
        uiToast(i >= 0 ? "Saved to Favorites" : "Station list is full");
        break;
      }
      default: editing = true; break;
    }
  }
}

bool tuneTick()
{
  if(overlay == OV_NONE && focus >= 0 && millis() - focusAt > 10000) { focus = -1; editing = false; return true; }
  return false;
}

static uint8_t sUnits(uint8_t rssi, uint8_t mode, int &over)
{
  over = 0;
  if(mode == MODE_FM) return 0;
  static const uint8_t th[] = { 1, 2, 3, 4, 10, 16, 22, 28, 34 };
  uint8_t s = 0;
  for(uint8_t i = 0; i < ITEM_COUNT(th); i++) if(rssi > th[i]) s = i + 1;
  if(s >= 9 && rssi > 44) over = ((rssi - 34) / 10) * 10;
  return s > 9 ? 9 : s;
}

// Tuning ruler: ticks slide under a fixed centre needle
static void drawRuler(uint32_t hz, uint8_t m, int y)
{
  uint32_t tick, major;
  if(m == MODE_FM) { tick = 100000; major = 1000000; }
  else if(modeIsSSB(m)) { tick = 1000; major = 10000; }
  else if(hz < 1711000) { tick = 10000; major = 100000; }
  else { tick = 5000; major = 50000; }
  const int px = 7;
  int64_t first = (int64_t)hz - (int64_t)tick * 23;
  first -= first % (int64_t)tick;
  for(int64_t f = first; f <= (int64_t)hz + (int64_t)tick * 23; f += tick)
  {
    if(f < 0) continue;
    int x = 160 + (int)((f - (int64_t)hz) * px / (int64_t)tick);
    if(x < 4 || x > 316) continue;
    bool maj = (f % major) == 0;
    spr.fillRect(x, y + (maj ? 0 : 6), 1, maj ? 12 : 6, maj ? T.dim : T.line);
    if(maj)
    {
      char lab[12];
      if(m == MODE_FM) snprintf(lab, sizeof(lab), "%lu", (unsigned long)(f / 1000000));
      else snprintf(lab, sizeof(lab), "%lu", (unsigned long)(f / 1000));
      uiText(lab, x, y + 20, F_CAP, T.dim, MC_DATUM);
    }
  }
  uiRRect(159, y - 3, 3, 18, 1, T.accent);
}

void tuneDraw()
{
  if(overlay == OV_BANDS) { drawBands(); return; }
  if(overlay == OV_KEYPAD) { drawKeypad(); return; }
  if(overlay == OV_SCOPE) { drawScope(); return; }

  char buf[72], f[24];
  uint32_t hz = radioFreq();
  uint8_t m = radioMode();
  uint16_t gc = uiGroupColor(groupOf(hz, m));
  uiStatusBar(bandLabel(hz, m), gc);

  // Mode, badges, frequency
  uiPill(8, 24, 48, 22, gc, uiOnColor(gc), modeName(m), F_SMB);
  Signal sg = radioSignal();
  int by = 52;
  if(sg.stereo) { uiBadge(8, by, T.surface2, T.text, "STEREO"); by += 18; }
  int16_t st = stationTuned();
  if(st >= 0 && (stations[st].flags & ST_FAV)) uiStar(16, by + 7, 7, T.fav);

  uiFormatFreq(f, sizeof(f), hz, m, false);
  uiText(f, 262, 46, F_XL, T.text, MR_DATUM);
  uiText(m == MODE_FM ? "MHz" : "kHz", 268, 56, F_SMB, T.dim, ML_DATUM);

  // Name / hint line
  if(focus >= 0)
  {
    snprintf(buf, sizeof(buf), "%s  \xC2\xB7  %s", chipName[focus], editing || focus == CH_BAND || focus >= CH_SCOPE ? chipHint[focus] : "Click to change");
    uiText(buf, 160, 82, F_SM, T.text, MC_DATUM);
  }
  else
  {
    const char *name = "";
    if(m == MODE_FM) name = rdsName();
    else { name = scheduleLookup(hz); if(!name) name = knownStation(hz); if(!name) name = ""; }
    const char *sub = m == MODE_FM ? rdsText() : "";
    if(*name && *sub) snprintf(buf, sizeof(buf), "%s  \xC2\xB7  %s", name, sub);
    else snprintf(buf, sizeof(buf), "%s", *name ? name : sub);
    char fit[72];
    uiFit(fit, sizeof(fit), buf, F_SMB, 300);
    uiText(fit, 160, 82, F_SMB, T.accent, MC_DATUM);
  }

  drawRuler(hz, m, 94);

  // Signal
  int over;
  uint8_t s = sUnits(sg.rssi, m, over);
  int segs = sg.rssi / 6 > 12 ? 12 : sg.rssi / 6;
  for(int i = 0; i < 12; i++) uiRRect(8 + i * 9, 124, 7, 5, 1, i < segs ? (i < 8 ? T.good : T.warn) : T.barOff);
  if(m == MODE_FM) snprintf(buf, sizeof(buf), "%u dBuV  \xC2\xB7  SNR %u dB", sg.rssi, sg.snr);
  else if(over) snprintf(buf, sizeof(buf), "S9+%d  \xC2\xB7  %u dBuV  \xC2\xB7  SNR %u", over, sg.rssi, sg.snr);
  else snprintf(buf, sizeof(buf), "S%u  \xC2\xB7  %u dBuV  \xC2\xB7  SNR %u", s, sg.rssi, sg.snr);
  uiText(buf, 312, 126, F_CAP, T.dim, MR_DATUM);

  // Control strip (scrolls with the focus)
  int w = 50;
  for(int k = 0; k < CHIPS_VISIBLE; k++)
  {
    int c = chipFirst + k;
    if(c >= CH_COUNT) break;
    int x = 6 + k * (w + 2), y = 134;
    bool on = focus == c;
    uint16_t bg = on ? (editing ? T.accent : T.sel) : T.surface;
    uint16_t fg = on ? (editing ? T.accentText : T.selText) : T.text;
    uint16_t cap = on ? fg : T.dim;
    if(on) uiGloss(x, y, w, 34, 10, bg); else uiRRect(x, y, w, 34, 10, bg);
    uiText(chipName[c], x + w / 2, y + 9, F_CAP, cap, MC_DATUM);
    if(c == CH_SEEK)
    {
      spr.fillTriangle(x + 13, y + 23, x + 21, y + 18, x + 21, y + 28, fg);
      spr.fillTriangle(x + 37, y + 23, x + 29, y + 18, x + 29, y + 28, fg);
    }
    else if(c == CH_SCOPE)
    {
      for(int b = 0; b < 7; b++) { int bh = (b == 3 ? 10 : b == 2 || b == 4 ? 6 : 3); spr.fillRect(x + 15 + b * 3, y + 28 - bh, 2, bh, fg); }
    }
    else if(c == CH_KEYPAD)
    {
      for(int r = 0; r < 2; r++) for(int q = 0; q < 3; q++) uiRRect(x + 16 + q * 7, y + 18 + r * 6, 5, 4, 1, fg);
    }
    else if(c == CH_SAVE) uiStar(x + w / 2, y + 23, 7, on ? fg : T.fav);
    else
    {
      chipValue(c, buf, sizeof(buf));
      uiLine(buf, x + w / 2, y + 23, w - 4, F_SMB, fg, MC_DATUM);
    }
  }
  // Scroll hints
  if(chipFirst > 0) spr.fillTriangle(1, 151, 4, 147, 4, 155, T.dim);
  if(chipFirst + CHIPS_VISIBLE < CH_COUNT) spr.fillTriangle(318, 151, 315, 147, 315, 155, T.dim);
}
