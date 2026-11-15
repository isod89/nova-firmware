//
// UiApps.cpp - the Apps section and the Ghost Box screen
//
//   Apps        Click: open the app   Hold: back to the radio
//   Ghost Box   Turn: sweep speed     Press + turn: volume
//               Click: mark this moment
//               Hold: controls (pause, band, direction, ... exit)
//
#include "Ui.h"

static uint8_t appsFrom = SCR_STATIONS;

//
// App menu: Hold inside any app. The first line always leads back to the radio.
//
struct AppMenuItem { char label[32]; uint8_t icon; uint16_t color; void (*fn)(); };
static AppMenuItem amItems[20];
static uint8_t amN = 0, amSel = 0;
static bool amOn = false;
static const char *amTitle = "";

void appMenuBegin(const char *title) { amN = 0; amSel = 0; amTitle = title; }

void appMenuAdd(const char *label, uint8_t icon, uint16_t color, void (*fn)())
{
  if(amN >= ITEM_COUNT(amItems)) return;
  snprintf(amItems[amN].label, sizeof(amItems[0].label), "%s", label);
  amItems[amN].icon = icon; amItems[amN].color = color; amItems[amN].fn = fn;
  amN++;
}

void appMenuShow() { amOn = true; uiRequestDraw(); }
bool appMenuActive() { return amOn; }

void appMenuInput(const Input &in)
{
  if(in.hold) { amOn = false; return; }
  if(in.turn && amN) amSel = (amSel + amN + (in.turn > 0 ? 1 : -1)) % amN;
  if(in.click && amN) { amOn = false; if(amItems[amSel].fn) amItems[amSel].fn(); }
}

void appMenuDraw()
{
  uiStatusBar(amTitle, T.text);
  const int rows = 5, rowH = 29, y0 = 21;
  int top = amSel - 2;
  if(top > amN - rows) top = amN - rows;
  if(top < 0) top = 0;
  for(int r = 0; r < rows && top + r < amN; r++)
  {
    int i = top + r, y = y0 + r * rowH;
    bool hl = i == amSel;
    if(hl) uiGloss(6, y, 302, rowH - 3, 10, T.sel); else uiRRect(6, y, 302, rowH - 3, 10, T.surface);
    uiTile(24, y + 13, 20, amItems[i].color, amItems[i].icon);
    uiText(amItems[i].label, 42, y + 13, F_SMB, hl ? T.selText : T.text, ML_DATUM);
  }
  uiScrollbar(312, y0, rows * rowH - 3, amN, rows, top);
}

// Leave any app for the Apps list
void appsOpenList()
{
  appsStopAll();
  uiShow(SCR_APPS);
}

void appsOpen(uint8_t from)
{
  if(from == SCR_STATIONS || from == SCR_TUNE) appsFrom = from;
  uiShow(SCR_APPS);
}

static uint8_t appSel = 0;

struct AppInfo { const char *name, *line, *caption; uint16_t color; uint8_t icon; };
static const AppInfo appList[] =
{
  { "Ghost Box", "ITC sweep for EVP sessions", "30-500 ms sweep  \xC2\xB7  session log", C_PURPLE, IC_GHOST },
  { "Noise Hunter", "Find what spoils reception", "Chargers, LED lights, power supplies...", C_TEAL, IC_WAVE },
  { "Storm Detector", "Lightning static on long wave", "Counts crashes  \xC2\xB7  trend  \xC2\xB7  alert", C_YELLOW, IC_BOLT },
};

static void openApp(uint8_t i)
{
  if(i == 0) { ghostStart(); uiShow(SCR_GHOST); }
  if(i == 1) { noiseStart(); uiShow(SCR_NOISE); }
  if(i == 2) { stormStart(); uiShow(SCR_STORM); }
}

void appsInput(const Input &in)
{
  if(in.hold) { uiShow(appsFrom); return; }
  if(in.turn) appSel = (appSel + ITEM_COUNT(appList) + (in.turn > 0 ? 1 : -1)) % ITEM_COUNT(appList);
  if(in.click) openApp(appSel);
}

void appsDraw()
{
  uiStatusBar("Apps", T.text);
  for(uint8_t i = 0; i < ITEM_COUNT(appList); i++)
  {
    const AppInfo &a = appList[i];
    int y = 22 + i * 44;
    bool sel = i == appSel;
    if(sel) uiGloss(6, y, 308, 41, 12, T.sel); else uiRRect(6, y, 308, 41, 12, T.surface);
    uiTile(28, y + 20, 30, a.color, a.icon);
    uiText(a.name, 52, y + 12, F_MD, sel ? T.selText : T.text, ML_DATUM);
    uiLine(sel ? a.caption : a.line, 52, y + 30, 230, F_CAP, sel ? T.selText : T.dim, ML_DATUM);
    int cx = 298, cy = y + 20;
    spr.drawLine(cx - 3, cy - 5, cx + 2, cy, sel ? T.selText : T.dim);
    spr.drawLine(cx + 2, cy, cx - 3, cy + 5, sel ? T.selText : T.dim);
  }
  uiLine("Turn choose   \xC2\xB7   Click open   \xC2\xB7   Hold back", 160, 159, 304, F_SM, T.dim, MC_DATUM);
}

//
// Ghost Box screen
//
enum GChip : uint8_t { G_MARK, G_QUESTION, G_PAUSE, G_SPEED, G_BAND, G_DIR, G_CONTENT, G_WIDTH, G_STEP, G_FLUTTER, G_CHAOS, G_NOISE,
                      G_GAPS, G_GATE, G_ANT, G_FROM, G_TO, G_CATCH, G_ABOVE, G_REACT, G_HOLD, G_WINDOW, G_ARM, G_SENS, G_OPEN, G_TIMER, G_BLIND,
                      G_VOL, G_EXIT, G_COUNT };
static const char *const gName[G_COUNT] = { "Mark", "Ask", "Pause", "Speed", "Band", "Sweep", "Play", "Width", "Step", "Jitter", "Chaos", "Noise",
                                            "Gaps", "Gate", "Aerial", "From", "To", "Catch", "Above", "React", "Hold", "Wait", "Arm", "Sens", "Open", "Timer",
                                            "Blind", "Vol", "Exit" };
static const char *const gHint[G_COUNT] =
{
  "Click: mark what you heard", "Click: mark the question you ask", "Click: hold this frequency", "Turn: time on each frequency",
  "Turn: band to sweep", "Turn: fwd, rev, bounce, random, shuffle, dual", "Turn: all, noise, stations, marked, clusters",
  "Turn: steps around each station (clusters)", "Turn: step between frequencies", "Click: half-step flutter on/off", "Turn: irregular rhythm",
  "Turn: raw static, normal, quiet", "Click: noise or silent gaps", "Turn: SNR needed to be heard", "Turn: less antenna, fewer stations",
  "Turn: start of the sweep", "Turn: end of the sweep", "Turn: time kept on a signal (a word)",
  "Turn: SNR that counts as a signal", "Turn: your reaction time, for marks", "Turn: after a mark, go back and stay",
  "Turn: silence after a question", "Click: wait muted for an RF burst", "Turn: dB above normal that opens",
  "Turn: seconds open after a burst", "Turn: session length", "Click: hide frequency and signal",
  "Turn: volume", "Click: back to the radio",
};
static const char *const contentNames[] = { "All", "Noise only", "Stations only", "Marked", "Clusters" };
#define G_VISIBLE 6

static int8_t focus = -1;
static bool editing = false;
static uint8_t first = 0;
static uint32_t focusAt = 0;
static bool stepped = false;
static uint32_t lastDraw = 0;

static const char *bandShort(uint8_t i)
{
  static const char *const n[] = { "FM", "FM 76", "FM 64", "AM", "LW", "49m", "41m", "31m", "25m", "19m", "SW" };
  return i < ITEM_COUNT(n) ? n[i] : "?";
}

static int rateIndex()
{
  int best = 0;
  for(int i = 0; i < ghostRateCount(); i++)
    if(abs((int)ghostRates[i] - (int)ghost.rate) < abs((int)ghostRates[best] - (int)ghost.rate)) best = i;
  return best;
}

static void changeRate(int d)
{
  int i = rateIndex() + d;
  if(i < 0) i = 0;
  if(i >= ghostRateCount()) i = ghostRateCount() - 1;
  ghost.rate = ghostRates[i];
  ghostSettingsChanged();
}

static void doMark(uint8_t kind = 0)
{
  uint16_t n = ghostMark(kind);
  char f[20], buf[64];
  uiFormatFreq(f, sizeof(f), radioFreq(), radioMode(), true);
  uint32_t s = ghostElapsed() / 1000;
  if(kind) snprintf(buf, sizeof(buf), "Question %u  \xC2\xB7  %02lu:%02lu", n, (unsigned long)(s / 60), (unsigned long)(s % 60));
  else if(ghost.blind) snprintf(buf, sizeof(buf), "Mark %u  \xC2\xB7  %02lu:%02lu", n, (unsigned long)(s / 60), (unsigned long)(s % 60));
  else snprintf(buf, sizeof(buf), "Mark %u  \xC2\xB7  %s  \xC2\xB7  %02lu:%02lu", n, f, (unsigned long)(s / 60), (unsigned long)(s % 60));
  uiToast(buf, 1500);
}
static void ghostQuestion() { doMark(1); }
static void ghostBlindToggle() { ghost.blind ^= 1; ghostSettingsChanged(); }

static void exitGhost();
static void ghostControls() { focus = G_PAUSE; first = 0; editing = false; focusAt = millis(); }
static void ghostPauseToggle() { ghostPause(!ghostPaused()); }
static void ghostMarkNow();

static void ghostMenu();
static void profileApply(uint8_t i)
{
  ghostApplyProfile(i);
  char buf[48]; snprintf(buf, sizeof(buf), "Profile: %s", ghostProfiles[i].name);
  uiToast(buf, 1800);
}
static void userApply(uint8_t i)
{
  if(!ghostUserProfile(i, nullptr)) { uiToast("Empty. Save your settings there first.", 2000); return; }
  ghostLoadUserProfile(i);
  char buf[32]; snprintf(buf, sizeof(buf), "My profile %u loaded", i + 1); uiToast(buf, 1800);
}
static void userSave(uint8_t i) { ghostSaveUserProfile(i); char buf[32]; snprintf(buf, sizeof(buf), "Saved as My profile %u", i + 1); uiToast(buf, 1800); }
#define PF(n) static void pf##n() { profileApply(n); }
PF(0) PF(1) PF(2) PF(3) PF(4) PF(5) PF(6) PF(7) PF(8)
static void (*const pfFns[])() = { pf0, pf1, pf2, pf3, pf4, pf5, pf6, pf7, pf8 };
static void up0() { userApply(0); } static void up1() { userApply(1); } static void up2() { userApply(2); }
static void us0() { userSave(0); } static void us1() { userSave(1); } static void us2() { userSave(2); }

static void ghostProfilesMenu()
{
  appMenuBegin("Profiles");
  appMenuAdd("Back", IC_BACK, C_GRAY, ghostMenu);
  for(uint8_t i = 0; i < ghostProfileCount() && i < ITEM_COUNT(pfFns); i++) appMenuAdd(ghostProfiles[i].name, IC_STAR, C_PURPLE, pfFns[i]);
  static const char *const un[] = { "My profile 1", "My profile 2", "My profile 3" };
  static const char *const sn[] = { "Save as My profile 1", "Save as My profile 2", "Save as My profile 3" };
  void (*const ua[])() = { up0, up1, up2 }, (*const sa[])() = { us0, us1, us2 };
  for(uint8_t i = 0; i < 3; i++) if(ghostUserProfile(i, nullptr)) appMenuAdd(un[i], IC_STAR, C_YELLOW, ua[i]);
  for(uint8_t i = 0; i < 3; i++) appMenuAdd(sn[i], IC_PLUS, C_GRAY, sa[i]);
  appMenuShow();
}

static void ghostMenu()
{
  appMenuBegin("Ghost Box");
  appMenuAdd("Back to the radio", IC_BACK, C_RED, exitGhost);
  appMenuAdd("Controls", IC_GEAR, C_GRAY, ghostControls);
  appMenuAdd(ghostPaused() ? "Resume the sweep" : "Pause the sweep", IC_STANDBY, C_ORANGE, ghostPauseToggle);
  appMenuAdd("Mark this moment", IC_STAR, C_YELLOW, ghostMarkNow);
  appMenuAdd("Question asked", IC_INFO, C_BLUE, ghostQuestion);
  appMenuAdd("Profiles", IC_LIST, C_TEAL, ghostProfilesMenu);
  appMenuAdd(ghost.blind ? "Show frequency and signal" : "Blind mode", IC_MOON, C_INDIGO, ghostBlindToggle);
  appMenuAdd("Other apps", IC_APPS, C_PURPLE, appsOpenList);
  appMenuShow();
}

static void exitGhost()
{
  uint16_t marks = ghostMarkCount();
  ghostStop();
  focus = -1; editing = false;
  uiGoHome();
  char buf[48];
  snprintf(buf, sizeof(buf), "Ghost Box closed  \xC2\xB7  %u mark%s", marks, marks == 1 ? "" : "s");
  uiToast(buf, 2200);
}

void ghostScreenEnter() { focus = -1; editing = false; first = 0; }

static void chipValue(uint8_t c, char *b, size_t n)
{
  switch(c)
  {
    case G_SPEED:   snprintf(b, n, "%ums", ghost.rate); break;
    case G_BAND:    snprintf(b, n, "%s", bandShort(ghost.band)); break;
    case G_DIR:     { static const char *const d[] = { "Fwd", "Rev", "Bounce", "Random", "Shuffle", "Dual" }; snprintf(b, n, "%s", d[ghost.dir % GHOST_DIRS]); break; }
    case G_STEP:
    {
      uint32_t s = ghostStepHz();
      if(s >= 1000000) snprintf(b, n, "%lu M", (unsigned long)(s / 1000000));
      else snprintf(b, n, "%lu k", (unsigned long)(s / 1000));
      break;
    }
    case G_FLUTTER: snprintf(b, n, "%s", ghost.flutter ? "On" : "Off"); break;
    case G_GAPS:    snprintf(b, n, "%s", ghost.gaps ? "Mute" : "Noise"); break;
    case G_GATE:    snprintf(b, n, "%u dB", ghost.gate); break;
    case G_ANT:     { static const char *const a[] = { "Full", "Med", "Low", "Min" }; snprintf(b, n, "%s", a[ghost.antenna & 3]); break; }
    case G_VOL:     snprintf(b, n, "%u", cfg.volume); break;
    case G_QUESTION: snprintf(b, n, "Q"); break;
    case G_CONTENT: { static const char *const c[] = { "All", "Noise", "Radio", "Marked", "Cluster" }; snprintf(b, n, "%s", c[ghost.content % GC_COUNT]); break; }
    case G_WIDTH:   snprintf(b, n, "\xC2\xB1%u", ghost.cluster); break;
    case G_NOISE:   { static const char *const c[] = { "Raw", "Norm", "Quiet" }; snprintf(b, n, "%s", c[ghost.noise % 3]); break; }
    case G_REACT:   snprintf(b, n, "%u.%u s", ghost.react / 10, ghost.react % 10); break;
    case G_HOLD:    if(ghost.onMark == 1) snprintf(b, n, "%u s", ghost.holdSec); else snprintf(b, n, "%s", ghost.onMark ? "Stop" : "Off"); break;
    case G_CATCH:   if(ghost.catchT) snprintf(b, n, "%u.%u s", ghost.catchT / 10, ghost.catchT % 10); else snprintf(b, n, "Off"); break;
    case G_ABOVE:   snprintf(b, n, "%u dB", ghost.catchSnr); break;
    case G_WINDOW:  if(ghost.window) snprintf(b, n, "%u s", ghost.window); else snprintf(b, n, "Off"); break;
    case G_ARM:     snprintf(b, n, "%s", ghost.trigger ? "On" : "Off"); break;
    case G_SENS:    snprintf(b, n, "%u dB", ghost.trigSens); break;
    case G_OPEN:    snprintf(b, n, "%u s", ghost.trigOpen); break;
    case G_CHAOS:   { static const char *const c[] = { "Off", "25%", "50%" }; snprintf(b, n, "%s", c[ghost.chaos % 3]); break; }
    case G_TIMER:   if(ghost.timer) snprintf(b, n, "%u min", ghost.timer); else snprintf(b, n, "Off"); break;
    case G_BLIND:   snprintf(b, n, "%s", ghost.blind ? "On" : "Off"); break;
    case G_FROM:
    case G_TO:
    {
      uint32_t v = c == G_FROM ? ghost.lo : ghost.hi;
      if(!v) snprintf(b, n, "Edge");
      else if(ghostBands[ghost.band].mode == MODE_FM) snprintf(b, n, "%lu.%lu", (unsigned long)(v / 1000000), (unsigned long)(v % 1000000 / 100000));
      else snprintf(b, n, "%lu", (unsigned long)(v / 1000));
      break;
    }
    default:        b[0] = '\0'; break;
  }
}

// Range edits move by ten steps; past the band edge means "the whole band"
static void adjustRange(bool isFrom, int d)
{
  const GhostBand &b = ghostBands[ghost.band];
  uint32_t st = b.mode == MODE_FM ? 500000 : b.step * 10;
  uint32_t &v = isFrom ? ghost.lo : ghost.hi;
  int64_t x = v ? v : (isFrom ? b.lo : b.hi);
  x += d * (int64_t)st;
  if(isFrom) { uint32_t top = ghost.hi ? ghost.hi : b.hi; v = x <= (int64_t)b.lo ? 0 : x >= (int64_t)top - st ? top - st : (uint32_t)x; }
  else { uint32_t bot = ghost.lo ? ghost.lo : b.lo; v = x >= (int64_t)b.hi ? 0 : x <= (int64_t)bot + st ? bot + st : (uint32_t)x; }
  ghostSettingsChanged();
  ghostApply();
}

static void adjust(uint8_t c, int d, int fast)
{
  switch(c)
  {
    case G_SPEED: changeRate(d); return;
    case G_BAND:  ghost.band = (ghost.band + ghostBandCount() + d) % ghostBandCount(); ghost.lo = ghost.hi = 0; ghostSettingsChanged(); ghostApply(); return;
    case G_DIR:   ghost.dir = (ghost.dir + GHOST_DIRS + d) % GHOST_DIRS; break;
    case G_STEP:  ghost.step = (ghost.step + 3 + d) % 3; ghostSettingsChanged(); ghostApply(); return;
    case G_GATE:  { int g = ghost.gate + d; ghost.gate = g < 1 ? 1 : g > 30 ? 30 : g; break; }
    case G_ANT:   ghost.antenna = (ghost.antenna + 4 + d) % 4; ghostSettingsChanged(); ghostApply(); return;
    case G_VOL:   { int v = cfg.volume + fast; radioSetVolume(v < 0 ? 0 : v > 63 ? 63 : v); configSave(); return; }
    case G_CONTENT: ghost.content = (ghost.content + GC_COUNT + d) % GC_COUNT; break;
    case G_WIDTH: { int v = ghost.cluster + d; ghost.cluster = v < 1 ? 1 : v > 10 ? 10 : v; break; }
    case G_NOISE: ghost.noise = (ghost.noise + 3 + d) % 3; break;
    case G_REACT: { int v = ghost.react + d; ghost.react = v < 0 ? 0 : v > 10 ? 10 : v; break; }
    case G_HOLD:
    {
      // Off, 1 to 10 s, Stop
      static const uint8_t o[] = { 0, 1, 2, 3, 4, 5, 8, 10, 255 };
      int k = ghost.onMark == 0 ? 0 : ghost.onMark == 2 ? 8 : 3;
      if(ghost.onMark == 1) for(int i = 1; i < 8; i++) if(o[i] == ghost.holdSec) k = i;
      k = (k + 9 + d) % 9;
      if(o[k] == 0) ghost.onMark = 0; else if(o[k] == 255) ghost.onMark = 2; else { ghost.onMark = 1; ghost.holdSec = o[k]; }
      break;
    }
    case G_CATCH:
    {
      static const uint8_t o[] = { 0, 5, 8, 10, 15, 20, 30, 50 };
      int k = 0; for(size_t i = 0; i < ITEM_COUNT(o); i++) if(o[i] == ghost.catchT) k = i;
      ghost.catchT = o[(k + ITEM_COUNT(o) + d) % ITEM_COUNT(o)];
      break;
    }
    case G_ABOVE: { int v = ghost.catchSnr + d; ghost.catchSnr = v < 2 ? 2 : v > 40 ? 40 : v; break; }
    case G_WINDOW:
    {
      static const uint8_t o[] = { 0, 5, 8, 10, 15, 20, 30 };
      int k = 0; for(size_t i = 0; i < ITEM_COUNT(o); i++) if(o[i] == ghost.window) k = i;
      ghost.window = o[(k + ITEM_COUNT(o) + d) % ITEM_COUNT(o)];
      break;
    }
    case G_SENS:  { int v = ghost.trigSens + d; ghost.trigSens = v < 2 ? 2 : v > 30 ? 30 : v; break; }
    case G_OPEN:
    {
      static const uint8_t o[] = { 5, 10, 15, 20, 30, 60 };
      int k = 1; for(size_t i = 0; i < ITEM_COUNT(o); i++) if(o[i] == ghost.trigOpen) k = i;
      ghost.trigOpen = o[(k + ITEM_COUNT(o) + d) % ITEM_COUNT(o)];
      break;
    }
    case G_CHAOS: ghost.chaos = (ghost.chaos + 3 + d) % 3; break;
    case G_TIMER:
    {
      static const uint8_t o[] = { 0, 5, 10, 15, 20, 30, 45, 60 };
      int k = 0; for(size_t i = 0; i < ITEM_COUNT(o); i++) if(o[i] == ghost.timer) k = i;
      ghost.timer = o[(k + ITEM_COUNT(o) + d) % ITEM_COUNT(o)];
      break;
    }
    case G_FROM:  adjustRange(true, d); return;
    case G_TO:    adjustRange(false, d); return;
    default: return;
  }
  ghostSettingsChanged();
}

static bool isValueChip(uint8_t c)
{
  return c == G_SPEED || c == G_BAND || c == G_DIR || c == G_STEP || c == G_GATE || c == G_ANT || c == G_VOL ||
         c == G_CONTENT || c == G_CHAOS || c == G_TIMER || c == G_FROM || c == G_TO || c == G_WIDTH || c == G_NOISE ||
         c == G_REACT || c == G_HOLD || c == G_WINDOW || c == G_SENS || c == G_OPEN || c == G_CATCH || c == G_ABOVE;
}

void ghostInput(const Input &in)
{
  if(!ghostActive()) { uiGoHome(); return; }
  if(appMenuActive()) { appMenuInput(in); return; }
  if(volumeActive()) { volumeInput(in); return; }

  if(focus < 0)
  {
    if(in.hold) { ghostMenu(); return; }
    if(in.click) { doMark(); return; }
    if(in.pushTurn) { volumeOpen(); Input t = in; t.pushTurn = false; volumeInput(t); return; }
    if(in.turn) changeRate(in.turn > 0 ? 1 : -1);
    return;
  }

  focusAt = millis();
  if(in.hold) { focus = -1; editing = false; return; }
  if(editing)
  {
    if(in.turn) adjust(focus, in.turn > 0 ? 1 : -1, in.turnFast);
    if(in.click) editing = false;
    return;
  }
  if(in.turn)
  {
    focus = (focus + G_COUNT + (in.turn > 0 ? 1 : -1)) % G_COUNT;
    if(focus < first) first = focus;
    if(focus >= first + G_VISIBLE) first = focus - G_VISIBLE + 1;
  }
  if(!in.click) return;
  switch(focus)
  {
    case G_MARK:    doMark(); break;
    case G_QUESTION: doMark(1); break;
    case G_BLIND:   ghost.blind ^= 1; ghostSettingsChanged(); break;
    case G_ARM:     ghost.trigger ^= 1; ghostSettingsChanged(); break;
    case G_PAUSE:   ghostPause(!ghostPaused()); break;
    case G_FLUTTER: ghost.flutter ^= 1; ghostSettingsChanged(); break;
    case G_GAPS:    ghost.gaps ^= 1; ghostSettingsChanged(); ghostApply(); break;
    case G_EXIT:    exitGhost(); break;
    default:        if(isValueChip(focus)) editing = true; break;
  }
}

void ghostStepped() { stepped = true; }

bool ghostUiTick()
{
  if(ghostTakeWindowDone())
  {
    screenWake();
    backlight(0); delay(90); backlightRestore();
    uiToast("Answer window closed", 1500);
    return true;
  }
  static uint32_t lastQuick = 0;
  if((ghostWindowLeft() || ghostHoldLeft() || ghostTrigState()) && millis() - lastQuick >= 250) { lastQuick = millis(); return true; }
  if(ghostTakeTimerDone())
  {
    screenWake();
    for(int i = 0; i < 3; i++) { backlight(0); delay(120); backlight(255); delay(120); }
    backlightRestore();
    char buf[48];
    snprintf(buf, sizeof(buf), "Session complete  \xC2\xB7  %u marks", ghostMarkCount());
    uiToast(buf, 5000);
    return true;
  }
  if(focus >= 0 && millis() - focusAt > 10000) { focus = -1; editing = false; return true; }
  // A step every 30 ms is faster than the screen needs: about 15 frames/s
  if(stepped && millis() - lastDraw >= 66) { stepped = false; return true; }
  static uint32_t clockTick = 0;
  if(millis() - clockTick >= 1000) { clockTick = millis(); return true; }
  return false;
}

static void flagIcon(int x, int y, uint16_t c)
{
  spr.fillRect(x, y - 7, 2, 15, c);
  spr.fillTriangle(x + 2, y - 7, x + 11, y - 3, x + 2, y + 1, c);
}

static void ghostMarkNow() { doMark(); }

void ghostDraw()
{
  lastDraw = millis();
  if(appMenuActive()) { appMenuDraw(); return; }
  char buf[72], f[24];
  uint32_t hz = radioFreq();
  uint8_t m = radioMode();
  const GhostBand &b = ghostBands[ghost.band];
  uint16_t accent = C_PURPLE;
  uiStatusBar("Ghost Box", accent);
  bool p = ghostPaused();
  uint32_t sec = ghostElapsed() / 1000;
  uint32_t left = ghostRemaining() / 1000;

  // Blind mode: nothing that could influence the listener, only time and marks
  if(ghost.blind && focus < 0)
  {
    uiPill(8, 24, 64, 22, p ? T.warn : C_INDIGO, 0xFFFF, p ? "PAUSED" : "BLIND", F_SMB);
    if(ghost.timer) snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(left / 60), (unsigned long)(left % 60));
    else snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    uiText(buf, 160, 74, F_XL, T.text, MC_DATUM);
    if(ghostWindowLeft())
    {
      snprintf(buf, sizeof(buf), "Answer window  \xC2\xB7  %lu s", (unsigned long)((ghostWindowLeft() + 999) / 1000));
      uiLine(buf, 160, 104, 300, F_SMB, C_YELLOW, MC_DATUM);
    }
    else uiLine(ghost.timer ? "left in the session" : "session time", 160, 104, 300, F_SM, T.dim, MC_DATUM);
    snprintf(buf, sizeof(buf), "%u marks", ghostMarkCount());
    uiText(buf, 160, 126, F_SMB, accent, MC_DATUM);
    uiLine("Click  \xC2\xB7  mark     Hold  \xC2\xB7  question, exit", 160, 157, 304, F_SM, T.dim, MC_DATUM);
    if(volumeActive()) volumeDraw();
    return;
  }

  // State and frequency
  {
    uint8_t ts = ghostTrigState();
    const char *pt = p ? "PAUSED" : ghostHoldKind() == 2 ? "CATCH" : ghostHoldLeft() ? "HOLD" : ts == 1 ? "ARMED" : ts == 2 ? "OPEN" : "SWEEP";
    uint16_t pc = p ? T.warn : ghostHoldKind() == 2 ? C_GREEN : ghostHoldLeft() ? C_ORANGE : ts == 1 ? C_INDIGO : ts == 2 ? C_RED : accent;
    uiPill(8, 24, 64, 22, pc, 0xFFFF, pt, F_SMB);
  }
  uiBadge(8, 52, T.surface2, T.text, bandShort(ghost.band));
  uiFormatFreq(f, sizeof(f), hz, m, false);
  uiText(f, 262, 46, F_XL, T.text, MR_DATUM);
  uiText(m == MODE_FM ? "MHz" : "kHz", 268, 56, F_SMB, T.dim, ML_DATUM);

  // Speed line or the focused control's hint
  if(focus >= 0)
  {
    snprintf(buf, sizeof(buf), "%s  \xC2\xB7  %s", gName[focus], editing ? "Turn to change, click when done" : gHint[focus]);
    uiLine(buf, 160, 82, 306, F_SM, T.text, MC_DATUM);
  }
  else if(ghostWindowLeft())
  {
    snprintf(buf, sizeof(buf), "Answer window  \xC2\xB7  %lu s", (unsigned long)((ghostWindowLeft() + 999) / 1000));
    uiLine(buf, 160, 82, 306, F_SMB, C_YELLOW, MC_DATUM);
  }
  else if(ghostTrigState() == 1)
  {
    snprintf(buf, sizeof(buf), "Waiting for an RF burst  \xC2\xB7  %u / %u dBuV", ghostTrigLevel(), ghostTrigBase() + ghost.trigSens);
    uiLine(buf, 160, 82, 306, F_SMB, C_INDIGO, MC_DATUM);
  }
  else if(ghostTrigState() == 2)
  {
    snprintf(buf, sizeof(buf), "RF burst  \xC2\xB7  open %lu s", (unsigned long)((ghostTrigOpenLeft() + 999) / 1000));
    uiLine(buf, 160, 82, 306, F_SMB, C_RED, MC_DATUM);
  }
  else
  {
    const char *content = ghostLearning() ? "Learning stations" : contentNames[ghost.content % GC_COUNT];
    snprintf(buf, sizeof(buf), "%u ms  \xC2\xB7  %s  \xC2\xB7  %s%s%s", ghost.rate, ghostDirName(ghost.dir), content,
             ghost.chaos ? "  \xC2\xB7  Chaos" : "", ghost.flutter ? "  \xC2\xB7  Flutter" : "");
    uiLine(buf, 160, 82, 306, F_SMB, accent, MC_DATUM);
  }

  // Activity: signal of the last steps, brighter when above the gate
  uint8_t r[76], s[76];
  uint8_t n = ghostHistory(r, s, 76);
  uiRRect(6, 91, 308, 30, 6, T.surface);
  for(uint8_t k = 0; k < n; k++)
  {
    int h = r[k] * 26 / 60;
    if(h > 26) h = 26;
    if(h < 1) h = 1;
    int x = 8 + (76 - n + k) * 4;
    bool heard = s[k] >= ghost.gate;
    spr.fillRect(x, 119 - h, 3, h, heard ? (k == n - 1 ? T.text : accent) : T.barOff);
  }

  // Position in the band, with this session's marks
  uint32_t lo = b.lo, hi = b.hi;
  int track = 304;
  spr.fillRect(8, 124, track, 2, T.surface2);
  if(ghost.lo || ghost.hi)
  {
    // The part of the band being swept
    uint32_t a = ghostBandLo(), z = ghostBandHi();
    int xa = 8 + (int)((uint64_t)(a - lo) * track / (hi - lo)), xz = 8 + (int)((uint64_t)(z - lo) * track / (hi - lo));
    spr.fillRect(xa, 123, xz - xa + 1, 4, uiMix(T.surface2, accent, 160));
  }
  for(uint16_t i = 0; i < ghostMarkCount(); i++)
  {
    const GhostPoint *mk = ghostMarkAt(i);
    if(mk && mk->hz >= lo && mk->hz <= hi) spr.fillRect(8 + (int)((uint64_t)(mk->hz - lo) * track / (hi - lo)), 122, 2, 6, T.warn);
  }
  if(hz >= lo && hz <= hi)
  {
    int x = 8 + (int)((uint64_t)(hz - lo) * track / (hi - lo));
    spr.fillCircle(x, 125, 3, T.text);
  }

  // Session line
  if(ghost.timer)
    snprintf(buf, sizeof(buf), "%02lu:%02lu left  \xC2\xB7  %u mark%s", (unsigned long)(left / 60), (unsigned long)(left % 60), ghostMarkCount(), ghostMarkCount() == 1 ? "" : "s");
  else
    snprintf(buf, sizeof(buf), "%02lu:%02lu  \xC2\xB7  %lu steps  \xC2\xB7  %u mark%s", (unsigned long)(sec / 60), (unsigned long)(sec % 60),
             (unsigned long)ghostSteps(), ghostMarkCount(), ghostMarkCount() == 1 ? "" : "s");
  uint8_t lr = n ? r[n - 1] : 0, ls = n ? s[n - 1] : 0;
  char sig[32];
  snprintf(sig, sizeof(sig), "%u dBuV  \xC2\xB7  SNR %u", lr, ls);
  int room = 304 - uiTextWidth(sig, F_CAP) - 10;
  if(uiTextWidth(buf, F_CAP) > room && !ghost.timer)   // long sessions: drop the step count
    snprintf(buf, sizeof(buf), "%02lu:%02lu  \xC2\xB7  %u mark%s", (unsigned long)(sec / 60), (unsigned long)(sec % 60),
             ghostMarkCount(), ghostMarkCount() == 1 ? "" : "s");
  uiText(buf, 8, 135, F_CAP, T.dim, ML_DATUM);
  uiText(sig, 312, 135, F_CAP, T.dim, MR_DATUM);

  // Control strip, or the gestures when it is closed
  if(focus < 0)
  {
    uiLine("Turn speed   \xC2\xB7   Click mark   \xC2\xB7   Hold menu", 160, 157, 304, F_SM, T.dim, MC_DATUM);
  }
  else
  {
    int w = 50;
    for(int k = 0; k < G_VISIBLE; k++)
    {
      int c = first + k;
      if(c >= G_COUNT) break;
      int x = 6 + k * (w + 2), y = 142;
      bool on = focus == c;
      uint16_t bg = on ? (editing ? T.accent : T.sel) : T.surface;
      uint16_t fg = on ? (editing ? T.accentText : T.selText) : T.text;
      if(on) uiGloss(x, y, w, 27, 9, bg); else uiRRect(x, y, w, 27, 9, bg);
      uiLine(gName[c], x + w / 2, y + 7, w - 4, F_CAP, on ? fg : T.dim, MC_DATUM);
      if(c == G_MARK) flagIcon(x + w / 2 - 5, y + 19, on ? fg : T.warn);
      else if(c == G_PAUSE)
      {
        if(p) spr.fillTriangle(x + w / 2 - 4, y + 13, x + w / 2 + 5, y + 18, x + w / 2 - 4, y + 23, fg);
        else { spr.fillRect(x + w / 2 - 5, y + 13, 3, 10, fg); spr.fillRect(x + w / 2 + 2, y + 13, 3, 10, fg); }
      }
      else if(c == G_EXIT)
      {
        spr.drawLine(x + w / 2 + 4, y + 13, x + w / 2 - 2, y + 18, fg);
        spr.drawLine(x + w / 2 - 2, y + 18, x + w / 2 + 4, y + 23, fg);
        spr.drawLine(x + w / 2 + 5, y + 13, x + w / 2 - 1, y + 18, fg);
        spr.drawLine(x + w / 2 - 1, y + 18, x + w / 2 + 5, y + 23, fg);
      }
      else
      {
        chipValue(c, buf, sizeof(buf));
        uiLine(buf, x + w / 2, y + 19, w - 4, F_SMB, fg, MC_DATUM);
      }
    }
    if(first > 0) spr.fillTriangle(1, 155, 4, 151, 4, 159, T.dim);
    if(first + G_VISIBLE < G_COUNT) spr.fillTriangle(318, 155, 315, 151, 315, 159, T.dim);
  }

  if(volumeActive()) volumeDraw();
}
