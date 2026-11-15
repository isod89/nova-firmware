//
// NOVA - firmware for ESP32-S3 + SI4732 pocket receivers (ATS Mini hardware)
//
// Shared declarations for every module.
//
#ifndef NOVA_H
#define NOVA_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#ifndef NOVA_SIM
#include <Arduino.h>
#include "Display.h"
#include "SI4735-fixed.h"
#else
#include "SimArduino.h"
#endif

#define NOVA_NAME     "NOVA"
#define NOVA_VERSION  "2.0.0"
#if defined(CONFIG_SPIRAM_MODE_OCT)
#define NOVA_VARIANT  "ospi"
#else
#define NOVA_VARIANT  "qspi"
#endif

//
// Hardware (ATS Mini V3/V4 pinout)
//
#define PIN_POWER_ON   15   // SI4732 LDO enable
#define RESET_PIN      16   // SI4732 reset
#define PIN_I2C_SCL    17
#define PIN_I2C_SDA    18
#define AUDIO_MUTE      3   // Hardware audio clamp (1 = mute)
#define PIN_AMP_EN     10   // Audio amplifier enable
#define PIN_LCD_BL     38   // Backlight PWM
#define ENCODER_PIN_A   2
#define ENCODER_PIN_B   1
#define ENCODER_PUSH   21
#define VBAT_MON        4

static constexpr int SCREEN_W = 320;
static constexpr int SCREEN_H = 170;

#define ITEM_COUNT(a) (sizeof(a) / sizeof((a)[0]))

//
// Modulation
//
enum Mode : uint8_t { MODE_FM = 0, MODE_LSB = 1, MODE_USB = 2, MODE_AM = 3, MODE_CW = 4, MODE_COUNT = 5 };
static inline bool modeIsSSB(uint8_t m) { return m == MODE_LSB || m == MODE_USB || m == MODE_CW; }

// Station groups used by the list and the scanner
enum Group : uint8_t { GROUP_FM = 0, GROUP_AM = 1, GROUP_SW = 2, GROUP_COUNT = 3 };
static inline uint8_t groupOf(uint32_t hz, uint8_t mode)
{
  return mode == MODE_FM ? GROUP_FM : hz < 1711000 ? GROUP_AM : GROUP_SW;
}

//
// Persistent settings
//
struct Config
{
  uint16_t version;
  // Receiver
  uint32_t freq;            // Hz
  uint8_t  mode;            // Mode
  uint8_t  volume;          // 0..63
  uint8_t  stepIdx[MODE_COUNT];
  uint8_t  bwIdx[MODE_COUNT];
  uint8_t  agc[3];          // FM, AM, SSB: 0 = auto, n = attenuation n-1
  uint8_t  avc[2];          // AM, SSB max gain 12..90
  uint8_t  softMute[2];     // AM, SSB 0..32
  uint8_t  squelch;         // 0 = off, else SNR threshold (dB)
  uint8_t  region;          // 0 Americas, 1 Europe/Africa, 2 Asia/Pacific
  uint8_t  fmStereo;        // 0 auto, 1 smart (mono early on weak signals), 2 mono
  uint8_t  rds;             // 1 = on
  uint8_t  audioClamp;      // 1 = hardware clamp present
  uint8_t  antennaAuto;     // LW/MW automatic antenna tuning
  int16_t  calSSB;          // Hz
  // Display / interface
  uint8_t  theme;
  uint8_t  brightness;      // 10..255
  uint16_t screenTimeout;   // s, 0 = never
  uint8_t  clock24;
  uint8_t  scrollDir;       // 0 = normal, 1 = reversed
  uint8_t  homeScreen;      // 0 = stations, 1 = tune
  uint8_t  encoderHalf;
  // Clock
  int8_t   utcOffset;       // 15 min units
  uint8_t  autoClock;       // set the clock from RDS
  // Power
  uint16_t sleepTimer;      // min, 0 = off
  // Scanner
  uint8_t  scanSpeed;       // 0 fast, 1 normal, 2 thorough
  uint8_t  scanSens;        // 0 = max, 1 = high, 2 = normal, 3 = low
  uint8_t  scanParts;       // SCAN_* bits: what a search covers
  uint8_t  scanBoot;        // 0 = always, 1 = if empty, 2 = never
  uint8_t  scanOffGrid;     // 1 = wide-filter sweep + fine centering
  uint8_t  listFilter;      // 0 all, 1 favourites, 2 new, 3 FM, 4 AM, 5 SW
  uint8_t  standbyPause;    // minutes between standby search cycles
  // 1.6
  uint8_t  upperCase;       // 1 = capital letters everywhere
  uint8_t  visStyle;        // visualizer style
  uint8_t  visIdle;         // 0 off, else seconds idle before the visualizer (index)
  uint8_t  visEco;          // 1 = fewer frames per second
  // 2.0
  uint8_t  bootAnim;        // 1 = animated boot screen
};

extern Config cfg;
void configDefaults();
void configLoad();
void configSave(bool now = false);    // debounced unless now
void configTick();

//
// Radio engine (Radio.cpp)
//
struct Signal { uint8_t rssi, snr; bool stereo; };
struct SignalFull { uint8_t rssi, snr, mult, blend; int8_t offset; bool pilot; };   // read now, for the visualizers

bool     radioInit();
void     radioApply();                       // apply cfg.freq/mode completely
bool     radioTune(uint32_t hz, uint8_t mode); // change frequency and/or mode
bool     radioSetFreq(uint32_t hz);          // retune inside the current mode
uint32_t radioFreq();
uint8_t  radioMode();
void     radioMute(bool on);                 // temporary mute (nested calls ok)
void     radioSetVolume(uint8_t v);
void     radioApplyAudio();                  // bandwidth, AGC, AVC, soft mute, stereo...
void     radioApplyStep();
Signal   radioSignal();
SignalFull radioSignalFull();
bool     radioPoll();                        // RSQ, squelch, RDS; true = something changed
bool     radioSeek(int dir);                 // hardware seek (AM/FM)
void     radioPowerDown();
uint32_t radioMinFreq(uint8_t mode);
uint32_t radioMaxFreq(uint8_t mode);
bool     radioSSBLoaded();

// RDS (FM)
const char *rdsName();                       // station name, "" if none
const char *rdsText();                       // radio text, "" if none
void        rdsClear();
void        radioRdsReset();
bool        radioRdsSync();                  // RDS decoder synchronised

// Ghost Box sweep (fast audible retune, no settings written)
void     radioSweepBegin();
bool     radioSweepTo(uint32_t hz);
void     radioGate(bool mute);                // quick chip mute between steps
void     radioAgcOverride(int8_t value);      // like cfg.agc: 0 auto, n = attenuation n-1, -1 = off
void     radioSoftMuteOverride(int8_t value);  // soft mute depth in dB, -1 = settings
void     radioSweepEnd();

// Low-level helpers for the scanner
void     radioScanBegin(uint8_t group, bool wideFilter);
void     radioScanFilter(bool wide);
void     radioScanDelay(uint8_t ms);         // quiet bus time after each retune
bool     radioMeasure(uint32_t hz, uint8_t &rssi, uint8_t &snr, bool &valid);
void     radioRead(uint8_t &rssi, uint8_t &snr);
void     radioScanThresholds(uint8_t group, uint8_t rssi, uint8_t snr);
void     radioScanEnd();

// Steps and bandwidths
struct StepDef { uint32_t hz; const char *label; };
const StepDef *stepList(uint8_t mode, uint8_t &count);
uint32_t       currentStep();
const char    *bandwidthLabel(uint8_t mode, uint8_t idx);
uint8_t        bandwidthCount(uint8_t mode);
const char    *modeName(uint8_t mode);

//
// System (System.cpp): clock, power, battery, backlight, storage
//
bool        clockValid();
uint32_t    clockUTC();                      // epoch seconds
void        clockSet(uint32_t epoch);
bool        clockLocalHM(uint8_t &h, uint8_t &m);
bool        clockUTCHM(uint8_t &h, uint8_t &m);
const char *clockText();                     // "14:30" / "2:30 PM" / NULL

float       batteryVolts();
uint8_t     batteryPercent();
bool        batteryCharging();

void        backlight(uint8_t level);        // 0..255
void        backlightRestore();
void        screenWake();
void        screenSleep();
void        cpuBoost(bool on);              // 160 MHz for the visualizers
bool        screenIsOff();
void        powerOff();
void        sleepTimerRestart();
uint32_t    sleepTimerLeftMin();
void        systemTick();                    // screen timeout, sleep timer

bool        storeLoad(const char *key, void *data, size_t size, size_t *got = nullptr);
bool        storeSave(const char *key, const void *data, size_t size);
void        storeErase();
size_t      psramSize();

//
// Station names (Schedule.cpp): EiBi schedules held in PSRAM
//
void        scheduleInit();
uint32_t    scheduleCount();
const char *scheduleLookup(uint32_t hz);     // name on the air now, or NULL
const char *bandLabel(uint32_t hz, uint8_t mode);
const char *knownStation(uint32_t hz);       // 24/7 time stations
bool        scheduleImportBegin();
bool        scheduleImportLine(char *line);
bool        scheduleImportEnd(uint32_t &count);

//
// Station list (Stations.cpp)
//
#define ST_MAX        400
#define ST_NAME_LEN   20      // 19 characters + end
#define ST_FAV        0x01
#define ST_MANUAL     0x02    // added by hand from the tune screen
#define ST_NEW        0x04    // found by a later scan, not listened to yet
#define ST_CUSTOM     0x08    // name typed by the user (web editor): shown first, never replaced
#define FILTER_COUNT  6
#define BLOCK_MAX     64

struct __attribute__((packed)) Station
{
  uint32_t freq;              // Hz (carrier for AM, display frequency for SSB)
  uint8_t  group;
  uint8_t  mode;
  uint8_t  rssi;              // measured during the scan, 0 = not heard
  uint8_t  snr;
  uint8_t  flags;
  char     name[ST_NAME_LEN]; // RDS name or user name (ST_CUSTOM)
};

extern Station *stations;
extern uint16_t stationCount;

void        stationsInit();
void        stationsSave();
void        stationsTick();
int16_t     stationFind(uint32_t hz, uint8_t group);
int16_t     stationTuned();               // entry matching the receiver, -1
void        stationLabel(uint16_t i, char *buf, size_t size, bool live);
void        stationToggleFav(uint16_t i);
int16_t     stationAddManual(uint32_t hz, uint8_t mode);
void        stationBlock(uint16_t i);
bool        stationIsBlocked(uint32_t hz);
uint8_t     blockedCount();
void        blockedClear();
void        stationsClear();
void        stationReplaceRange(uint8_t group, uint32_t lo, uint32_t hi, const Station *fresh, uint16_t n, bool markNew);
uint16_t    stationNewCount();
void        stationSeen(int16_t i);
void        stationLearnName();             // RDS name of the tuned FM station
bool        stationsReplaceAll(const Station *list, uint16_t n);   // web editor
uint32_t    blockedAt(uint8_t i);
void        blockedReplace(const uint32_t *list, uint8_t n);

// Filtered view used by the stations screen
extern uint16_t *view;
extern uint16_t viewCount;
void        viewRebuild();
const char *filterName(uint8_t f);

//
// Scanner (Scanner.cpp)
//
struct ScanState
{
  uint32_t done, total;       // channels
  uint32_t freq;
  uint8_t  group;
  uint16_t found;
  uint16_t confirmDone, confirmTotal;
  uint32_t startedAt;
  const char *phase;
  uint32_t recent[3];
  uint8_t  recentGroup[3];
  uint8_t  recentCount;
};
// What a search covers (cfg.scanParts bits)
#define SCAN_FM      0x01     // FM 87.5-108 MHz
#define SCAN_FMLOW   0x02     // FM 64-87.4 MHz
#define SCAN_MW      0x04     // medium wave
#define SCAN_LW      0x08     // long wave
#define SCAN_SWBC    0x10     // the 14 shortwave broadcast bands
#define SCAN_SWREST  0x20     // the rest of 1.7-30 MHz (tropical, ham, CB, utility)
#define SCAN_ALL     0x3F
enum ScanSpeed : uint8_t { SPEED_FAST, SPEED_NORMAL, SPEED_THOROUGH };
const char *scanSpeedName(uint8_t s);
uint32_t    scanEstimate(uint8_t parts, uint8_t speed, uint32_t *channels = nullptr);   // seconds
void        scanOverride(uint8_t parts, int8_t speed);   // next search only; 0 / -1 = settings
bool        scanRun();                      // full scan, false if aborted (results kept)
void        scanStart(bool background);
bool        scanStep();                     // one segment; false when finished or stopped
bool        scanActive();
bool        scanWasAborted();
extern ScanState scanState;
uint32_t    scanPlanChannels();

//
// Input
//
struct Input
{
  int16_t turn;         // detents (signed)
  int16_t turnFast;     // accelerated
  bool    pushTurn;     // turned while pressed
  bool    click;        // < 0.5 s
  bool    hold;         // 0.5 .. 2 s, on release
  bool    longHold;     // >= 2 s, once while held
};
bool inputAbort();      // for blocking operations

//
// User interface (Ui*.cpp)
//
enum Screen : uint8_t { SCR_STATIONS, SCR_TUNE, SCR_MENU, SCR_SETTINGS, SCR_INFO, SCR_APPS, SCR_GHOST, SCR_NOISE, SCR_STORM, SCR_SEARCH, SCR_VISUAL };
// Band scope measurement (mutes audio, restores the receiver afterwards)
void radioScope(uint32_t center, uint32_t step, uint16_t n, uint8_t *rssi, uint8_t *snr, bool (*abortFn)());

void uiInit();
void splashStart();     // animated boot screen, on the other core
void splashFinish();    // wait for it (a click skips)
void splashAbort();
void uiShow(uint8_t screen);
uint8_t uiScreen();
void uiInput(const Input &in);
void uiDraw();
void uiRequestDraw();
bool uiTick();
void uiToast(const char *text, uint32_t ms = 1800);
void uiDrawScan(bool force = false);
void uiMessage(const char *title, const char *line);
void uiStartScan();
void uiGoHome();
void visualOpen(uint8_t back);   // full-screen visualizer, Hold returns to "back"
uint16_t uiReadPixel(int x, int y);   // RGB565 of the last frame (web editor mirror)
const char *themeNameAt(uint8_t i);
uint8_t themeCount();
void standbyEnter();
bool standbyActive();
void standbyTick();

//
// Ghost Box (Ghost.cpp): ITC radio sweep application
//
struct GhostBand { const char *name; uint32_t lo, hi; uint8_t mode; uint32_t step; };
struct GhostCfg
{
  uint8_t  version;
  uint8_t  band;       // index in ghostBands
  uint16_t rate;       // ms per frequency
  uint8_t  dir;        // 0 forward, 1 reverse, 2 bounce, 3 random
  uint8_t  step;       // 0 normal, 1 double, 2 half
  uint8_t  flutter;    // 1 = half-step jitter inside each dwell
  uint8_t  gaps;       // 0 = noise, 1 = mute weak frequencies
  uint8_t  gate;       // SNR threshold (dB) for mute gaps
  uint8_t  antenna;    // 0 normal, 1 reduced, 2 low, 3 minimum
  // version 2
  uint8_t  content;    // 0 mixed, 1 noise only (skip stations), 2 stations only, 3 hot spots (marked frequencies)
  uint8_t  chaos;      // irregular dwell: 0 off, 1 = +/-25 %, 2 = +/-50 %
  uint8_t  timer;      // session length in minutes, 0 = none
  uint8_t  blind;      // 1 = the screen hides frequency and signal (Estes method)
  uint32_t lo, hi;     // custom range inside the band, 0 = band edges
  // version 3
  uint8_t  react;      // reaction time correction for marks, in 100 ms
  uint8_t  onMark;     // after a response mark: 0 keep sweeping, 1 hold the heard frequency 3 s, 2 pause on it
  uint8_t  window;     // answer window after a question, seconds (0 = none)
  uint8_t  noise;      // noise control (chip soft mute): 0 raw, 1 normal, 2 quiet
  uint8_t  cluster;    // clusters content: steps on each side of a station
  uint8_t  trigger;    // 1 = RF trigger: muted and parked until an RF burst opens the sweep
  uint8_t  trigSens;   // dB above the baseline that opens the sweep
  uint8_t  trigOpen;   // seconds the sweep stays open after a trigger
  // version 4
  uint8_t  catchT;     // catch: stay on a frequency with a signal, in 100 ms (0 = off)
  uint8_t  catchSnr;   // SNR (dB) that counts as a signal for the catch
  uint8_t  holdSec;    // seconds to hold the heard frequency after a mark
};
struct GhostPoint { uint32_t t; uint32_t hz; uint8_t rssi, snr; } __attribute__((packed));
enum GhostContent : uint8_t { GC_MIXED, GC_NOISE, GC_STATIONS, GC_HOT, GC_CLUSTER, GC_COUNT };
#define GHOST_DIRS 6
struct GhostProfile { const char *name; uint8_t band; uint16_t rate; uint8_t dir, step, flutter, gaps, gate, antenna, content, chaos, noise, cluster, blind, timer; };
extern const GhostProfile ghostProfiles[];
uint8_t ghostProfileCount();
void    ghostApplyProfile(uint8_t i);
bool    ghostUserProfile(uint8_t slot, GhostCfg *out);  // slot 0..2
void    ghostSaveUserProfile(uint8_t slot);
void    ghostLoadUserProfile(uint8_t slot);
uint32_t ghostHoldLeft();                // ms left holding a frequency
uint8_t  ghostHoldKind();                // 0 none, 1 after a mark, 2 caught a signal
uint16_t ghostCatches();                 // signals caught this session
uint32_t ghostWindowLeft();              // ms left in the answer window
bool    ghostTakeWindowDone();
uint8_t ghostTrigState();                // 0 off, 1 armed (muted, waiting), 2 open
uint32_t ghostTrigOpenLeft();
uint8_t ghostTrigLevel();                // last RSSI on the watch frequency
uint8_t ghostTrigBase();
// Session history in flash (settings and marks of the last sessions)
uint8_t ghostHistCount();
bool    ghostHistInfo(uint8_t i, uint32_t &id, uint32_t &epoch, uint32_t &dur, uint32_t &steps, uint16_t &marks, GhostCfg &c);
uint16_t ghostHistMarks(uint8_t i, GhostPoint *m, uint8_t *kind, uint16_t max);
void    ghostHistClear();

extern GhostCfg ghost;
extern const GhostBand ghostBands[];
uint8_t     ghostBandCount();
extern const uint16_t ghostRates[];
uint8_t     ghostRateCount();
const char *ghostDirName(uint8_t d);
void        ghostInit();
void        ghostStart();
void        ghostStop();
bool        ghostActive();
bool        ghostPaused();
void        ghostPause(bool on);
bool        ghostTick();                     // true = new step (redraw)
void        ghostApply();                    // settings changed while running
void        ghostSettingsChanged();          // save later
uint16_t    ghostMark(uint8_t kind = 0);     // 0 response, 1 question; returns the mark number
uint8_t     ghostMarkKind(uint16_t i);
uint32_t    ghostRemaining();                // ms left with a session timer, 0 = none
bool        ghostTakeTimerDone();            // the session timer just ran out
bool        ghostLearning();                 // stations only: still learning where they are
uint16_t    ghostCarriers();                 // carriers known in this band
uint32_t    ghostBandLo();
uint32_t    ghostBandHi();
uint32_t    ghostElapsed();                  // ms since the session started
uint32_t    ghostSteps();
uint32_t    ghostStartEpoch();
uint16_t    ghostMarkCount();
const GhostPoint *ghostMarkAt(uint16_t i);
uint32_t    ghostTraceTotal();               // points logged since the start
uint32_t    ghostTraceOldest();              // first index still kept
bool        ghostTraceAt(uint32_t i, GhostPoint &p);
uint32_t    ghostStepHz();
uint8_t     ghostHistory(uint8_t *rssi, uint8_t *snr, uint8_t n);   // last n steps

//
// Measurement apps (Tools.cpp): Noise Hunter and Storm Detector
//
struct ToolBand { const char *name; const char *shortName; uint32_t lo, hi, step; };
#define NOISE_HIST   150      // 30 s at 5 values per second
#define NOISE_SPOTS  50
struct NoiseCfg  { uint8_t version, band, att, pad; uint32_t hz; };
struct NoiseSpot { uint32_t t, hz; uint8_t level, peak; int16_t base; };
extern NoiseCfg noiseCfg;
extern const ToolBand noiseBands[];
uint8_t  noiseBandCount();
void     noiseStart();
void     noiseStop();
bool     noiseActive();
bool     noiseTick();                 // true = new history point
void     noiseFindQuiet();
void     noiseSetBand(uint8_t band);
void     noiseSetHz(uint32_t hz);
void     noiseSetAtt(uint8_t att);
void     noiseSettingsChanged();
void     noiseZero();
void     noiseResetMax();
uint8_t  noiseSpot();
uint8_t  noiseLevel();
uint8_t  noisePeak();
uint8_t  noiseMax();
uint8_t  noiseSnr();
int16_t  noiseBase();                 // -1 = no zero set
uint32_t noiseElapsed();
uint8_t  noiseSpotCount();
const NoiseSpot *noiseSpotAt(uint8_t i);
uint16_t noiseHistory(uint8_t *out, uint16_t n);

#define STORM_MINUTES 240
enum StormAlert : uint8_t { STORM_QUIET, STORM_DISTANT, STORM_APPROACH, STORM_NEAR, STORM_NOISY };
struct StormCfg    { uint8_t version, thresh, flash, pad; uint32_t hz; };
struct StormEvent  { uint32_t t; uint8_t peak; uint16_t dur; } __attribute__((packed));
struct StormMinute { uint32_t minute; uint16_t count; uint8_t maxPeak; uint32_t sumPeak; };
extern StormCfg stormCfg;
void     stormStart();
void     stormStop();
bool     stormActive();
bool     stormTick();                 // true = something changed
bool     stormTakeAlert();            // the alert level just went up
void     stormFindQuiet();
void     stormSetHz(uint32_t hz);
void     stormSettingsChanged();
void     stormReset();
void     stormListen(bool on);
bool     stormListening();
uint8_t  stormAlert();
int8_t   stormTrend();
uint8_t  stormLevel();
uint8_t  stormBase();
uint32_t stormElapsed();
uint32_t stormStartEpoch();
uint32_t stormEventTotal();
uint32_t stormEventOldest();
bool     stormEventAt(uint32_t i, StormEvent &e);
uint32_t stormCount(uint32_t minutes);
int32_t  stormLastAgo();              // ms, -1 = none
uint8_t  stormLastPeak();
uint16_t stormMinutes(StormMinute *out, uint16_t n);

void     toolsInit();
// Wi-Fi, only on request (EiBi download and clock), off the rest of the time
typedef void (*WifiProgress)(const char *stage, uint32_t n);
enum : uint8_t { WIFI_JOB_TEST, WIFI_JOB_EIBI };
void        wifiInit();
bool        wifiConfigured();
const char *wifiSsid();
void        wifiSetCreds(const char *ssid, const char *pass);
void        wifiForget();
bool        wifiRun(uint8_t job, WifiProgress fn, char *msg, size_t mn);   // msg: result or the reason it failed
bool     appRunning();
void     appsStopAll();

// USB (Usb.cpp)
void usbInit();
bool usbTick();
bool usbConnected();
bool usbScanPoll();          // during a search: answer the editor, true = stop asked

#endif // NOVA_H
