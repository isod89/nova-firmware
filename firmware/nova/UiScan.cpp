//
// UiScan.cpp - "Search stations": choose what to search and how fast,
// with the time each choice takes
//
#include "Ui.h"

struct Choice { const char *name; uint8_t parts; };   // parts 0 = the parts chosen in Settings
static const Choice choices[] =
{
  { "Everything you chose", 0 },
  { "FM", SCAN_FM },
  { "AM and long wave", SCAN_MW | SCAN_LW },
  { "Shortwave bands", SCAN_SWBC },
  { "All shortwave", SCAN_SWBC | SCAN_SWREST },
};
enum { ROW_SPEED = ITEM_COUNT(choices), ROW_MORE, ROW_COUNT };

static uint8_t from = SCR_STATIONS, sel = 0;
static uint32_t est[ITEM_COUNT(choices)];

static void refresh()
{
  for(size_t i = 0; i < ITEM_COUNT(choices); i++)
    est[i] = scanEstimate(choices[i].parts ? choices[i].parts : cfg.scanParts, cfg.scanSpeed);
}

void scanMenuOpen(uint8_t back)
{
  from = back;
  sel = 0;
  refresh();
  uiShow(SCR_SEARCH);
}

static void partsText(uint8_t p, char *b, size_t n)
{
  b[0] = '\0';
  static const struct { uint8_t bit; const char *t; } names[] =
  { { SCAN_FM, "FM" }, { SCAN_FMLOW, "FM low" }, { SCAN_MW, "MW" }, { SCAN_LW, "LW" }, { SCAN_SWBC, "SW bands" }, { SCAN_SWREST, "SW rest" } };
  for(auto &x : names)
    if(p & x.bit) { if(b[0]) strncat(b, " \xC2\xB7 ", n - strlen(b) - 1); strncat(b, x.t, n - strlen(b) - 1); }
}

static void timeText(uint32_t t, char *b, size_t n)
{
  if(t < 90) snprintf(b, n, "%lu s", (unsigned long)t);
  else snprintf(b, n, "%lu min", (unsigned long)((t + 30) / 60));
}

void scanMenuInput(const Input &in)
{
  if(in.hold) { if(from == SCR_SETTINGS) settingsOpenScanner(); else uiShow(from); return; }
  if(in.turn) sel = (sel + ROW_COUNT + (in.turn > 0 ? 1 : -1)) % ROW_COUNT;
  if(!in.click) return;
  if(sel == ROW_SPEED) { cfg.scanSpeed = (cfg.scanSpeed + 1) % 3; configSave(); refresh(); return; }
  if(sel == ROW_MORE) { settingsOpenScanner(); return; }
  const Choice &c = choices[sel];
  scanOverride(c.parts ? c.parts : cfg.scanParts, cfg.scanSpeed);
  uiStartScan();
}

void scanMenuDraw()
{
  uiStatusBar("Search stations", T.text);
  const int rows = 4, rowH = 30, y0 = 22;
  int top = sel - 1;
  if(top > ROW_COUNT - rows) top = ROW_COUNT - rows;
  if(top < 0) top = 0;
  char sub[48], t[16], name[40];
  for(int r = 0; r < rows && top + r < ROW_COUNT; r++)
  {
    int i = top + r, y = y0 + r * rowH;
    bool hl = i == sel;
    uint16_t fg = hl ? T.selText : T.text, dim = hl ? T.selText : T.dim;
    if(hl) uiGloss(6, y, 302, rowH - 3, 10, T.sel); else uiRRect(6, y, 302, rowH - 3, 10, T.surface);
    if(i < (int)ITEM_COUNT(choices))
    {
      const Choice &c = choices[i];
      uiTile(24, y + 13, 20, i == 0 ? C_GREEN : C_TEAL, IC_RADAR);
      if(i == 0)
      {
        partsText(cfg.scanParts, sub, sizeof(sub));
        char fit[48];
        uiFit(fit, sizeof(fit), sub, F_CAP, 150);
        uiText(c.name, 42, y + 8, F_SMB, fg, ML_DATUM);
        uiText(fit, 42, y + 20, F_CAP, dim, ML_DATUM);
      }
      else uiText(c.name, 42, y + 13, F_SMB, fg, ML_DATUM);
      timeText(est[i], t, sizeof(t));
      snprintf(name, sizeof(name), "~ %s", t);
      uiText(name, 298, y + 13, F_SMB, dim, MR_DATUM);
    }
    else if(i == ROW_SPEED)
    {
      uiTile(24, y + 13, 20, C_ORANGE, IC_DIAL);
      uiText("Speed", 42, y + 13, F_SMB, fg, ML_DATUM);
      uiText(scanSpeedName(cfg.scanSpeed), 298, y + 13, F_SMB, hl ? T.selText : T.accent, MR_DATUM);
    }
    else
    {
      uiTile(24, y + 13, 20, C_GRAY, IC_GEAR);
      uiText("More options", 42, y + 13, F_SMB, fg, ML_DATUM);
      int cx = 296, cy = y + 13;
      spr.drawLine(cx - 2, cy - 4, cx + 2, cy, dim);
      spr.drawLine(cx + 2, cy, cx - 2, cy + 4, dim);
    }
  }
  uiScrollbar(312, y0, rows * rowH - 3, ROW_COUNT, rows, top);
  const char *hint = sel == ROW_SPEED ? "Fast skips the second looks; Thorough finds the faintest"
                   : sel == ROW_MORE ? "Choose the parts, sensitivity, off-grid search"
                   : "Click to start  \xC2\xB7  press the knob to stop";
  uiLine(hint, 160, 156, 304, F_CAP, T.dim, MC_DATUM);
}
