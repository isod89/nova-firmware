//
// Scanner.cpp - finds every station the receiver can hear
//
// Coverage (each part can be switched on or off, cfg.scanParts)
//   FM      87.5-108 MHz, 100 kHz          FM low  64-87.4 MHz
//   MW      520-1710 kHz on the 10 kHz AND 9 kHz grids at once (Fast: region grid)
//   LW      153-279 kHz, 9 kHz
//   SW      the 14 broadcast bands (ordered for the time of day), then
//           everything else from 1715 kHz to 30 MHz, without gaps
// Speed (cfg.scanSpeed): Fast / Normal / Thorough change the dwell on each
// channel, the second chances, the off-grid centring and the RDS wait.
// The time each part really takes is learned and used for the estimates.
//
// Method, per segment
//   1. Sweep every channel (SW with the 6 kHz filter, so a carrier anywhere
//      between two 5 kHz grid points still falls inside the filter).
//   2. Measure the noise floor of the segment (median RSSI) and set the
//      detection threshold relative to it: quiet segments reveal weak
//      stations, noisy ones do not flood the list.
//   3. Second chance: channels just below the threshold are measured again
//      after a longer dwell (shortwave fading).
//   4. Keep local peaks only; a point 12 dB below a station up to 10 kHz away
//      is that station's splatter.
//   5. Confirm each peak: back to the 3 kHz filter, centre it to the exact
//      kHz within +/-3 kHz (off-grid stations), then 5 readings, best 3 averaged.
//   6. Commit the segment to the list at once: stopping keeps everything.
//
#include "Nova.h"

#define SEG_MAX 512

ScanState scanState;

struct Segment { uint8_t group; uint32_t lo, hi, step; uint8_t dual; };

struct SwBand { uint16_t lo, hi; };
static const SwBand swBands[] =
{
  {  2300,  2495 }, {  3200,  3400 }, {  3900,  4000 }, {  4750,  5060 },
  {  5900,  6200 }, {  7200,  7600 }, {  9400,  9900 }, { 11600, 12100 },
  { 13570, 13870 }, { 15100, 15800 }, { 17480, 17900 }, { 18900, 19020 },
  { 21450, 21850 }, { 25670, 26100 },
};

// Sensitivity: { minimum RSSI, margin above noise floor, minimum SNR } for FM and AM
struct Sens { uint8_t fmR, fmD, fmS, amR, amD, amS; };
static const Sens sensTable[4] =
{
  {  4,  3,  2,  4,  3,  3 },   // Max: catch everything (confirmation filters noise)
  {  6,  5,  3,  6,  4,  4 },   // High
  { 12,  8,  6, 10,  6,  6 },   // Normal
  { 25, 15, 12, 22, 14, 12 },   // Low
};

// Speed profiles
struct Speed
{
  uint8_t  fmDelay, swDelay;      // ms the I2C bus stays quiet after a retune (AM keeps 25: antenna tuning)
  bool     secondRead;            // extra reading per channel (Max sensitivity only)
  uint8_t  retries;               // second chances per segment
  uint16_t swRestStep;            // Hz, shortwave outside the broadcast bands
  int8_t   centreSpan;            // kHz each side for off-grid centring
  uint8_t  centreDwell;           // ms per centring point
  uint8_t  settleFm, settleAm;    // ms before the confirmation readings
  uint8_t  reads, readGap;        // confirmation readings
  uint16_t rdsMax, rdsBail;       // ms waiting for an RDS name / giving up without RDS sync
  bool     mwDual;                // medium wave on both 9 and 10 kHz grids
};
static const Speed speeds[3] =
{
  //  FM  SW  2nd    retry SW rest  ctr dwell FM  AM  reads gap  RDS   bail  MW dual
  {    6,  8, false,  0,   5000,    3,  20,   25,  50,  3,  12,   900,  350, false },   // Fast
  {   10, 12, false, 32,   5000,    3,  30,   40, 100,  5,  20,  1400,  500, true  },   // Normal
  {   15, 20, true,  64,   5000,    3,  40,   60, 150,  5,  30,  1800,  600, true  },   // Thorough
};

const char *scanSpeedName(uint8_t s)
{
  static const char *const n[] = { "Fast", "Normal", "Thorough" };
  return n[s < 3 ? s : 1];
}

static uint8_t ovParts = 0;
static int8_t ovSpeed = -1;
void scanOverride(uint8_t parts, int8_t speed) { ovParts = parts & SCAN_ALL; ovSpeed = speed > 2 ? 1 : speed; }
static uint8_t partsNow() { return ovParts ? ovParts : (cfg.scanParts & SCAN_ALL ? cfg.scanParts & SCAN_ALL : SCAN_ALL); }
static uint8_t speedNow() { return ovSpeed >= 0 ? (uint8_t)ovSpeed : cfg.scanSpeed < 3 ? cfg.scanSpeed : (uint8_t)SPEED_NORMAL; }
static const Speed &SP() { return speeds[speedNow()]; }

static uint8_t buildPlan(Segment *p, uint8_t maxSeg, uint8_t parts, uint8_t speed)
{
  uint8_t n = 0;
  const Speed &sp = speeds[speed];
  auto add = [&](uint8_t g, uint32_t lo, uint32_t hi, uint32_t st, uint8_t dual)
  { if(n < maxSeg) p[n++] = Segment{ g, lo, hi, st, dual }; };

  if(parts & SCAN_FM) add(GROUP_FM, 87500000, 108000000, 100000, 0);
  if(parts & SCAN_FMLOW) add(GROUP_FM, 64000000, 87400000, 100000, 0);

  if(parts & SCAN_MW)
  {
    if(sp.mwDual) add(GROUP_AM, 520000, 1710000, 10000, 1);
    else if(cfg.region == 0) add(GROUP_AM, 520000, 1710000, 10000, 0);
    else add(GROUP_AM, 531000, 1602000, 9000, 0);
  }
  if(parts & SCAN_LW) add(GROUP_AM, 153000, 279000, 9000, 0);
  if(!(parts & (SCAN_SWBC | SCAN_SWREST))) return n;

  uint8_t h, m;
  bool known = clockLocalHM(h, m);
  bool day = known && h >= 8 && h < 17;
  static const uint8_t nightOrder[] = { 6, 4, 5, 7, 3, 1, 2, 0, 8, 9, 10, 11, 12, 13 };
  static const uint8_t dayOrder[]   = { 9, 10, 7, 8, 6, 12, 11, 5, 4, 13, 3, 2, 1, 0 };
  const uint8_t *order = day ? dayOrder : nightOrder;
  if(parts & SCAN_SWBC)
    for(size_t k = 0; k < ITEM_COUNT(swBands); k++)
      add(GROUP_SW, swBands[order[k]].lo * 1000UL, swBands[order[k]].hi * 1000UL, 5000, 0);

  if(parts & SCAN_SWREST)
  {
    uint32_t stK = sp.swRestStep / 1000;
    uint32_t cur = 1715;
    for(size_t k = 0; k <= ITEM_COUNT(swBands); k++)
    {
      uint32_t end = k < ITEM_COUNT(swBands) ? swBands[k].lo - 5 : 30000;
      while(cur <= end)
      {
        uint32_t stop = cur + 1000 - stK < end ? cur + 1000 - stK : end;
        add(GROUP_SW, cur * 1000UL, stop * 1000UL, sp.swRestStep, 0);
        cur = stop + stK;
      }
      if(k < ITEM_COUNT(swBands)) cur = swBands[k].hi + 5;
    }
  }
  return n;
}

static uint16_t segFreqs(const Segment &s, uint32_t *out)
{
  uint16_t n = 0;
  if(!s.dual)
  {
    for(uint32_t f = s.lo; f <= s.hi && n < SEG_MAX; f += s.step) { if(out) out[n] = f; n++; }
    return n;
  }
  uint32_t a = 520000, b = 531000;
  while((a <= 1710000 || b <= 1701000) && n < SEG_MAX)
  {
    uint32_t f;
    if(b > 1701000 || (a <= 1710000 && a < b)) { f = a; a += 10000; }
    else if(a > 1710000 || b < a)              { f = b; b += 9000; }
    else                                       { f = a; a += 10000; b += 9000; }
    if(out) out[n] = f;
    n++;
  }
  return n;
}

uint32_t scanPlanChannels()
{
  static Segment segs[96];
  uint8_t ns = buildPlan(segs, ITEM_COUNT(segs), partsNow(), speedNow());
  uint32_t total = 0;
  for(uint8_t i = 0; i < ns; i++) total += segFreqs(segs[i], nullptr);
  return total;
}

//
// Learned timing: ms per channel and per candidate, for each group and speed
//
struct Timing { uint8_t version; uint8_t pad; uint16_t ch[3][3]; uint16_t cand[3][3]; };
static Timing timing;
static bool timingLoaded = false, timingDirty = false;

static void timingLoad()
{
  if(timingLoaded) return;
  timingLoaded = true;
  size_t got = 0;
  if(storeLoad("scantime", &timing, sizeof(timing), &got) && got == sizeof(timing) && timing.version == 1) return;
  static const uint16_t ch[3][3]   = { { 12, 17, 27 }, { 30, 32, 40 }, { 12, 16, 28 } };       // [group][speed], guesses until learned
  static const uint16_t cand[3][3] = { { 800, 1200, 1800 }, { 200, 450, 700 }, { 300, 550, 1000 } };
  timing.version = 1;
  memcpy(timing.ch, ch, sizeof(ch));
  memcpy(timing.cand, cand, sizeof(cand));
}

static void timingLearn(uint16_t &slot, uint32_t ms, uint32_t count)
{
  if(!count) return;
  uint32_t v = ms / count;
  if(v < 1 || v > 60000) return;
  slot = (uint16_t)((slot * 3 + v) / 4);    // smooth over a few searches
  timingDirty = true;
}

// Stations expected in a segment: what the list holds there, or a typical count
static uint32_t expected(const Segment &s)
{
  uint32_t n = 0;
  for(uint16_t i = 0; i < stationCount; i++)
    if(stations[i].group == s.group && stations[i].freq >= s.lo && stations[i].freq <= s.hi) n++;
  if(n || stationCount) return n + (s.group == GROUP_SW ? 1 : 0);
  if(s.group == GROUP_FM) return (s.hi - s.lo) / 1500000;
  if(s.group == GROUP_AM) return s.lo < 500000 ? 1 : 10;
  return (s.hi - s.lo) / 150000 + 1;
}

uint32_t scanEstimate(uint8_t parts, uint8_t speed, uint32_t *channels)
{
  timingLoad();
  static Segment segs[96];
  if(speed > 2) speed = SPEED_NORMAL;
  uint8_t ns = buildPlan(segs, ITEM_COUNT(segs), parts & SCAN_ALL, speed);
  uint32_t ms = 1500, total = 0;
  for(uint8_t i = 0; i < ns; i++)
  {
    uint32_t n = segFreqs(segs[i], nullptr);
    total += n;
    uint8_t g = segs[i].group;
    ms += n * timing.ch[g][speed] + expected(segs[i]) * timing.cand[g][speed];
  }
  if(channels) *channels = total;
  return (ms + 500) / 1000;
}

static uint8_t median(const uint8_t *v, uint16_t n, uint8_t *tmp)
{
  memcpy(tmp, v, n);
  for(uint16_t i = 1; i < n; i++)
  {
    uint8_t k = tmp[i]; int j = i - 1;
    while(j >= 0 && tmp[j] > k) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = k;
  }
  return n ? tmp[n / 2] : 0;
}

static void remember(uint32_t f, uint8_t g)
{
  ScanState &s = scanState;
  if(s.recentCount < 3) { s.recent[s.recentCount] = f; s.recentGroup[s.recentCount++] = g; return; }
  for(int i = 0; i < 2; i++) { s.recent[i] = s.recent[i + 1]; s.recentGroup[i] = s.recentGroup[i + 1]; }
  s.recent[2] = f; s.recentGroup[2] = g;
}

static bool aborted = false;

// Carriers found during this run, to merge the same carrier reached from two segments
static Station *runFound = nullptr;   // PSRAM, 512 entries
static uint16_t runFoundN = 0;
static bool seenThisRun(uint32_t f, uint32_t tol)
{
  for(uint16_t k = 0; k < runFoundN; k++)
    if((runFound[k].freq > f ? runFound[k].freq - f : f - runFound[k].freq) <= tol) return true;
  return false;
}
static bool checkAbort() { if(!aborted && inputAbort()) aborted = true; return aborted; }

// Several readings after the AGC settled; the best three are averaged
static void settledReading(uint8_t &r, uint8_t &s, uint32_t settleMs)
{
  const Speed &sp = SP();
  uint8_t n = sp.reads < 3 ? 3 : sp.reads > 5 ? 5 : sp.reads;
  delay(settleMs);
  uint8_t rr[5], ss[5];
  for(uint8_t k = 0; k < n; k++)
  {
    delay(sp.readGap);
    radioRead(rr[k], ss[k]);
  }
  for(uint8_t a = 0; a < n; a++)
    for(uint8_t b = a + 1; b < n; b++)
    {
      if(rr[b] > rr[a]) { uint8_t t = rr[a]; rr[a] = rr[b]; rr[b] = t; }
      if(ss[b] > ss[a]) { uint8_t t = ss[a]; ss[a] = ss[b]; ss[b] = t; }
    }
  r = (rr[0] + rr[1] + rr[2]) / 3;
  s = (ss[0] + ss[1] + ss[2]) / 3;
}

static bool scanSegment(const Segment &seg, uint32_t *bf, uint8_t *br, uint8_t *bs, uint8_t *bv, uint8_t *tmp,
                        Station *found, uint16_t &nFound)
{
  const Sens &S = sensTable[cfg.scanSens < 4 ? cfg.scanSens : 0];
  const Speed &sp = SP();
  bool maxSens = cfg.scanSens == 0;
  uint32_t tSweep = millis();
  bool fm = seg.group == GROUP_FM;
  bool wide = seg.group == GROUP_SW && cfg.scanOffGrid;
  uint16_t n = segFreqs(seg, bf);
  nFound = 0;

  scanState.group = seg.group;
  scanState.confirmTotal = scanState.confirmDone = 0;
  scanState.phase = "Sweeping";
  radioScanFilter(wide);

  // 1. Sweep
  for(uint16_t i = 0; i < n; i++)
  {
    uint8_t r = 0, s = 0; bool v = false;
    if(!radioMeasure(bf[i], r, s, v)) { r = s = 0; v = false; }
    else if(maxSens && sp.secondRead)
    {
      // Second, slightly later reading: weak signals settle after the tune
      uint8_t r2, s2;
      delay(8);
      radioRead(r2, s2);
      if(r2 > r) r = r2;
      if(s2 > s) s = s2;
    }
    br[i] = r; bs[i] = s; bv[i] = v;
    scanState.freq = bf[i];
    scanState.done++;
    if(checkAbort()) return false;
    uiDrawScan();
  }

  timingLearn(timing.ch[seg.group][speedNow()], millis() - tSweep, n);

  // 2. Thresholds relative to the noise floor of this segment
  uint8_t floorR = median(br, n, tmp);
  uint8_t thR = fm ? S.fmR : S.amR;
  uint8_t d = fm ? S.fmD : S.amD;
  if(floorR + d > thR) thR = floorR + d;
  uint8_t thS = fm ? S.fmS : S.amS;
  radioScanThresholds(seg.group, thR, thS);

  // 3. Second chance for near misses (fading)
  uint8_t retries = 0;
  uint8_t maxRetries = maxSens ? sp.retries : (sp.retries > 32 ? 32 : sp.retries);
  for(uint16_t i = 0; i < n && retries < maxRetries; i++)
  {
    bool pass = br[i] >= thR && bs[i] >= thS;
    bool near = br[i] + d / 2 >= thR && bs[i] + 2 >= thS;
    if(pass || !near || stationIsBlocked(bf[i])) continue;
    uint8_t r, s; bool v;
    if(radioMeasure(bf[i], r, s, v))
    {
      uint8_t r2, s2;
      settledReading(r2, s2, 80);
      if(r2 > br[i]) br[i] = r2;
      if(s2 > bs[i]) bs[i] = s2;
      if(v) bv[i] = 1;
    }
    retries++;
    if(checkAbort()) return false;
  }

  // 4. Local peaks only
  uint32_t win = fm ? 100000 : seg.dual ? 6000 : seg.step;
  uint32_t shadow = fm ? 0 : 10000;
  uint32_t reach = win > shadow ? win : shadow;
  uint32_t grid = cfg.region == 0 ? 10000 : 9000;
  auto key = [&](uint16_t k) -> int
  {
    int pref = !seg.dual ? 0 : (grid == 10000 ? !(bf[k] % 10000) : !((bf[k] - 531000) % 9000));
    return (br[k] + bs[k]) * 2 + pref;
  };

  static uint16_t cand[SEG_MAX];
  uint16_t nc = 0;
  for(uint16_t i = 0; i < n; i++)
  {
    if(br[i] < thR || bs[i] < thS || stationIsBlocked(bf[i])) continue;
    if(fm && !bv[i] && (br[i] < thR + (maxSens ? 6 : 10) || bs[i] < thS + (maxSens ? 3 : 5))) continue;
    int score = key(i), raw = br[i] + bs[i];
    bool peak = true;
    for(int j = i - 1; peak && j >= 0 && bf[i] - bf[j] <= reach; j--)
    {
      if(bf[i] - bf[j] <= win && key(j) >= score) peak = false;
      if(bf[i] - bf[j] <= shadow && br[j] + bs[j] >= raw + 24) peak = false;
    }
    for(uint16_t j = i + 1; peak && j < n && bf[j] - bf[i] <= reach; j++)
    {
      if(bf[j] - bf[i] <= win && key(j) > score) peak = false;
      if(bf[j] - bf[i] <= shadow && br[j] + bs[j] >= raw + 24) peak = false;
    }
    if(peak) cand[nc++] = i;
  }

  // 5. Confirm
  if(wide) radioScanFilter(false);
  scanState.phase = "Checking";
  scanState.confirmTotal = nc;
  uint32_t tConfirm = millis();
  for(uint16_t k = 0; k < nc && nFound < 256; k++)
  {
    uint32_t f = bf[cand[k]];
    scanState.confirmDone = k + 1;
    scanState.freq = f;
    uiDrawScan(true);

    // Exact centre for shortwave (off-grid stations)
    if(seg.group == GROUP_SW && cfg.scanOffGrid)
    {
      uint32_t best = f; int bestScore = -1;
      for(int off = -sp.centreSpan * 1000; off <= sp.centreSpan * 1000; off += 1000)
      {
        uint8_t r, s; bool v;
        if(!radioMeasure(f + off, r, s, v)) continue;
        delay(sp.centreDwell);
        radioRead(r, s);
        int sc = r + s * 2 - (off ? 1 : 0);
        if(sc > bestScore) { bestScore = sc; best = f + off; }
        if(checkAbort()) return false;
      }
      f = best;
    }

    // Already found (two candidates converged on the same carrier)
    bool dup = false;
    for(uint16_t q = 0; q < nFound; q++)
      if((found[q].freq > f ? found[q].freq - f : f - found[q].freq) <= 1000) dup = true;
    if(seg.group == GROUP_SW && seenThisRun(f, 2000)) dup = true;
    if(dup || stationIsBlocked(f)) continue;

    uint8_t r, s; bool v;
    if(!radioMeasure(f, r, s, v)) continue;
    settledReading(r, s, fm ? sp.settleFm : sp.settleAm);
    if(r < thR || s < thS) { if(checkAbort()) return false; continue; }

    Station st;
    memset(&st, 0, sizeof(st));
    st.freq = f; st.group = seg.group; st.mode = fm ? MODE_FM : MODE_AM;
    st.rssi = r; st.snr = s;

    // FM: give RDS a moment for the station name
    if(fm && cfg.rds)
    {
      radioRdsReset();
      uint32_t t0 = millis();
      while(millis() - t0 < sp.rdsMax)
      {
        radioPoll();
        if(rdsName()[0]) { snprintf(st.name, ST_NAME_LEN, "%s", rdsName()); break; }
        // No RDS synchronisation after a moment: the station has no RDS
        if(millis() - t0 > sp.rdsBail && !radioRdsSync()) break;
        if(checkAbort()) return false;
        delay(40);
        uiDrawScan();
      }
    }

    found[nFound++] = st;
    if(runFound && runFoundN < 512) runFound[runFoundN++] = st;
    scanState.found++;
    remember(f, seg.group);
    if(checkAbort()) return false;
  }
  timingLearn(timing.cand[seg.group][speedNow()], millis() - tConfirm, nc);
  return true;
}

static Segment segs[96];
static uint8_t segCount = 0, segIdx = 0;
static int8_t configured = -1;
static bool running = false, background = false, markNew = false;
static uint32_t *bf = nullptr;
static uint8_t *br, *bs, *bv, *tmp;
static Station *found;

bool scanActive() { return running; }
bool scanWasAborted() { return aborted; }

void scanStart(bool bg)
{
  if(!bf)
  {
    bf    = static_cast<uint32_t *>(ps_malloc(SEG_MAX * sizeof(uint32_t)));
    br    = static_cast<uint8_t *>(ps_malloc(SEG_MAX));
    bs    = static_cast<uint8_t *>(ps_malloc(SEG_MAX));
    bv    = static_cast<uint8_t *>(ps_malloc(SEG_MAX));
    tmp   = static_cast<uint8_t *>(ps_malloc(SEG_MAX));
    found = static_cast<Station *>(ps_malloc(256 * sizeof(Station)));
    runFound = static_cast<Station *>(ps_malloc(512 * sizeof(Station)));
    if(!bf || !br || !bs || !bv || !tmp || !found || !runFound) { bf = nullptr; return; }
  }
  timingLoad();
  segCount = buildPlan(segs, ITEM_COUNT(segs), partsNow(), speedNow());
  segIdx = 0;
  configured = -1;
  background = bg;
  markNew = stationCount > 0;       // the very first scan is not "new"
  memset(&scanState, 0, sizeof(scanState));
  for(uint8_t i = 0; i < segCount; i++) scanState.total += segFreqs(segs[i], nullptr);
  scanState.startedAt = millis();
  scanState.phase = "Starting";
  aborted = false;
  runFoundN = 0;
  inputAbort();                     // drop any pending input
  if(!background) { backlight(255); uiDrawScan(true); }   // full brightness: no PWM noise
  running = true;
}

static void scanFinish()
{
  running = false;
  ovParts = 0; ovSpeed = -1;          // a one-off choice lasts one search
  if(timingDirty) { storeSave("scantime", &timing, sizeof(timing)); timingDirty = false; }
  radioScanEnd();
  if(!background) backlightRestore();
  stationsSave();
  viewRebuild();
}

bool scanStep()
{
  if(!running) return false;
  if(segIdx >= segCount || aborted) { scanFinish(); return false; }
  const Segment &seg = segs[segIdx];
  if(configured != seg.group)
  {
    scanState.group = seg.group;
    scanState.freq = seg.lo;
    uiDrawScan(true);
    radioScanBegin(seg.group, seg.group == GROUP_SW && cfg.scanOffGrid);
    radioScanDelay(seg.group == GROUP_FM ? SP().fmDelay : seg.group == GROUP_AM ? 25 : SP().swDelay);
    configured = seg.group;
  }
  uint16_t nFound = 0;
  if(scanSegment(seg, bf, br, bs, bv, tmp, found, nFound))
  {
    uint32_t lo = seg.lo > 3000 ? seg.lo - 3000 : seg.lo, hi = seg.hi + 3000;
    // Stations already found by a neighbouring segment of this run stay
    for(uint16_t k = 0; k < runFoundN && nFound < 256; k++)
    {
      const Station &r = runFound[k];
      if(r.group != seg.group || r.freq < lo || r.freq > hi) continue;
      bool have = false;
      for(uint16_t q = 0; q < nFound; q++) if(found[q].freq == r.freq) { have = true; break; }
      if(!have) found[nFound++] = r;
    }
    stationReplaceRange(seg.group, lo, hi, found, nFound, markNew);
  }
  segIdx++;
  if(aborted || segIdx >= segCount) { scanFinish(); return false; }
  return true;
}

bool scanRun()
{
  scanStart(false);
  while(scanStep()) {}
  return !aborted;
}
