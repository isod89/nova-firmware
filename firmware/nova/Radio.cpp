//
// Radio.cpp - SI4732 receiver engine
//
// One continuous tuning range per modulation (no band table):
//   FM       64.0 - 108.0 MHz
//   AM/SSB   150 kHz - 30 MHz
// SSB/CW use the SI4732 SSB patch. The receiver is tuned on a 28 kHz grid and
// the BFO covers the rest, which gives smooth 10 Hz tuning.
//
#include "Nova.h"
#include <Wire.h>
#include "patch_init.h"

SI4735_fixed rx;

#define FM_MIN_10K   6400
#define FM_MAX_10K  10800
#define AM_MIN_KHZ    150
#define AM_MAX_KHZ  30000
#define MAX_BFO     14000          // Hz, SSB fine tuning range around the receiver grid
#define CW_OFFSET     700          // Hz, CW tone

static uint32_t curHz = 99100000;
static uint8_t  curMode = MODE_FM;
static bool     ssbLoaded = false;
static uint8_t  muteDepth = 0;
static bool     squelched = false;
static Signal   sig = { 0, 0, false };

static char rdsPS[16] = "";
static char rdsRT[72] = "";
static uint32_t lastClockSync = 0;

//
// Steps, bandwidths, names
//
static const StepDef fmSteps[]  = { {10000, "10k"}, {50000, "50k"}, {100000, "100k"}, {200000, "200k"}, {1000000, "1M"} };   // labels fit the chip
static const StepDef amSteps[]  = { {1000, "1k"}, {5000, "5k"}, {9000, "9k"}, {10000, "10k"}, {100000, "100k"}, {1000000, "1M"} };
static const StepDef ssbSteps[] = { {10, "10"}, {50, "50"}, {100, "100"}, {500, "500"}, {1000, "1k"}, {5000, "5k"} };

// Bandwidth tables: chip index and label, ordered from wide to narrow
struct BwDef { uint8_t chip; const char *label; };
static const BwDef fmBw[]  = { {0, "Auto"}, {1, "110k"}, {2, "84k"}, {3, "60k"}, {4, "40k"} };
static const BwDef amBw[]  = { {0, "6.0k"}, {1, "4.0k"}, {2, "3.0k"}, {6, "2.5k"}, {3, "2.0k"}, {5, "1.8k"}, {4, "1.0k"} };
static const BwDef ssbBw[] = { {3, "4.0k"}, {2, "3.0k"}, {1, "2.2k"}, {0, "1.2k"}, {5, "1.0k"}, {4, "0.5k"} };

static const BwDef *bwTable(uint8_t mode, uint8_t &n)
{
  if(mode == MODE_FM) { n = ITEM_COUNT(fmBw); return fmBw; }
  if(mode == MODE_AM) { n = ITEM_COUNT(amBw); return amBw; }
  n = ITEM_COUNT(ssbBw); return ssbBw;
}

const StepDef *stepList(uint8_t mode, uint8_t &count)
{
  if(mode == MODE_FM) { count = ITEM_COUNT(fmSteps); return fmSteps; }
  if(mode == MODE_AM) { count = ITEM_COUNT(amSteps); return amSteps; }
  count = ITEM_COUNT(ssbSteps); return ssbSteps;
}

uint32_t currentStep()
{
  uint8_t n;
  const StepDef *s = stepList(curMode, n);
  uint8_t i = cfg.stepIdx[curMode] < n ? cfg.stepIdx[curMode] : 0;
  return s[i].hz;
}

const char *bandwidthLabel(uint8_t mode, uint8_t idx)
{
  uint8_t n;
  const BwDef *t = bwTable(mode, n);
  return t[idx < n ? idx : 0].label;
}

uint8_t bandwidthCount(uint8_t mode) { uint8_t n; bwTable(mode, n); return n; }

static uint8_t bwChip(uint8_t mode)
{
  uint8_t n;
  const BwDef *t = bwTable(mode, n);
  return t[cfg.bwIdx[mode] < n ? cfg.bwIdx[mode] : 0].chip;
}

const char *modeName(uint8_t mode)
{
  static const char *const names[] = { "FM", "LSB", "USB", "AM", "CW" };
  return mode < MODE_COUNT ? names[mode] : "?";
}

uint32_t radioMinFreq(uint8_t mode) { return mode == MODE_FM ? FM_MIN_10K * 10000UL : AM_MIN_KHZ * 1000UL; }
uint32_t radioMaxFreq(uint8_t mode) { return mode == MODE_FM ? FM_MAX_10K * 10000UL : AM_MAX_KHZ * 1000UL; }
uint32_t radioFreq() { return curHz; }
uint8_t  radioMode() { return curMode; }
bool     radioSSBLoaded() { return ssbLoaded; }
Signal   radioSignal() { return sig; }

//
// Audio mute: temporary mutes nest; squelch is separate
//
static bool hwMuted = false;
static bool sweeping = false;       // Ghost Box sweep running
static int8_t agcOverride = -1;     // Ghost Box antenna attenuation, -1 = settings
static int8_t smOverride = -1;      // Ghost Box noise control (soft mute), -1 = settings

static void applyMute()
{
  bool want = muteDepth > 0 || squelched;
  if(want == hwMuted) return;
  hwMuted = want;
  if(want)
  {
    if(cfg.audioClamp) digitalWrite(AUDIO_MUTE, HIGH);
    else digitalWrite(PIN_AMP_EN, LOW);
    delay(20);
    rx.setAudioMute(true);
  }
  else
  {
    if(cfg.audioClamp) digitalWrite(AUDIO_MUTE, LOW);
    delay(20);
    rx.setAudioMute(false);
    if(!cfg.audioClamp) digitalWrite(PIN_AMP_EN, HIGH);
  }
}

void radioMute(bool on)
{
  if(on) muteDepth++;
  else if(muteDepth) muteDepth--;
  applyMute();
}

void radioSetVolume(uint8_t v)
{
  cfg.volume = v > 63 ? 63 : v;
  rx.setVolume(cfg.volume);
}

//
// Tuning helpers
//
static uint32_t ssbReceiverHz(uint32_t hz)
{
  const uint32_t spacing = 2 * MAX_BFO;
  uint32_t r = ((hz + MAX_BFO) / spacing) * spacing;
  if(r < AM_MIN_KHZ * 1000UL) r = AM_MIN_KHZ * 1000UL;
  if(r > AM_MAX_KHZ * 1000UL) r = AM_MAX_KHZ * 1000UL;
  return r;
}

static uint32_t ssbTuneHz(uint32_t hz) { return curMode == MODE_CW ? hz - CW_OFFSET : hz; }

static uint16_t chipUnits(uint32_t hz)
{
  if(curMode == MODE_FM) return hz / 10000;
  if(modeIsSSB(curMode)) return ssbReceiverHz(ssbTuneHz(hz)) / 1000;
  return hz / 1000;
}

static void applyBFO()
{
  int32_t tune = (int32_t)ssbTuneHz(curHz);
  int32_t bfo = tune - (int32_t)ssbReceiverHz(tune);
  rx.setSSBBfo(-(bfo + cfg.calSSB));
}

void radioApplyAudio()
{
  // Bandwidth
  switch(curMode)
  {
    case MODE_FM: rx.setFmBandwidth(bwChip(MODE_FM)); break;
    case MODE_AM: rx.setBandwidth(bwChip(MODE_AM), 1); break;
    default:
    {
      uint8_t b = bwChip(curMode);
      rx.setSSBAudioBandwidth(b);
      rx.setSSBSidebandCutoffFilter(b == 0 || b == 4 || b == 5 ? 0 : 1);
      break;
    }
  }

  // AGC / attenuation
  uint8_t a = curMode == MODE_FM ? cfg.agc[0] : curMode == MODE_AM ? cfg.agc[1] : cfg.agc[2];
  if(agcOverride >= 0) a = agcOverride;
  rx.setAutomaticGainControl(a > 0 ? 1 : 0, a > 1 ? a - 1 : 0);

  if(curMode != MODE_FM)
  {
    uint8_t s = modeIsSSB(curMode) ? 1 : 0;
    rx.setAvcAmMaxGain(cfg.avc[s]);
    rx.setAmSoftMuteMaxAttenuation(smOverride >= 0 ? smOverride : cfg.softMute[s]);
  }
  else
  {
    rx.setFmSoftMuteMaxAttenuation(smOverride >= 0 ? smOverride : 16);   // 16 dB is the chip's default
    rx.setFMDeEmphasis(cfg.region == 0 ? 0x2 : 0x1);
    if(cfg.fmStereo >= 2) rx.setFmStereoOff();
    else
    {
      rx.setFmStereoOn();
      // Stereo/mono blend (AN332 FM_BLEND_*): Smart blends to mono much
      // earlier, so weak stereo stations stay clean instead of hissing
      bool smart = cfg.fmStereo == 1;
      rx.setFmBlendRssiStereoThreshold(smart ? 55 : 49);
      rx.setFmBLendRssiMonoThreshold(smart ? 38 : 30);
      rx.setFmBlendSnrStereoThreshold(smart ? 33 : 27);
      rx.setFmBLendSnrMonoThreshold(smart ? 20 : 14);
      rx.setFmBlendMultiPathStereoThreshold(smart ? 10 : 20);
      rx.setFmBlendMultiPathMonoThreshold(smart ? 40 : 60);
    }
  }

  rx.setVolume(cfg.volume);
}

void radioApplyStep()
{
  if(curMode == MODE_FM)
  {
    uint32_t st = currentStep();
    rx.setFrequencyStep(st / 10000 ? st / 10000 : 1);
    rx.setSeekFmSpacing(st >= 100000 ? 10 : st >= 50000 ? 5 : 1);
  }
  else if(curMode == MODE_AM)
  {
    uint32_t st = currentStep();
    rx.setFrequencyStep(st / 1000 ? (st >= 1000000 ? 1000 : st / 1000) : 1);
    rx.setSeekAmSpacing(cfg.region == 0 ? 10 : 9);
  }
  else rx.setFrequencyStep(0);
}

// Configure the chip for a modulation and tune it (no unmute)
static void setupChip(uint8_t mode, uint32_t hz, bool antennaAuto)
{
  curMode = mode;
  curHz = hz;

  if(mode == MODE_FM)
  {
    ssbLoaded = false;
    uint32_t st = currentStep();
    rx.setFM(FM_MIN_10K, FM_MAX_10K, hz / 10000, st / 10000 ? st / 10000 : 10);
    rx.setSeekFmLimits(FM_MIN_10K, FM_MAX_10K);
    rx.setSeekFmRssiThreshold(5);
    rx.setSeekFmSNRThreshold(2);
    rx.RdsInit();
    rx.setRdsConfig(cfg.rds ? 1 : 0, 2, 2, 2, 2);
    rx.setGpioCtl(1, 0, 0);
    rx.setGpio(0, 0, 0);
  }
  else
  {
    if(modeIsSSB(mode))
    {
      if(!ssbLoaded)
      {
        uiMessage("Loading SSB", "One moment...");
        rx.loadPatch(ssb_patch_content, sizeof(ssb_patch_content), bwChip(mode));
        ssbLoaded = true;
      }
      rx.setSSB(AM_MIN_KHZ, AM_MAX_KHZ, chipUnits(hz), 0, mode == MODE_LSB ? 1 : 2);
      rx.setSSBConfig(bwChip(mode), 1, 0, 1, 0, 1);
    }
    else
    {
      ssbLoaded = false;
      rx.setAM(AM_MIN_KHZ, AM_MAX_KHZ, hz / 1000, 5);
      rx.setSeekAmRssiThreshold(10);
      rx.setSeekAmSNRThreshold(3);
    }

    bool changed = rx.configureAntennaCapacitor(antennaAuto && hz < 1800000);
    uint16_t units = chipUnits(hz);
    if(changed) rx.setFrequencyConfirmed(units); else rx.waitFrequencyConfirmed(units);
    if(modeIsSSB(mode)) applyBFO();

    rx.setGpioCtl(1, 0, 0);
    rx.setGpio(1, 0, 0);
    rx.setSeekAmLimits(AM_MIN_KHZ, AM_MAX_KHZ);
  }
}

void radioApply()
{
  radioMute(true);
  uint32_t hz = cfg.freq;
  uint8_t mode = cfg.mode < MODE_COUNT ? cfg.mode : (uint8_t)MODE_FM;
  if(hz < radioMinFreq(mode) || hz > radioMaxFreq(mode)) hz = mode == MODE_FM ? 99100000 : 9650000;
  setupChip(mode, hz, cfg.antennaAuto);
  radioApplyStep();
  radioApplyAudio();
  delay(60);
  rdsClear();
  sig = { 0, 0, false };
  radioMute(false);
}

bool radioSetFreq(uint32_t hz)
{
  if(hz < radioMinFreq(curMode)) hz = radioMinFreq(curMode);
  if(hz > radioMaxFreq(curMode)) hz = radioMaxFreq(curMode);

  if(modeIsSSB(curMode))
  {
    uint16_t before = chipUnits(curHz);
    curHz = hz;
    uint16_t after = chipUnits(hz);
    if(before != after)
    {
      rx.setFrequencyConfirmed(after);
      radioApplyAudio();     // the SSB AGC must follow a receiver retune
    }
    applyBFO();
  }
  else
  {
    if(curMode == MODE_AM)
    {
      // Antenna tuning differs between LW/MW and SW
      bool want = cfg.antennaAuto && hz < 1800000;
      bool was = cfg.antennaAuto && curHz < 1800000;
      if(want != was) rx.configureAntennaCapacitor(want);
    }
    curHz = hz;
    if(!rx.setFrequencyConfirmed(chipUnits(hz))) return false;
    if(curMode == MODE_FM) { rx.RdsInit(); rdsClear(); }
  }

  cfg.freq = curHz;
  cfg.mode = curMode;
  configSave();
  return true;
}

bool radioTune(uint32_t hz, uint8_t mode)
{
  if(mode >= MODE_COUNT) mode = MODE_AM;
  // FM frequencies are only valid in FM, and the other way round
  if(mode == MODE_FM && hz < radioMinFreq(MODE_FM)) mode = MODE_AM;
  if(mode != MODE_FM && hz > radioMaxFreq(MODE_AM)) mode = MODE_FM;

  if(mode == curMode)
  {
    radioMute(true);
    bool ok = radioSetFreq(hz);
    radioMute(false);
    return ok;
  }

  cfg.freq = hz;
  cfg.mode = mode;
  radioApply();
  configSave();
  return true;
}

//
// Signal, squelch, RDS
//
static void trimCopy(char *dst, size_t size, const char *src)
{
  size_t o = 0;
  while(*src == ' ') src++;
  for(; *src && o + 1 < size; src++)
  {
    uint8_t c = *src;
    if(c == '\r' || c == '\n') break;
    dst[o++] = (c >= 0x20 && c < 0x7F) ? c : ' ';
  }
  while(o && dst[o - 1] == ' ') o--;
  dst[o] = '\0';
}

const char *rdsName() { return rdsPS; }
const char *rdsText() { return rdsRT; }
void rdsClear() { rdsPS[0] = rdsRT[0] = '\0'; }
void radioRdsReset() { rx.RdsInit(); rdsClear(); }
bool radioRdsSync() { rx.getRdsStatus(0, 0, 1); return rx.getRdsSync(); }   // status only, keeps the FIFO

static bool pollRds()
{
  if(curMode != MODE_FM || !cfg.rds) return false;
  bool changed = false;
  for(uint8_t i = 0; i < 25; i++)
  {
    rx.getRdsStatus();
    uint8_t count = rx.getNumRdsFifoUsed();
    if(!count) break;

    const char *ps = rx.getRdsStationName();
    if(ps)
    {
      char t[16];
      trimCopy(t, sizeof(t), ps);
      if(t[0] && strcmp(t, rdsPS)) { strcpy(rdsPS, t); changed = true; }
    }
    const char *rt = rx.getRdsVersionCode() ? rx.getRdsText2B() : rx.getRdsText2A();
    if(rt)
    {
      char t[72];
      trimCopy(t, sizeof(t), rt);
      if(t[0] && strcmp(t, rdsRT)) { strcpy(rdsRT, t); changed = true; }
    }
    uint32_t epoch;
    if(cfg.autoClock && rx.getRdsUTCEpoch(&epoch) && epoch > 1700000000UL &&
       (!lastClockSync || millis() - lastClockSync > 600000UL))
    {
      clockSet(epoch);
      lastClockSync = millis();
      changed = true;
    }
    if(count == 1) break;
  }
  return changed;
}

bool radioPoll()
{
  static uint32_t lastRsq = 0, lastRds = 0;
  uint32_t now = millis();
  bool changed = false;

  if(now - lastRsq >= 200)
  {
    lastRsq = now;
    rx.getCurrentReceivedSignalQuality();
    Signal s = { rx.getCurrentRSSI(), rx.getCurrentSNR(), curMode == MODE_FM && rx.getCurrentPilot() };
    if(s.rssi != sig.rssi || s.snr != sig.snr || s.stereo != sig.stereo) changed = true;
    sig = s;

    bool sq = !sweeping && cfg.squelch && sig.snr < cfg.squelch;
    if(sq != squelched) { squelched = sq; applyMute(); }
  }

  if(!sweeping && now - lastRds >= 250)
  {
    lastRds = now;
    changed |= pollRds();
  }
  return changed;
}

//
// Seek (AM/FM): the chip searches by itself
//
static void seekShow(uint16_t f)
{
  curHz = curMode == MODE_FM ? f * 10000UL : f * 1000UL;
  uiDraw();
}

static bool seekStopCheck() { return inputAbort(); }

bool radioSeek(int dir)
{
  if(modeIsSSB(curMode)) return false;
  radioMute(true);
  rdsClear();
  rx.setMaxSeekTime(120000);
  rx.seekStationProgress(seekShow, seekStopCheck, dir > 0 ? 1 : 0);
  uint16_t f = rx.getCurrentFrequency();
  uint32_t hz = curMode == MODE_FM ? f * 10000UL : f * 1000UL;
  radioSetFreq(hz);
  radioMute(false);
  return true;
}

//
// Ghost Box sweep: audible, fast, nothing written to the settings
//
void radioSweepBegin()
{
  sweeping = true;
  rdsClear();
  rx.setMaxDelaySetFrequency(curMode == MODE_FM ? 20 : 25);
}

bool radioSweepTo(uint32_t hz)
{
  if(hz < radioMinFreq(curMode) || hz > radioMaxFreq(curMode)) return false;
  if(curMode == MODE_AM)
  {
    bool want = cfg.antennaAuto && hz < 1800000;
    bool was = cfg.antennaAuto && curHz < 1800000;
    if(want != was) rx.configureAntennaCapacitor(want);
  }
  curHz = hz;
  return rx.setFrequencyConfirmed(chipUnits(hz));
}

void radioGate(bool mute)
{
  // Quick chip mute only (no clamp delays): used between steps
  if(!hwMuted) rx.setAudioMute(mute);
}

void radioSoftMuteOverride(int8_t value)
{
  if(value == smOverride) return;
  smOverride = value;
  radioApplyAudio();
}

void radioAgcOverride(int8_t value)
{
  agcOverride = value;
  radioApplyAudio();
}

void radioSweepEnd()
{
  sweeping = false;
  if(!hwMuted) rx.setAudioMute(false);
  agcOverride = -1;
  smOverride = -1;
  rx.setMaxDelaySetFrequency(30);
}

//
// Scanner helpers
//
static bool scanMuted = false;

void radioScanBegin(uint8_t group, bool wideFilter)
{
  // Called once per group (FM, AM, SW): mute only once for the whole scan
  if(!scanMuted) { radioMute(true); scanMuted = true; }
  setupChip(group == GROUP_FM ? MODE_FM : MODE_AM,
            group == GROUP_FM ? 87500000 : group == GROUP_AM ? 520000 : 5000000,
            group == GROUP_AM);
  if(group != GROUP_FM) rx.setBandwidth(wideFilter ? 0 : 2, 1);
  rx.setAutomaticGainControl(0, 0);
  // Keep the I2C bus quiet while the chip tunes, then poll for completion
  rx.setMaxDelaySetFrequency(group == GROUP_FM ? 15 : group == GROUP_AM ? 25 : 20);
}

void radioScanDelay(uint8_t ms) { rx.setMaxDelaySetFrequency(ms); }

SignalFull radioSignalFull()
{
  rx.getCurrentReceivedSignalQuality();
  SignalFull f;
  bool fm = curMode == MODE_FM;
  f.rssi = rx.getCurrentRSSI();
  f.snr = rx.getCurrentSNR();
  f.mult = fm ? rx.getCurrentMultipath() : 0;
  f.blend = fm ? rx.getCurrentStereoBlend() : 0;
  f.pilot = fm && rx.getCurrentPilot();
  f.offset = (int8_t)rx.getCurrentSignedFrequencyOffset();
  return f;
}

void radioScanFilter(bool wide)
{
  if(curMode == MODE_AM) rx.setBandwidth(wide ? 0 : 2, 1);
}

void radioScanThresholds(uint8_t group, uint8_t r, uint8_t s)
{
  if(group == GROUP_FM) { rx.setSeekFmRssiThreshold(r); rx.setSeekFmSNRThreshold(s); }
  else { rx.setSeekAmRssiThreshold(r); rx.setSeekAmSNRThreshold(s); }
}

bool radioMeasure(uint32_t hz, uint8_t &r, uint8_t &s, bool &valid)
{
  curHz = hz;
  rx.setFrequency(chipUnits(hz));
  uint32_t started = millis();
  while(!rx.pollTuneStatus())
  {
    if(millis() - started >= TUNE_TIMEOUT_MS) return false;
    delay(5);
  }
  valid = rx.getStatusValid();
  rx.getCurrentReceivedSignalQuality();
  r = rx.getCurrentRSSI();
  s = rx.getCurrentSNR();
  return true;
}

void radioRead(uint8_t &r, uint8_t &s)
{
  rx.getCurrentReceivedSignalQuality();
  r = rx.getCurrentRSSI();
  s = rx.getCurrentSNR();
}

void radioScanEnd()
{
  rx.setMaxDelaySetFrequency(30);
  radioApply();
  if(scanMuted) { radioMute(false); scanMuted = false; }
}

//
// Band scope: measure n channels around a centre frequency
//
void radioScope(uint32_t center, uint32_t step, uint16_t n, uint8_t *r, uint8_t *s, bool (*abortFn)())
{
  uint8_t mode = curMode;
  uint32_t keep = curHz;
  uint8_t group = mode == MODE_FM ? GROUP_FM : (center < 1711000 ? GROUP_AM : GROUP_SW);
  radioScanBegin(group, false);
  for(uint16_t i = 0; i < n; i++)
  {
    int64_t f = (int64_t)center + ((int32_t)i - n / 2) * (int64_t)step;
    bool v;
    r[i] = s[i] = 0;
    if(f >= radioMinFreq(group == GROUP_FM ? MODE_FM : MODE_AM) && f <= radioMaxFreq(group == GROUP_FM ? MODE_FM : MODE_AM))
      radioMeasure((uint32_t)f, r[i], s[i], v);
    if(abortFn && abortFn()) break;
  }
  curHz = keep;
  radioScanEnd();
}

//
// Power
//
bool radioInit()
{
  pinMode(PIN_AMP_EN, OUTPUT);
  digitalWrite(PIN_AMP_EN, LOW);
  pinMode(AUDIO_MUTE, OUTPUT);
  digitalWrite(AUDIO_MUTE, LOW);
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
  delay(100);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  rx.setI2CFastModeCustom(800000UL);
  if(!rx.getDeviceI2CAddress(RESET_PIN)) return false;
  rx.setup(RESET_PIN, 1 /* MW */);
  delay(100);   // power-up settling (fixes "chip not detected" on some units)

  hwMuted = false;
  muteDepth = 0;
  digitalWrite(AUDIO_MUTE, cfg.audioClamp ? HIGH : LOW);
  digitalWrite(PIN_AMP_EN, HIGH);
  hwMuted = cfg.audioClamp;
  radioApply();
  return true;
}

void radioPowerDown()
{
  muteDepth = 1;
  squelched = false;
  hwMuted = false;
  if(cfg.audioClamp) digitalWrite(AUDIO_MUTE, HIGH);
  digitalWrite(PIN_AMP_EN, LOW);
  rx.setAudioMute(true);
  rx.powerDown();
}
