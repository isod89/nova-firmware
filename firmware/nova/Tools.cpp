//
// Tools.cpp - measurement apps built on the receiver's signal meter
//
//   Noise Hunter    finds what pollutes reception (chargers, LED drivers,
//                   power supplies...): a quiet frequency is chosen, the
//                   noise level is measured continuously and compared to a
//                   reference ("zero"). Start low (MW) to find the area,
//                   go higher (7-28 MHz) to pinpoint: high-frequency noise
//                   only carries a short distance.
//
//   Storm Detector  counts lightning "sferics": the static crashes lightning
//                   radiates mostly below 1 MHz. The receiver sits on the
//                   quietest long-wave frequency with a fixed gain and
//                   watches for short bursts above the noise floor. Counts,
//                   strength and trend give an estimate of the storm activity.
//
#include "Nova.h"

#define TOOLS_VERSION 1

// Both apps tune with a fixed receiver gain: the signal meter then follows
// the real input level (automatic gain would flatten it)
static int8_t fixedGain(uint8_t att) { return att + 1; }   // cfg.agc scale: n = attenuation n-1

// Quietest channel of a range: the one with the lowest SNR (no carrier),
// then the lowest level. Takes a few seconds; the screen shows progress.
static uint32_t findQuiet(uint32_t lo, uint32_t hi, uint32_t step, const char *what)
{
  uint32_t best = lo;
  int bestScore = 1 << 30;
  uint32_t n = (hi - lo) / step + 1, k = 0;
  for(uint32_t f = lo; f <= hi; f += step, k++)
  {
    radioSweepTo(f);
    delay(4);
    uint8_t r = 0, s = 0, r2 = 0, s2 = 0;
    radioRead(r, s);
    delay(6);
    radioRead(r2, s2);
    int score = (s > s2 ? s : s2) * 256 + (r + r2);
    if(score < bestScore) { bestScore = score; best = f; }
    if(!(k % 8))
    {
      char buf[48];
      snprintf(buf, sizeof(buf), "%s  %lu%%", what, (unsigned long)(k * 100 / n));
      uiMessage("Finding a quiet frequency", buf);
    }
  }
  radioSweepTo(best);
  uiRequestDraw();
  return best;
}

//
// Noise Hunter
//
const ToolBand noiseBands[] =
{
  { "LW 150-300 kHz",   "LW",    150000,   300000,  3000 },
  { "MW 520-1710 kHz",  "MW",    520000,  1710000, 10000 },
  { "3.5 MHz",          "3.5M", 3500000,  3800000,  5000 },
  { "7 MHz",            "7M",   7000000,  7200000,  5000 },
  { "14 MHz",           "14M", 14000000, 14350000,  5000 },
  { "21 MHz",           "21M", 21000000, 21450000,  5000 },
  { "28 MHz",           "28M", 28000000, 29700000, 10000 },
};
uint8_t noiseBandCount() { return ITEM_COUNT(noiseBands); }

NoiseCfg noiseCfg;
static bool nActive = false, nDirty = false;
static uint32_t nDirtyAt = 0, nOldHz = 0, nStart = 0, nLastSample = 0;
static uint8_t nOldMode = MODE_FM;
static float nLevel = 0;
static uint8_t nPeakNow = 0, nMax = 0, nSnr = 0;
static uint32_t nPeakAt = 0;
static int16_t nBase = -1;
static uint8_t nHist[NOISE_HIST];
static uint16_t nHistPos = 0, nHistN = 0;
static uint32_t nHistAt = 0;
static NoiseSpot nSpots[NOISE_SPOTS];
static uint8_t nSpotN = 0;

static void noiseDefaults()
{
  memset(&noiseCfg, 0, sizeof(noiseCfg));
  noiseCfg.version = TOOLS_VERSION;
  noiseCfg.band = 1;
  noiseCfg.att = 0;
}

void noiseSettingsChanged() { nDirty = true; nDirtyAt = millis(); }

static void noiseApplyGain() { radioAgcOverride(fixedGain(noiseCfg.att)); }

void noiseFindQuiet()
{
  const ToolBand &b = noiseBands[noiseCfg.band];
  noiseCfg.hz = findQuiet(b.lo, b.hi, b.step, b.name);
  noiseSettingsChanged();
  nBase = -1;
  nMax = 0;
}

void noiseStart()
{
  if(nActive) return;
  appsStopAll();
  nOldHz = radioFreq();
  nOldMode = radioMode();
  const ToolBand &b = noiseBands[noiseCfg.band];
  radioTune(b.lo, MODE_AM);
  radioSweepBegin();
  noiseApplyGain();
  nActive = true;
  nStart = millis();
  nBase = -1; nMax = 0; nLevel = 0; nHistN = nHistPos = 0; nSpotN = 0;
  if(noiseCfg.hz >= b.lo && noiseCfg.hz <= b.hi) radioSweepTo(noiseCfg.hz);
  else noiseFindQuiet();
}

void noiseStop()
{
  if(!nActive) return;
  nActive = false;
  radioSweepEnd();
  radioTune(nOldHz, nOldMode);
  radioApplyAudio();
  if(nDirty) { storeSave("noise", &noiseCfg, sizeof(noiseCfg)); nDirty = false; }
}

bool noiseActive() { return nActive; }

void noiseSetBand(uint8_t band)
{
  noiseCfg.band = band % noiseBandCount();
  noiseCfg.hz = 0;
  noiseSettingsChanged();
  if(nActive) noiseFindQuiet();
}

void noiseSetHz(uint32_t hz)
{
  const ToolBand &b = noiseBands[noiseCfg.band];
  if(hz < b.lo) hz = b.lo;
  if(hz > b.hi) hz = b.hi;
  noiseCfg.hz = hz;
  noiseSettingsChanged();
  if(nActive) { radioSweepTo(hz); nBase = -1; }
}

void noiseSetAtt(uint8_t att)
{
  noiseCfg.att = att > 36 ? 36 : att;
  noiseSettingsChanged();
  if(nActive) noiseApplyGain();
}

void noiseZero() { nBase = (int16_t)(nLevel + 0.5f); nMax = 0; }
void noiseResetMax() { nMax = 0; }

uint8_t noiseSpot()
{
  if(!nActive) return 0;
  NoiseSpot &s = nSpots[nSpotN < NOISE_SPOTS ? nSpotN : NOISE_SPOTS - 1];
  s.t = millis() - nStart; s.hz = radioFreq(); s.level = noiseLevel(); s.peak = nPeakNow; s.base = nBase;
  if(nSpotN < NOISE_SPOTS) nSpotN++;
  return nSpotN;
}

bool noiseTick()
{
  if(nDirty && millis() - nDirtyAt > 3000) { storeSave("noise", &noiseCfg, sizeof(noiseCfg)); nDirty = false; }
  if(!nActive) return false;
  uint32_t now = millis();
  if(now - nLastSample < 10) return false;
  nLastSample = now;
  uint8_t r = 0, s = 0;
  radioRead(r, s);
  nSnr = s;
  // About 0.3 s averaging for the display, peak held 1 s
  nLevel = nLevel <= 0 ? r : nLevel + (r - nLevel) * 0.08f;
  if(r >= nPeakNow || now - nPeakAt > 1000) { nPeakNow = r; nPeakAt = now; }
  if(r > nMax) nMax = r;
  bool step = false;
  if(now - nHistAt >= 200)
  {
    nHistAt = now;
    nHist[nHistPos] = (uint8_t)(nLevel + 0.5f);
    nHistPos = (nHistPos + 1) % NOISE_HIST;
    if(nHistN < NOISE_HIST) nHistN++;
    step = true;
  }
  return step;
}

uint8_t noiseLevel() { return (uint8_t)(nLevel + 0.5f); }
uint8_t noisePeak() { return nPeakNow; }
uint8_t noiseMax() { return nMax; }
uint8_t noiseSnr() { return nSnr; }
int16_t noiseBase() { return nBase; }
uint32_t noiseElapsed() { return nActive ? millis() - nStart : 0; }
uint8_t noiseSpotCount() { return nSpotN; }
const NoiseSpot *noiseSpotAt(uint8_t i) { return i < nSpotN ? &nSpots[i] : nullptr; }

uint16_t noiseHistory(uint8_t *out, uint16_t n)
{
  if(n > nHistN) n = nHistN;
  for(uint16_t k = 0; k < n; k++) out[k] = nHist[(nHistPos + NOISE_HIST - n + k) % NOISE_HIST];
  return n;
}

//
// Storm Detector
//
StormCfg stormCfg;
static bool sActive = false, sDirty = false, sInPulse = false, sListen = false;
static uint32_t sDirtyAt = 0, sOldHz = 0, sStart = 0, sStartEpoch = 0, sPulseAt = 0, sLastEnd = 0, sHighSince = 0, sEvalAt = 0;
static uint8_t sOldMode = MODE_FM;
static float sBase = 0;
static uint8_t sLevel = 0, sPulsePeak = 0, sAlert = 0;
static int8_t sTrend = 0;
static StormEvent *sEvents = nullptr;
static uint32_t sEventCap = 0, sEventN = 0;
static StormMinute sMin[STORM_MINUTES];
static bool sAlertPending = false;

static void stormDefaults()
{
  memset(&stormCfg, 0, sizeof(stormCfg));
  stormCfg.version = TOOLS_VERSION;
  stormCfg.thresh = 10;
  stormCfg.flash = 1;
}

void stormSettingsChanged() { sDirty = true; sDirtyAt = millis(); }

void stormFindQuiet()
{
  // Long wave, where lightning static is strongest and stations are few
  stormCfg.hz = findQuiet(153000, 300000, 3000, "Long wave");
  stormSettingsChanged();
  sBase = 0;
}

void stormStart()
{
  if(sActive) return;
  appsStopAll();
  sOldHz = radioFreq();
  sOldMode = radioMode();
  radioTune(153000, MODE_AM);
  radioSweepBegin();
  radioAgcOverride(fixedGain(0));
  if(!sEvents)
  {
    static const uint32_t sizes[] = { 20000, 5000, 1000 };
    for(size_t i = 0; i < ITEM_COUNT(sizes) && !sEvents; i++)
    {
      sEvents = static_cast<StormEvent *>(ps_malloc(sizes[i] * sizeof(StormEvent)));
      if(sEvents) sEventCap = sizes[i];
    }
  }
  memset(sMin, 0, sizeof(sMin));
  sEventN = 0; sInPulse = false; sAlert = 0; sTrend = 0; sBase = 0; sLastEnd = 0; sHighSince = 0;
  sListen = false;
  radioGate(true);                    // silent watch; click to listen to the crackles
  sActive = true;
  sStart = millis();
  sStartEpoch = clockValid() ? clockUTC() : 0;
  if(stormCfg.hz >= 150000 && stormCfg.hz <= 300000) radioSweepTo(stormCfg.hz);
  else stormFindQuiet();
}

void stormStop()
{
  if(!sActive) return;
  sActive = false;
  radioGate(false);
  radioSweepEnd();
  radioTune(sOldHz, sOldMode);
  radioApplyAudio();
  if(sDirty) { storeSave("storm", &stormCfg, sizeof(stormCfg)); sDirty = false; }
}

bool stormActive() { return sActive; }
void stormListen(bool on) { sListen = on; if(sActive) radioGate(!on); }
bool stormListening() { return sListen; }

void stormReset()
{
  memset(sMin, 0, sizeof(sMin));
  sEventN = 0; sAlert = 0; sTrend = 0;
  sStart = millis();
  sStartEpoch = clockValid() ? clockUTC() : 0;
}

void stormSetHz(uint32_t hz)
{
  if(hz < 150000) hz = 150000;
  if(hz > 300000) hz = 300000;
  stormCfg.hz = hz;
  stormSettingsChanged();
  if(sActive) { radioSweepTo(hz); sBase = 0; }
}

static StormMinute &minuteBin(uint32_t minute)
{
  StormMinute &m = sMin[minute % STORM_MINUTES];
  if(m.minute != minute + 1) { memset(&m, 0, sizeof(m)); m.minute = minute + 1; }
  return m;
}

static void addEvent(uint32_t now, uint8_t peak, uint16_t dur)
{
  if(sEvents && sEventCap)
  {
    StormEvent &e = sEvents[sEventN % sEventCap];
    e.t = now - sStart; e.peak = peak; e.dur = dur;
  }
  sEventN++;
  StormMinute &m = minuteBin((now - sStart) / 60000);
  if(m.count < 65535) m.count++;
  if(peak > m.maxPeak) m.maxPeak = peak;
  m.sumPeak += peak;
}

// Strikes and mean strength over a window of whole minutes ending now
static void window(uint32_t fromAgo, uint32_t toAgo, uint32_t &count, float &avg, uint32_t &strong)
{
  uint32_t nowMin = (millis() - sStart) / 60000;
  count = 0; strong = 0;
  uint32_t sum = 0;
  for(uint32_t a = fromAgo; a < toAgo && a <= nowMin; a++)
  {
    uint32_t mi = nowMin - a;
    const StormMinute &m = sMin[mi % STORM_MINUTES];
    if(m.minute != mi + 1) continue;
    count += m.count; sum += m.sumPeak;
    if(m.maxPeak >= 30) strong++;
  }
  avg = count ? (float)sum / count : 0;
}

static void evaluate()
{
  uint32_t n1, n5, n15, nPrev, s5, sx;
  float a1, a5, a15, aPrev;
  window(0, 1, n1, a1, sx);
  window(0, 5, n5, a5, s5);
  window(0, 15, n15, a15, sx);
  window(5, 15, nPrev, aPrev, sx);
  uint8_t level;
  if(n1 > 150) level = STORM_NOISY;                         // a device, not lightning
  else if(!n15) level = STORM_QUIET;
  else if(s5 >= 3 || (n5 >= 3 && a5 >= 25)) level = STORM_NEAR;
  else if(n5 >= 3 && a5 >= aPrev + 3 && n5 * 2 >= nPrev) level = STORM_APPROACH;
  else level = STORM_DISTANT;
  sTrend = !nPrev && n5 ? 1 : (a5 > aPrev + 2 ? 1 : a5 + 2 < aPrev ? -1 : 0);
  if(level != STORM_NOISY && level > sAlert && level >= STORM_APPROACH) sAlertPending = true;
  sAlert = level;
}

bool stormTick()
{
  if(sDirty && millis() - sDirtyAt > 3000) { storeSave("storm", &stormCfg, sizeof(stormCfg)); sDirty = false; }
  if(!sActive) return false;
  bool changed = false;
  // A short burst of readings each pass: crashes last only tens of ms
  uint32_t t0 = millis();
  do
  {
    uint32_t now = millis();
    uint8_t r = 0, s = 0;
    radioRead(r, s);
    sLevel = r;
    if(sBase <= 0) sBase = r;
    float thr = stormCfg.thresh;
    if(!sInPulse)
    {
      if(r >= sBase + thr) { sInPulse = true; sPulseAt = now; sPulsePeak = r - sBase; sHighSince = now; }
      else if(r <= sBase + 3) sBase += (r - sBase) * 0.01f;     // follow the noise floor slowly
    }
    else
    {
      if(r - sBase > sPulsePeak) sPulsePeak = r - sBase;
      if(r < sBase + thr / 2)
      {
        sInPulse = false;
        uint16_t dur = now - sPulseAt;
        if(now - sLastEnd >= 100) { addEvent(now, sPulsePeak, dur); changed = true; }
        sLastEnd = now;
      }
      else if(now - sHighSince > 1500)
      {
        // Too long for lightning: a carrier or a device. New floor.
        sInPulse = false;
        sBase = r;
      }
    }
    delayMicroseconds(700);
  } while(millis() - t0 < 6);

  if(millis() - sEvalAt >= 5000) { sEvalAt = millis(); evaluate(); changed = true; }
  return changed;
}

bool stormTakeAlert() { bool a = sAlertPending; sAlertPending = false; return a; }
uint8_t stormAlert() { return sAlert; }
int8_t stormTrend() { return sTrend; }
uint8_t stormLevel() { return sLevel; }
uint8_t stormBase() { return (uint8_t)(sBase + 0.5f); }
uint32_t stormElapsed() { return sActive ? millis() - sStart : 0; }
uint32_t stormStartEpoch() { return sStartEpoch; }
uint32_t stormEventTotal() { return sEventN; }
uint32_t stormEventOldest() { return sEventN > sEventCap ? sEventN - sEventCap : 0; }

bool stormEventAt(uint32_t i, StormEvent &e)
{
  if(!sEvents || i >= sEventN || i < stormEventOldest()) return false;
  e = sEvents[i % sEventCap];
  return true;
}

uint32_t stormCount(uint32_t minutes)
{
  uint32_t n; float a; uint32_t s;
  window(0, minutes, n, a, s);
  return n;
}

int32_t stormLastAgo()
{
  if(!sEventN) return -1;
  StormEvent e;
  if(!stormEventAt(sEventN - 1, e)) return -1;
  return (int32_t)(millis() - sStart - e.t);
}

uint8_t stormLastPeak()
{
  StormEvent e;
  return sEventN && stormEventAt(sEventN - 1, e) ? e.peak : 0;
}

uint16_t stormMinutes(StormMinute *out, uint16_t n)
{
  // Last n minutes, oldest first (empty minutes included)
  uint32_t nowMin = (millis() - sStart) / 60000;
  if(n > nowMin + 1) n = nowMin + 1;
  if(n > STORM_MINUTES) n = STORM_MINUTES;
  for(uint16_t k = 0; k < n; k++)
  {
    uint32_t mi = nowMin - (n - 1 - k);
    const StormMinute &m = sMin[mi % STORM_MINUTES];
    if(m.minute == mi + 1) out[k] = m;
    else { memset(&out[k], 0, sizeof(StormMinute)); out[k].minute = mi + 1; }
  }
  return n;
}

//
// Shared
//
void toolsInit()
{
  size_t got = 0;
  if(!storeLoad("noise", &noiseCfg, sizeof(noiseCfg), &got) || got != sizeof(noiseCfg) || noiseCfg.version != TOOLS_VERSION) noiseDefaults();
  if(noiseCfg.band >= noiseBandCount()) noiseCfg.band = 1;
  if(!storeLoad("storm", &stormCfg, sizeof(stormCfg), &got) || got != sizeof(stormCfg) || stormCfg.version != TOOLS_VERSION) stormDefaults();
  if(stormCfg.thresh < 4 || stormCfg.thresh > 30) stormCfg.thresh = 10;
}

bool appRunning() { return ghostActive() || nActive || sActive; }

void appsStopAll()
{
  if(ghostActive()) ghostStop();
  if(nActive) noiseStop();
  if(sActive) stormStop();
}
