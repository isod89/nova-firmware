#include <unistd.h>
// Simulated hardware: radio, clock, battery, storage
#include "Nova.h"
#include "Ui.h"
#include <map>
#include <string>
#include <vector>

lgfx::LGFX_Sprite spr;
uint64_t simMs = 1000;
uint32_t millis() { return (uint32_t)simMs; }
void delay(uint32_t ms) { simMs += ms; }

// ---- storage
static std::map<std::string, std::vector<uint8_t>> store;
bool storeLoad(const char *k, void *d, size_t n, size_t *got) { auto it = store.find(k); if(it == store.end() || it->second.size() > n) { if(got) *got = 0; return false; } memcpy(d, it->second.data(), it->second.size()); if(got) *got = it->second.size(); return it->second.size() > 0; }
bool storeSave(const char *k, const void *d, size_t n) { store[k].assign((const uint8_t *)d, (const uint8_t *)d + n); return true; }
void storeErase() { store.clear(); }
size_t psramSize() { return 8u << 20; }

// ---- clock: 14:30 UTC
static uint32_t epochBase = 1794753000; // 2026-11-15 14:30 UTC approx
bool clockValid() { return true; }
uint32_t clockUTC() { return epochBase + (uint32_t)(simMs / 1000); }
void clockSet(uint32_t e) { epochBase = e - (uint32_t)(simMs / 1000); }

float batteryVolts() { return 3.95f; }
int backlightLevel = 0;
void backlight(uint8_t l) { backlightLevel = l; }
bool poweredOff = false;
void powerOff() { poweredOff = true; }

// ---- airwaves
struct Air { uint32_t f; int rssi, snr; const char *name; const char *rt; };
std::vector<Air> airFM = {
  { 88100000, 38, 22, "CBC R1", "Morning news with the weather"}, { 89500000, 30, 17, "CLASSIC", ""},
  { 91100000, 45, 28, "JAZZ FM", "Now playing: Take Five"}, { 93500000, 22, 10, "", ""},
  { 94900000, 52, 30, "ROCK 95", ""}, { 97300000, 47, 27, "HITS 97", "Top 40 countdown"},
  { 99100000, 55, 32, "INFO 99", "Traffic and weather every 10 minutes"}, {102100000, 18, 8, "COUNTRY", ""},
  {104500000, 41, 24, "CHFI", ""}, {106500000, 26, 13, "", ""}, { 76500000, 20, 9, "", ""},
};
std::vector<Air> airAM = {
  {  680000, 34, 20, "", ""}, {  740000, 40, 24, "", ""}, { 1010000, 30, 16, "", ""}, { 1430000, 20, 10, "", ""},
  { 5960000, 24, 12, "", ""}, { 6070000, 30, 15, "", ""}, { 7325000, 26, 14, "", ""}, { 9580000, 22, 11, "", ""},
  { 9650000, 40, 22, "", ""}, {11780000, 28, 14, "", ""}, {15140000, 31, 17, "", ""}, {17770000, 19, 9, "", ""},
  {  198000, 22, 10, "", ""}, { 1062000, 14,  7, "", ""}, { 3330000, 25, 14, "", ""}, { 5000000, 27, 15, "", ""},
  { 6925000, 17,  8, "", ""}, {27025000, 15,  7, "", ""}, {13500000, 30, 20, "", ""},
  { 4747000, 18,  9, "", ""},   // off-grid tropical station
};
static uint32_t noise = 12345;
static int rnd(int n) { noise = noise * 1103515245 + 12345; return (noise >> 16) % n; }
static bool wideFilter = false;

static void airAt(uint32_t f, bool fm, uint8_t &r, uint8_t &s, bool &valid)
{
  int br = 2 + rnd(4), bs = rnd(3); valid = false;
  for(auto &a : (fm ? airFM : airAM))
  {
    long df = labs((long)f - (long)a.f);
    int rr, ss;
    if(fm) { long d = df / 100000; if(d > 3) continue; rr = a.rssi - 12 * d; ss = a.snr - 12 * d; if(d == 0) valid = true; }
    else
    {
      long k = df / 1000;
      if(k > 12) continue;
      long pass = wideFilter ? 3 : 1;       // carrier stays inside the filter up to +/- pass kHz
      long out = k <= pass ? 0 : k - pass;
      rr = a.rssi - (int)(k <= pass ? k : pass + out * 3) - (wideFilter ? 2 : 0);
      ss = a.snr  - (int)(k <= pass ? k : pass + out * 3) - (wideFilter ? 3 : 0);
      if(k < 1) valid = true;
    }
    if(rr > br) br = rr;
    if(ss > bs) bs = ss;
  }
  r = br < 0 ? 0 : br; s = bs < 0 ? 0 : bs;
}

static uint32_t hz = 99100000; static uint8_t mode = MODE_FM;
static char ps[16] = "", rt[72] = ""; static uint64_t tunedAt = 0;
static Signal sg = {0,0,false};

uint32_t radioFreq() { return hz; }
uint8_t radioMode() { return mode; }
uint32_t radioMinFreq(uint8_t m) { return m == MODE_FM ? 64000000 : 150000; }
uint32_t radioMaxFreq(uint8_t m) { return m == MODE_FM ? 108000000 : 30000000; }
bool radioSSBLoaded() { return modeIsSSB(mode); }
Signal radioSignal() { return sg; }
void radioMute(bool) {}
void radioSetVolume(uint8_t v) { cfg.volume = v; }
void radioApplyAudio() {} void radioApplyStep() {}
const char *modeName(uint8_t m) { static const char *n[] = {"FM","LSB","USB","AM","CW"}; return n[m % 5]; }
static const StepDef fmSteps[]  = { {10000, "10k"}, {50000, "50k"}, {100000, "100k"}, {200000, "200k"}, {1000000, "1M"} };
static const StepDef amSteps[]  = { {1000, "1k"}, {5000, "5k"}, {9000, "9k"}, {10000, "10k"}, {100000, "100k"}, {1000000, "1M"} };
static const StepDef ssbSteps[] = { {10, "10"}, {50, "50"}, {100, "100"}, {500, "500"}, {1000, "1k"}, {5000, "5k"} };
const StepDef *stepList(uint8_t m, uint8_t &c) { if(m == MODE_FM) { c = 5; return fmSteps; } if(m == MODE_AM) { c = 6; return amSteps; } c = 6; return ssbSteps; }
uint32_t currentStep() { uint8_t n; const StepDef *s = stepList(mode, n); return s[cfg.stepIdx[mode] % n].hz; }
const char *bandwidthLabel(uint8_t m, uint8_t i) { static const char *fm[] = {"Auto","110k","84k","60k","40k"}, *am[] = {"6.0k","4.0k","3.0k","2.5k","2.0k","1.8k","1.0k"}, *ssb[] = {"4.0k","3.0k","2.2k","1.2k","1.0k","0.5k"}; return m == MODE_FM ? fm[i % 5] : m == MODE_AM ? am[i % 7] : ssb[i % 6]; }
uint8_t bandwidthCount(uint8_t m) { return m == MODE_FM ? 5 : m == MODE_AM ? 7 : 6; }

static void retune() { tunedAt = simMs; ps[0] = rt[0] = 0; }
bool radioSetFreq(uint32_t f) { hz = f; cfg.freq = f; cfg.mode = mode; retune(); return true; }
bool radioTune(uint32_t f, uint8_t m) { mode = m; simMs += (m == mode) ? 30 : 300; return radioSetFreq(f); }
void radioApply() { mode = cfg.mode; hz = cfg.freq; retune(); }
const char *rdsName() { return ps; }
const char *rdsText() { return rt; }
void rdsClear() { ps[0] = rt[0] = 0; }
void radioRdsReset() { retune(); }
bool radioRdsSync() { for(auto &a : airFM) if(a.f == hz && a.name[0]) return true; return false; }
bool radioPoll()
{
  uint8_t r, s; bool v;
  airAt(modeIsSSB(mode) ? hz : hz, mode == MODE_FM, r, s, v);
  bool ch = r != sg.rssi || s != sg.snr;
  sg = { r, s, mode == MODE_FM && s > 15 };
  if(mode == MODE_FM && simMs - tunedAt > 400)
    for(auto &a : airFM) if(a.f == hz && a.name[0] && strcmp(ps, a.name)) { strcpy(ps, a.name); strcpy(rt, a.rt); ch = true; }
  return ch;
}
bool radioSeek(int dir) { return false; }
void radioScanBegin(uint8_t g, bool wide) { mode = g == GROUP_FM ? MODE_FM : MODE_AM; wideFilter = wide && g != GROUP_FM; simMs += 300; }
void radioScanFilter(bool wide) { wideFilter = wide; }
void radioScanThresholds(uint8_t, uint8_t, uint8_t) {}
static int simSlow = getenv("SIM_SLOW") ? atoi(getenv("SIM_SLOW")) : 0;
bool radioMeasure(uint32_t f, uint8_t &r, uint8_t &s, bool &v) { if(simSlow) usleep(simSlow); hz = f; retune(); simMs += mode == MODE_FM ? 25 : 32; airAt(f, mode == MODE_FM, r, s, v); return true; }
int simNoise = 0;            // extra local noise (Noise Hunter test)
uint64_t simStormFrom = 0;   // lightning crashes from this time, growing stronger
void delayMicroseconds(uint32_t us) { static uint32_t acc = 0; acc += us; simMs += acc / 1000; acc %= 1000; }
void radioRead(uint8_t &r, uint8_t &s)
{
  bool v; airAt(hz, mode == MODE_FM, r, s, v);
  if(mode != MODE_FM) r += simNoise;
  if(simStormFrom && mode != MODE_FM && hz < 400000 && simMs > simStormFrom)
  {
    uint64_t t = simMs - simStormFrom;
    uint64_t period = 9000 - (t / 60000) * 600;            // more frequent with time
    if(period < 1500) period = 1500;
    if(t % period < 40) r += 12 + (uint8_t)(t / 60000) * 2;  // stronger with time
  }
}
void radioScanEnd() { radioApply(); }
bool radioInit() { return true; }

void radioScope(uint32_t center, uint32_t step, uint16_t n, uint8_t *r, uint8_t *s, bool (*abortFn)())
{
  bool fm = mode == MODE_FM; bool v;
  bool savedWide = wideFilter; wideFilter = false;
  for(uint16_t i = 0; i < n; i++) { uint32_t f = center + ((int32_t)i - n / 2) * (int32_t)step; airAt(f, fm, r[i], s[i], v); simMs += 30; }
  wideFilter = savedWide;
}
void radioPowerDown() {}
void uiMessage(const char *, const char *);

// Ghost Box sweep primitives
void radioSweepBegin() {}
bool radioSweepTo(uint32_t f) { hz = f; retune(); simMs += mode == MODE_FM ? 20 : 25; return true; }
void radioGate(bool) {}
void radioAgcOverride(int8_t) {}
void radioSoftMuteOverride(int8_t) {}
void radioSweepEnd() {}
void radioScanDelay(uint8_t) {}
SignalFull radioSignalFull()
{
  uint8_t r, s; radioRead(r, s);
  SignalFull f; f.rssi = r + rand() % 5; f.snr = s + rand() % 4; f.mult = mode == MODE_FM ? rand() % 12 : 0;
  f.blend = mode == MODE_FM && r > 40 ? 90 : 20; f.pilot = mode == MODE_FM && r > 40; f.offset = (rand() % 7) - 3;
  return f;
}
