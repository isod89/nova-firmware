//
// UiMenu.cpp - the menu (hold the knob) and the settings pages
//
#include "Ui.h"
#ifndef NOVA_SIM
#include <esp_system.h>
#endif

//
// Menu
//
enum MenuAct : uint8_t
{
  MA_FAV, MA_TUNE_HERE, MA_BLOCK, MA_SAVE,
  MA_VISUAL, MA_STATIONS, MA_TUNE, MA_APPS, MA_SCAN, MA_STANDBY, MA_SETTINGS, MA_SCREEN_OFF, MA_POWER, MA_BACK
};

struct MenuItem { uint8_t act; uint8_t icon; uint16_t color; char label[32]; };

static MenuItem items[14];
static uint8_t itemCount = 0, menuSel = 0, menuFrom = SCR_STATIONS;
static int16_t menuStation = -1;

static void addItem(uint8_t act, uint8_t icon, const char *label, uint16_t color)
{
  if(itemCount >= ITEM_COUNT(items)) return;
  items[itemCount].act = act;
  items[itemCount].icon = icon;
  items[itemCount].color = color;
  snprintf(items[itemCount].label, sizeof(items[0].label), "%s", label);
  itemCount++;
}

void menuOpen(uint8_t from)
{
  menuFrom = from;
  itemCount = 0;
  menuSel = 0;
  menuStation = stationTuned();

  if(from == SCR_STATIONS && menuStation >= 0)
  {
    bool fav = stations[menuStation].flags & ST_FAV;
    addItem(MA_FAV, IC_STAR, fav ? "Remove from Favorites" : "Add to Favorites", C_YELLOW);
    addItem(MA_TUNE_HERE, IC_DIAL, "Fine tune this station", C_ORANGE);
    addItem(MA_BLOCK, IC_BLOCK, "Block (not a station)", C_RED);
  }
  if(from == SCR_TUNE) addItem(MA_SAVE, IC_STAR, "Save to Favorites", C_YELLOW);

  addItem(MA_VISUAL, IC_WAVE, "Visualizer", C_PINK);
  addItem(MA_STATIONS, IC_LIST, "Stations", C_BLUE);
  addItem(MA_TUNE, IC_DIAL, "Free tuning", C_ORANGE);
  addItem(MA_APPS, IC_APPS, "Apps", C_PURPLE);
  addItem(MA_SCAN, IC_RADAR, "Search stations", C_GREEN);
  addItem(MA_STANDBY, IC_STANDBY, "Standby search", C_INDIGO);
  addItem(MA_SETTINGS, IC_GEAR, "Settings", C_GRAY);
  addItem(MA_SCREEN_OFF, IC_MOON, "Screen off", C_PURPLE);
  addItem(MA_POWER, IC_POWER, "Power off", C_RED);
  uiShow(SCR_MENU);
}

static void blockConfirmed()
{
  if(menuStation >= 0) stationBlock(menuStation);
  uiShow(SCR_STATIONS);
  uiToast("Blocked - it will not come back");
}

void menuInput(const Input &in)
{
  if(in.hold) { uiShow(menuFrom); return; }
  if(in.turn) menuSel = (menuSel + itemCount + (in.turn > 0 ? 1 : -1)) % itemCount;
  if(!in.click) return;

  switch(items[menuSel].act)
  {
    case MA_FAV:
      stationToggleFav(menuStation);
      uiShow(SCR_STATIONS);
      uiToast(stations[menuStation].flags & ST_FAV ? "Added to Favorites" : "Removed from Favorites");
      break;
    case MA_TUNE_HERE: uiShow(SCR_TUNE); break;
    case MA_BLOCK: uiConfirm("Block this station?", "Use it for noise and receiver whistles", "Block", blockConfirmed); break;
    case MA_SAVE:
      uiShow(SCR_TUNE);
      uiToast(stationAddManual(radioFreq(), radioMode()) >= 0 ? "Saved to Favorites" : "Station list is full");
      break;
    case MA_VISUAL: visualOpen(menuFrom); break;
    case MA_STATIONS: uiShow(SCR_STATIONS); break;
    case MA_TUNE: uiShow(SCR_TUNE); break;
    case MA_APPS: appsOpen(menuFrom); break;
    case MA_SCAN: scanMenuOpen(menuFrom); break;
    case MA_STANDBY: uiShow(menuFrom); standbyEnter(); break;
    case MA_SETTINGS: settingsOpen(); break;
    case MA_SCREEN_OFF: uiShow(menuFrom); screenSleep(); break;
    case MA_POWER: powerOff(); break;
  }
}

void menuDraw()
{
  uiStatusBar("Menu", T.text);
  const int rows = 5, rowH = 29, y0 = 21;
  int top = menuSel - 2;
  if(top > itemCount - rows) top = itemCount - rows;
  if(top < 0) top = 0;
  for(int r = 0; r < rows && top + r < itemCount; r++)
  {
    int i = top + r, y = y0 + r * rowH;
    bool hl = i == menuSel;
    if(hl) uiGloss(6, y, 302, rowH - 3, 10, T.sel); else uiRRect(6, y, 302, rowH - 3, 10, T.surface);
    uiTile(24, y + 13, 20, items[i].color, items[i].icon);
    uiText(items[i].label, 42, y + 13, F_SMB, hl ? T.selText : T.text, ML_DATUM);
    int cx = 296, cy = y + 13;
    spr.drawLine(cx - 2, cy - 4, cx + 2, cy, hl ? T.selText : T.dim);
    spr.drawLine(cx + 2, cy, cx - 2, cy + 4, hl ? T.selText : T.dim);
  }
  uiScrollbar(312, y0, rows * rowH - 3, itemCount, rows, top);
}

//
// Settings
//
enum Kind : uint8_t { K_PAGE, K_VALUE, K_ACTION, K_INFO };

enum SetId : uint8_t
{
  // Root pages
  P_ROOT, P_SCAN, P_AUDIO, P_RECEPTION, P_DISPLAY, P_CLOCK, P_POWER, P_NAMES, P_ABOUT,
  // Scan
  S_SPEED, S_SENS, S_P_FM, S_P_FMLOW, S_P_MW, S_P_LW, S_P_SWBC, S_P_SWREST, S_EST, S_BOOT, S_OFFGRID, S_STANDBY, S_RUN, S_UNBLOCK, S_CLEAR,
  // Audio
  A_VOLUME, A_SQUELCH, A_SOFTMUTE, A_AVC, A_STEREO, A_CLAMP,
  // Reception
  R_REGION, R_BW_AM, R_BW_FM, R_BW_SSB, R_AGC_AM, R_AGC_FM, R_ANTENNA, R_RDS, R_CAL,
  // Display
  D_THEME, D_CAPS, D_BRIGHT, D_TIMEOUT, D_HOME, D_SCROLL, D_VISIDLE, D_VISECO, D_BOOTANIM,
  // Clock
  C_HOUR, C_MIN, C_OFFSET, C_FORMAT, C_AUTO,
  // Power
  W_SLEEP, W_BATTERY,
  // Names
  N_STATUS, N_WIFI, N_WCLOCK, N_HOW1, N_HOW2,
  // About
  I_VERSION, I_STATIONS, I_PSRAM, I_BATTERY, I_CREDITS, I_RESET,
};

struct SetItem { uint8_t id; uint8_t kind; const char *label; uint8_t icon; };

static const SetItem rootItems[] =
{
  { P_SCAN, K_PAGE, "Scanner", IC_RADAR },       { P_AUDIO, K_PAGE, "Audio", IC_SPEAKER },
  { P_RECEPTION, K_PAGE, "Reception", IC_ANTENNA }, { P_DISPLAY, K_PAGE, "Display", IC_BRUSH },
  { P_CLOCK, K_PAGE, "Clock", IC_CLOCK },        { P_POWER, K_PAGE, "Power", IC_BATTERY },
  { P_NAMES, K_PAGE, "Station names", IC_BOOK }, { P_ABOUT, K_PAGE, "About", IC_INFO },
};
static const uint16_t rootColors[] = { C_GREEN, C_PINK, C_TEAL, C_BLUE, C_ORANGE, C_GREEN, C_INDIGO, C_GRAY };

// On/Off settings are shown as switches and change with a single click
static bool isToggle(uint8_t id, bool &on)
{
  switch(id)
  {
    case S_OFFGRID: on = cfg.scanOffGrid; return true;
    case A_CLAMP:   on = cfg.audioClamp; return true;
    case R_RDS:     on = cfg.rds; return true;
    case R_ANTENNA: on = cfg.antennaAuto; return true;
    case C_AUTO:    on = cfg.autoClock; return true;
    case D_SCROLL:  on = cfg.scrollDir; return true;
    case D_CAPS:    on = cfg.upperCase; return true;
    case D_VISECO:  on = cfg.visEco; return true;
    case D_BOOTANIM: on = cfg.bootAnim; return true;
    case S_P_FM:     on = cfg.scanParts & SCAN_FM; return true;
    case S_P_FMLOW:  on = cfg.scanParts & SCAN_FMLOW; return true;
    case S_P_MW:     on = cfg.scanParts & SCAN_MW; return true;
    case S_P_LW:     on = cfg.scanParts & SCAN_LW; return true;
    case S_P_SWBC:   on = cfg.scanParts & SCAN_SWBC; return true;
    case S_P_SWREST: on = cfg.scanParts & SCAN_SWREST; return true;
    default:        return false;
  }
}
static const SetItem scanItems[] =
{
  { S_SPEED, K_VALUE, "Speed", 0 }, { S_SENS, K_VALUE, "Sensitivity", 0 },
  { S_P_FM, K_VALUE, "FM 87.5-108 MHz", 0 }, { S_P_FMLOW, K_VALUE, "FM 64-87 MHz (low band)", 0 },
  { S_P_MW, K_VALUE, "Medium wave (AM)", 0 }, { S_P_LW, K_VALUE, "Long wave", 0 },
  { S_P_SWBC, K_VALUE, "Shortwave bands", 0 }, { S_P_SWREST, K_VALUE, "Shortwave, all the rest", 0 },
  { S_EST, K_INFO, "Time for a search", 0 },
  { S_OFFGRID, K_VALUE, "Off-grid search", 0 }, { S_BOOT, K_VALUE, "Scan at power on", 0 },
  { S_STANDBY, K_VALUE, "Standby: pause between", 0 },
  { S_RUN, K_ACTION, "Search now", 0 }, { S_UNBLOCK, K_ACTION, "Unblock all", 0 }, { S_CLEAR, K_ACTION, "Clear station list", 0 },
};
static const SetItem audioItems[] =
{
  { A_VOLUME, K_VALUE, "Volume", 0 }, { A_SQUELCH, K_VALUE, "Squelch", 0 }, { A_SOFTMUTE, K_VALUE, "Soft mute (AM)", 0 },
  { A_AVC, K_VALUE, "Max gain (AM)", 0 }, { A_STEREO, K_VALUE, "FM stereo", 0 }, { A_CLAMP, K_VALUE, "Click suppression", 0 },
};
static const SetItem receptionItems[] =
{
  { R_REGION, K_VALUE, "Region", 0 }, { R_RDS, K_VALUE, "RDS", 0 }, { R_ANTENNA, K_VALUE, "LW/MW antenna tuning", 0 },
  { R_BW_AM, K_VALUE, "AM filter", 0 }, { R_BW_FM, K_VALUE, "FM filter", 0 }, { R_BW_SSB, K_VALUE, "SSB filter", 0 },
  { R_AGC_AM, K_VALUE, "AM gain control", 0 }, { R_AGC_FM, K_VALUE, "FM gain control", 0 }, { R_CAL, K_VALUE, "SSB calibration", 0 },
};
static const SetItem displayItems[] =
{
  { D_THEME, K_VALUE, "Theme", 0 }, { D_CAPS, K_VALUE, "Capital letters", 0 }, { D_BRIGHT, K_VALUE, "Brightness", 0 },
  { D_TIMEOUT, K_VALUE, "Screen timeout", 0 }, { D_HOME, K_VALUE, "Home screen", 0 }, { D_SCROLL, K_VALUE, "Reverse knob", 0 },
  { D_VISIDLE, K_VALUE, "Visualizer when idle", 0 }, { D_VISECO, K_VALUE, "Visualizer: battery saver", 0 },
  { D_BOOTANIM, K_VALUE, "Boot animation", 0 },
};
static const SetItem clockItems[] =
{
  { C_HOUR, K_VALUE, "Hour", 0 }, { C_MIN, K_VALUE, "Minute", 0 }, { C_OFFSET, K_VALUE, "Time zone (UTC)", 0 },
  { C_FORMAT, K_VALUE, "Format", 0 }, { C_AUTO, K_VALUE, "Set from FM (RDS)", 0 },
};
static const SetItem powerItems[] = { { W_SLEEP, K_VALUE, "Sleep timer", 0 }, { W_BATTERY, K_INFO, "Battery", 0 } };
static const SetItem namesItems[] =
{
  { N_STATUS, K_INFO, "Schedules", 0 }, { N_WIFI, K_ACTION, "Update over Wi-Fi", 0 }, { N_WCLOCK, K_ACTION, "Wi-Fi test, set clock", 0 },
  { N_HOW1, K_INFO, "Or in the web editor", 0 },
  { N_HOW2, K_INFO, "Station names page, USB", 0 },
};
static const SetItem aboutItems[] =
{
  { I_VERSION, K_INFO, "Firmware", 0 }, { I_STATIONS, K_INFO, "Stations", 0 }, { I_PSRAM, K_INFO, "Memory", 0 },
  { I_BATTERY, K_INFO, "Battery", 0 }, { I_CREDITS, K_INFO, "License", 0 }, { I_RESET, K_ACTION, "Factory reset", 0 },
};

static const SetItem *pageItems(uint8_t page, uint8_t &n)
{
  switch(page)
  {
    case P_SCAN:      n = ITEM_COUNT(scanItems);      return scanItems;
    case P_AUDIO:     n = ITEM_COUNT(audioItems);     return audioItems;
    case P_RECEPTION: n = ITEM_COUNT(receptionItems); return receptionItems;
    case P_DISPLAY:   n = ITEM_COUNT(displayItems);   return displayItems;
    case P_CLOCK:     n = ITEM_COUNT(clockItems);     return clockItems;
    case P_POWER:     n = ITEM_COUNT(powerItems);     return powerItems;
    case P_NAMES:     n = ITEM_COUNT(namesItems);     return namesItems;
    case P_ABOUT:     n = ITEM_COUNT(aboutItems);     return aboutItems;
    default:          n = ITEM_COUNT(rootItems);      return rootItems;
  }
}

static const char *pageTitle(uint8_t page)
{
  if(page == P_ROOT) return "Settings";
  for(size_t i = 0; i < ITEM_COUNT(rootItems); i++) if(rootItems[i].id == page) return rootItems[i].label;
  return "Settings";
}

static uint8_t page = P_ROOT, sel = 0, parentSel = 0;
static bool editing = false;

void settingsOpen() { page = P_ROOT; sel = 0; editing = false; uiShow(SCR_SETTINGS); }
void settingsOpenScanner() { page = P_SCAN; sel = 0; parentSel = 0; editing = false; uiShow(SCR_SETTINGS); }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int wrapi(int v, int n) { return ((v % n) + n) % n; }

static void valueText(uint8_t id, char *b, size_t n)
{
  static const char *const regions[] = { "Americas", "Europe / Africa", "Asia / Pacific" };
  static const char *const sens[] = { "Max", "High", "Normal", "Low" };
  static const char *const boot[] = { "Always", "If list empty", "Never" };
  uint8_t h, m;
  switch(id)
  {
    case D_VISIDLE: { static const char *const o[] = { "Off", "After 30 s", "After 1 min", "After 5 min" }; snprintf(b, n, "%s", o[cfg.visIdle % 4]); break; }
    case S_SPEED:   snprintf(b, n, "%s", scanSpeedName(cfg.scanSpeed)); break;
    case S_EST:     { uint32_t t = scanEstimate(cfg.scanParts, cfg.scanSpeed); if(t < 90) snprintf(b, n, "about %lu s", (unsigned long)t); else snprintf(b, n, "about %lu min", (unsigned long)((t + 30) / 60)); break; }
    case S_SENS:    snprintf(b, n, "%s", sens[cfg.scanSens % 4]); break;
    case S_STANDBY: snprintf(b, n, "%u min", cfg.standbyPause); break;
    case S_OFFGRID: snprintf(b, n, "%s", cfg.scanOffGrid ? "On" : "Off"); break;
    case S_BOOT:    snprintf(b, n, "%s", boot[cfg.scanBoot % 3]); break;
    case S_RUN:     snprintf(b, n, "%lu ch", (unsigned long)scanPlanChannels()); break;
    case S_UNBLOCK: snprintf(b, n, "%u", blockedCount()); break;
    case S_CLEAR:   snprintf(b, n, "%u", stationCount); break;
    case A_VOLUME:  snprintf(b, n, "%u", cfg.volume); break;
    case A_SQUELCH: if(cfg.squelch) snprintf(b, n, "SNR %u dB", cfg.squelch); else snprintf(b, n, "Off"); break;
    case A_SOFTMUTE:snprintf(b, n, "%u dB", cfg.softMute[0]); break;
    case A_AVC:     snprintf(b, n, "%u dB", cfg.avc[0]); break;
    case A_STEREO:  { static const char *const st[] = { "Auto", "Smart", "Mono" }; snprintf(b, n, "%s", st[cfg.fmStereo % 3]); } break;
    case A_CLAMP:   snprintf(b, n, "%s", cfg.audioClamp ? "On" : "Off"); break;
    case R_REGION:  snprintf(b, n, "%s", regions[cfg.region % 3]); break;
    case R_RDS:     snprintf(b, n, "%s", cfg.rds ? "On" : "Off"); break;
    case R_ANTENNA: snprintf(b, n, "%s", cfg.antennaAuto ? "Auto" : "Off"); break;
    case R_BW_AM:   snprintf(b, n, "%s", bandwidthLabel(MODE_AM, cfg.bwIdx[MODE_AM])); break;
    case R_BW_FM:   snprintf(b, n, "%s", bandwidthLabel(MODE_FM, cfg.bwIdx[MODE_FM])); break;
    case R_BW_SSB:  snprintf(b, n, "%s", bandwidthLabel(MODE_USB, cfg.bwIdx[MODE_USB])); break;
    case R_AGC_AM:  if(cfg.agc[1] > 1) snprintf(b, n, "Atten. %u", cfg.agc[1] - 1); else snprintf(b, n, cfg.agc[1] ? "Off" : "Auto"); break;
    case R_AGC_FM:  if(cfg.agc[0] > 1) snprintf(b, n, "Atten. %u", cfg.agc[0] - 1); else snprintf(b, n, cfg.agc[0] ? "Off" : "Auto"); break;
    case R_CAL:     snprintf(b, n, "%+d Hz", cfg.calSSB); break;
    case D_THEME:   snprintf(b, n, "%s", themes[cfg.theme % themeCount()].name); break;
    case D_BRIGHT:  snprintf(b, n, "%u%%", (cfg.brightness * 100 + 127) / 255); break;
    case D_TIMEOUT: if(cfg.screenTimeout) snprintf(b, n, "%u s", cfg.screenTimeout); else snprintf(b, n, "Never"); break;
    case D_HOME:    snprintf(b, n, "%s", cfg.homeScreen ? "Free tuning" : "Stations"); break;
    case D_SCROLL:  snprintf(b, n, "%s", cfg.scrollDir ? "Reversed" : "Normal"); break;
    case C_HOUR:    if(clockLocalHM(h, m)) snprintf(b, n, "%02u", h); else snprintf(b, n, "--"); break;
    case C_MIN:     if(clockLocalHM(h, m)) snprintf(b, n, "%02u", m); else snprintf(b, n, "--"); break;
    case C_OFFSET:  snprintf(b, n, "%+d:%02d", cfg.utcOffset / 4, abs(cfg.utcOffset % 4) * 15); break;
    case C_FORMAT:  snprintf(b, n, "%s", cfg.clock24 ? "24 h" : "12 h"); break;
    case C_AUTO:    snprintf(b, n, "%s", cfg.autoClock ? "On" : "Off"); break;
    case W_SLEEP:   if(cfg.sleepTimer) snprintf(b, n, "%u min", cfg.sleepTimer); else snprintf(b, n, "Off"); break;
    case W_BATTERY:
    case I_BATTERY: snprintf(b, n, "%u%%  %.2f V%s", batteryPercent(), batteryVolts(), batteryCharging() ? "  chg" : ""); break;
    case N_WIFI:    snprintf(b, n, "%s", wifiConfigured() ? wifiSsid() : "Set in editor"); break;
    case N_WCLOCK:  b[0] = '\0'; break;
    case N_STATUS:  if(scheduleCount()) snprintf(b, n, "%lu", (unsigned long)scheduleCount()); else snprintf(b, n, "None"); break;
    case I_VERSION: snprintf(b, n, "%s %s", NOVA_NAME, NOVA_VERSION); break;
    case I_STATIONS:snprintf(b, n, "%u  (%u fav)", stationCount, [](){ uint16_t c = 0; for(uint16_t i = 0; i < stationCount; i++) c += !!(stations[i].flags & ST_FAV); return c; }()); break;
    case I_PSRAM:   snprintf(b, n, "PSRAM %lu MB", (unsigned long)(psramSize() >> 20)); break;
    case I_CREDITS: snprintf(b, n, "GPL-3.0, open source"); break;
    default:        b[0] = '\0'; break;
  }
}

static void setLocalTime(int dh, int dm)
{
  uint8_t h = 12, m = 0;
  clockLocalHM(h, m);
  int hh = wrapi(h + dh, 24), mm = wrapi(m + dm, 60);
  // Today's date is kept when the clock is already set
  uint32_t day = clockValid() ? clockUTC() / 86400 : 20000;
  int32_t local = hh * 3600 + mm * 60 - cfg.utcOffset * 900;
  clockSet(day * 86400 + ((local % 86400) + 86400) % 86400);
}

static void adjust(uint8_t id, int d, int fast)
{
  switch(id)
  {
    case S_SPEED:   cfg.scanSpeed = wrapi(cfg.scanSpeed + d, 3); break;
    case S_P_FM: case S_P_FMLOW: case S_P_MW: case S_P_LW: case S_P_SWBC: case S_P_SWREST:
    {
      static const uint8_t bit[] = { SCAN_FM, SCAN_FMLOW, SCAN_MW, SCAN_LW, SCAN_SWBC, SCAN_SWREST };
      uint8_t b = bit[id - S_P_FM];
      if((cfg.scanParts & ~b & SCAN_ALL) || !(cfg.scanParts & b)) cfg.scanParts ^= b;   // keep at least one
      else uiToast("Keep at least one part", 1200);
      break;
    }
    case S_SENS:    cfg.scanSens = wrapi(cfg.scanSens + d, 4); break;
    case S_STANDBY: { static const uint8_t o[] = { 0, 5, 10, 30, 60 }; int k = 0; for(size_t i = 0; i < ITEM_COUNT(o); i++) if(o[i] == cfg.standbyPause) k = i; cfg.standbyPause = o[wrapi(k + d, ITEM_COUNT(o))]; } break;
    case S_OFFGRID: cfg.scanOffGrid ^= 1; break;
    case S_BOOT:    cfg.scanBoot = wrapi(cfg.scanBoot + d, 3); break;
    case A_VOLUME:  radioSetVolume(clampi(cfg.volume + fast, 0, 63)); break;
    case A_SQUELCH: cfg.squelch = clampi(cfg.squelch + d, 0, 30); break;
    case A_SOFTMUTE:cfg.softMute[0] = cfg.softMute[1] = clampi(cfg.softMute[0] + d, 0, 32); radioApplyAudio(); break;
    case A_AVC:     cfg.avc[0] = cfg.avc[1] = clampi(cfg.avc[0] + d * 2, 12, 90); radioApplyAudio(); break;
    case A_STEREO:  cfg.fmStereo = wrapi(cfg.fmStereo + d, 3); radioApplyAudio(); break;
    case A_CLAMP:   cfg.audioClamp ^= 1; break;
    case R_REGION:  cfg.region = wrapi(cfg.region + d, 3); radioApplyAudio(); radioApplyStep(); break;
    case R_RDS:     cfg.rds ^= 1; if(radioMode() == MODE_FM) radioApply(); break;
    case R_ANTENNA: cfg.antennaAuto ^= 1; break;
    case R_BW_AM:   cfg.bwIdx[MODE_AM] = wrapi(cfg.bwIdx[MODE_AM] + d, bandwidthCount(MODE_AM)); radioApplyAudio(); break;
    case R_BW_FM:   cfg.bwIdx[MODE_FM] = wrapi(cfg.bwIdx[MODE_FM] + d, bandwidthCount(MODE_FM)); radioApplyAudio(); break;
    case R_BW_SSB:  cfg.bwIdx[MODE_LSB] = cfg.bwIdx[MODE_USB] = wrapi(cfg.bwIdx[MODE_USB] + d, bandwidthCount(MODE_USB)); radioApplyAudio(); break;
    case R_AGC_AM:  cfg.agc[1] = clampi(cfg.agc[1] + d, 0, 37); radioApplyAudio(); break;
    case R_AGC_FM:  cfg.agc[0] = clampi(cfg.agc[0] + d, 0, 27); radioApplyAudio(); break;
    case R_CAL:     cfg.calSSB = clampi(cfg.calSSB + fast * 10, -2000, 2000); if(modeIsSSB(radioMode())) radioSetFreq(radioFreq()); break;
    case D_THEME:   cfg.theme = wrapi(cfg.theme + d, themeCount()); break;
    case D_BRIGHT:  cfg.brightness = clampi(cfg.brightness + fast * 5, 10, 255); backlight(cfg.brightness); break;
    case D_TIMEOUT:
    {
      static const uint16_t opts[] = { 0, 15, 30, 60, 120, 300, 600 };
      int k = 0; for(size_t i = 0; i < ITEM_COUNT(opts); i++) if(opts[i] == cfg.screenTimeout) k = i;
      cfg.screenTimeout = opts[wrapi(k + d, ITEM_COUNT(opts))];
      break;
    }
    case D_HOME:    cfg.homeScreen ^= 1; break;
    case D_SCROLL:  cfg.scrollDir ^= 1; break;
    case D_CAPS:    cfg.upperCase ^= 1; break;
    case D_VISECO:  cfg.visEco ^= 1; break;
    case D_BOOTANIM: cfg.bootAnim ^= 1; break;
    case D_VISIDLE: cfg.visIdle = wrapi(cfg.visIdle + d, 4); break;
    case C_HOUR:    setLocalTime(d, 0); break;
    case C_MIN:     setLocalTime(0, fast); break;
    case C_OFFSET:  cfg.utcOffset = clampi(cfg.utcOffset + d, -48, 56); break;
    case C_FORMAT:  cfg.clock24 ^= 1; break;
    case C_AUTO:    cfg.autoClock ^= 1; break;
    case W_SLEEP:   cfg.sleepTimer = clampi(cfg.sleepTimer + d * 15, 0, 240); sleepTimerRestart(); break;
  }
  configSave();
}

static void doUnblock() { blockedClear(); uiToast("Blocked list cleared"); }
static void doClear() { stationsClear(); uiToast("Station list cleared"); }
static void doReset()
{
  storeErase();
#ifndef NOVA_SIM
  esp_restart();
#endif
}

static void activate(const SetItem &it)
{
  switch(it.id)
  {
    case S_RUN:     scanMenuOpen(SCR_SETTINGS); return;
    case S_UNBLOCK: uiConfirm("Unblock all?", "Blocked frequencies can be found again", "Unblock", doUnblock); return;
    case S_CLEAR:   uiConfirm("Clear the station list?", "Favorites are deleted too", "Clear", doClear); return;
    case I_RESET:   uiConfirm("Factory reset?", "All settings and stations are erased", "Reset", doReset); return;
    case N_WIFI:
    case N_WCLOCK:
    {
      if(!wifiConfigured()) { uiToast("Save your Wi-Fi network in the web editor first", 2500); return; }
      char msg[96];
      bool eibi = it.id == N_WIFI;
      bool ok = wifiRun(eibi ? WIFI_JOB_EIBI : WIFI_JOB_TEST, nullptr, msg, sizeof(msg));
      char buf[128];
      if(ok && eibi) snprintf(buf, sizeof(buf), "%s schedules loaded", msg);
      else snprintf(buf, sizeof(buf), "%s", ok ? (clockValid() ? "Clock set" : "Joined, but no time server") : msg);
      uiShow(SCR_SETTINGS);
      uiToast(buf, 3000);
      return;
    }
  }
}

void settingsInput(const Input &in)
{
  uint8_t n;
  const SetItem *it = pageItems(page, n);

  if(editing)
  {
    if(in.turn) adjust(it[sel].id, in.turn > 0 ? 1 : -1, in.turnFast);
    if(in.click || in.hold) editing = false;
    return;
  }

  if(in.hold)
  {
    if(page == P_ROOT) uiGoHome();
    else { page = P_ROOT; sel = parentSel; }
    return;
  }
  if(in.turn) sel = wrapi(sel + (in.turn > 0 ? 1 : -1), n);
  if(in.click)
  {
    switch(it[sel].kind)
    {
      case K_PAGE:   parentSel = sel; page = it[sel].id; sel = 0; break;
      case K_VALUE:
      {
        bool on;
        if(isToggle(it[sel].id, on)) adjust(it[sel].id, 1, 1);
        else editing = true;
        break;
      }
      case K_ACTION: activate(it[sel]); break;
      default: break;
    }
  }
}

bool settingsTick() { return false; }

void settingsDraw()
{
  uint8_t n;
  const SetItem *it = pageItems(page, n);
  char title[40];
  snprintf(title, sizeof(title), "%s", pageTitle(page));
  uiStatusBar(title, T.text);

  const int rows = 5, rowH = 29, y0 = 21;
  int top = sel - 2;
  if(top > n - rows) top = n - rows;
  if(top < 0) top = 0;
  char val[40];
  for(int r = 0; r < rows && top + r < n; r++)
  {
    int i = top + r, y = y0 + r * rowH;
    bool hl = i == sel;
    bool edit = hl && editing;
    if(hl) uiGloss(6, y, 302, rowH - 3, 10, T.sel); else uiRRect(6, y, 302, rowH - 3, 10, T.surface);
    uint16_t fg = hl ? T.selText : (it[i].kind == K_INFO ? T.dim : T.text);
    int x = 14;
    if(page == P_ROOT) { uiTile(24, y + rowH / 2 - 1, 20, rootColors[i % ITEM_COUNT(rootColors)], it[i].icon); x = 42; }
    uiText(it[i].label, x, y + rowH / 2 - 1, F_SMB, fg, ML_DATUM);

    if(it[i].kind == K_PAGE)
    {
      int cx = 296, cy = y + rowH / 2 - 1;
      spr.fillTriangle(cx - 3, cy - 5, cx - 3, cy + 5, cx + 3, cy, hl ? T.selText : T.dim);
    }
    else if(bool on = false; isToggle(it[i].id, on)) uiToggle(266, y + 3, on);
    else
    {
      valueText(it[i].id, val, sizeof(val));
      if(val[0])
      {
        if(edit)
        {
          spr.setFont(F_SMB);
          int w = spr.textWidth(val) + 20;
          uiRRect(302 - w, y + 3, w, rowH - 9, 9, T.accent);
          uiText(val, 302 - w / 2, y + rowH / 2 - 1, F_SMB, T.accentText, MC_DATUM);
        }
        else uiLine(val, 300, y + rowH / 2 - 1, 130, F_SM, hl ? T.selText : T.dim, MR_DATUM);
      }
    }
  }
  uiScrollbar(314, y0, rows * rowH - 3, n, rows, top);
}
