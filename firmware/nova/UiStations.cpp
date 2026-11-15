//
// UiStations.cpp - home screen: every station found, one turn away
//
//   Turn          next / previous station (tuned at once)
//   Push + turn   filter: All, Favorites, New, FM, AM, Shortwave
//   Click         volume
//   Hold          menu
//
#include "Ui.h"

#define ROWS 3
#define ROW_H 25
#define LIST_Y 92

static int16_t sel = 0;
static int16_t pending = -1;
static uint32_t pendingAt = 0;
static uint32_t filterHudUntil = 0;

static void selectTuned()
{
  int16_t t = stationTuned();
  for(uint16_t v = 0; v < viewCount; v++) if(view[v] == t) { sel = v; return; }
  if(sel >= viewCount) sel = viewCount ? viewCount - 1 : 0;
}

void stationsEnter() { pending = -1; selectTuned(); }

static void tuneTo(uint16_t i) { radioTune(stations[i].freq, stations[i].mode); }

void stationsAfterScan(uint32_t oldFreq, uint8_t oldMode)
{
  int16_t t = stationFind(oldFreq, groupOf(oldFreq, oldMode));
  if(t < 0 && viewCount) t = view[0];
  if(t >= 0) tuneTo(t);
  selectTuned();
}

void stationsInput(const Input &in)
{
  if(in.hold) { menuOpen(SCR_STATIONS); return; }
  if(volumeActive()) { volumeInput(in); return; }

  if(in.pushTurn)
  {
    cfg.listFilter = (cfg.listFilter + (in.turn > 0 ? 1 : FILTER_COUNT - 1)) % FILTER_COUNT;
    configSave();
    viewRebuild();
    selectTuned();
    filterHudUntil = millis() + 1500;
    return;
  }

  if(in.turn && viewCount)
  {
    int16_t d = cfg.scrollDir ? -in.turn : in.turn;
    sel = ((sel + d) % (int)viewCount + viewCount) % viewCount;
    pending = sel;
    pendingAt = millis();
    return;
  }

  if(in.click)
  {
    if(!stationCount) { uiStartScan(); return; }
    if(!viewCount) { cfg.listFilter = 0; viewRebuild(); selectTuned(); return; }
    volumeOpen();
  }
}

bool stationsUiTick()
{
  bool redraw = false;
  if(filterHudUntil && (int32_t)(millis() - filterHudUntil) >= 0) { filterHudUntil = 0; redraw = true; }
  if(pending >= 0 && pending < viewCount)
  {
    const Station &s = stations[view[pending]];
    if(millis() - pendingAt >= (s.mode == radioMode() ? 120UL : 350UL))
    {
      tuneTo(view[pending]);
      pending = -1;
      redraw = true;
    }
  }
  else pending = -1;

  // Listening to a new station: it is not new any more
  static uint32_t seenCheck = 0;
  if(pending < 0 && millis() - seenCheck > 3000)
  {
    seenCheck = millis();
    int16_t t = stationTuned();
    if(t >= 0 && (stations[t].flags & ST_NEW)) { stationSeen(t); redraw = true; }
  }
  return redraw;
}

static void drawHero()
{
  char name[64], f[24], line[96], buf[96];
  uiRRect(6, 22, 308, 64, 16, T.surface);

  if(!viewCount)
  {
    uiTile(38, 54, 40, C_GREEN, IC_RADAR);
    uiText(stationCount ? "Nothing in this list" : "No stations yet", 70, 44, F_MD, T.text, ML_DATUM);
    uiText(stationCount ? "Click to show all stations" : "Click to search all bands", 70, 66, F_SM, T.dim, ML_DATUM);
    return;
  }

  uint16_t i = view[sel];
  const Station &s = stations[i];
  bool tuned = pending < 0 && stationTuned() == (int16_t)i;
  uint16_t gc = uiGroupColor(s.group);

  // Artwork tile
  uiRRect(14, 30, 48, 48, 12, gc);
  uint16_t on = uiOnColor(gc);
  if(s.group == GROUP_FM)
  {
    char mhz[12];
    snprintf(mhz, sizeof(mhz), "%lu.%lu", (unsigned long)(s.freq / 1000000), (unsigned long)((s.freq % 1000000) / 100000));
    uiText("FM", 38, 47, F_MD, on, MC_DATUM);
    uiText(mhz, 38, 66, F_CAP, on, MC_DATUM);
  }
  else
  {
    uiText(uiGroupShort(s.group), 38, 47, F_MD, on, MC_DATUM);
    uiText(uiShortBand(s.freq, s.mode), 38, 66, F_CAP, on, MC_DATUM);
  }

  // Title and subtitle
  stationLabel(i, name, sizeof(name), tuned);
  uiFormatFreq(f, sizeof(f), s.freq, s.mode, true);
  const char *title = name[0] ? name : f;
  int maxw = 196;
  const lgfx::IFont *tf = uiTextWidth(title, F_LG) <= maxw ? F_LG : F_MD;
  uiFit(buf, sizeof(buf), title, tf, maxw);
  uiText(buf, 72, 44, tf, (s.rssi || tuned) ? T.text : T.dim, ML_DATUM);

  const char *extra = bandLabel(s.freq, s.mode);
  if(tuned && s.group == GROUP_FM && rdsText()[0]) extra = rdsText();
  if(!s.rssi && !tuned) extra = "Not heard on the last scan";
  if(name[0]) snprintf(line, sizeof(line), "%s  \xC2\xB7  %s", f, extra);
  else snprintf(line, sizeof(line), "%s", extra);
  if(s.mode != MODE_FM && s.mode != MODE_AM) { snprintf(buf, sizeof(buf), "%s  %.80s", modeName(s.mode), line); snprintf(line, sizeof(line), "%s", buf); }
  uiFit(buf, sizeof(buf), line, F_SM, 228);
  uiText(buf, 72, 68, F_SM, T.dim, ML_DATUM);

  // Signal and badges
  Signal sg = radioSignal();
  uiBars(282, 30, 16, tuned ? uiSignalLevel(sg.rssi, sg.snr) : uiSignalLevel(s.rssi, s.snr), gc, T.barOff, 4, 2);
  if(s.flags & ST_FAV) uiStar(296, 66, 7, T.fav);
  if(s.flags & ST_NEW) uiBadge(250, 59, T.accent, T.accentText, "NEW");
}

static void drawRows()
{
  char buf[48], f[16], name[40];
  if(!viewCount)
  {
    uiLine("Turn  \xC2\xB7  change station", 160, 106, 304, F_SM, T.dim, MC_DATUM);
    uiLine("Click  \xC2\xB7  volume      Hold  \xC2\xB7  menu", 160, 128, 304, F_SM, T.dim, MC_DATUM);
    uiLine("Hold 2 s  \xC2\xB7  power and standby", 160, 150, 304, F_SM, T.dim, MC_DATUM);
    return;
  }

  int top = sel - 1;
  if(top > (int)viewCount - ROWS) top = viewCount - ROWS;
  if(top < 0) top = 0;
  int16_t tuned = stationTuned();

  for(int r = 0; r < ROWS && top + r < viewCount; r++)
  {
    int v = top + r;
    uint16_t i = view[v];
    const Station &s = stations[i];
    int y = LIST_Y + r * ROW_H;
    bool hl = v == sel;
    if(hl) uiGloss(6, y, 302, ROW_H - 2, 10, T.sel);
    uint16_t fg = hl ? T.selText : (s.rssi ? T.text : T.dim);
    uint16_t dim = hl ? T.selText : T.dim;

    spr.fillCircle(18, y + 11, 4, uiGroupColor(s.group));
    uiFormatFreq(f, sizeof(f), s.freq, s.mode, false);
    stationLabel(i, name, sizeof(name), (int16_t)i == tuned);

    int right = 300;
    uiText(f, right, y + 11, F_SM, dim, MR_DATUM);
    right -= uiTextWidth(f, F_SM) + 8;
    if(s.flags & ST_FAV) { uiStar(right - 6, y + 11, 6, hl ? T.selText : T.fav); right -= 18; }
    if(s.flags & ST_NEW)
    {
      int bw = uiTextWidth("NEW", F_CAP) + 10;
      uiBadge(right - bw, y + 4, hl ? T.selText : T.accent, hl ? T.sel : T.accentText, "NEW");
      right -= bw + 6;
    }

    const char *primary = name[0] ? name : bandLabel(s.freq, s.mode);
    uiFit(buf, sizeof(buf), primary, F_SMB, right - 34);
    uiText(buf, 30, y + 11, F_SMB, name[0] ? fg : dim, ML_DATUM);
  }
  uiScrollbar(312, LIST_Y, ROWS * ROW_H - 2, viewCount, ROWS, top);
}

static void drawFilterHud()
{
  static const char *const labels[FILTER_COUNT] = { "All", "\xE2\x80\xA6", "New", "FM", "AM", "SW" };
  uiRRect(6, 96, 308, 40, 14, T.surface2);
  int w = 300 / FILTER_COUNT;
  for(int k = 0; k < FILTER_COUNT; k++)
  {
    int x = 10 + k * w;
    bool on = k == cfg.listFilter;
    if(on) uiGloss(x, 100, w - 2, 32, 10, T.sel);
    uint16_t c = on ? T.selText : T.text;
    if(k == 1) uiStar(x + w / 2 - 1, 116, 7, on ? T.selText : T.fav);
    else uiText(labels[k], x + w / 2 - 1, 116, F_SMB, c, MC_DATUM);
  }
}

void stationsDraw()
{
  char title[40];
  if(viewCount) snprintf(title, sizeof(title), "%s  %u/%u", filterName(cfg.listFilter), sel + 1, viewCount);
  else snprintf(title, sizeof(title), "%s", filterName(cfg.listFilter));
  uiStatusBar(title, T.dim);
  drawHero();
  if(filterHudUntil) drawFilterHud();
  else if(volumeActive()) { drawRows(); volumeDraw(); }
  else drawRows();
}
