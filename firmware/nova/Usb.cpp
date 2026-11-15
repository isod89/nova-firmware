//
// Usb.cpp - the USB serial line protocol used by the NOVA Editor and NOVA
// Installer (Chrome / Edge, Web Serial). Fields are separated by TAB, one
// command per line. Wi-Fi is only used on request (WIFI commands, Wifi.cpp).
//
//   NOVA?                         -> NOVA <version>
//   INFO                          -> INFO <key> <value> ... END
//   STATUS                        -> STATUS freq mode rssi snr stereo volume battery% screen name text
//   CFG GET                       -> CFG <key> <value> ... END
//   CFG SET <key> <value>         -> OK <key> <value> | ERR <reason>
//   CFG DEFAULTS                  -> OK            settings back to defaults (stations kept)
//   ST LIST                       -> ST freq mode rssi snr flags name label ... END <n>
//   ST BEGIN / ST+ ... / ST COMMIT-> READY / - / DONE <n>   replace the whole list
//   BL LIST                       -> BL freq ... END <n>
//   BL BEGIN / BL+ freq / BL COMMIT -> READY / - / DONE <n>
//   TUNE <hz> <mode>              -> OK | ERR
//   SCAN [parts speed]            -> OK, then SCANDONE <stations> when finished
//   SCAN EST [parts speed]        -> EST <seconds> <channels>
//   GB CAPS                       -> GBB band list, GBR rates, END
//   GB STATUS                     -> GB active paused band hz mode rate dir step flutter gaps gate antenna volume elapsed steps marks trace rssi snr epoch
//                                    content chaos timer blind lo hi left learning carriers bandLo bandHi
//                                    react onMark window noise cluster trigger trigSens trigOpen holdLeft windowLeft trigState trigLevel trigBase trigOpenLeft
//                                    catchT catchSnr holdSec holdKind catches
//   GB PROFILES / PROFILE <n> | U<slot> | SAVE <slot>   GB HIST [i | CLEAR]   (marks kind: 0 response, 1 question, 2 RF trigger)
//   GB START / STOP / PAUSE / RESUME -> OK
//   GB MARK [Q]                   -> OK <n>   (Q = a question asked)
//   GB SET <key> <value>          -> OK <key> <value>   (band rate dir step flutter gaps gate antenna content chaos timer blind lo hi react onMark window noise cluster trigger trigSens trigOpen catchT catchSnr holdSec)
//   GB MARKS                      -> GBM n t hz rssi snr kind ... END
//   GB TRACE <from>               -> GBT i t hz rssi snr (500 max) ... END next total oldest
//   NH CAPS / STATUS / SPOTS / START / STOP / ZERO / MAX / QUIET / SPOT / SET band|hz|att v
//   SD STATUS / MIN / EVENTS <from> / START / STOP / RESET / QUIET / LISTEN / MUTE / SET thresh|flash|hz v
//   while a search runs:  NOVA? -> NOVA <version> SCAN <percent>
//                         SCAN STOP -> OK (search stopped, results kept)
//                         anything else -> BUSY
//   SHOT [D]                      -> SHOT 320 170, Q<y> <base64 run-length rows>, END <rows>  (D: changed rows only)
//   TIME <epoch>                  -> OK            set the clock (UTC)
//   WIFI GET | SETHEX ssid pass | FORGET | TEST | EIBI   (WPROG stage n ... then WIFI OK ip rssi clock / DONE count / FAIL reason)
//   EIBI BEGIN                    -> READY         start receiving eibi.txt lines
//   <eibi.txt lines>              -> ACK <n>       every 100 lines (flow control)
//   EIBI END                      -> DONE <count>  sort, save, use
//
#include "Nova.h"

// Found by the web editor inside nova.ino.bin to check the update matches the radio
extern "C" const char novaImageTag[] __attribute__((used)) = "NOVA-IMAGE " NOVA_VERSION " " NOVA_VARIANT " END";

static char line[320];
static uint16_t len = 0;
static bool importing = false;
static uint32_t received = 0, parsed = 0, lastDraw = 0;
static Station *pendingList = nullptr;
static uint16_t pendingN = 0;
static uint32_t pendingBlock[BLOCK_MAX];
static uint8_t pendingBlockN = 0;
static bool scanRequested = false;

void usbInit()
{
#ifndef NOVA_SIM
  Serial.setRxBufferSize(8192);
  Serial.setTxTimeoutMs(50);
  Serial.begin(115200);
#endif
}

//
// Settings table: every adjustable value of the Settings menu
//
struct CfgKey
{
  const char *key;
  int16_t min, max;
  int  (*get)();
  void (*set)(int v);
};

static void applyAudio() { radioApplyAudio(); }

static const CfgKey keys[] =
{
  { "volume",        0,    63, [] { return (int)cfg.volume; },        [](int v) { radioSetVolume(v); } },
  { "squelch",       0,    30, [] { return (int)cfg.squelch; },       [](int v) { cfg.squelch = v; } },
  { "softMute",      0,    32, [] { return (int)cfg.softMute[0]; },   [](int v) { cfg.softMute[0] = cfg.softMute[1] = v; applyAudio(); } },
  { "maxGain",      12,    90, [] { return (int)cfg.avc[0]; },        [](int v) { cfg.avc[0] = cfg.avc[1] = v; applyAudio(); } },
  { "fmStereo",      0,     2, [] { return (int)cfg.fmStereo; },      [](int v) { cfg.fmStereo = v; applyAudio(); } },
  { "clickSupp",     0,     1, [] { return (int)cfg.audioClamp; },    [](int v) { cfg.audioClamp = v; } },
  { "region",        0,     2, [] { return (int)cfg.region; },        [](int v) { cfg.region = v; applyAudio(); radioApplyStep(); } },
  { "rds",           0,     1, [] { return (int)cfg.rds; },           [](int v) { cfg.rds = v; if(radioMode() == MODE_FM) radioApply(); } },
  { "antennaAuto",   0,     1, [] { return (int)cfg.antennaAuto; },   [](int v) { cfg.antennaAuto = v; } },
  { "bwAM",          0,     6, [] { return (int)cfg.bwIdx[MODE_AM]; },[](int v) { cfg.bwIdx[MODE_AM] = v; applyAudio(); } },
  { "bwFM",          0,     4, [] { return (int)cfg.bwIdx[MODE_FM]; },[](int v) { cfg.bwIdx[MODE_FM] = v; applyAudio(); } },
  { "bwSSB",         0,     5, [] { return (int)cfg.bwIdx[MODE_USB]; },[](int v) { cfg.bwIdx[MODE_USB] = cfg.bwIdx[MODE_LSB] = v; applyAudio(); } },
  { "agcAM",         0,    37, [] { return (int)cfg.agc[1]; },        [](int v) { cfg.agc[1] = v; applyAudio(); } },
  { "agcFM",         0,    27, [] { return (int)cfg.agc[0]; },        [](int v) { cfg.agc[0] = v; applyAudio(); } },
  { "calSSB",    -2000,  2000, [] { return (int)cfg.calSSB; },        [](int v) { cfg.calSSB = v; if(modeIsSSB(radioMode())) radioSetFreq(radioFreq()); } },
  { "theme",         0,    31, [] { return (int)cfg.theme; },         [](int v) { cfg.theme = v < themeCount() ? v : 0; } },
  { "brightness",   10,   255, [] { return (int)cfg.brightness; },    [](int v) { cfg.brightness = v; backlight(v); } },
  { "screenTimeout", 0,   600, [] { return (int)cfg.screenTimeout; }, [](int v) { cfg.screenTimeout = v; } },
  { "homeScreen",    0,     1, [] { return (int)cfg.homeScreen; },    [](int v) { cfg.homeScreen = v; } },
  { "reverseKnob",   0,     1, [] { return (int)cfg.scrollDir; },     [](int v) { cfg.scrollDir = v; } },
  { "clock24",       0,     1, [] { return (int)cfg.clock24; },       [](int v) { cfg.clock24 = v; } },
  { "utcOffset",   -48,    56, [] { return (int)cfg.utcOffset; },     [](int v) { cfg.utcOffset = v; } },
  { "autoClock",     0,     1, [] { return (int)cfg.autoClock; },     [](int v) { cfg.autoClock = v; } },
  { "sleepTimer",    0,   240, [] { return (int)cfg.sleepTimer; },    [](int v) { cfg.sleepTimer = v; sleepTimerRestart(); } },
  { "scanSpeed",     0,     2, [] { return (int)cfg.scanSpeed; },     [](int v) { cfg.scanSpeed = v; } },
  { "scanSens",      0,     3, [] { return (int)cfg.scanSens; },      [](int v) { cfg.scanSens = v; } },
  { "scanParts",     1,    63, [] { return (int)cfg.scanParts; },     [](int v) { cfg.scanParts = v & SCAN_ALL; } },
  { "scanBoot",      0,     2, [] { return (int)cfg.scanBoot; },      [](int v) { cfg.scanBoot = v; } },
  { "scanOffGrid",   0,     1, [] { return (int)cfg.scanOffGrid; },   [](int v) { cfg.scanOffGrid = v; } },
  { "standbyPause",  0,    60, [] { return (int)cfg.standbyPause; },  [](int v) { cfg.standbyPause = v; } },
  { "upperCase",     0,     1, [] { return (int)cfg.upperCase; },     [](int v) { cfg.upperCase = v; } },
  { "visStyle",      0,     5, [] { return (int)cfg.visStyle; },      [](int v) { cfg.visStyle = v; } },
  { "visIdle",       0,     3, [] { return (int)cfg.visIdle; },       [](int v) { cfg.visIdle = v; } },
  { "visEco",        0,     1, [] { return (int)cfg.visEco; },        [](int v) { cfg.visEco = v; } },
  { "bootAnim",      0,     1, [] { return (int)cfg.bootAnim; },      [](int v) { cfg.bootAnim = v; } },
  { "listFilter",    0, FILTER_COUNT - 1, [] { return (int)cfg.listFilter; }, [](int v) { cfg.listFilter = v; viewRebuild(); } },
};

static const CfgKey *findKey(const char *k)
{
  for(size_t i = 0; i < ITEM_COUNT(keys); i++) if(!strcmp(keys[i].key, k)) return &keys[i];
  return nullptr;
}

//
// Helpers
//
// Split a line on TAB in place; returns the number of fields
static uint8_t split(char *l, char **f, uint8_t max)
{
  uint8_t n = 0;
  f[n++] = l;
  for(char *p = l; *p && n < max; p++) if(*p == '\t') { *p = '\0'; f[n++] = p + 1; }
  return n;
}

// Printable ASCII only (names travel in TAB-separated lines)
static void clean(char *dst, size_t size, const char *src)
{
  size_t n = 0;
  for(; *src && n + 1 < size; src++)
    dst[n++] = (*src >= 32 && *src < 127) ? *src : ' ';
  dst[n] = '\0';
  while(n && dst[n - 1] == ' ') dst[--n] = '\0';
}

static void b64row(const uint8_t *in, size_t n)
{
  static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  static char out[1300];
  size_t o = 0;
  for(size_t i = 0; i < n; i += 3)
  {
    uint32_t v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = tbl[(v >> 18) & 63];
    out[o++] = tbl[(v >> 12) & 63];
    out[o++] = i + 1 < n ? tbl[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < n ? tbl[v & 63] : '=';
  }
  out[o] = '\0';
  Serial.print(out);
}

static void sendInfo()
{
  Serial.println("INFO");
  Serial.printf("version\t%s\n", NOVA_VERSION);
  Serial.printf("variant\t%s\n", NOVA_VARIANT);
  Serial.printf("tag\t%s\n", novaImageTag);
  Serial.printf("stations\t%u\n", stationCount);
  Serial.printf("stationMax\t%u\n", ST_MAX);
  Serial.printf("nameMax\t%u\n", ST_NAME_LEN - 1);
  Serial.printf("blocked\t%u\n", blockedCount());
  Serial.printf("schedules\t%lu\n", (unsigned long)scheduleCount());
  Serial.printf("battery\t%u\n", batteryPercent());
  Serial.printf("volts\t%.2f\n", batteryVolts());
  Serial.printf("clock\t%lu\n", clockValid() ? (unsigned long)clockUTC() : 0UL);
  Serial.printf("psram\t%u\n", (unsigned)(psramSize() / 1024));
  Serial.printf("channels\t%lu\n", (unsigned long)scanPlanChannels());
  for(uint8_t i = 0; i < themeCount(); i++) Serial.printf("theme\t%s\n", themeNameAt(i));
  Serial.println("END");
}

static void sendStatus()
{
  Signal s = radioSignal();
  char name[64] = "", text[72] = "";
  int16_t i = stationTuned();
  if(i >= 0) stationLabel(i, name, sizeof(name), true);
  else if(radioMode() == MODE_FM && rdsName()[0]) snprintf(name, sizeof(name), "%s", rdsName());
  if(radioMode() == MODE_FM) clean(text, sizeof(text), rdsText());
  const char *band = bandLabel(radioFreq(), radioMode());
  char n2[64];
  clean(n2, sizeof(n2), name);
  Serial.printf("STATUS\t%lu\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%s\t%s\t%s\n",
                (unsigned long)radioFreq(), radioMode(), s.rssi, s.snr, s.stereo ? 1 : 0, cfg.volume,
                batteryPercent(), screenIsOff() ? 0 : 1, n2, text, band ? band : "");
}

static void sendStations()
{
  char name[ST_NAME_LEN + 4], label[64], tmp[64];
  for(uint16_t i = 0; i < stationCount; i++)
  {
    const Station &s = stations[i];
    clean(name, sizeof(name), s.name);
    tmp[0] = '\0';
    if(s.group != GROUP_FM)
    {
      const char *n = scheduleLookup(s.freq);
      if(!n) n = knownStation(s.freq);
      if(n) snprintf(tmp, sizeof(tmp), "%s", n);
    }
    clean(label, sizeof(label), tmp);
    Serial.printf("ST\t%lu\t%u\t%u\t%u\t%u\t%s\t%s\n", (unsigned long)s.freq, s.mode, s.rssi, s.snr, s.flags, name, label);
  }
  Serial.printf("END\t%u\n", stationCount);
}

// Screen copy for the editor. Rows are run-length coded (count, pixel) and,
// in delta mode, only rows that changed since the last copy are sent: a
// still screen costs a few bytes instead of 108 kB (big USB bursts can be
// heard as clicks in the audio).
static uint32_t rowHash[SCREEN_H];

static void sendShot(bool delta)
{
  static uint8_t raw[SCREEN_W * 3];
  uint16_t sent = 0;
  Serial.printf("SHOT\t%d\t%d\n", SCREEN_W, SCREEN_H);
  for(int y = 0; y < SCREEN_H; y++)
  {
    uint32_t h = 2166136261u;
    size_t o = 0;
    uint16_t prev = 0, run = 0;
    for(int x = 0; x < SCREEN_W; x++)
    {
      uint16_t c = uiReadPixel(x, y);
      h = (h ^ c) * 16777619u;
      if(run && c == prev && run < 255) { run++; continue; }
      if(run) { raw[o++] = run; raw[o++] = prev >> 8; raw[o++] = prev & 0xFF; }
      prev = c; run = 1;
    }
    raw[o++] = run; raw[o++] = prev >> 8; raw[o++] = prev & 0xFF;
    if(delta && h == rowHash[y]) continue;
    rowHash[y] = h;
    Serial.printf("Q%d\t", y);
    b64row(raw, o);
    Serial.print('\n');
    sent++;
  }
  Serial.printf("END\t%u\n", sent);
}

//
// Ghost Box
//
static void ghostCommand(char **f, uint8_t n)
{
  const char *a = n > 1 ? f[1] : "";
  if(!strcmp(a, "CAPS"))
  {
    for(uint8_t i = 0; i < ghostBandCount(); i++)
      Serial.printf("GBB\t%u\t%s\t%lu\t%lu\t%u\n", i, ghostBands[i].name, (unsigned long)ghostBands[i].lo, (unsigned long)ghostBands[i].hi, ghostBands[i].mode);
    Serial.print("GBR");
    for(uint8_t i = 0; i < ghostRateCount(); i++) Serial.printf("\t%u", ghostRates[i]);
    Serial.println();
    Serial.println("END");
    return;
  }
  if(!strcmp(a, "STATUS"))
  {
    Signal s = radioSignal();
    GhostPoint last = { 0, 0, s.rssi, s.snr };
    if(ghostTraceTotal()) ghostTraceAt(ghostTraceTotal() - 1, last);
    Serial.printf("GB\t%u\t%u\t%u\t%lu\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%lu\t%lu\t%u\t%lu\t%u\t%u\t%lu",
                  ghostActive(), ghostPaused(), ghost.band, (unsigned long)radioFreq(), radioMode(), ghost.rate, ghost.dir, ghost.step,
                  ghost.flutter, ghost.gaps, ghost.gate, ghost.antenna, cfg.volume, (unsigned long)ghostElapsed(), (unsigned long)ghostSteps(),
                  ghostMarkCount(), (unsigned long)ghostTraceTotal(), last.rssi, last.snr, (unsigned long)ghostStartEpoch());
    Serial.printf("\t%u\t%u\t%u\t%u\t%lu\t%lu\t%lu\t%u\t%u\t%lu\t%lu", ghost.content, ghost.chaos, ghost.timer, ghost.blind,
                  (unsigned long)ghost.lo, (unsigned long)ghost.hi, (unsigned long)ghostRemaining(), ghostLearning(), ghostCarriers(),
                  (unsigned long)ghostBandLo(), (unsigned long)ghostBandHi());
    Serial.printf("\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%lu\t%lu\t%u\t%u\t%u\t%lu", ghost.react, ghost.onMark, ghost.window, ghost.noise,
                  ghost.cluster, ghost.trigger, ghost.trigSens, ghost.trigOpen, (unsigned long)ghostHoldLeft(), (unsigned long)ghostWindowLeft(),
                  ghostTrigState(), ghostTrigLevel(), ghostTrigBase(), (unsigned long)ghostTrigOpenLeft());
    Serial.printf("\t%u\t%u\t%u\t%u\t%u\n", ghost.catchT, ghost.catchSnr, ghost.holdSec, ghostHoldKind(), ghostCatches());
    return;
  }
  if(!strcmp(a, "START"))
  {
    if(standbyActive() || scanActive()) { Serial.println("ERR\tbusy"); return; }
    if(!ghostActive()) { ghostStart(); uiShow(SCR_GHOST); }
    screenWake(); uiRequestDraw();
    Serial.println("OK");
    return;
  }
  if(!strcmp(a, "STOP"))
  {
    if(ghostActive()) { ghostStop(); uiGoHome(); uiToast("Ghost Box closed"); }
    Serial.println("OK");
    return;
  }
  if(!strcmp(a, "PAUSE") || !strcmp(a, "RESUME"))
  {
    ghostPause(!strcmp(a, "PAUSE"));
    uiRequestDraw();
    Serial.println(ghostActive() ? "OK" : "ERR\tnot running");
    return;
  }
  if(!strcmp(a, "MARK"))
  {
    if(!ghostActive()) { Serial.println("ERR\tnot running"); return; }
    uint8_t kind = n > 2 && (f[2][0] == 'Q' || f[2][0] == 'q') ? 1 : 0;
    uint16_t m = ghostMark(kind);
    char buf[48];
    snprintf(buf, sizeof(buf), kind ? "Question %u from the computer" : "Mark %u from the computer", m);
    uiToast(buf, 1200);
    Serial.printf("OK\t%u\n", m);
    return;
  }
  if(!strcmp(a, "SET") && n >= 4)
  {
    const char *k = f[2];
    int v = atoi(f[3]);
    if(!strcmp(k, "band"))         { ghost.band = v; ghost.lo = ghost.hi = 0; }
    else if(!strcmp(k, "rate"))    ghost.rate = v;
    else if(!strcmp(k, "dir"))     ghost.dir = v;
    else if(!strcmp(k, "step"))    ghost.step = v;
    else if(!strcmp(k, "flutter")) ghost.flutter = v;
    else if(!strcmp(k, "gaps"))    ghost.gaps = v;
    else if(!strcmp(k, "gate"))    ghost.gate = v;
    else if(!strcmp(k, "antenna")) ghost.antenna = v;
    else if(!strcmp(k, "content")) ghost.content = v;
    else if(!strcmp(k, "chaos"))   ghost.chaos = v;
    else if(!strcmp(k, "timer"))   ghost.timer = v;
    else if(!strcmp(k, "blind"))   ghost.blind = v;
    else if(!strcmp(k, "lo"))      ghost.lo = strtoul(f[3], nullptr, 10);
    else if(!strcmp(k, "hi"))      ghost.hi = strtoul(f[3], nullptr, 10);
    else if(!strcmp(k, "react"))   ghost.react = v;
    else if(!strcmp(k, "onMark"))  ghost.onMark = v;
    else if(!strcmp(k, "window"))  ghost.window = v;
    else if(!strcmp(k, "noise"))   ghost.noise = v;
    else if(!strcmp(k, "cluster")) ghost.cluster = v;
    else if(!strcmp(k, "trigger")) ghost.trigger = v;
    else if(!strcmp(k, "trigSens")) ghost.trigSens = v;
    else if(!strcmp(k, "trigOpen")) ghost.trigOpen = v;
    else if(!strcmp(k, "catchT"))  ghost.catchT = v;
    else if(!strcmp(k, "catchSnr")) ghost.catchSnr = v;
    else if(!strcmp(k, "holdSec")) ghost.holdSec = v;
    else { Serial.println("ERR\tunknown key"); return; }
    ghostSettingsChanged();
    ghostApply();
    uiRequestDraw();
    Serial.printf("OK\t%s\t%s\n", k, f[3]);
    return;
  }
  if(!strcmp(a, "PROFILES"))
  {
    for(uint8_t i = 0; i < ghostProfileCount(); i++) Serial.printf("GBP\t%u\t%s\n", i, ghostProfiles[i].name);
    for(uint8_t i = 0; i < 3; i++) Serial.printf("GBU\t%u\t%u\n", i, ghostUserProfile(i, nullptr));
    Serial.println("END");
    return;
  }
  if(!strcmp(a, "PROFILE") && n >= 3)
  {
    // GB PROFILE <n>   GB PROFILE U<slot>   GB PROFILE SAVE <slot>
    if(!strcmp(f[2], "SAVE") && n >= 4) ghostSaveUserProfile(atoi(f[3]));
    else if(f[2][0] == 'U') { if(!ghostUserProfile(atoi(f[2] + 1), nullptr)) { Serial.println("ERR\tempty"); return; } ghostLoadUserProfile(atoi(f[2] + 1)); }
    else if((uint8_t)atoi(f[2]) < ghostProfileCount()) ghostApplyProfile(atoi(f[2]));
    else { Serial.println("ERR\tno such profile"); return; }
    uiRequestDraw();
    Serial.println("OK");
    return;
  }
  if(!strcmp(a, "HIST"))
  {
    // GB HIST: list   GB HIST <i>: settings and marks   GB HIST CLEAR
    if(n >= 3 && !strcmp(f[2], "CLEAR")) { ghostHistClear(); Serial.println("OK"); return; }
    uint32_t id, ep, dur, st; uint16_t mk; GhostCfg c;
    if(n >= 3)
    {
      uint8_t i = atoi(f[2]);
      if(!ghostHistInfo(i, id, ep, dur, st, mk, c)) { Serial.println("ERR\tno such session"); return; }
      Serial.printf("GBHC\t%lu\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%lu\t%lu\t%u\t%u\t%u\t%u\t%u\t%u\n",
                    (unsigned long)id, c.band, c.rate, c.dir, c.step, c.flutter, c.gaps, c.gate, c.antenna, c.content, c.chaos, c.timer, c.blind,
                    (unsigned long)c.lo, (unsigned long)c.hi, c.react, c.onMark, c.window, c.noise, c.cluster, c.trigger);
      static GhostPoint m[200]; static uint8_t k[200];
      uint16_t got = ghostHistMarks(i, m, k, 200);
      for(uint16_t j = 0; j < got; j++) Serial.printf("GBM\t%u\t%lu\t%lu\t%u\t%u\t%u\n", j + 1, (unsigned long)m[j].t, (unsigned long)m[j].hz, m[j].rssi, m[j].snr, k[j]);
      Serial.printf("END\t%u\n", got);
      return;
    }
    uint8_t cnt = ghostHistCount();
    for(uint8_t i = 0; i < cnt; i++)
      if(ghostHistInfo(i, id, ep, dur, st, mk, c))
        Serial.printf("GBH\t%u\t%lu\t%lu\t%lu\t%lu\t%u\t%u\n", i, (unsigned long)id, (unsigned long)ep, (unsigned long)dur, (unsigned long)st, mk, c.band);
    Serial.printf("END\t%u\n", cnt);
    return;
  }
  if(!strcmp(a, "MARKS"))
  {
    for(uint16_t i = 0; i < ghostMarkCount(); i++)
    {
      const GhostPoint *m = ghostMarkAt(i);
      Serial.printf("GBM\t%u\t%lu\t%lu\t%u\t%u\t%u\n", i + 1, (unsigned long)m->t, (unsigned long)m->hz, m->rssi, m->snr, ghostMarkKind(i));
    }
    Serial.printf("END\t%u\n", ghostMarkCount());
    return;
  }
  if(!strcmp(a, "TRACE"))
  {
    uint32_t from = n > 2 ? strtoul(f[2], nullptr, 10) : 0;
    if(from < ghostTraceOldest()) from = ghostTraceOldest();
    uint32_t i = from, sent = 0;
    GhostPoint p;
    while(i < ghostTraceTotal() && sent < 500 && ghostTraceAt(i, p))
    {
      Serial.printf("GBT\t%lu\t%lu\t%lu\t%u\t%u\n", (unsigned long)i, (unsigned long)p.t, (unsigned long)p.hz, p.rssi, p.snr);
      i++; sent++;
    }
    Serial.printf("END\t%lu\t%lu\t%lu\n", (unsigned long)i, (unsigned long)ghostTraceTotal(), (unsigned long)ghostTraceOldest());
    return;
  }
  Serial.println("ERR");
}

//
// Noise Hunter and Storm Detector
//
static void noiseCommand(char **f, uint8_t n)
{
  const char *a = n > 1 ? f[1] : "";
  if(!strcmp(a, "CAPS"))
  {
    for(uint8_t i = 0; i < noiseBandCount(); i++)
      Serial.printf("NHB\t%u\t%s\t%s\t%lu\t%lu\t%lu\n", i, noiseBands[i].name, noiseBands[i].shortName,
                    (unsigned long)noiseBands[i].lo, (unsigned long)noiseBands[i].hi, (unsigned long)noiseBands[i].step);
    Serial.println("END");
    return;
  }
  if(!strcmp(a, "STATUS"))
  {
    Serial.printf("NH\t%u\t%u\t%lu\t%u\t%u\t%u\t%d\t%u\t%u\t%u\t%u\t%lu\n", noiseActive(), noiseCfg.band, (unsigned long)radioFreq(),
                  noiseLevel(), noisePeak(), noiseMax(), noiseBase(), noiseSnr(), cfg.volume, noiseSpotCount(), noiseCfg.att, (unsigned long)noiseElapsed());
    return;
  }
  if(!strcmp(a, "SPOTS"))
  {
    for(uint8_t i = 0; i < noiseSpotCount(); i++)
    {
      const NoiseSpot *s = noiseSpotAt(i);
      Serial.printf("NHS\t%u\t%lu\t%lu\t%u\t%u\t%d\n", i + 1, (unsigned long)s->t, (unsigned long)s->hz, s->level, s->peak, s->base);
    }
    Serial.printf("END\t%u\n", noiseSpotCount());
    return;
  }
  if(!strcmp(a, "START"))
  {
    if(standbyActive() || scanActive()) { Serial.println("ERR\tbusy"); return; }
    screenWake();
    if(!noiseActive()) { noiseStart(); uiShow(SCR_NOISE); }
    Serial.println("OK");
    return;
  }
  if(!strcmp(a, "STOP")) { if(noiseActive()) { noiseStop(); uiGoHome(); } Serial.println("OK"); return; }
  if(!noiseActive()) { Serial.println("ERR\tnot running"); return; }
  if(!strcmp(a, "ZERO")) { noiseZero(); uiToast("Zero set from the computer", 1200); Serial.println("OK"); return; }
  if(!strcmp(a, "MAX")) { noiseResetMax(); Serial.println("OK"); return; }
  if(!strcmp(a, "QUIET")) { noiseFindQuiet(); Serial.printf("OK\t%lu\n", (unsigned long)radioFreq()); return; }
  if(!strcmp(a, "SPOT")) { Serial.printf("OK\t%u\n", noiseSpot()); return; }
  if(!strcmp(a, "SET") && n >= 4)
  {
    long v = atol(f[3]);
    if(!strcmp(f[2], "band")) noiseSetBand(v);
    else if(!strcmp(f[2], "hz")) noiseSetHz(v);
    else if(!strcmp(f[2], "att")) noiseSetAtt(v);
    else { Serial.println("ERR\tunknown key"); return; }
    uiRequestDraw();
    Serial.printf("OK\t%s\t%ld\n", f[2], v);
    return;
  }
  Serial.println("ERR");
}

static void stormCommand(char **f, uint8_t n)
{
  const char *a = n > 1 ? f[1] : "";
  if(!strcmp(a, "STATUS"))
  {
    Serial.printf("SD\t%u\t%lu\t%u\t%u\t%u\t%u\t%lu\t%lu\t%lu\t%ld\t%u\t%u\t%u\t%u\t%lu\t%lu\t%lu\t%d\n", stormActive(), (unsigned long)radioFreq(),
                  stormCfg.thresh, stormBase(), stormLevel(), stormAlert(), (unsigned long)stormCount(1), (unsigned long)stormCount(10),
                  (unsigned long)stormCount(60), (long)stormLastAgo(), stormLastPeak(), stormListening(), cfg.volume, stormCfg.flash,
                  (unsigned long)stormElapsed(), (unsigned long)stormEventTotal(), (unsigned long)stormStartEpoch(), stormTrend());
    return;
  }
  if(!strcmp(a, "MIN"))
  {
    static StormMinute m[STORM_MINUTES];
    uint16_t k = stormMinutes(m, STORM_MINUTES);
    for(uint16_t i = 0; i < k; i++)
      Serial.printf("SDM\t%lu\t%u\t%u\t%u\n", (unsigned long)(m[i].minute - 1), m[i].count, m[i].maxPeak, m[i].count ? (unsigned)(m[i].sumPeak / m[i].count) : 0);
    Serial.printf("END\t%u\n", k);
    return;
  }
  if(!strcmp(a, "EVENTS"))
  {
    uint32_t from = n > 2 ? strtoul(f[2], nullptr, 10) : 0;
    if(from < stormEventOldest()) from = stormEventOldest();
    uint32_t i = from, sent = 0;
    StormEvent e;
    while(i < stormEventTotal() && sent < 500 && stormEventAt(i, e)) { Serial.printf("SDE\t%lu\t%lu\t%u\t%u\n", (unsigned long)i, (unsigned long)e.t, e.peak, e.dur); i++; sent++; }
    Serial.printf("END\t%lu\t%lu\n", (unsigned long)i, (unsigned long)stormEventTotal());
    return;
  }
  if(!strcmp(a, "START"))
  {
    if(standbyActive() || scanActive()) { Serial.println("ERR\tbusy"); return; }
    screenWake();
    if(!stormActive()) { stormStart(); uiShow(SCR_STORM); }
    Serial.println("OK");
    return;
  }
  if(!strcmp(a, "STOP")) { if(stormActive()) { stormStop(); uiGoHome(); } Serial.println("OK"); return; }
  if(!stormActive()) { Serial.println("ERR\tnot running"); return; }
  if(!strcmp(a, "RESET")) { stormReset(); Serial.println("OK"); return; }
  if(!strcmp(a, "QUIET")) { stormFindQuiet(); Serial.printf("OK\t%lu\n", (unsigned long)radioFreq()); return; }
  if(!strcmp(a, "LISTEN") || !strcmp(a, "MUTE")) { stormListen(!strcmp(a, "LISTEN")); uiRequestDraw(); Serial.println("OK"); return; }
  if(!strcmp(a, "SET") && n >= 4)
  {
    long v = atol(f[3]);
    if(!strcmp(f[2], "thresh")) { stormCfg.thresh = v < 4 ? 4 : v > 30 ? 30 : v; stormSettingsChanged(); }
    else if(!strcmp(f[2], "flash")) { stormCfg.flash = !!v; stormSettingsChanged(); }
    else if(!strcmp(f[2], "hz")) stormSetHz(v);
    else { Serial.println("ERR\tunknown key"); return; }
    uiRequestDraw();
    Serial.printf("OK\t%s\t%ld\n", f[2], v);
    return;
  }
  Serial.println("ERR");
}

//
// Command handler
//
static void handle(char *l)
{
  if(importing)
  {
    if(!strncmp(l, "EIBI END", 8))
    {
      uint32_t count = 0;
      uiMessage("Saving station names", "Sorting and writing...");
      bool ok = scheduleImportEnd(count);
      importing = false;
      Serial.printf(ok ? "DONE %lu\n" : "FAIL\n", (unsigned long)count);
      char buf[48];
      snprintf(buf, sizeof(buf), ok ? "%lu schedules loaded" : "Import failed", (unsigned long)count);
      uiRequestDraw();
      uiToast(buf, 3000);
      return;
    }
    received++;
    if(scheduleImportLine(l)) parsed++;
    if(!(received % 100)) Serial.printf("ACK %lu\n", (unsigned long)received);
    if(millis() - lastDraw > 250)
    {
      lastDraw = millis();
      char buf[48];
      snprintf(buf, sizeof(buf), "%lu lines, %lu stations", (unsigned long)received, (unsigned long)parsed);
      screenWake();
      uiMessage("Receiving station names", buf);
    }
    return;
  }

  char *f[10];
  // ST+ lines may carry a name: split only what is needed
  if(!strncmp(l, "ST+\t", 4))
  {
    uint8_t n = split(l, f, 7);
    if(!pendingList || n < 6) { Serial.println("ERR"); return; }
    if(pendingN >= ST_MAX) return;
    Station &s = pendingList[pendingN];
    memset(&s, 0, sizeof(s));
    s.freq = strtoul(f[1], nullptr, 10);
    s.mode = atoi(f[2]);
    s.rssi = atoi(f[3]);
    s.snr = atoi(f[4]);
    s.flags = atoi(f[5]) & (ST_FAV | ST_MANUAL | ST_NEW | ST_CUSTOM);
    if(n > 6) clean(s.name, ST_NAME_LEN, f[6]);
    if(!s.name[0]) s.flags &= ~ST_CUSTOM;
    if(s.mode < MODE_COUNT && s.freq) pendingN++;
    return;
  }
  if(!strncmp(l, "BL+\t", 4))
  {
    uint32_t hz = strtoul(l + 4, nullptr, 10);
    if(hz && pendingBlockN < BLOCK_MAX) pendingBlock[pendingBlockN++] = hz;
    return;
  }

  // Commands carry no free text: spaces work as well as TABs ("CFG SET volume 30")
  for(char *p = l; *p; p++) if(*p == ' ') *p = '\t';
  uint8_t n = 0;
  {
    char *save = nullptr;
    for(char *t = strtok_r(l, "\t", &save); t && n < 10; t = strtok_r(nullptr, "\t", &save)) f[n++] = t;
  }
  if(!n) return;
  const char *c = f[0];
  const char *a = n > 1 ? f[1] : "";

  if(!strcmp(c, "NOVA?")) { Serial.printf("NOVA %s\n", NOVA_VERSION); return; }
  if(!strcmp(c, "INFO")) { sendInfo(); return; }
  if(!strcmp(c, "STATUS")) { sendStatus(); return; }
  if(!strcmp(c, "SHOT")) { sendShot(!strcmp(a, "D")); return; }
  if(!strcmp(c, "VIS"))
  {
    if(!strcmp(a, "OFF")) { if(uiScreen() == SCR_VISUAL) uiGoHome(); }
    else if(!appRunning() && !standbyActive() && !scanActive()) { screenWake(); if(uiScreen() != SCR_VISUAL) visualOpen(uiScreen()); }
    else { Serial.println("ERR\tbusy"); return; }
    Serial.println("OK");
    return;
  }

  if(!strcmp(c, "WIFI"))
  {
    // WIFI GET | SETHEX <ssid hex> <password hex> | FORGET | TEST | EIBI
    if(!strcmp(a, "GET")) { Serial.printf("WIFI\t%u\t%s\n", wifiConfigured(), wifiSsid()); return; }
    if(!strcmp(a, "FORGET")) { wifiForget(); Serial.println("OK"); return; }
    if(!strcmp(a, "SETHEX") && n >= 3)
    {
      auto unhex = [](const char *h, char *out, size_t max) {
        size_t o = 0;
        for(; h[0] && h[1] && o + 1 < max; h += 2) { char t[3] = { h[0], h[1], 0 }; out[o++] = (char)strtoul(t, nullptr, 16); }
        out[o] = '\0';
      };
      char ssid[33], pass[65];
      unhex(f[2], ssid, sizeof(ssid));
      unhex(n >= 4 ? f[3] : "", pass, sizeof(pass));
      wifiSetCreds(ssid, pass);
      Serial.println("OK");
      return;
    }
    if(!strcmp(a, "TEST") || !strcmp(a, "EIBI"))
    {
      if(scanActive() || standbyActive()) { Serial.println("FAIL\tThe radio is busy (search or Standby)"); return; }
      char msg[96];
      bool eibi = !strcmp(a, "EIBI");
      bool ok = wifiRun(eibi ? WIFI_JOB_EIBI : WIFI_JOB_TEST, [](const char *stage, uint32_t n) { Serial.printf("WPROG\t%s\t%lu\n", stage, (unsigned long)n); }, msg, sizeof(msg));
      if(ok) Serial.printf(eibi ? "DONE %s\n" : "WIFI OK\t%s\n", msg);
      else Serial.printf("FAIL\t%s\n", msg);
      uiGoHome();
      uiToast(ok ? (eibi ? "Station names updated" : "Wi-Fi works") : msg, 3000);
      return;
    }
    Serial.println("ERR");
    return;
  }
  if(!strcmp(c, "TIME"))
  {
    uint32_t e = strtoul(a, nullptr, 10);
    if(e > 1700000000UL) { clockSet(e); Serial.println("OK"); uiRequestDraw(); }
    else Serial.println("ERR");
    return;
  }

  if(!strcmp(c, "EIBI") && !strcmp(a, "BEGIN"))
  {
    if(scheduleImportBegin())
    {
      importing = true;
      received = parsed = 0;
      Serial.println("READY");
    }
    else Serial.println("FAIL");
    return;
  }

  if(!strcmp(c, "CFG"))
  {
    if(!strcmp(a, "GET"))
    {
      for(size_t i = 0; i < ITEM_COUNT(keys); i++) Serial.printf("CFG\t%s\t%d\n", keys[i].key, keys[i].get());
      Serial.println("END");
      return;
    }
    if(!strcmp(a, "SET") && n >= 4)
    {
      const CfgKey *k = findKey(f[2]);
      if(!k) { Serial.println("ERR\tunknown key"); return; }
      int v = atoi(f[3]);
      if(v < k->min) v = k->min;
      if(v > k->max) v = k->max;
      k->set(v);
      configSave();
      uiRequestDraw();
      Serial.printf("OK\t%s\t%d\n", k->key, k->get());
      return;
    }
    if(!strcmp(a, "DEFAULTS"))
    {
      uint32_t hz = radioFreq();
      uint8_t m = radioMode();
      configDefaults();
      cfg.freq = hz; cfg.mode = m;
      configSave(true);
      backlight(cfg.brightness);
      radioApply();
      viewRebuild();
      uiRequestDraw();
      Serial.println("OK");
      return;
    }
    Serial.println("ERR");
    return;
  }

  if(!strcmp(c, "ST"))
  {
    if(!strcmp(a, "LIST")) { sendStations(); return; }
    if(!strcmp(a, "BEGIN"))
    {
      if(!pendingList) pendingList = static_cast<Station *>(ps_malloc(ST_MAX * sizeof(Station)));
      pendingN = 0;
      Serial.println(pendingList ? "READY" : "FAIL");
      return;
    }
    if(!strcmp(a, "COMMIT"))
    {
      if(!pendingList) { Serial.println("ERR"); return; }
      stationsReplaceAll(pendingList, pendingN);
      Serial.printf("DONE\t%u\n", stationCount);
      char buf[48];
      snprintf(buf, sizeof(buf), "Station list updated  -  %u", stationCount);
      screenWake();
      uiToast(buf, 2500);
      return;
    }
    Serial.println("ERR");
    return;
  }

  if(!strcmp(c, "BL"))
  {
    if(!strcmp(a, "LIST"))
    {
      for(uint8_t i = 0; i < blockedCount(); i++) Serial.printf("BL\t%lu\n", (unsigned long)blockedAt(i));
      Serial.printf("END\t%u\n", blockedCount());
      return;
    }
    if(!strcmp(a, "BEGIN")) { pendingBlockN = 0; Serial.println("READY"); return; }
    if(!strcmp(a, "COMMIT"))
    {
      blockedReplace(pendingBlock, pendingBlockN);
      Serial.printf("DONE\t%u\n", blockedCount());
      return;
    }
    Serial.println("ERR");
    return;
  }

  if(!strcmp(c, "GB")) { ghostCommand(f, n); return; }

  if(!strcmp(c, "NH")) { noiseCommand(f, n); return; }
  if(!strcmp(c, "SD")) { stormCommand(f, n); return; }

  if(appRunning() && (!strcmp(c, "TUNE") || !strcmp(c, "SCAN"))) { Serial.println("ERR\tapp running"); return; }

  if(!strcmp(c, "TUNE") && n >= 3)
  {
    uint32_t hz = strtoul(f[1], nullptr, 10);
    uint8_t m = atoi(f[2]);
    if(m >= MODE_COUNT || hz < radioMinFreq(m) || hz > radioMaxFreq(m)) { Serial.println("ERR\tout of range"); return; }
    if(standbyActive()) { Serial.println("ERR\tstandby"); return; }
    radioTune(hz, m);
    screenWake();
    // Show it where it makes sense: the station card if it is in the list, else free tuning
    int16_t t = stationTuned();
    bool listed = false;
    for(uint16_t k = 0; t >= 0 && k < viewCount; k++) if(view[k] == (uint16_t)t) { listed = true; break; }
    uint8_t scr = uiScreen();
    if(scr == SCR_STATIONS || scr == SCR_TUNE) uiShow(listed ? SCR_STATIONS : SCR_TUNE);
    uiRequestDraw();
    Serial.println("OK");
    return;
  }

  if(!strcmp(c, "SCAN") && !strcmp(a, "STOP")) { Serial.println("OK"); return; }
  if(!strcmp(c, "SCAN") && !strcmp(a, "EST"))
  {
    uint8_t parts = n > 2 ? atoi(f[2]) : cfg.scanParts;
    uint8_t speed = n > 3 ? atoi(f[3]) : cfg.scanSpeed;
    uint32_t ch = 0, sec = scanEstimate(parts ? parts : cfg.scanParts, speed, &ch);
    Serial.printf("EST\t%lu\t%lu\n", (unsigned long)sec, (unsigned long)ch);
    return;
  }
  if(!strcmp(c, "SCAN"))
  {
    if(standbyActive() || scanActive()) { Serial.println("ERR\tbusy"); return; }
    if(n > 1) scanOverride(atoi(f[1]), n > 2 ? atoi(f[2]) : -1);   // SCAN <parts> <speed>: this search only
    scanRequested = true;           // started from usbTick, after the answer is out
    Serial.println("OK");
    return;
  }

  if(c[0]) Serial.println("ERR\tunknown command");
}

// A search keeps the main loop busy for minutes: answer just enough so the
// editor knows what is going on, and let it stop the search
bool usbScanPoll()
{
  if(importing) return false;
  bool stop = false;
  uint16_t budget = 512;
  while(Serial.available() && budget--)
  {
    char c = Serial.read();
    if(c != '\n') { if(len < sizeof(line) - 1) line[len++] = c; continue; }
    line[len] = '\0';
    if(len && line[len - 1] == '\r') line[--len] = '\0';
    for(char *p = line; *p; p++) if(*p == '\t') *p = ' ';
    if(!strcmp(line, "NOVA?"))
      Serial.printf("NOVA %s SCAN %lu\n", NOVA_VERSION,
                    (unsigned long)(scanState.total ? scanState.done * 100 / scanState.total : 0));
    else if(!strcmp(line, "SCAN STOP")) { stop = true; Serial.println("OK"); }
    else if(len) Serial.println("BUSY");
    len = 0;
  }
  return stop;
}

bool usbConnected()
{
#ifndef NOVA_SIM
  return (bool)Serial || batteryVolts() > 4.3f;
#else
  return false;
#endif
}

bool usbTick()
{
  bool any = false;
  uint16_t budget = 4096;
  while(Serial.available() && budget--)
  {
    char c = Serial.read();
    any = true;
    if(c == '\n')
    {
      line[len] = '\0';
      if(len && line[len - 1] == '\r') line[len - 1] = '\0';
      handle(line);
      len = 0;
    }
    else if(len < sizeof(line) - 1) line[len++] = c;
  }
  if(scanRequested)
  {
    scanRequested = false;
    screenWake();
    uiStartScan();
    Serial.printf("SCANDONE\t%u\n", stationCount);
  }
  return any;
}
