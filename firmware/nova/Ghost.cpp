//
// Ghost.cpp - Ghost Box: a fast audible radio sweep for ITC / EVP sessions,
// in the manner of the dedicated "spirit box" receivers (P-SB7, SB11...).
//
// Each frequency is held for the sweep rate (30-500 ms), forward, reverse,
// bounce or random, on FM, AM, LW or a shortwave band. Options: half-step
// "flutter", mute the empty frequencies (gate on SNR), antenna attenuation.
//
// For serious work every step is logged with its time, frequency and signal
// (trace), and the operator can drop marks. With the trace, any word heard on
// a recording can be matched to the frequency that was playing at that time.
//
#include "Nova.h"
#ifndef NOVA_SIM
#include <LittleFS.h>
#endif

#define GHOST_VERSION 4
#define MARK_MAX      200
#define HIST_N        128

const GhostBand ghostBands[] =
{
  { "FM 88-108 MHz",       88000000, 108000000, MODE_FM, 100000 },
  { "FM 76-108 MHz",       76000000, 108000000, MODE_FM, 100000 },
  { "FM 64-108 MHz",       64000000, 108000000, MODE_FM, 100000 },
  { "AM 530-1700 kHz",       530000,   1700000, MODE_AM,  10000 },
  { "LW 153-279 kHz",        153000,    279000, MODE_AM,   9000 },
  { "SW 49m",               5900000,   6200000, MODE_AM,   5000 },
  { "SW 41m",               7200000,   7600000, MODE_AM,   5000 },
  { "SW 31m",               9400000,   9900000, MODE_AM,   5000 },
  { "SW 25m",              11600000,  12100000, MODE_AM,   5000 },
  { "SW 19m",              15100000,  15800000, MODE_AM,   5000 },
  { "SW 2.3-26 MHz",        2300000,  26100000, MODE_AM,   5000 },
};
uint8_t ghostBandCount() { return ITEM_COUNT(ghostBands); }

const uint16_t ghostRates[] = { 30, 40, 50, 60, 70, 80, 90, 100, 125, 150, 175, 200, 250, 300, 350, 400, 500 };
uint8_t ghostRateCount() { return ITEM_COUNT(ghostRates); }

const char *ghostDirName(uint8_t d)
{
  static const char *const n[] = { "Forward", "Reverse", "Bounce", "Random", "Shuffle", "Dual" };
  return d < GHOST_DIRS ? n[d] : n[0];
}

// Ready-made settings, after the way the well-known spirit boxes are used
const GhostProfile ghostProfiles[] =
{
  //  name               band rate dir step flut gaps gate ant content     chaos noise clus blind timer
  { "Classic FM",          0, 200, 0,  0,   1,   0,   4,   0, GC_MIXED,     0,   1,    3,   0,    0 },
  { "Fast FM",             0,  50, 0,  0,   1,   0,   4,   0, GC_MIXED,     0,   1,    3,   0,    0 },
  { "Reverse FM",          1, 150, 1,  2,   0,   0,   4,   0, GC_MIXED,     0,   1,    3,   0,    0 },
  { "AM sweep",            3, 150, 0,  0,   0,   0,   4,   0, GC_MIXED,     0,   1,    3,   0,    0 },
  { "Estes session",       1, 150, 1,  0,   1,   0,   4,   0, GC_MIXED,     0,   1,    3,   1,   15 },
  { "Hot-spot clusters",   0,  70, 0,  2,   0,   0,   4,   0, GC_CLUSTER,   0,   1,    3,   0,    0 },
  { "Noise only",          1, 100, 0,  2,   0,   0,   4,   0, GC_NOISE,     0,   0,    3,   0,    0 },
  { "Slow AM, irregular",  3, 300, 3,  0,   0,   0,   4,   0, GC_MIXED,     1,   0,    3,   0,    0 },
  { "Shortwave chaos",    10, 100, 4,  0,   0,   0,   4,   0, GC_MIXED,     2,   0,    3,   0,    0 },
};
uint8_t ghostProfileCount() { return ITEM_COUNT(ghostProfiles); }

GhostCfg ghost;

static bool active = false, paused = false, first = true, gated = false;
static bool saveDirty = false;
static uint32_t dirtyAt = 0;
static uint32_t cur = 0, startMs = 0, startEpoch = 0, nextAt = 0, steps = 0;
static int8_t sign = 1;
static uint8_t phase = 0;
static uint8_t lastR = 0, lastS = 0;
static uint32_t oldHz = 0;
static uint8_t oldMode = MODE_FM;

static GhostPoint *trace = nullptr;
static uint32_t traceCap = 0, traceTotal = 0;
static GhostPoint marks[MARK_MAX];
static uint8_t markKind[MARK_MAX];
static uint16_t markN = 0;
static bool timerDone = false, timerFlag = false;
static uint32_t holdUntil = 0, windowUntil = 0;
static uint8_t holdKind = 0;
static uint16_t catchN = 0;
static uint32_t caughtHz[8], caughtAt[8];     // not twice in a row on the same station
static uint8_t caughtPos = 0;
static bool windowFlag = false;
static uint8_t trigState = 0;          // 0 off, 1 armed, 2 open
static uint32_t trigUntil = 0, trigArmedAt = 0, trigPollAt = 0, watchHz = 0;
static float trigBase = 0;
static uint8_t trigLast = 0, quietR = 255;
static uint32_t quietHz = 0;
static uint32_t shufK = 0, shufA = 1, shufC = 0, shufN = 0;
static uint32_t dualA = 0, dualB = 0;
static bool dualSide = false;
static void trigArm();
static void historySave();

// Carrier map of the band (on the step grid): stations from the list and
// every frequency heard above the gate during the session
static uint8_t cmap[2048];
static uint16_t carriers = 0;
static uint32_t mapLo = 0, mapStep = 1;
static uint8_t mapBand = 255, mapStepSel = 255;
static uint8_t histR[HIST_N], histS[HIST_N];
static uint8_t histPos = 0, histN = 0;

static void defaults()
{
  memset(&ghost, 0, sizeof(ghost));
  ghost.version = GHOST_VERSION;
  ghost.band = 0;
  ghost.rate = 200;          // the usual starting point on FM
  ghost.dir = 0;
  ghost.step = 0;
  ghost.flutter = 0;
  ghost.gaps = 0;
  ghost.gate = 4;
  ghost.antenna = 0;
  ghost.content = GC_MIXED;
  ghost.chaos = 0;
  ghost.timer = 0;
  ghost.blind = 0;
  ghost.lo = ghost.hi = 0;
  ghost.react = 4;
  ghost.onMark = 0;
  ghost.window = 0;
  ghost.noise = 1;
  ghost.cluster = 3;
  ghost.trigger = 0;
  ghost.trigSens = 6;
  ghost.trigOpen = 10;
  ghost.catchT = 0;
  ghost.catchSnr = 12;
  ghost.holdSec = 3;
}

static void sanitize()
{
  if(ghost.band >= ghostBandCount()) ghost.band = 0;
  if(ghost.rate < 30) ghost.rate = 30;
  if(ghost.rate > 500) ghost.rate = 500;
  if(ghost.dir >= GHOST_DIRS) ghost.dir = 0;
  if(ghost.step > 2) ghost.step = 0;
  ghost.flutter = !!ghost.flutter;
  ghost.gaps = !!ghost.gaps;
  if(ghost.gate < 1) ghost.gate = 1;
  if(ghost.gate > 30) ghost.gate = 30;
  if(ghost.antenna > 3) ghost.antenna = 0;
  if(ghost.content >= GC_COUNT) ghost.content = GC_MIXED;
  if(ghost.react > 10) ghost.react = 4;
  if(ghost.onMark > 2) ghost.onMark = 0;
  if(ghost.window > 60) ghost.window = 0;
  if(ghost.noise > 2) ghost.noise = 1;
  if(ghost.cluster < 1 || ghost.cluster > 10) ghost.cluster = 3;
  ghost.trigger = !!ghost.trigger;
  if(ghost.trigSens < 2 || ghost.trigSens > 30) ghost.trigSens = 6;
  if(ghost.trigOpen < 3 || ghost.trigOpen > 120) ghost.trigOpen = 10;
  if(ghost.catchT > 100) ghost.catchT = 0;
  if(ghost.catchSnr < 2 || ghost.catchSnr > 40) ghost.catchSnr = 12;
  if(ghost.holdSec < 1 || ghost.holdSec > 30) ghost.holdSec = 3;
  if(ghost.chaos > 2) ghost.chaos = 0;
  if(ghost.timer > 120) ghost.timer = 0;
  ghost.blind = !!ghost.blind;
}

void ghostInit()
{
  size_t got = 0;
  GhostCfg saved;
  memset(&saved, 0, sizeof(saved));
  bool ok = storeLoad("ghost", &saved, sizeof(saved), &got);
  defaults();
  if(ok && got == sizeof(saved) && saved.version == GHOST_VERSION) ghost = saved;
  else if(ok && saved.version >= 1 && saved.version < GHOST_VERSION && got <= sizeof(saved))
  {
    // Settings of an older version: keep them, the new ones start at their defaults
    GhostCfg d = ghost;
    memcpy(&ghost, &saved, got);
    if(saved.version < 2) { ghost.content = d.content; ghost.chaos = d.chaos; ghost.timer = d.timer; ghost.blind = d.blind; ghost.lo = d.lo; ghost.hi = d.hi; }
    if(saved.version < 3) { ghost.react = d.react; ghost.onMark = d.onMark; ghost.window = d.window; ghost.noise = d.noise; ghost.cluster = d.cluster; }
    if(saved.version < 3) { ghost.trigger = d.trigger; ghost.trigSens = d.trigSens; ghost.trigOpen = d.trigOpen; }
    ghost.catchT = d.catchT; ghost.catchSnr = d.catchSnr; ghost.holdSec = d.holdSec;
    ghost.version = GHOST_VERSION;
  }
  sanitize();
}

static int8_t noiseValue();
void ghostSettingsChanged()
{
  sanitize();
  saveDirty = true;
  dirtyAt = millis();
  if(active)
  {
    radioSoftMuteOverride(noiseValue());
    if(ghost.trigger && !trigState) trigArm();
    else if(!ghost.trigger && trigState) { trigState = 0; if(gated) { radioGate(false); gated = false; } nextAt = millis(); first = true; }
  }
}

void ghostApplyProfile(uint8_t i)
{
  if(i >= ghostProfileCount()) return;
  const GhostProfile &p = ghostProfiles[i];
  ghost.band = p.band; ghost.rate = p.rate; ghost.dir = p.dir; ghost.step = p.step; ghost.flutter = p.flutter; ghost.gaps = p.gaps;
  ghost.gate = p.gate; ghost.antenna = p.antenna; ghost.content = p.content; ghost.chaos = p.chaos; ghost.noise = p.noise;
  ghost.cluster = p.cluster; ghost.blind = p.blind; ghost.timer = p.timer; ghost.lo = ghost.hi = 0;
  ghostSettingsChanged();
  ghostApply();
}

static const char *const userKeys[] = { "gbp1", "gbp2", "gbp3" };
bool ghostUserProfile(uint8_t slot, GhostCfg *out)
{
  if(slot > 2) return false;
  GhostCfg c; size_t got = 0;
  if(!storeLoad(userKeys[slot], &c, sizeof(c), &got) || got != sizeof(c) || c.version != GHOST_VERSION) return false;
  if(out) *out = c;
  return true;
}
void ghostSaveUserProfile(uint8_t slot) { if(slot <= 2) storeSave(userKeys[slot], &ghost, sizeof(ghost)); }
void ghostLoadUserProfile(uint8_t slot)
{
  GhostCfg c;
  if(!ghostUserProfile(slot, &c)) return;
  ghost = c;
  ghostSettingsChanged();
  ghostApply();
}

//
// Band geometry
//
static const GhostBand &band() { return ghostBands[ghost.band]; }

static uint32_t baseLo()
{
  // Medium wave follows the region's 9 kHz grid outside the Americas
  if(band().mode == MODE_AM && band().lo == 530000 && cfg.region) return 531000;
  return band().lo;
}
uint32_t ghostStepHz();
static uint32_t bandLo()
{
  uint32_t lo = baseLo(), st = ghostStepHz();
  if(ghost.lo > lo && ghost.lo < band().hi) lo += ((ghost.lo - lo + st - 1) / st) * st;
  return lo;
}

uint32_t ghostStepHz()
{
  uint32_t s = band().step;
  if(band().mode == MODE_AM && band().lo == 530000 && cfg.region) s = 9000;
  if(ghost.step == 1) s *= 2;
  if(ghost.step == 2) s = band().mode == MODE_FM ? s / 2 : ((s / 2 + 500) / 1000) * 1000;
  if(band().mode == MODE_FM && s < 10000) s = 10000;
  if(band().mode != MODE_FM && s < 1000) s = 1000;
  return s;
}

static uint32_t bandHi()
{
  uint32_t lo = bandLo(), st = ghostStepHz();
  uint32_t hi = band().hi;
  if(ghost.hi && ghost.hi > lo && ghost.hi < hi) hi = ghost.hi;
  return lo + ((hi - lo) / st) * st;
}
uint32_t ghostBandLo() { return bandLo(); }
uint32_t ghostBandHi() { return bandHi(); }

//
// Carrier map
//
static int32_t mapIndex(uint32_t f)
{
  if(f < mapLo) return -1;
  uint32_t i = (f - mapLo + mapStep / 2) / mapStep;
  return i < sizeof(cmap) * 8 ? (int32_t)i : -1;
}
static bool isCarrier(uint32_t f) { int32_t i = mapIndex(f); return i >= 0 && (cmap[i >> 3] & (1 << (i & 7))); }
static void setCarrier(uint32_t f)
{
  int32_t i = mapIndex(f);
  if(i < 0 || (cmap[i >> 3] & (1 << (i & 7)))) return;
  cmap[i >> 3] |= 1 << (i & 7);
  carriers++;
}
static void mapReset()
{
  memset(cmap, 0, sizeof(cmap));
  carriers = 0;
  mapLo = baseLo(); mapStep = ghostStepHz(); mapBand = ghost.band; mapStepSel = ghost.step;
  // Stations already in the list are carriers from the start
  uint8_t g = groupOf(band().lo, band().mode);
  for(uint16_t k = 0; k < stationCount; k++)
    if(stations[k].group == g && stations[k].freq >= band().lo && stations[k].freq <= band().hi) setCarrier(stations[k].freq);
}
uint16_t ghostCarriers() { return carriers; }
bool ghostLearning() { return ghost.content == GC_STATIONS && !carriers; }

// Marked frequencies of this session, sorted, for the hot spots mode
static uint32_t hot[64];
static uint8_t hotN = 0;
static void hotBuild()
{
  hotN = 0;
  for(uint16_t k = 0; k < markN && hotN < ITEM_COUNT(hot); k++)
  {
    uint32_t f = marks[k].hz;
    if(f < baseLo() || f > band().hi) continue;
    bool have = false;
    for(uint8_t j = 0; j < hotN; j++) if(hot[j] == f) have = true;
    if(!have) hot[hotN++] = f;
  }
  for(uint8_t a = 1; a < hotN; a++) { uint32_t v = hot[a]; int b = a - 1; while(b >= 0 && hot[b] > v) { hot[b + 1] = hot[b]; b--; } hot[b + 1] = v; }
}

static uint32_t flutterHz()
{
  if(band().mode == MODE_FM) return 50000;
  uint32_t h = ((ghostStepHz() / 2) / 1000) * 1000;
  return h ? h : 1000;
}

static int8_t antennaValue()
{
  // Same scale as cfg.agc: 0 = automatic, n = attenuation n-1
  static const int8_t fm[] = { 0, 10, 19, 27 }, am[] = { 0, 13, 25, 37 };
  return band().mode == MODE_FM ? fm[ghost.antenna] : am[ghost.antenna];
}

// Soft mute depth of the chip: raw static, the radio's normal, or quiet
static int8_t noiseValue()
{
  static const int8_t fm[] = { 0, 16, 31 }, am[] = { 0, 8, 40 };
  return band().mode == MODE_FM ? fm[ghost.noise] : am[ghost.noise];
}

static uint32_t snap(uint32_t hz)
{
  uint32_t lo = bandLo(), hi = bandHi(), st = ghostStepHz();
  if(hz < lo) return lo;
  if(hz > hi) return hi;
  return lo + ((hz - lo + st / 2) / st) * st;
}

//
// Session
//
static void enterBand(uint32_t start)
{
  if(mapBand != ghost.band || mapStepSel != ghost.step) mapReset();
  cur = snap(start);
  radioTune(cur, band().mode);
  radioSweepBegin();
  radioAgcOverride(antennaValue());
  radioSoftMuteOverride(noiseValue());
  gated = false;
  dualA = bandLo(); dualB = bandHi(); dualSide = false;
  shufN = 0;
  quietR = 255; quietHz = 0;
  first = true;
  phase = 0;
}

void ghostStart()
{
  if(active) return;
  if(noiseActive()) noiseStop();
  if(stormActive()) stormStop();
  oldHz = radioFreq();
  oldMode = radioMode();
  if(!trace)
  {
    // Keep as many steps as the memory allows (about 10 bytes each)
    static const uint32_t sizes[] = { 120000, 60000, 30000, 10000 };
    for(size_t i = 0; i < ITEM_COUNT(sizes) && !trace; i++)
    {
      trace = static_cast<GhostPoint *>(ps_malloc(sizes[i] * sizeof(GhostPoint)));
      if(trace) traceCap = sizes[i];
    }
  }
  traceTotal = 0;
  markN = 0;
  hotN = 0;
  holdUntil = windowUntil = 0; windowFlag = false; trigState = 0; holdKind = 0; catchN = 0;
  memset(caughtHz, 0, sizeof(caughtHz));
  timerDone = timerFlag = false;
  mapBand = 255;                       // fresh carrier map for each session
  histN = histPos = 0;
  steps = 0;
  sign = 1;
  lastR = lastS = 0;
  startMs = millis();
  startEpoch = clockValid() ? clockUTC() : 0;
  enterBand(ghost.dir == 1 ? band().hi : bandLo());
  active = true;
  paused = false;
  nextAt = millis();
  if(ghost.trigger) trigArm();
}

void ghostStop()
{
  if(!active) return;
  historySave();
  active = false;
  trigState = 0; holdUntil = windowUntil = 0;
  paused = false;
  if(gated) { radioGate(false); gated = false; }
  radioSweepEnd();
  radioTune(oldHz, oldMode);
  radioApplyAudio();
  if(saveDirty) { storeSave("ghost", &ghost, sizeof(ghost)); saveDirty = false; }
}

bool ghostActive() { return active; }
bool ghostPaused() { return paused; }

void ghostPause(bool on)
{
  if(!active) return;
  paused = on;
  if(on && gated) { radioGate(false); gated = false; }   // hold = listen to it
  if(!on) { nextAt = millis(); holdUntil = 0; if(trigState == 1) { trigState = 0; trigArm(); } }
}

// Settings changed while running: band, antenna or gaps
void ghostApply()
{
  if(!active) return;
  if(mapBand != ghost.band || mapStepSel != ghost.step) mapReset();
  if(radioMode() != band().mode || cur < bandLo() || cur > bandHi())
    enterBand(ghost.dir == 1 ? band().hi : bandLo());
  else
  {
    cur = snap(cur);
    radioAgcOverride(antennaValue());
    radioSoftMuteOverride(noiseValue());
  }
  if(!ghost.gaps && gated) { radioGate(false); gated = false; }
}

static uint32_t stepNext(uint32_t cur);

static bool nearCarrier(uint32_t f)
{
  uint32_t st = ghostStepHz();
  for(int k = -(int)ghost.cluster; k <= (int)ghost.cluster; k++)
  {
    int64_t g = (int64_t)f + (int64_t)k * st;
    if(g > 0 && isCarrier((uint32_t)g)) return true;
  }
  return false;
}

static bool allowed(uint32_t f)
{
  switch(ghost.content)
  {
    case GC_NOISE:    return !isCarrier(f);
    case GC_STATIONS: return !carriers || isCarrier(f);
    case GC_CLUSTER:  return !carriers || nearCarrier(f);
    default:          return true;
  }
}

static uint32_t gcd32(uint32_t a, uint32_t b) { while(b) { uint32_t t = a % b; a = b; b = t; } return a; }

static void advance()
{
  if(ghost.content == GC_HOT)
  {
    hotBuild();
    if(hotN)
    {
      // Only the frequencies that gave something, in the chosen order
      uint8_t i = 0;
      while(i < hotN && hot[i] < cur) i++;
      bool on = i < hotN && hot[i] == cur;
      switch(ghost.dir)
      {
        case 0: i = on ? (i + 1) % hotN : i % hotN; break;
        case 1: i = i == 0 ? hotN - 1 : i - 1; break;
        case 2:
          if(sign > 0 && (on ? i + 1 : i) >= hotN) sign = -1;
          else if(sign < 0 && i == 0) sign = 1;
          i = sign > 0 ? (on ? i + 1 : i) % hotN : (i ? i - 1 : 0);
          break;
        default: i = hotN > 1 ? (uint8_t)random(hotN) : 0; break;
      }
      cur = hot[i];
      return;
    }
  }
  uint32_t next = stepNext(cur);
  for(int tries = 0; tries < 2000 && !allowed(next); tries++) next = stepNext(next);
  cur = next;
}

static uint32_t stepNext(uint32_t cur)
{
  uint32_t lo = bandLo(), hi = bandHi(), st = ghostStepHz();
  switch(ghost.dir)
  {
    case 0: cur = cur + st > hi ? lo : cur + st; break;
    case 1: cur = cur < lo + st ? hi : cur - st; break;
    case 2:
      if(sign > 0 && cur + st > hi) sign = -1;
      else if(sign < 0 && cur < lo + st) sign = 1;
      cur = sign > 0 ? cur + st : cur - st;
      break;
    case 4:
    {
      // Shuffle: every frequency once per round, in a new order each round
      uint32_t n = (hi - lo) / st + 1;
      if(n != shufN || shufK >= n)
      {
        shufN = n; shufK = 0;
        shufA = 1;
        for(int t = 0; t < 40 && n > 3; t++) { uint32_t a = 2 + (uint32_t)random(n - 3); if(gcd32(a, n) == 1) { shufA = a; break; } }
        shufC = n > 1 ? (uint32_t)random(n) : 0;
      }
      cur = lo + (uint32_t)(((uint64_t)shufA * shufK + shufC) % n) * st;
      shufK++;
      break;
    }
    case 5:
      // Dual: two sweeps, one forward and one reverse, taking turns
      dualSide = !dualSide;
      if(dualA < lo || dualA > hi) dualA = lo;
      if(dualB < lo || dualB > hi) dualB = hi;
      if(dualSide) { dualA = dualA + st > hi ? lo : dualA + st; cur = dualA; }
      else { dualB = dualB < lo + st ? hi : dualB - st; cur = dualB; }
      break;
    default:
    {
      uint32_t n = (hi - lo) / st + 1;
      uint32_t next = cur;
      while(n > 1 && next == cur) next = lo + (uint32_t)(random(n)) * st;
      cur = next;
      break;
    }
  }
  return cur;
}

bool ghostTick()
{
  if(saveDirty && millis() - dirtyAt > 3000) { storeSave("ghost", &ghost, sizeof(ghost)); saveDirty = false; }
  if(!active || paused) return false;

  uint32_t now = millis();
  if(ghost.timer && !timerDone && now - startMs >= ghost.timer * 60000UL)
  {
    timerDone = timerFlag = true;
    ghostPause(true);
    return true;
  }
  bool changed = false;
  if(windowUntil && (int32_t)(now - windowUntil) >= 0) { windowUntil = 0; windowFlag = true; changed = true; }
  if(holdUntil)
  {
    if((int32_t)(now - holdUntil) < 0) return changed;
    holdUntil = 0; holdKind = 0; nextAt = now; changed = true;
  }
  if(trigState == 1)
  {
    // Armed: muted on the quietest frequency seen, waiting for an RF burst
    if(now - trigPollAt < 40) return changed;
    trigPollAt = now;
    uint8_t r = 0, sn = 0;
    radioRead(r, sn);
    trigLast = r;
    if(now - trigArmedAt < 1500) { trigBase = trigBase * 0.7f + r * 0.3f; return true; }   // settle
    if(r >= trigBase + ghost.trigSens)
    {
      trigState = 2;
      trigUntil = now + ghost.trigOpen * 1000UL;
      if(gated) { radioGate(false); gated = false; }
      ghostMark(2);
      first = true;
      nextAt = now;
      return true;
    }
    trigBase = trigBase * 0.97f + r * 0.03f;
    return true;
  }
  if(trigState == 2 && (int32_t)(now - trigUntil) >= 0) { trigArm(); return true; }
  if((int32_t)(now - nextAt) < 0) return changed;

  uint32_t f, dwell;
  if(ghost.flutter && phase == 1)
  {
    // Second half of the dwell, a half step away
    f = cur + flutterHz() <= band().hi ? cur + flutterHz() : cur - flutterHz();
    phase = 0;
    dwell = ghost.rate / 2;
  }
  else
  {
    if(!first) advance();
    first = false;
    f = cur;
    steps++;
    phase = ghost.flutter ? 1 : 0;
    dwell = ghost.flutter ? ghost.rate - ghost.rate / 2 : ghost.rate;
  }

  radioSweepTo(f);
  uint8_t r = 0, s = 0;
  radioRead(r, s);
  lastR = r; lastS = s;
  if(phase == 1 || !ghost.flutter) { if(s >= ghost.gate) setCarrier(f); }   // learn where stations are
  if(r < quietR || (r == quietR && !quietHz)) { quietR = r; quietHz = f; }   // the RF trigger listens there

  if(ghost.gaps)
  {
    bool g = s < ghost.gate;
    if(g != gated) { radioGate(g); gated = g; }
  }

  // Catch: something is there, stay long enough to hear a word
  bool caught = false;
  if(ghost.catchT && s >= ghost.catchSnr && trigState != 1)
  {
    bool recent = false;
    for(uint8_t k = 0; k < 8; k++) if(caughtHz[k] == f && now - caughtAt[k] < 30000) recent = true;
    if(!recent)
    {
      caughtHz[caughtPos] = f; caughtAt[caughtPos] = now; caughtPos = (caughtPos + 1) % 8;
      holdUntil = now + ghost.catchT * 100UL; holdKind = 2; catchN++;
      if(gated) { radioGate(false); gated = false; }
      phase = 0;
      caught = true;
    }
  }

  if(trace && traceCap)
  {
    GhostPoint &p = trace[traceTotal % traceCap];
    p.t = now - startMs; p.hz = f; p.rssi = r; p.snr = s;
  }
  traceTotal++;

  histR[histPos] = r; histS[histPos] = s;
  histPos = (histPos + 1) % HIST_N;
  if(histN < HIST_N) histN++;

  // Chaos: an irregular rhythm, like a hand on the dial
  if(ghost.chaos)
  {
    int32_t span = dwell * (ghost.chaos == 1 ? 25 : 50) / 100;
    dwell = dwell - span + (uint32_t)random(2 * span + 1);
    if(dwell < 25) dwell = 25;
  }
  if(caught) { nextAt = holdUntil; return true; }
  // Keep the rhythm, but never try to catch up after a delay
  nextAt += dwell;
  if((int32_t)(millis() - nextAt) > 0) nextAt = millis() + dwell / 2;
  return true;
}

// The frequency that was playing a reaction time before now
static bool heardAt(uint32_t t, GhostPoint &out)
{
  if(!trace || !traceTotal) return false;
  uint32_t oldest = ghostTraceOldest();
  for(uint32_t i = traceTotal; i-- > oldest;)
  {
    const GhostPoint &p = trace[i % traceCap];
    if(p.t <= t) { out = p; return true; }
    if(traceTotal - i > 400) break;
  }
  return false;
}

uint16_t ghostMark(uint8_t kind)
{
  if(!active) return 0;
  uint32_t now = millis() - startMs;
  GhostPoint m = { now, radioFreq(), lastR, lastS };
  if(kind == 0 && trigState != 1)
  {
    GhostPoint h;
    uint32_t back = ghost.react * 100UL;
    if(heardAt(now > back ? now - back : 0, h)) { m.hz = h.hz; m.rssi = h.rssi; m.snr = h.snr; }
  }
  if(markN < MARK_MAX) { markKind[markN] = kind; marks[markN++] = m; }
  if(kind == 1 && ghost.window) windowUntil = millis() + ghost.window * 1000UL;
  if(kind == 0 && ghost.onMark && !paused && trigState != 1)
  {
    // Go back to what was heard and stay there
    radioSweepTo(m.hz);
    if(gated) { radioGate(false); gated = false; }
    if(ghost.onMark == 1) { holdUntil = millis() + ghost.holdSec * 1000UL; holdKind = 1; }
    else ghostPause(true);
  }
  return markN;
}
uint32_t ghostHoldLeft() { return holdUntil && (int32_t)(holdUntil - millis()) > 0 ? holdUntil - millis() : 0; }
uint8_t ghostHoldKind() { return ghostHoldLeft() ? holdKind : 0; }
uint16_t ghostCatches() { return catchN; }
uint32_t ghostWindowLeft() { return windowUntil && (int32_t)(windowUntil - millis()) > 0 ? windowUntil - millis() : 0; }
bool ghostTakeWindowDone() { bool f = windowFlag; windowFlag = false; return f; }
uint8_t ghostTrigState() { return active ? trigState : 0; }
uint32_t ghostTrigOpenLeft() { return trigState == 2 && (int32_t)(trigUntil - millis()) > 0 ? trigUntil - millis() : 0; }
uint8_t ghostTrigLevel() { return trigLast; }
uint8_t ghostTrigBase() { return (uint8_t)(trigBase + 0.5f); }

static void trigArm()
{
  trigState = 1;
  watchHz = quietHz ? quietHz : cur;
  radioSweepTo(watchHz);
  radioGate(true); gated = true;
  trigArmedAt = trigPollAt = millis();
  uint8_t r = 0, sn = 0;
  radioRead(r, sn);
  trigBase = r; trigLast = r;
}

//
// Session history: settings and marks of the last sessions, kept in flash
//
#define HIST_MAX   20
#define HIST_MAGIC 0x4E474248UL     // "NGBH"
struct HistHead { uint32_t magic, id, epoch, dur, steps; uint16_t marks, pad; GhostCfg cfg; };
#ifdef NOVA_SIM
// The simulator keeps the history in memory
struct SimHist { HistHead h; GhostPoint m[MARK_MAX]; uint8_t k[MARK_MAX]; };
static SimHist simHist[HIST_MAX];
static uint8_t simHistN = 0;
static uint32_t simHistId = 0;
static void historySave()
{
  if(!markN && steps < 100) return;
  if(simHistN == HIST_MAX) { memmove(&simHist[0], &simHist[1], sizeof(SimHist) * (HIST_MAX - 1)); simHistN--; }
  SimHist &e = simHist[simHistN++];
  e.h = { HIST_MAGIC, ++simHistId, startEpoch, millis() - startMs, steps, markN, 0, ghost };
  memcpy(e.m, marks, sizeof(marks)); memcpy(e.k, markKind, sizeof(markKind));
}
static SimHist *simAt(uint8_t i) { return i < simHistN ? &simHist[simHistN - 1 - i] : nullptr; }
uint8_t ghostHistCount() { return simHistN; }
bool ghostHistInfo(uint8_t i, uint32_t &id, uint32_t &epoch, uint32_t &dur, uint32_t &st, uint16_t &mk, GhostCfg &c)
{
  SimHist *e = simAt(i); if(!e) return false;
  id = e->h.id; epoch = e->h.epoch; dur = e->h.dur; st = e->h.steps; mk = e->h.marks; c = e->h.cfg; return true;
}
uint16_t ghostHistMarks(uint8_t i, GhostPoint *m, uint8_t *kind, uint16_t max)
{
  SimHist *e = simAt(i); if(!e) return 0;
  uint16_t n = e->h.marks < max ? e->h.marks : max;
  memcpy(m, e->m, n * sizeof(GhostPoint)); memcpy(kind, e->k, n); return n;
}
void ghostHistClear() { simHistN = 0; }
#else
static bool fsOk() { return LittleFS.begin(false, "/littlefs", 10, "littlefs"); }
static void histPath(char *b, size_t n, uint32_t id) { snprintf(b, n, "/gb/%08lu.bin", (unsigned long)id); }

// Session ids, newest first
static uint8_t histIds(uint32_t *ids, uint8_t max)
{
  uint8_t n = 0;
  if(!fsOk()) return 0;
  File d = LittleFS.open("/gb");
  if(!d || !d.isDirectory()) return 0;
  for(File f = d.openNextFile(); f; f = d.openNextFile())
  {
    uint32_t id = strtoul(f.name(), nullptr, 10);
    if(!id) continue;
    if(n < max) ids[n++] = id;
    else { uint8_t lo = 0; for(uint8_t k = 1; k < n; k++) if(ids[k] < ids[lo]) lo = k; if(id > ids[lo]) ids[lo] = id; }
  }
  for(uint8_t a = 1; a < n; a++) { uint32_t v = ids[a]; int b = a - 1; while(b >= 0 && ids[b] < v) { ids[b + 1] = ids[b]; b--; } ids[b + 1] = v; }
  return n;
}

static void historySave()
{
  if(!markN && steps < 100) return;              // nothing worth keeping
  if(!fsOk()) return;
  if(!LittleFS.exists("/gb")) LittleFS.mkdir("/gb");
  uint32_t ids[HIST_MAX + 1];
  uint8_t n = histIds(ids, HIST_MAX + 1);
  uint32_t id = n ? ids[0] + 1 : 1;
  // Drop the oldest beyond the limit
  for(uint8_t k = HIST_MAX - 1; k < n; k++) { char p[32]; histPath(p, sizeof(p), ids[k]); LittleFS.remove(p); }
  char p[32]; histPath(p, sizeof(p), id);
  File f = LittleFS.open(p, "w");
  if(!f) return;
  HistHead h = { HIST_MAGIC, id, startEpoch, millis() - startMs, steps, markN, 0, ghost };
  f.write((const uint8_t *)&h, sizeof(h));
  f.write((const uint8_t *)marks, markN * sizeof(GhostPoint));
  f.write(markKind, markN);
  f.close();
}

static bool histOpen(uint8_t i, File &f, HistHead &h)
{
  uint32_t ids[HIST_MAX];
  uint8_t n = histIds(ids, HIST_MAX);
  if(i >= n) return false;
  char p[32]; histPath(p, sizeof(p), ids[i]);
  f = LittleFS.open(p, "r");
  if(!f || f.read((uint8_t *)&h, sizeof(h)) != sizeof(h) || h.magic != HIST_MAGIC) { if(f) f.close(); return false; }
  return true;
}
uint8_t ghostHistCount() { uint32_t ids[HIST_MAX]; return histIds(ids, HIST_MAX); }
bool ghostHistInfo(uint8_t i, uint32_t &id, uint32_t &epoch, uint32_t &dur, uint32_t &st, uint16_t &mk, GhostCfg &c)
{
  File f; HistHead h;
  if(!histOpen(i, f, h)) return false;
  f.close();
  id = h.id; epoch = h.epoch; dur = h.dur; st = h.steps; mk = h.marks; c = h.cfg;
  return true;
}
uint16_t ghostHistMarks(uint8_t i, GhostPoint *m, uint8_t *kind, uint16_t max)
{
  File f; HistHead h;
  if(!histOpen(i, f, h)) return 0;
  uint16_t n = h.marks < max ? h.marks : max;
  f.read((uint8_t *)m, n * sizeof(GhostPoint));
  f.seek(sizeof(h) + h.marks * sizeof(GhostPoint));
  f.read(kind, n);
  f.close();
  return n;
}
void ghostHistClear()
{
  uint32_t ids[HIST_MAX];
  uint8_t n = histIds(ids, HIST_MAX);
  for(uint8_t k = 0; k < n; k++) { char p[32]; histPath(p, sizeof(p), ids[k]); LittleFS.remove(p); }
}
#endif
uint8_t ghostMarkKind(uint16_t i) { return i < markN ? markKind[i] : 0; }
uint32_t ghostRemaining()
{
  if(!active || !ghost.timer) return 0;
  uint32_t total = ghost.timer * 60000UL, el = millis() - startMs;
  return el >= total ? 0 : total - el;
}
bool ghostTakeTimerDone() { bool f = timerFlag; timerFlag = false; return f; }

uint32_t ghostElapsed() { return active ? millis() - startMs : 0; }
uint32_t ghostSteps() { return steps; }
uint32_t ghostStartEpoch() { return startEpoch; }
uint16_t ghostMarkCount() { return markN; }
const GhostPoint *ghostMarkAt(uint16_t i) { return i < markN ? &marks[i] : nullptr; }
uint32_t ghostTraceTotal() { return traceTotal; }
uint32_t ghostTraceOldest() { return traceTotal > traceCap ? traceTotal - traceCap : 0; }

bool ghostTraceAt(uint32_t i, GhostPoint &p)
{
  if(!trace || !traceCap || i >= traceTotal || i < ghostTraceOldest()) return false;
  p = trace[i % traceCap];
  return true;
}

uint8_t ghostHistory(uint8_t *r, uint8_t *s, uint8_t n)
{
  if(n > histN) n = histN;
  for(uint8_t k = 0; k < n; k++)
  {
    uint8_t i = (histPos + HIST_N - n + k) % HIST_N;
    r[k] = histR[i]; s[k] = histS[i];
  }
  return n;
}
