//
// UiTools.cpp - screens of the Noise Hunter and Storm Detector apps
//
//   Turn: volume   Click: Noise Hunter = zero here, Storm = listen on/off
//   Hold: controls strip (turn to choose, click to use or change, hold to close)
//
#include "Ui.h"

#define STRIP_VISIBLE 6

struct Strip
{
  const char *const *names;
  const char *const *hints;
  uint8_t count;
  int8_t focus;
  uint8_t first;
  bool editing;
  uint32_t at;
};

static void stripOpen(Strip &s, int8_t focus) { s.focus = focus; s.first = 0; s.editing = false; s.at = millis(); }
static void stripClose(Strip &s) { s.focus = -1; s.editing = false; }

static void stripMove(Strip &s, int d)
{
  s.focus = (s.focus + s.count + d) % s.count;
  if(s.focus < s.first) s.first = s.focus;
  if(s.focus >= s.first + STRIP_VISIBLE) s.first = s.focus - STRIP_VISIBLE + 1;
}

static void stripDraw(const Strip &s, void (*value)(uint8_t, char *, size_t))
{
  int w = 50;
  char buf[24];
  for(int k = 0; k < STRIP_VISIBLE; k++)
  {
    int c = s.first + k;
    if(c >= s.count) break;
    int x = 6 + k * (w + 2), y = 142;
    bool on = s.focus == c;
    uint16_t bg = on ? (s.editing ? T.accent : T.sel) : T.surface;
    uint16_t fg = on ? (s.editing ? T.accentText : T.selText) : T.text;
    if(on) uiGloss(x, y, w, 27, 9, bg); else uiRRect(x, y, w, 27, 9, bg);
    uiText(s.names[c], x + w / 2, y + 7, F_CAP, on ? fg : T.dim, MC_DATUM);
    value(c, buf, sizeof(buf));
    uiLine(buf, x + w / 2, y + 19, w - 4, F_SMB, fg, MC_DATUM);
  }
  if(s.first > 0) spr.fillTriangle(1, 155, 4, 151, 4, 159, T.dim);
  if(s.first + STRIP_VISIBLE < s.count) spr.fillTriangle(318, 155, 315, 151, 315, 159, T.dim);
}

static void hintLine(const Strip &s, const char *idle, uint16_t color)
{
  char buf[80];
  if(s.focus >= 0)
  {
    snprintf(buf, sizeof(buf), "%s  \xC2\xB7  %s", s.names[s.focus], s.editing ? "Turn to change, click when done" : s.hints[s.focus]);
    uiLine(buf, 160, 82, 304, F_SM, T.text, MC_DATUM);
  }
  else uiLine(idle, 160, 82, 304, F_SMB, color, MC_DATUM);
}

static void volumeTurn(const Input &in)
{
  volumeOpen();
  Input t = in;
  t.pushTurn = false;
  volumeInput(t);
}

static bool stepped = false;
static uint32_t lastDraw = 0;

void toolStepped()
{
  stepped = true;
  if(stormTakeAlert())
  {
    uint8_t a = stormAlert();
    if(stormCfg.flash)
    {
      screenWake();
      for(int i = 0; i < 3; i++) { backlight(0); delay(120); backlight(255); delay(120); }
      backlightRestore();
    }
    uiToast(a >= STORM_NEAR ? "Storm nearby" : "Storm approaching", 4000);
  }
}

static bool toolUiTick(Strip &s, uint16_t frameMs)
{
  if(s.focus >= 0 && millis() - s.at > 10000) { stripClose(s); return true; }
  if(stepped && millis() - lastDraw >= frameMs) { stepped = false; return true; }
  static uint32_t clockTick = 0;
  if(millis() - clockTick >= 1000) { clockTick = millis(); return true; }
  return false;
}

static void exitTool(const char *what)
{
  appsStopAll();
  uiGoHome();
  uiToast(what, 1800);
}
static void exitNoise() { exitTool("Noise Hunter closed"); }
static void exitStorm() { exitTool("Storm Detector closed"); }

//
// Noise Hunter
//
enum NChip : uint8_t { N_ZERO, N_SPOT, N_BAND, N_QUIET, N_FREQ, N_RANGE, N_VOL, N_MAX, N_EXIT, N_COUNT };
static const char *const nNames[N_COUNT] = { "Zero", "Spot", "Band", "Quiet", "Freq", "Range", "Volume", "Max", "Exit" };
static const char *const nHints[N_COUNT] =
{
  "Click: this level becomes 0 dB", "Click: save this reading", "Turn: band, click: find a quiet frequency there",
  "Click: look for a quieter frequency", "Turn: move the frequency", "Turn: less gain when very close",
  "Turn: volume", "Click: clear the highest level", "Click: back to the radio",
};
static Strip nStrip = { nNames, nHints, N_COUNT, -1, 0, false, 0 };
static uint8_t nPendingBand = 0;

void noiseScreenEnter() { stripClose(nStrip); }

static void noiseControls() { stripOpen(nStrip, N_SPOT); }
static void noiseSpotNow()
{
  char buf[48];
  uint8_t k = noiseSpot(); int d = noiseBase() >= 0 ? noiseLevel() - noiseBase() : 0;
  snprintf(buf, sizeof(buf), "Spot %u  \xC2\xB7  %u dBuV  \xC2\xB7  %+d dB", k, noiseLevel(), d);
  uiToast(buf, 1800);
}
static void noiseMenu()
{
  appMenuBegin("Noise Hunter");
  appMenuAdd("Back to the radio", IC_BACK, C_RED, exitNoise);
  appMenuAdd("Controls", IC_GEAR, C_GRAY, noiseControls);
  appMenuAdd("Save this reading", IC_STAR, C_YELLOW, noiseSpotNow);
  appMenuAdd("Find a quiet frequency", IC_RADAR, C_TEAL, noiseFindQuiet);
  appMenuAdd("Clear the highest level", IC_RESET, C_ORANGE, noiseResetMax);
  appMenuAdd("Other apps", IC_APPS, C_PURPLE, appsOpenList);
  appMenuShow();
}

static void nValue(uint8_t c, char *b, size_t n)
{
  switch(c)
  {
    case N_ZERO:  snprintf(b, n, "%s", noiseBase() >= 0 ? "Reset" : "Set"); break;
    case N_SPOT:  snprintf(b, n, "#%u", noiseSpotCount() + 1); break;
    case N_BAND:  snprintf(b, n, "%s", noiseBands[nStrip.editing && nStrip.focus == N_BAND ? nPendingBand : noiseCfg.band].shortName); break;
    case N_QUIET: snprintf(b, n, "Find"); break;
    case N_FREQ:  snprintf(b, n, "%lu", (unsigned long)(radioFreq() / 1000)); break;
    case N_RANGE: snprintf(b, n, noiseCfg.att ? "-%u" : "Max", noiseCfg.att); break;
    case N_VOL:   snprintf(b, n, "%u", cfg.volume); break;
    case N_MAX:   snprintf(b, n, "%u", noiseMax()); break;
    default:      snprintf(b, n, "Back"); break;
  }
}

void noiseInput(const Input &in)
{
  if(!noiseActive()) { uiGoHome(); return; }
  if(appMenuActive()) { appMenuInput(in); return; }
  if(volumeActive()) { volumeInput(in); return; }
  Strip &s = nStrip;
  if(s.focus < 0)
  {
    if(in.hold) { noiseMenu(); return; }
    if(in.click) { noiseZero(); uiToast("Zero set: walk around and watch the level", 1800); return; }
    if(in.turn) volumeTurn(in);
    return;
  }
  s.at = millis();
  if(in.hold) { stripClose(s); return; }
  if(s.editing)
  {
    int d = in.turn > 0 ? 1 : in.turn < 0 ? -1 : 0;
    if(d)
    {
      const ToolBand &b = noiseBands[noiseCfg.band];
      switch(s.focus)
      {
        case N_BAND:  nPendingBand = (nPendingBand + noiseBandCount() + d) % noiseBandCount(); break;
        case N_FREQ:  noiseSetHz(radioFreq() + d * (int32_t)b.step); break;
        case N_RANGE: { int a = noiseCfg.att + d * 3; noiseSetAtt(a < 0 ? 0 : a > 36 ? 36 : a); break; }
        case N_VOL:   { int v = cfg.volume + in.turnFast; radioSetVolume(v < 0 ? 0 : v > 63 ? 63 : v); configSave(); break; }
      }
    }
    if(in.click)
    {
      s.editing = false;
      if(s.focus == N_BAND && nPendingBand != noiseCfg.band) noiseSetBand(nPendingBand);
    }
    return;
  }
  if(in.turn) stripMove(s, in.turn > 0 ? 1 : -1);
  if(!in.click) return;
  switch(s.focus)
  {
    case N_ZERO:  noiseZero(); uiToast("Zero set", 1200); break;
    case N_SPOT:  noiseSpotNow(); break;
    case N_BAND:  nPendingBand = noiseCfg.band; s.editing = true; break;
    case N_QUIET: noiseFindQuiet(); break;
    case N_MAX:   noiseResetMax(); break;
    case N_EXIT:  stripClose(s); exitNoise(); break;
    default:      s.editing = true; break;
  }
}

bool noiseUiTick() { return toolUiTick(nStrip, 100); }

static uint16_t deltaColor(int d) { return d < 6 ? T.good : d < 15 ? T.warn : T.bad; }

void noiseDraw()
{
  lastDraw = millis();
  if(appMenuActive()) { appMenuDraw(); return; }
  char buf[72];
  const ToolBand &b = noiseBands[noiseCfg.band];
  uiStatusBar("Noise Hunter", C_TEAL);

  uiPill(8, 24, 52, 22, C_TEAL, 0xFFFF, b.shortName, F_SMB);
  uiFormatFreq(buf, sizeof(buf), radioFreq(), MODE_AM, true);
  uiText(buf, 8, 58, F_CAP, T.dim, ML_DATUM);

  // The big number: change from zero, or the level itself
  int lvl = noiseLevel(), base = noiseBase();
  if(base >= 0)
  {
    int d = lvl - base;
    snprintf(buf, sizeof(buf), "%+d", d);
    uiText(buf, 236, 46, F_XL, deltaColor(d), MR_DATUM);
    uiText("dB", 242, 40, F_SMB, T.dim, ML_DATUM);
    uiLine("from zero", 242, 57, 74, F_CAP, T.dim, ML_DATUM);
  }
  else
  {
    snprintf(buf, sizeof(buf), "%d", lvl);
    uiText(buf, 236, 46, F_XL, T.text, MR_DATUM);
    uiText("dBuV", 242, 40, F_SMB, T.dim, ML_DATUM);
    uiLine("noise", 242, 57, 74, F_CAP, T.dim, ML_DATUM);
  }

  hintLine(nStrip, base >= 0 ? "Louder = closer to the source" : "Click to set zero here, then walk around", C_TEAL);

  // Meter 0-60 dBuV with peak and zero marks
  int mx = 8, mw = 304, my = 92;
  uiRRect(mx, my, mw, 10, 5, T.surface2);
  int w = lvl * mw / 60; if(w > mw) w = mw;
  if(w > 0) uiRRect(mx, my, w < 10 ? 10 : w, 10, 5, base >= 0 ? deltaColor(lvl - base) : C_TEAL);
  int px = mx + (noisePeak() > 60 ? 60 : noisePeak()) * mw / 60;
  spr.fillRect(px - 1, my - 3, 2, 16, T.text);
  if(base >= 0) { int bx = mx + (base > 60 ? 60 : base) * mw / 60; spr.fillTriangle(bx - 4, my + 15, bx + 4, my + 15, bx, my + 11, T.dim); }

  // Last 30 seconds
  uint8_t h[NOISE_HIST];
  uint16_t n = noiseHistory(h, NOISE_HIST);
  uiRRect(6, 106, 308, 24, 6, T.surface);
  int hmax = 10;
  for(uint16_t k = 0; k < n; k++) if(h[k] + 4 > hmax) hmax = h[k] + 4;
  for(uint16_t k = 1; k < n; k++)
  {
    int x0 = 8 + (NOISE_HIST - n + k - 1) * 2, x1 = x0 + 2;
    int y0 = 127 - h[k - 1] * 18 / hmax, y1 = 127 - h[k] * 18 / hmax;
    spr.drawLine(x0, y0, x1, y1, C_TEAL);
    spr.drawLine(x0, y0 + 1, x1, y1 + 1, C_TEAL);
  }

  snprintf(buf, sizeof(buf), "Now %d  \xC2\xB7  Peak %u  \xC2\xB7  Max %u dBuV", lvl, noisePeak(), noiseMax());
  uiText(buf, 8, 136, F_CAP, T.dim, ML_DATUM);
  if(noiseSpotCount()) { snprintf(buf, sizeof(buf), "%u spots", noiseSpotCount()); uiText(buf, 312, 136, F_CAP, T.dim, MR_DATUM); }

  if(nStrip.focus < 0) uiLine("Turn volume   \xC2\xB7   Click zero   \xC2\xB7   Hold menu", 160, 157, 304, F_SM, T.dim, MC_DATUM);
  else stripDraw(nStrip, nValue);
  if(volumeActive()) volumeDraw();
}

//
// Storm Detector
//
enum SChip : uint8_t { S_LISTEN, S_THRESH, S_QUIET, S_FREQ, S_FLASH, S_VOL, S_RESET, S_EXIT, S_COUNT };
static const char *const sNames[S_COUNT] = { "Listen", "Thresh", "Quiet", "Freq", "Flash", "Volume", "Reset", "Exit" };
static const char *const sHints[S_COUNT] =
{
  "Click: hear the crackles or keep silent", "Turn: dB above the noise for a crash", "Click: look for a quieter frequency",
  "Turn: move the frequency", "Click: flash the screen on alerts", "Turn: volume", "Click: clear the counts", "Click: back to the radio",
};
static Strip sStrip = { sNames, sHints, S_COUNT, -1, 0, false, 0 };

void stormScreenEnter() { stripClose(sStrip); }

static void stormControls() { stripOpen(sStrip, S_THRESH); }
static void stormListenToggle() { stormListen(!stormListening()); }
static void stormClear() { stormReset(); uiToast("Counts cleared", 1200); }
static void stormMenu()
{
  appMenuBegin("Storm Detector");
  appMenuAdd("Back to the radio", IC_BACK, C_RED, exitStorm);
  appMenuAdd("Controls", IC_GEAR, C_GRAY, stormControls);
  appMenuAdd(stormListening() ? "Silent watch" : "Listen to the static", IC_SPEAKER, C_BLUE, stormListenToggle);
  appMenuAdd("Find a quiet frequency", IC_RADAR, C_TEAL, stormFindQuiet);
  appMenuAdd("Clear the counts", IC_RESET, C_ORANGE, stormClear);
  appMenuAdd("Other apps", IC_APPS, C_PURPLE, appsOpenList);
  appMenuShow();
}

static void sValue(uint8_t c, char *b, size_t n)
{
  switch(c)
  {
    case S_LISTEN: snprintf(b, n, "%s", stormListening() ? "On" : "Off"); break;
    case S_THRESH: snprintf(b, n, "%u dB", stormCfg.thresh); break;
    case S_QUIET:  snprintf(b, n, "Find"); break;
    case S_FREQ:   snprintf(b, n, "%lu", (unsigned long)(radioFreq() / 1000)); break;
    case S_FLASH:  snprintf(b, n, "%s", stormCfg.flash ? "On" : "Off"); break;
    case S_VOL:    snprintf(b, n, "%u", cfg.volume); break;
    case S_RESET:  snprintf(b, n, "Clear"); break;
    default:       snprintf(b, n, "Back"); break;
  }
}

void stormInput(const Input &in)
{
  if(!stormActive()) { uiGoHome(); return; }
  if(appMenuActive()) { appMenuInput(in); return; }
  if(volumeActive()) { volumeInput(in); return; }
  Strip &s = sStrip;
  if(s.focus < 0)
  {
    if(in.hold) { stormMenu(); return; }
    if(in.click) { stormListen(!stormListening()); uiToast(stormListening() ? "Listening to the static" : "Silent watch", 1200); return; }
    if(in.turn) volumeTurn(in);
    return;
  }
  s.at = millis();
  if(in.hold) { stripClose(s); return; }
  if(s.editing)
  {
    int d = in.turn > 0 ? 1 : in.turn < 0 ? -1 : 0;
    if(d)
    {
      switch(s.focus)
      {
        case S_THRESH: { int t = stormCfg.thresh + d; stormCfg.thresh = t < 4 ? 4 : t > 30 ? 30 : t; stormSettingsChanged(); break; }
        case S_FREQ:   stormSetHz(radioFreq() + d * 1000); break;
        case S_VOL:    { int v = cfg.volume + in.turnFast; radioSetVolume(v < 0 ? 0 : v > 63 ? 63 : v); configSave(); break; }
      }
    }
    if(in.click) s.editing = false;
    return;
  }
  if(in.turn) stripMove(s, in.turn > 0 ? 1 : -1);
  if(!in.click) return;
  switch(s.focus)
  {
    case S_LISTEN: stormListen(!stormListening()); break;
    case S_QUIET:  stormFindQuiet(); break;
    case S_FLASH:  stormCfg.flash ^= 1; stormSettingsChanged(); break;
    case S_RESET:  stormReset(); uiToast("Counts cleared", 1200); break;
    case S_EXIT:   stripClose(s); exitStorm(); break;
    default:       s.editing = true; break;
  }
}

bool stormUiTick() { return toolUiTick(sStrip, 250); }

void stormDraw()
{
  lastDraw = millis();
  if(appMenuActive()) { appMenuDraw(); return; }
  char buf[72];
  uiStatusBar("Storm Detector", C_YELLOW);

  static const char *const names[] = { "QUIET", "DISTANT", "APPROACHING", "NEARBY", "NOISY" };
  const uint16_t colors[] = { T.good, C_BLUE, C_ORANGE, C_RED, C_GRAY };
  uint8_t a = stormAlert();
  int pw = uiTextWidth(names[a], F_SMB) + 22;
  uiPill(8, 24, pw, 22, colors[a], 0xFFFF, names[a], F_SMB);
  const char *trend = stormTrend() > 0 ? "Getting stronger" : stormTrend() < 0 ? "Getting weaker" : "Steady";
  uiText(a == STORM_NOISY ? "Local interference" : a == STORM_QUIET ? "No lightning heard" : trend, 8, 58, F_CAP, T.dim, ML_DATUM);

  uint32_t n10 = stormCount(10);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)n10);
  uiText(buf, 236, 46, F_XL, T.text, MR_DATUM);
  uiLine("crashes", 242, 40, 74, F_SMB, T.dim, ML_DATUM);
  uiLine("in 10 min", 242, 57, 74, F_CAP, T.dim, ML_DATUM);

  int32_t ago = stormLastAgo();
  char idle[64];
  if(ago < 0) snprintf(idle, sizeof(idle), "Watching %lu kHz for lightning static", (unsigned long)(radioFreq() / 1000));
  else
  {
    uint32_t s = ago / 1000;
    snprintf(idle, sizeof(idle), "Last crash %lu:%02lu ago  \xC2\xB7  +%u dB", (unsigned long)(s / 60), (unsigned long)(s % 60), stormLastPeak());
  }
  hintLine(sStrip, idle, C_YELLOW);

  // Last 60 minutes, one bar per minute, colour = strongest crash
  StormMinute m[60];
  uint16_t n = stormMinutes(m, 60);
  uint16_t top = 1;
  for(uint16_t k = 0; k < n; k++) if(m[k].count > top) top = m[k].count;
  uiRRect(6, 91, 308, 34, 6, T.surface);
  for(uint16_t k = 0; k < n; k++)
  {
    int x = 9 + (60 - n + k) * 5;
    int h = m[k].count ? 2 + m[k].count * 28 / top : 1;
    uint16_t c = !m[k].count ? T.barOff : m[k].maxPeak >= 30 ? C_RED : m[k].maxPeak >= 20 ? C_ORANGE : C_YELLOW;
    spr.fillRect(x, 122 - h, 4, h, c);
  }

  snprintf(buf, sizeof(buf), "Now %lu  \xC2\xB7  1 h %lu", (unsigned long)stormCount(1), (unsigned long)stormCount(60));
  uiText(buf, 8, 136, F_CAP, T.dim, ML_DATUM);
  snprintf(buf, sizeof(buf), "Noise %u dBuV  \xC2\xB7  %s", stormBase(), stormListening() ? "Sound" : "Silent");
  uiText(buf, 312, 136, F_CAP, T.dim, MR_DATUM);

  if(sStrip.focus < 0) uiLine("Turn volume   \xC2\xB7   Click listen   \xC2\xB7   Hold menu", 160, 157, 304, F_SM, T.dim, MC_DATUM);
  else stripDraw(sStrip, sValue);
  if(volumeActive()) volumeDraw();
}
