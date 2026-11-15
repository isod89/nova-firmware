//
// System.cpp - settings, clock, battery, backlight, screen sleep, power
//
#include "Nova.h"

#ifndef NOVA_SIM
#include <Preferences.h>
#include <nvs_flash.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <sys/time.h>
#include <time.h>
extern LGFX tft;
static Preferences prefs;
#define STORE_NS   "nova"
#define STORE_PART "settings"
#endif

#define CONFIG_VERSION 8

Config cfg;

//
// Settings
//
void configDefaults()
{
  memset(&cfg, 0, sizeof(cfg));
  cfg.version     = CONFIG_VERSION;
  cfg.freq        = 99100000;
  cfg.mode        = MODE_FM;
  cfg.volume      = 35;
  cfg.stepIdx[MODE_FM]  = 2;   // 100 kHz
  cfg.stepIdx[MODE_AM]  = 1;   // 5 kHz
  cfg.stepIdx[MODE_LSB] = 2;   // 100 Hz
  cfg.stepIdx[MODE_USB] = 2;
  cfg.stepIdx[MODE_CW]  = 1;   // 50 Hz
  cfg.bwIdx[MODE_FM]  = 0;     // Auto
  cfg.bwIdx[MODE_AM]  = 2;     // 3.0 kHz
  cfg.bwIdx[MODE_LSB] = 2;     // 2.2 kHz
  cfg.bwIdx[MODE_USB] = 2;
  cfg.bwIdx[MODE_CW]  = 5;     // 0.5 kHz
  cfg.avc[0] = cfg.avc[1] = 48;
  cfg.softMute[0] = cfg.softMute[1] = 4;
  cfg.region      = 0;         // Americas
  cfg.rds         = 1;
  cfg.audioClamp  = 1;
  cfg.antennaAuto = 1;
  cfg.theme       = 5;         // NOVA
  cfg.brightness  = 160;
  cfg.screenTimeout = 0;
  cfg.clock24     = 1;
  cfg.utcOffset   = -20;       // UTC-5
  cfg.autoClock   = 1;
  cfg.scanSpeed   = SPEED_NORMAL;
  cfg.scanSens    = 0;         // Max
  cfg.fmStereo    = 1;         // Smart
  cfg.standbyPause = 10;
  cfg.scanParts   = SCAN_ALL;
  cfg.scanBoot    = 2;         // never search by itself at power on
  cfg.scanOffGrid = 1;
  cfg.upperCase   = 1;
  cfg.visStyle    = 0;
  cfg.visIdle     = 0;
  cfg.visEco      = 0;
  cfg.bootAnim    = 1;
}

static bool configDirty = false;
static uint32_t configDirtyAt = 0;

void configLoad()
{
  Config loaded;
  memset(&loaded, 0, sizeof(loaded));
  size_t got = 0;
  bool ok = storeLoad("cfg", &loaded, sizeof(loaded), &got);
  if(ok && got == sizeof(loaded) && loaded.version == CONFIG_VERSION)
    cfg = loaded;
  else if(ok && got <= sizeof(loaded) && loaded.version == 7)
  {
    // 1.6 to 1.9 settings: one new field at the end
    cfg = loaded;
    cfg.bootAnim = 1;
    cfg.version = CONFIG_VERSION;
    configSave(true);
  }
  else if(ok && got <= sizeof(loaded) && loaded.version == 6)
  {
    // 1.5 settings: same start, the 1.6 fields are new
    cfg = loaded;
    cfg.upperCase = 1; cfg.visStyle = 0; cfg.visIdle = 0; cfg.visEco = 0; cfg.bootAnim = 1;
    cfg.version = CONFIG_VERSION;
    configSave(true);
  }
  else if(ok && got <= sizeof(loaded) && (loaded.version == 4 || loaded.version == 5))
  {
    // Older settings: same layout, keep them. Version 4 searched at power on;
    // up to 1.4 the two scan fields were "depth" (0 complete) and "bands"
    // (0 all, 1 FM+AM, 2 FM): turn them into the parts to search.
    cfg = loaded;
    if(loaded.version == 4) cfg.scanBoot = 2;
    bool full = loaded.scanSpeed == 0;
    uint8_t bands = loaded.scanParts;
    uint8_t parts = SCAN_FM | (full ? SCAN_FMLOW : 0);
    if(bands <= 1) parts |= SCAN_MW | (full ? SCAN_LW : 0);
    if(bands == 0) parts |= SCAN_SWBC | (full ? SCAN_SWREST : 0);
    cfg.scanParts = parts;
    cfg.scanSpeed = SPEED_NORMAL;
    cfg.upperCase = 1; cfg.visStyle = 0; cfg.visIdle = 0; cfg.visEco = 0; cfg.bootAnim = 1;
    cfg.version = CONFIG_VERSION;
    configSave(true);
  }
  else
    configDefaults();

  // Sanity
  if(cfg.mode >= MODE_COUNT) cfg.mode = MODE_FM;
  if(cfg.volume > 63) cfg.volume = 35;
  if(cfg.brightness < 10) cfg.brightness = 10;
  if(cfg.region > 2) cfg.region = 0;
  if(cfg.listFilter >= FILTER_COUNT) cfg.listFilter = 0;
  if(cfg.fmStereo > 2) cfg.fmStereo = 1;
  if(cfg.scanSens > 3) cfg.scanSens = 0;
  if(cfg.scanSpeed > 2) cfg.scanSpeed = SPEED_NORMAL;
  if(!(cfg.scanParts & SCAN_ALL)) cfg.scanParts = SCAN_ALL;
}

void configSave(bool now)
{
  configDirty = true;
  configDirtyAt = millis();
  if(now)
  {
    storeSave("cfg", &cfg, sizeof(cfg));
    configDirty = false;
  }
}

void configTick()
{
  // Write at most every few seconds, after the user stops turning the knob
  if(configDirty && millis() - configDirtyAt > 4000) configSave(true);
}

//
// Storage (NVS "settings" partition)
//
#ifndef NOVA_SIM
bool storeLoad(const char *key, void *data, size_t size, size_t *got)
{
  if(!prefs.begin(STORE_NS, true, STORE_PART)) { if(got) *got = 0; return false; }
  size_t len = prefs.getBytesLength(key);
  size_t n = 0;
  if(len && len <= size) n = prefs.getBytes(key, data, len);
  prefs.end();
  if(got) *got = n;
  return n > 0;
}

bool storeSave(const char *key, const void *data, size_t size)
{
  if(!prefs.begin(STORE_NS, false, STORE_PART)) return false;
  bool ok = size ? prefs.putBytes(key, data, size) == size : prefs.remove(key);
  prefs.end();
  return ok;
}

void storeErase()
{
  nvs_flash_erase_partition(STORE_PART);
  nvs_flash_init_partition(STORE_PART);
}

size_t psramSize() { return ESP.getPsramSize(); }

//
// Clock (UTC kept by the ESP32 RTC, also across power-off deep sleep)
//
static bool clockSetFlag = false;

bool clockValid()
{
  if(!clockSetFlag) clockSetFlag = time(nullptr) > 1700000000;
  return clockSetFlag;
}

uint32_t clockUTC() { return time(nullptr); }

void clockSet(uint32_t epoch)
{
  struct timeval tv = { (time_t)epoch, 0 };
  settimeofday(&tv, nullptr);
  clockSetFlag = true;
}
#endif

bool clockUTCHM(uint8_t &h, uint8_t &m)
{
  if(!clockValid()) return false;
  uint32_t t = clockUTC() % 86400;
  h = t / 3600; m = (t / 60) % 60;
  return true;
}

bool clockLocalHM(uint8_t &h, uint8_t &m)
{
  if(!clockValid()) return false;
  int32_t t = (int32_t)(clockUTC() % 86400) + cfg.utcOffset * 900;
  t = ((t % 86400) + 86400) % 86400;
  h = t / 3600; m = (t / 60) % 60;
  return true;
}

const char *clockText()
{
  static char buf[12];
  uint8_t h, m;
  if(!clockLocalHM(h, m)) return nullptr;
  if(cfg.clock24) snprintf(buf, sizeof(buf), "%02u:%02u", h, m);
  else snprintf(buf, sizeof(buf), "%u:%02u %s", h % 12 ? h % 12 : 12, m, h < 12 ? "AM" : "PM");
  return buf;
}

//
// Battery
//
#ifndef NOVA_SIM
static float vbat = 4.0f;

float batteryVolts()
{
  static uint32_t last = 0;
  if(!last || millis() - last > 2000)
  {
    last = millis();
    uint32_t sum = 0;
    for(int i = 0; i < 10; i++) sum += analogRead(VBAT_MON);
    float v = (sum / 10.0f) * 1.702f / 1000.0f;
    vbat = vbat * 0.7f + v * 0.3f;
  }
  return vbat;
}
#endif

bool batteryCharging() { return batteryVolts() > 4.3f; }

uint8_t batteryPercent()
{
  // Li-ion discharge curve, approximated in segments
  static const float v[] = { 3.30f, 3.50f, 3.62f, 3.70f, 3.76f, 3.82f, 3.90f, 3.98f, 4.06f, 4.15f };
  static const uint8_t p[] = { 0, 5, 12, 25, 40, 55, 68, 80, 90, 100 };
  float x = batteryVolts();
  if(x <= v[0]) return 0;
  for(size_t i = 1; i < ITEM_COUNT(v); i++)
    if(x <= v[i]) return p[i - 1] + (uint8_t)((x - v[i - 1]) / (v[i] - v[i - 1]) * (p[i] - p[i - 1]));
  return 100;
}

//
// Backlight and screen sleep
//
static bool screenOff = false;
static uint32_t lastActivity = 0;
static uint32_t sleepDeadline = 0;

#ifndef NOVA_SIM
void backlight(uint8_t level) { ledcWrite(PIN_LCD_BL, level); }
#endif
void backlightRestore() { if(!screenOff) backlight(cfg.brightness); }
bool screenIsOff() { return screenOff; }

void screenWake()
{
  lastActivity = millis();
  if(!screenOff) return;
  screenOff = false;
#ifndef NOVA_SIM
  tft.wakeup();
#endif
  uiRequestDraw();
  backlight(cfg.brightness);
}

void cpuBoost(bool on)
{
#ifndef NOVA_SIM
  setCpuFrequencyMhz(on ? 160 : 80);
#else
  (void)on;
#endif
}

void screenSleep()
{
  if(screenOff) return;
  screenOff = true;
  backlight(0);
#ifndef NOVA_SIM
  tft.sleep();
#endif
}

void sleepTimerRestart()
{
  sleepDeadline = cfg.sleepTimer ? millis() + cfg.sleepTimer * 60000UL : 0;
}

uint32_t sleepTimerLeftMin()
{
  if(!sleepDeadline) return 0;
  int32_t left = (int32_t)(sleepDeadline - millis());
  return left > 0 ? (left + 59999) / 60000 : 0;
}

void systemTick()
{
  if(cfg.screenTimeout && !screenOff && millis() - lastActivity > cfg.screenTimeout * 1000UL) screenSleep();
  if(sleepDeadline && (int32_t)(millis() - sleepDeadline) >= 0) powerOff();
}

//
// Power off: deep sleep, wake up with the knob button
//
#ifndef NOVA_SIM
static void holdLow(int pin)
{
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  gpio_hold_en((gpio_num_t)pin);
}

void powerOff()
{
  configSave(true);
  stationsSave();
  radioPowerDown();
  backlight(0);
  tft.sleep();
  delay(150);

  // Wait for the button to be released, or it would wake us up at once
  while(digitalRead(ENCODER_PUSH) == LOW) delay(20);
  delay(200);

  ledcDetach(PIN_LCD_BL);
  holdLow(PIN_LCD_BL);
  holdLow(PIN_AMP_EN);
  holdLow(PIN_POWER_ON);
  gpio_deep_sleep_hold_en();

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  rtc_gpio_init((gpio_num_t)ENCODER_PUSH);
  rtc_gpio_set_direction((gpio_num_t)ENCODER_PUSH, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)ENCODER_PUSH);
  rtc_gpio_pulldown_dis((gpio_num_t)ENCODER_PUSH);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)ENCODER_PUSH, 0);
  esp_deep_sleep_start();
}
#endif
