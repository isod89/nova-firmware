//
// Ui.cpp - design system (themes, type, primitives), screen manager,
// dialogs, scan screen and standby search
//
#include "Ui.h"
#include "Fonts.h"
#include <math.h>

const Theme themes[] =
{
  //  name         bg      surface surface2 line    text    dim     accent  accTxt  sel     selTxt  good    warn    bad     fm      am      sw      barOff  fav
  { "Dark",      0x0000, 0x18E3, 0x2965, 0x39C7, 0xFFFF, 0x8C72, 0x0C3F, 0xFFFF, 0x0C3F, 0xFFFF, 0x368B, 0xFCE1, 0xFA27, 0x669F, 0xFCE1, 0x368B, 0x39C7, 0xFEA1 },
  { "Light",     0xF79E, 0xFFFF, 0xE73D, 0xD69A, 0x0000, 0x6B6E, 0x03DF, 0xFFFF, 0x03DF, 0xFFFF, 0x362B, 0xFCA0, 0xF9C6, 0x357C, 0xFCA0, 0x362B, 0xD69A, 0xFE60 },
  { "Material",  0x1082, 0x18E3, 0x2965, 0x39C7, 0xEF5D, 0x9D14, 0x8DBF, 0x08E7, 0x8DBF, 0x08E7, 0x8652, 0xFEAC, 0xF450, 0x8DBF, 0xFD6E, 0x8652, 0x3A08, 0xFEAC },
  { "Night red", 0x0000, 0x1000, 0x2000, 0x3800, 0xFA08, 0xA104, 0xF986, 0x0000, 0x7800, 0xFD96, 0xFA8A, 0xFA8A, 0xFA8A, 0xFB0C, 0xE208, 0xC986, 0x2800, 0xFB8E },
  { "Amber",     0x0840, 0x1880, 0x20C0, 0x3940, 0xFE07, 0xB424, 0xFD80, 0x1880, 0xFD80, 0x1880, 0xFE8C, 0xFD80, 0xFBC8, 0xFE8C, 0xFD80, 0xED07, 0x3120, 0xFF11 },
  { "NOVA",          0x0863, 0x10C6, 0x1928, 0x29AA, 0xF7BF, 0x8C96, 0x7B7F, 0xFFFF, 0x7B7F, 0xFFFF, 0x3EF2, 0xFDA8, 0xFAEF, 0x5F3F, 0xFDA8, 0x3EF2, 0x29AA, 0xF55F },
  { "Ocean",         0x00A4, 0x0107, 0x098A, 0x122D, 0xE7BF, 0x7D38, 0x05BB, 0x00A4, 0x05BB, 0x00A4, 0x2EB7, 0xFDE4, 0xFB8E, 0x3DFF, 0xFDE4, 0x2EB7, 0x122D, 0xFF31 },
  { "Forest",        0x08A2, 0x1103, 0x1984, 0x2A26, 0xEFBD, 0x8D93, 0x4E11, 0x08A2, 0x4E11, 0x08A2, 0x8775, 0xFE62, 0xFB8E, 0x7E9F, 0xFDCE, 0x8775, 0x2A26, 0xFF08 },
  { "Sunset",        0x1862, 0x28C4, 0x3905, 0x4966, 0xFF9D, 0xC4D3, 0xFBCB, 0x1862, 0xFBCB, 0x1862, 0x7F14, 0xFE4A, 0xFA6D, 0xFCF9, 0xFE4A, 0x7F14, 0x4966, 0xFE8C },
  { "Graphite",      0x1082, 0x18E3, 0x2945, 0x39C7, 0xEF7D, 0x9CD3, 0xE71C, 0x1082, 0xE71C, 0x1082, 0x7ED1, 0xF609, 0xF38C, 0xA63F, 0xF609, 0x7ED1, 0x39C7, 0xF609 },
  { "Nord",          0x29A8, 0x3A0A, 0x426B, 0x4AAD, 0xEF7E, 0xA557, 0x8E1A, 0x29A8, 0x8E1A, 0x29A8, 0xA5F1, 0xEE51, 0xBB0D, 0x8518, 0xD42E, 0xA5F1, 0x4AAD, 0xEE51 },
  { "Dracula",       0x2946, 0x31A8, 0x3A0A, 0x4A8D, 0xFFDE, 0xA537, 0xBC9F, 0x2946, 0xBC9F, 0x2946, 0x57CF, 0xF7D1, 0xFAAA, 0x8F5F, 0xFDCD, 0x57CF, 0x4A8D, 0xFBD8 },
  { "Solarized",     0x0146, 0x01A8, 0x0A0A, 0x2AAC, 0xEF5A, 0x9514, 0x245A, 0xFFBC, 0x245A, 0xFFBC, 0x84C0, 0xB440, 0xD985, 0x2D13, 0xCA42, 0x84C0, 0x2AAC, 0xD1B0 },
  { "Matrix",        0x0040, 0x00C1, 0x0142, 0x0203, 0x3FEF, 0x1D09, 0x07EC, 0x00C1, 0x07EC, 0x00C1, 0x7FF4, 0xCFE7, 0xFACB, 0x3FEF, 0x9FEB, 0x06AA, 0x0203, 0xCFE7 },
  { "Retro LCD",     0xADD1, 0xBE33, 0x9D6F, 0x8CCD, 0x1942, 0x4AC6, 0x29E3, 0xCEB5, 0x29E3, 0xCEB5, 0x2AE3, 0x6AC2, 0x78E2, 0x19C9, 0x6A42, 0x2AE3, 0x8CCD, 0x5A42 },
  { "Paper",         0xF79D, 0xFFFF, 0xEF3B, 0xDEB8, 0x2924, 0x7B8C, 0xC201, 0xFFFF, 0xC201, 0xFFFF, 0x1407, 0xB281, 0xB8E3, 0x0354, 0xC201, 0x1407, 0xDEB8, 0xCC40 },
  { "Arctic",        0xEF9F, 0xFFFF, 0xE75E, 0xCEBC, 0x08A5, 0x63B1, 0x0D3D, 0xFFFF, 0x0D3D, 0xFFFF, 0x1509, 0xDBA0, 0xD924, 0x0438, 0xEAC1, 0x1509, 0xCEBC, 0xED81 },
  { "Rose",          0x1883, 0x28E4, 0x3926, 0x49A8, 0xFF5E, 0xC4D6, 0xFB75, 0x1883, 0xFB75, 0x1883, 0x8F16, 0xFE4D, 0xFAEB, 0xBD3F, 0xFE4D, 0x8F16, 0x49A8, 0xFE9D },
  { "High contrast", 0x0000, 0x1082, 0x2104, 0xFFFF, 0xFFFF, 0xE73C, 0xFFE0, 0x0000, 0xFFE0, 0x0000, 0x07E0, 0xFFE0, 0xF986, 0x07FF, 0xFD00, 0x07E0, 0x4208, 0xFFE0 },
};
uint8_t themeCount() { return ITEM_COUNT(themes); }
const char *themeNameAt(uint8_t i) { return themes[i < ITEM_COUNT(themes) ? i : 0].name; }
uint16_t uiReadPixel(int x, int y) { return spr.readPixel(x, y); }

//
// Fonts
//
static lgfx::PointerWrapper fontData[7];
static lgfx::VLWfont fontObj[7];
const lgfx::IFont *F_CAP = &lgfx::fonts::Font2, *F_SM = &lgfx::fonts::Font2, *F_SMB = &lgfx::fonts::Font2,
                  *F_MD = &lgfx::fonts::Font4, *F_LG = &lgfx::fonts::Font4, *F_XL = &lgfx::fonts::Font7, *F_XLS = &lgfx::fonts::Font4;

void uiFontsInit()
{
  const uint8_t *src[7] = { font_caption, font_body, font_body_b, font_title, font_large, font_freq, font_freq_s };
  const lgfx::IFont **dst[7] = { &F_CAP, &F_SM, &F_SMB, &F_MD, &F_LG, &F_XL, &F_XLS };
  for(int i = 0; i < 7; i++)
  {
    fontData[i].set(src[i]);
    if(fontObj[i].loadFont(&fontData[i])) *dst[i] = &fontObj[i];
  }
}

static uint8_t screen = SCR_STATIONS;
static bool needDraw = true;
static char toastText[48] = "";
static uint32_t toastUntil = 0;

static bool confirmOn = false;
static const char *confirmTitle, *confirmText, *confirmYes;
static ConfirmFn confirmFn;
static uint8_t confirmSel = 0;

static bool powerOn = false;
static uint8_t powerSel = 0;

//
// Primitives
//
// Capital letters (Display > Capital letters): everything is shown in
// capitals for legibility, except unit symbols whose case carries meaning
const char *uiCaps(const char *s)
{
  static char out[2][160];
  static uint8_t k = 0;
  if(!cfg.upperCase || !s) return s;
  char *o = out[k ^= 1];
  size_t n = 0;
  while(*s && n + 1 < sizeof(out[0]))
  {
    if((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z'))
    {
      const char *e = s;
      while((*e >= 'a' && *e <= 'z') || (*e >= 'A' && *e <= 'Z')) e++;
      size_t len = e - s;
      static const char *const keep[] = { "kHz", "MHz", "Hz", "dBuV", "dB", "ms", "k", "s", "dBm" };
      bool unit = false;
      for(auto u : keep) if(strlen(u) == len && !strncmp(u, s, len)) { unit = true; break; }
      for(size_t i = 0; i < len && n + 1 < sizeof(out[0]); i++) o[n++] = unit ? s[i] : (s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
      s = e;
    }
    else o[n++] = *s++;
  }
  o[n] = '\0';
  return o;
}

void uiText(const char *s, int x, int y, const lgfx::IFont *f, uint16_t color, uint8_t datum)
{
  spr.setFont(f);
  spr.setTextDatum(datum);
  spr.setTextColor(color);
  spr.drawString(uiCaps(s), x, y);
}

int uiTextWidth(const char *s, const lgfx::IFont *f) { spr.setFont(f); return spr.textWidth(uiCaps(s)); }

// Anti-aliased rounded rectangle (plain one when too small to matter)
void uiRRect(int x, int y, int w, int h, int r, uint16_t c)
{
  if(r >= 3 && w > 2 * r && h >= 2 * r) spr.fillSmoothRoundRect(x, y, w, h, r, c);
  else spr.fillRoundRect(x, y, w, h, r, c);
}

uint16_t uiMix(uint16_t a, uint16_t b, uint8_t t)
{
  int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
  return ((ar + (br - ar) * t / 255) << 11) | ((ag + (bg - ag) * t / 255) << 5) | (ab + (bb - ab) * t / 255);
}

// Rounded rectangle with a soft light from the top: tiles, pills, selections
void uiGloss(int x, int y, int w, int h, int r, uint16_t c)
{
  uiRRect(x, y, w, h, r, c);
  int gh = h * 3 / 5;
  for(int i = 1; i < gh; i++)
  {
    int in = 1;
    int ry = i < r ? r - i : 0;                       // corner inset for this row
    if(ry) { float dy = ry; in = 1 + r - (int)sqrtf((float)r * r - dy * dy); }
    int lw = w - 2 * in;
    if(lw <= 0) continue;
    spr.drawFastHLine(x + in, y + i, lw, uiMix(c, 0xFFFF, (uint8_t)(48 - 48 * i / gh)));
  }
}

// Centered line that always fits: its font, then the caption font, then cut
void uiLine(const char *s, int x, int y, int maxw, const lgfx::IFont *f, uint16_t color, uint8_t datum)
{
  char buf[128];
  if(uiTextWidth(s, f) > maxw && f != F_CAP) f = F_CAP;
  uiFit(buf, sizeof(buf), s, f, maxw);
  uiText(buf, x, y, f, color, datum);
}

void uiFit(char *buf, size_t size, const char *text, const lgfx::IFont *font, int maxw)
{
  snprintf(buf, size, "%s", uiCaps(text));
  spr.setFont(font);
  if(spr.textWidth(buf) <= maxw) return;
  size_t n = strlen(buf);
  char probe[96];
  while(n > 1)
  {
    buf[--n] = '\0';
    while(n && buf[n - 1] == ' ') buf[--n] = '\0';
    snprintf(probe, sizeof(probe), "%s\xE2\x80\xA6", buf);   // ellipsis
    if(spr.textWidth(probe) <= maxw) { snprintf(buf, size, "%s", probe); return; }
  }
}

void uiPill(int x, int y, int w, int h, uint16_t bg, uint16_t fg, const char *text, const lgfx::IFont *font)
{
  uiGloss(x, y, w, h, h / 2, bg);
  uiText(text, x + w / 2, y + h / 2, font, fg, MC_DATUM);
}

void uiBadge(int x, int y, uint16_t bg, uint16_t fg, const char *text)
{
  int w = uiTextWidth(text, F_CAP) + 10;
  uiRRect(x, y, w, 15, 7, bg);
  uiText(text, x + w / 2, y + 7, F_CAP, fg, MC_DATUM);
}

void uiBars(int x, int y, int h, uint8_t level, uint16_t on, uint16_t off, int bw, int gap)
{
  for(int i = 0; i < 5; i++)
  {
    int bh = 3 + (h - 3) * i / 4;
    uiRRect(x + i * (bw + gap), y + h - bh, bw, bh, 1, i < level ? on : off);
  }
}

void uiStar(int cx, int cy, int r, uint16_t color)
{
  // Five-pointed star from three triangles
  float pts[10][2];
  for(int i = 0; i < 10; i++)
  {
    float a = -1.5708f + i * 0.6283f;
    float rr = (i & 1) ? r * 0.45f : r;
    pts[i][0] = cx + rr * cosf(a);
    pts[i][1] = cy + rr * sinf(a);
  }
  for(int i = 0; i < 10; i += 2)
    spr.fillTriangle(cx, cy, pts[i][0], pts[i][1], pts[(i + 1) % 10][0], pts[(i + 1) % 10][1], color),
    spr.fillTriangle(cx, cy, pts[i][0], pts[i][1], pts[(i + 9) % 10][0], pts[(i + 9) % 10][1], color);
}

void uiToggle(int x, int y, bool on)
{
  uiRRect(x, y, 34, 20, 10, on ? T.good : T.barOff);
  spr.fillSmoothCircle(on ? x + 24 : x + 10, y + 10, 8, 0xFFFF);
}

void uiTile(int cx, int cy, int size, uint16_t color, uint8_t icon)
{
  uiGloss(cx - size / 2, cy - size / 2, size, size, size / 4, color);
  uiIcon(icon, cx, cy, 0xFFFF);
}

void uiRing(int cx, int cy, int r, int thick, uint8_t pct, uint16_t color)
{
  spr.fillArc(cx, cy, r, r - thick, 0, 360, T.barOff);
  if(pct) spr.fillArc(cx, cy, r, r - thick, 270, 270 + 360 * pct / 100, color);
}

uint8_t uiSignalLevel(uint8_t rssi, uint8_t snr)
{
  if(!rssi && !snr) return 0;
  return snr >= 25 ? 5 : snr >= 18 ? 4 : snr >= 12 ? 3 : snr >= 6 ? 2 : 1;
}

uint16_t uiGroupColor(uint8_t g) { return g == GROUP_FM ? T.fm : g == GROUP_AM ? T.am : T.sw; }

uint16_t uiOnColor(uint16_t c)
{
  int r = (c >> 11) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31;
  return (r * 299 + g * 587 + b * 114) / 1000 > 150 ? 0x0000 : 0xFFFF;
}
const char *uiGroupShort(uint8_t g) { return g == GROUP_FM ? "FM" : g == GROUP_AM ? "AM" : "SW"; }

const char *uiShortBand(uint32_t hz, uint8_t mode)
{
  static char b[8];
  if(mode == MODE_FM) return "FM";
  if(hz < 300000) return "LW";
  if(hz < 1711000) return "MW";
  const char *l = bandLabel(hz, mode);
  if(l[0] >= '1' && l[0] <= '9' && strchr(l, 'm'))
  {
    size_t n = strchr(l, 'm') - l + 1;
    snprintf(b, sizeof(b), "%.*s", (int)n, l);
    return b;
  }
  return strstr(l, "CB") ? "CB" : "SW";
}

void uiFormatFreq(char *buf, size_t size, uint32_t hz, uint8_t mode, bool unit)
{
  if(mode == MODE_FM)
    snprintf(buf, size, unit ? "%lu.%02lu MHz" : "%lu.%02lu", (unsigned long)(hz / 1000000), (unsigned long)((hz % 1000000) / 10000));
  else if(modeIsSSB(mode))
    snprintf(buf, size, unit ? "%lu.%02lu kHz" : "%lu.%02lu", (unsigned long)(hz / 1000), (unsigned long)((hz % 1000) / 10));
  else
    snprintf(buf, size, unit ? "%lu kHz" : "%lu", (unsigned long)(hz / 1000));
}

void uiScrollbar(int x, int y, int h, uint16_t total, uint16_t visible, uint16_t first)
{
  if(total <= visible) return;
  int th = h * visible / total;
  if(th < 10) th = 10;
  int ty = y + (h - th) * first / (total - visible);
  uiRRect(x, ty, 3, th, 1, T.dim);
}

void uiBattery(int x, int y)
{
  uint8_t pct = batteryPercent();
  bool chg = batteryCharging();
  uint16_t c = chg ? T.good : pct <= 15 ? T.bad : T.text;
  spr.drawRoundRect(x, y + 3, 23, 11, 3, T.dim);
  uiRRect(x + 23, y + 6, 2, 5, 1, T.dim);
  int w = chg ? 19 : (19 * pct) / 100;
  if(w > 0) uiRRect(x + 2, y + 5, w, 7, 2, c);
  if(chg)
  {
    spr.fillTriangle(x + 13, y + 3, x + 8, y + 9, x + 12, y + 9, T.bg);
    spr.fillTriangle(x + 11, y + 8, x + 15, y + 8, x + 10, y + 14, T.bg);
  }
}

void uiStatusBar(const char *title, uint16_t titleColor)
{
  const char *clk = clockText();
  uiText(clk ? clk : "--:--", 8, 9, F_SMB, T.text, ML_DATUM);

  char buf[40];
  uiFit(buf, sizeof(buf), title, F_SMB, 170);
  uiText(buf, 160, 9, F_SMB, titleColor, MC_DATUM);

  int x = 290;
  uiBattery(x, 1);
  char pct[6];
  snprintf(pct, sizeof(pct), "%u", batteryPercent());
  uiText(pct, x - 4, 9, F_CAP, T.dim, MR_DATUM);
  x -= 4 + uiTextWidth(pct, F_CAP) + 6;
  if(usbConnected()) { uiText("USB", x, 9, F_CAP, T.warn, MR_DATUM); x -= uiTextWidth("USB", F_CAP) + 6; }
  if(sleepTimerLeftMin()) { spr.fillCircle(x - 5, 9, 5, T.dim); spr.fillCircle(x - 2, 7, 5, T.bg); }
}

void uiIcon(uint8_t id, int cx, int cy, uint16_t c)
{
  switch(id)
  {
    case IC_LIST:
      for(int i = -1; i <= 1; i++) { spr.fillCircle(cx - 5, cy + i * 5, 1, c); uiRRect(cx - 2, cy + i * 5 - 1, 9, 2, 1, c); }
      break;
    case IC_DIAL:
      spr.fillArc(cx, cy, 7, 5, 0, 360, c);
      spr.drawLine(cx, cy, cx + 3, cy - 4, c); spr.fillCircle(cx, cy, 1, c);
      break;
    case IC_RADAR:
      spr.fillArc(cx - 4, cy + 4, 12, 10, 270, 360, c);
      spr.fillArc(cx - 4, cy + 4, 7, 5, 270, 360, c);
      spr.fillCircle(cx - 4, cy + 4, 2, c);
      break;
    case IC_STAR: uiStar(cx, cy, 7, c); break;
    case IC_GEAR:
      for(int a = 0; a < 8; a++) { float r = a * 0.785f; spr.fillCircle(cx + 6 * cosf(r), cy + 6 * sinf(r), 2, c); }
      spr.fillArc(cx, cy, 6, 3, 0, 360, c);
      break;
    case IC_MOON: spr.fillArc(cx, cy, 7, 0, 60, 300, c); break;
    case IC_APPS:
      for(int i = 0; i < 2; i++) for(int j = 0; j < 2; j++) uiRRect(cx - 7 + i * 8, cy - 7 + j * 8, 6, 6, 2, c);
      break;
    case IC_GHOST:
    {
      int r = 7;
      spr.fillCircle(cx, cy - 2, r, c);
      spr.fillRect(cx - r, cy - 2, 2 * r + 1, 8, c);
      for(int k = 0; k < 4; k++) spr.fillCircle(cx - r + 2 + k * 4, cy + 6, 2, c);
      spr.fillCircle(cx - 3, cy - 2, 1, 0x2104);
      spr.fillCircle(cx + 3, cy - 2, 1, 0x2104);
      spr.fillRect(cx - 4, cy - 3, 3, 3, 0x2104);
      spr.fillRect(cx + 2, cy - 3, 3, 3, 0x2104);
      break;
    }
    case IC_WAVE:
      for(int i = 0; i < 6; i++)
      {
        static const int8_t hh[] = { 3, 7, 2, 8, 4, 6 };
        uiRRect(cx - 8 + i * 3, cy - hh[i], 2, hh[i] * 2, 1, c);
      }
      break;
    case IC_BOLT:
      spr.fillTriangle(cx + 2, cy - 9, cx - 5, cy + 1, cx + 1, cy + 1, c);
      spr.fillTriangle(cx - 1, cy - 1, cx + 5, cy - 1, cx - 2, cy + 9, c);
      break;
    case IC_BACK:
      spr.fillTriangle(cx - 7, cy, cx - 1, cy - 6, cx - 1, cy + 6, c);
      uiRRect(cx - 2, cy - 2, 9, 4, 1, c);
      break;
    case IC_STANDBY:
      spr.fillArc(cx - 2, cy + 2, 6, 0, 60, 300, c);
      spr.fillArc(cx + 1, cy - 1, 9, 7, 300, 360, c);
      break;
    case IC_POWER:
      spr.fillArc(cx, cy + 1, 7, 5, 300, 600, c);
      uiRRect(cx - 1, cy - 8, 3, 8, 1, c);
      break;
    case IC_INFO: spr.fillArc(cx, cy, 7, 5, 0, 360, c); spr.fillRect(cx - 1, cy - 1, 2, 5, c); spr.fillRect(cx - 1, cy - 4, 2, 2, c); break;
    case IC_BLOCK: spr.fillArc(cx, cy, 7, 5, 0, 360, c); spr.drawLine(cx - 4, cy + 4, cx + 4, cy - 4, c); spr.drawLine(cx - 5, cy + 4, cx + 4, cy - 5, c); break;
    case IC_PLUS: uiRRect(cx - 6, cy - 1, 13, 3, 1, c); uiRRect(cx - 1, cy - 6, 3, 13, 1, c); break;
    case IC_CLOCK: spr.fillArc(cx, cy, 7, 5, 0, 360, c); spr.drawLine(cx, cy, cx, cy - 4, c); spr.drawLine(cx, cy, cx + 3, cy + 2, c); break;
    case IC_SPEAKER:
      spr.fillRect(cx - 6, cy - 2, 4, 5, c); spr.fillTriangle(cx - 3, cy, cx + 2, cy - 6, cx + 2, cy + 6, c);
      spr.fillArc(cx + 2, cy, 7, 6, 310, 410, c);
      break;
    case IC_ANTENNA:
      spr.drawLine(cx, cy - 6, cx, cy + 7, c); spr.drawLine(cx + 1, cy - 6, cx + 1, cy + 7, c);
      spr.fillArc(cx, cy - 4, 7, 6, 200, 340, c);
      break;
    case IC_BRUSH: spr.fillCircle(cx - 3, cy + 3, 4, c); spr.drawLine(cx - 1, cy + 1, cx + 6, cy - 6, c); spr.drawLine(cx, cy + 1, cx + 7, cy - 6, c); break;
    case IC_BATTERY: spr.drawRoundRect(cx - 7, cy - 4, 13, 9, 2, c); spr.fillRect(cx + 6, cy - 1, 2, 3, c); spr.fillRect(cx - 5, cy - 2, 7, 5, c); break;
    case IC_BOOK: spr.drawRoundRect(cx - 7, cy - 6, 7, 12, 1, c); spr.drawRoundRect(cx, cy - 6, 7, 12, 1, c); break;
    case IC_RESET: spr.fillArc(cx, cy, 7, 5, 40, 330, c); spr.fillTriangle(cx + 5, cy - 8, cx + 8, cy - 1, cx + 1, cy - 3, c); break;
  }
}

void uiPush()
{
#ifndef NOVA_SIM
  spr.pushSprite(0, 0);
#else
  simPush();
#endif
}

//
// Screen manager
//
uint8_t uiScreen() { return screen; }
static uint32_t lastInputAt = 0;
void uiRequestDraw() { needDraw = true; }

void uiShow(uint8_t s)
{
  screen = s;
  if(s == SCR_STATIONS) stationsEnter();
  if(s == SCR_TUNE) tuneEnter();
  if(s == SCR_GHOST) ghostScreenEnter();
  if(s == SCR_NOISE) noiseScreenEnter();
  if(s == SCR_STORM) stormScreenEnter();
  needDraw = true;
}

void uiGoHome() { uiShow(cfg.homeScreen ? SCR_TUNE : SCR_STATIONS); }

void uiToast(const char *text, uint32_t ms)
{
  snprintf(toastText, sizeof(toastText), "%s", text);
  toastUntil = millis() + ms;
  needDraw = true;
}

void uiConfirm(const char *title, const char *text, const char *yes, ConfirmFn fn)
{
  confirmOn = true;
  confirmTitle = title; confirmText = text; confirmYes = yes; confirmFn = fn;
  confirmSel = 0;
  needDraw = true;
}

static void drawAlert(const char *title, const char *text, const char *no, const char *yes, uint8_t sel)
{
  uiRRect(26, 20, 268, 132, 16, T.surface);
  uiText(title, 160, 46, F_MD, T.text, MC_DATUM);
  char buf[64];
  uiFit(buf, sizeof(buf), text, F_SM, 240);
  uiText(buf, 160, 72, F_SM, T.dim, MC_DATUM);
  uiPill(40, 104, 116, 34, sel == 0 ? T.sel : T.surface2, sel == 0 ? T.selText : T.text, no, F_SMB);
  uiPill(164, 104, 116, 34, sel == 1 ? T.bad : T.surface2, sel == 1 ? 0xFFFF : T.bad, yes, F_SMB);
}

void uiMessage(const char *title, const char *line)
{
  if(screenIsOff()) return;
  spr.fillSprite(T.bg);
  uiRRect(30, 46, 260, 78, 16, T.surface);
  uiText(title, 160, 72, F_MD, T.text, MC_DATUM);
  uiText(line, 160, 98, F_SM, T.dim, MC_DATUM);
  uiPush();
}


//
// Volume HUD
//
static bool volOn = false;
static uint32_t volAt = 0;

void volumeOpen() { volOn = true; volAt = millis(); needDraw = true; }
bool volumeActive() { return volOn; }

void volumeInput(const Input &in)
{
  volAt = millis();
  if(in.turn)
  {
    int v = (int)cfg.volume + in.turnFast;
    radioSetVolume(v < 0 ? 0 : v > 63 ? 63 : v);
    configSave();
  }
  if(in.click) volOn = false;
  needDraw = true;
}

void volumeDraw()
{
  char buf[8];
  uiRRect(12, 118, 296, 44, 22, T.surface2);
  uiIcon(IC_SPEAKER, 34, 140, T.text);
  int w = (206 * cfg.volume) / 63;
  uiRRect(52, 136, 206, 8, 4, T.barOff);
  if(w) uiRRect(52, 136, w < 8 ? 8 : w, 8, 4, T.accent);
  spr.fillCircle(52 + w, 140, 7, 0xFFFF);
  snprintf(buf, sizeof(buf), "%u", cfg.volume);
  uiText(buf, 294, 140, F_SMB, T.text, MR_DATUM);
}

//
// Scan screen
//
void uiDrawScan(bool force)
{
  static uint32_t last = 0;
  uint32_t now = millis();
  if(!force && now - last < 90) return;
  last = now;
  if(screenIsOff()) return;

  const ScanState &s = scanState;
  char buf[64], f[24];
  spr.fillSprite(T.bg);
  uiStatusBar("Searching", T.accent);

  uint32_t pct = s.total ? (s.done * 100UL) / s.total : 0;
  if(pct > 100) pct = 100;
  uint16_t gc = uiGroupColor(s.group);
  uiRing(62, 84, 44, 7, pct, gc);
  snprintf(buf, sizeof(buf), "%lu%%", (unsigned long)pct);
  uiText(buf, 62, 80, F_MD, T.text, MC_DATUM);
  uiText(s.confirmTotal ? "checking" : "sweeping", 62, 98, F_CAP, T.dim, MC_DATUM);

  uint8_t mode = s.group == GROUP_FM ? MODE_FM : MODE_AM;
  uiText(bandLabel(s.freq, mode), 124, 36, F_SMB, gc, ML_DATUM);
  uiFormatFreq(f, sizeof(f), s.freq, mode, false);
  uiText(f, 122, 64, F_XLS, T.text, ML_DATUM);
  uiText(mode == MODE_FM ? "MHz" : "kHz", 126 + uiTextWidth(f, F_XLS), 70, F_SM, T.dim, ML_DATUM);

  snprintf(buf, sizeof(buf), "%u found", s.found);
  uiText(buf, 124, 98, F_MD, T.text, ML_DATUM);
  if(s.done > s.total / 50 && s.done < s.total)
  {
    uint32_t left = (uint32_t)((uint64_t)(now - s.startedAt) * (s.total - s.done) / s.done / 1000);
    if(left >= 60) snprintf(buf, sizeof(buf), "~%lu min left", (unsigned long)((left + 30) / 60));
    else snprintf(buf, sizeof(buf), "< 1 min left");
    uiText(buf, 304, 98, F_SM, T.dim, MR_DATUM);
  }

  int x = 12;
  for(int i = 0; i < s.recentCount; i++)
  {
    uiFormatFreq(f, sizeof(f), s.recent[i], s.recentGroup[i] == GROUP_FM ? MODE_FM : MODE_AM, false);
    int w = uiTextWidth(f, F_SMB) + 20;
    uiRRect(x, 140, w, 22, 11, T.surface);
    spr.fillCircle(x + 9, 151, 3, uiGroupColor(s.recentGroup[i]));
    uiText(f, x + 14 + (w - 14) / 2 - 2, 151, F_SMB, T.text, MC_DATUM);
    x += w + 6;
  }
  uiText("Press to stop", 308, 151, F_CAP, T.dim, MR_DATUM);
  uiPush();
}

void uiStartScan()
{
  uint32_t oldFreq = radioFreq();
  uint8_t oldMode = radioMode();
  bool complete = scanRun();
  uiShow(SCR_STATIONS);
  stationsAfterScan(oldFreq, oldMode);
  char buf[48];
  uint16_t n = stationNewCount();
  if(n) snprintf(buf, sizeof(buf), "%u stations  -  %u new", stationCount, n);
  else snprintf(buf, sizeof(buf), complete ? "%u stations" : "Stopped  -  %u stations", stationCount);
  uiToast(buf, 2500);
}

//
// Standby: screen and sound off, the receiver keeps searching
//
static bool standby = false;
static uint32_t standbyIdleSince = 0;
static uint16_t standbyNewAtStart = 0;

bool standbyActive() { return standby; }

void standbyEnter()
{
  if(appRunning()) { appsStopAll(); uiGoHome(); }
  standby = true;
  standbyNewAtStart = stationNewCount();
  standbyIdleSince = 0;
  radioMute(true);
  screenSleep();
}

static void standbyExit()
{
  if(!standby) return;
  standby = false;
  if(scanActive()) { while(scanStep()) {} }   // finishes at once: the abort flag is set
  radioMute(false);
  screenWake();
  uiShow(SCR_STATIONS);
  uint16_t n = stationNewCount();
  char buf[48];
  if(n > standbyNewAtStart) snprintf(buf, sizeof(buf), "%u new station%s found", n - standbyNewAtStart, n - standbyNewAtStart > 1 ? "s" : "");
  else snprintf(buf, sizeof(buf), "No new stations");
  uiToast(buf, 3000);
}

void standbyTick()
{
  if(!standby) return;
  if(scanActive())
  {
    if(!scanStep())
    {
      if(scanWasAborted()) { standbyExit(); return; }
      standbyIdleSince = millis();
    }
    return;
  }
  if(!standbyIdleSince || millis() - standbyIdleSince > cfg.standbyPause * 60000UL) scanStart(true);
}

//
// Input routing
//
void uiInput(const Input &in)
{
  bool any = in.turn || in.click || in.hold || in.longHold;
  if(!any) return;
  lastInputAt = millis();
  if(standby) { standbyExit(); return; }
  if(screenIsOff()) { screenWake(); return; }
  screenWake();
  needDraw = true;

  if(in.longHold && !powerOn) { powerOn = true; powerSel = 0; confirmOn = false; return; }

  if(powerOn)
  {
    if(in.turn) powerSel = (powerSel + (in.turn > 0 ? 1 : 3)) % 4;
    if(in.click)
    {
      powerOn = false;
      if(powerSel == 1) standbyEnter();
      if(powerSel == 2) screenSleep();
      if(powerSel == 3) powerOff();
    }
    if(in.hold) powerOn = false;
    return;
  }

  if(confirmOn)
  {
    if(in.turn) confirmSel ^= 1;
    if(in.click) { confirmOn = false; if(confirmSel && confirmFn) confirmFn(); }
    if(in.hold) confirmOn = false;
    return;
  }

  switch(screen)
  {
    case SCR_STATIONS: stationsInput(in); break;
    case SCR_TUNE:     tuneInput(in); break;
    case SCR_MENU:     menuInput(in); break;
    case SCR_SETTINGS: settingsInput(in); break;
    case SCR_APPS:     appsInput(in); break;
    case SCR_GHOST:    ghostInput(in); break;
    case SCR_NOISE:    noiseInput(in); break;
    case SCR_STORM:    stormInput(in); break;
    case SCR_SEARCH:   scanMenuInput(in); break;
    case SCR_VISUAL:   visualInput(in); break;
  }
}

bool uiTick()
{
  bool redraw = false;
  if(toastUntil && (int32_t)(millis() - toastUntil) >= 0) { toastUntil = 0; redraw = true; }
  if(volOn && millis() - volAt > 3000) { volOn = false; redraw = true; }
  switch(screen)
  {
    case SCR_STATIONS: redraw |= stationsUiTick(); break;
    case SCR_TUNE:     redraw |= tuneTick(); break;
    case SCR_SETTINGS: redraw |= settingsTick(); break;
    case SCR_GHOST:    redraw |= ghostUiTick(); break;
    case SCR_NOISE:    redraw |= noiseUiTick(); break;
    case SCR_STORM:    redraw |= stormUiTick(); break;
    case SCR_VISUAL:   redraw |= visualTick(); break;
  }
  // Visualizer after a while without touching the knob (Display settings)
  static const uint16_t idleSecs[] = { 0, 30, 60, 300 };
  if(cfg.visIdle && cfg.visIdle < 4 && (screen == SCR_STATIONS || screen == SCR_TUNE) && !screenIsOff() && !volOn &&
     !standby && !scanActive() && millis() - lastInputAt > idleSecs[cfg.visIdle] * 1000UL)
  {
    visualOpen(screen);
    redraw = true;
  }
  static uint32_t lastRefresh = 0;
  if(millis() - lastRefresh > 15000) { lastRefresh = millis(); redraw = true; }
  return redraw || needDraw;
}

void uiDraw()
{
  needDraw = false;
  if(screenIsOff()) return;
  // The visualizers draw over their last frame (trails)
  if(screen != SCR_VISUAL || powerOn || confirmOn) spr.fillSprite(T.bg);

  if(powerOn)
  {
    uiStatusBar("Power", T.text);
    static const char *const opts[] = { "Cancel", "Standby", "Screen", "Power" };
    static const char *const subs[] = { "Back", "Searches", "Turns off", "Turns off" };
    static const uint8_t icons[] = { IC_RESET, IC_STANDBY, IC_MOON, IC_POWER };
    static const uint16_t colors[] = { C_GRAY, C_INDIGO, C_PURPLE, C_RED };
    for(int i = 0; i < 4; i++)
    {
      int x = 6 + i * 78;
      bool sel = i == powerSel;
      if(sel) uiGloss(x, 30, 74, 118, 14, T.sel); else uiRRect(x, 30, 74, 118, 14, T.surface);
      uiTile(x + 37, 64, 34, colors[i], icons[i]);
      uiLine(opts[i], x + 37, 104, 70, F_SMB, sel ? T.selText : T.text, MC_DATUM);
      uiLine(subs[i], x + 37, 124, 70, F_CAP, sel ? T.selText : T.dim, MC_DATUM);
    }
    uiText("Turn to choose, click to confirm", 160, 160, F_CAP, T.dim, MC_DATUM);
    uiPush();
    return;
  }

  if(confirmOn)
  {
    drawAlert(confirmTitle, confirmText, "Cancel", confirmYes, confirmSel);
    uiPush();
    return;
  }

  switch(screen)
  {
    case SCR_STATIONS: stationsDraw(); break;
    case SCR_TUNE:     tuneDraw(); break;
    case SCR_MENU:     menuDraw(); break;
    case SCR_SETTINGS: settingsDraw(); break;
    case SCR_APPS:     appsDraw(); break;
    case SCR_GHOST:    ghostDraw(); break;
    case SCR_NOISE:    noiseDraw(); break;
    case SCR_STORM:    stormDraw(); break;
    case SCR_SEARCH:   scanMenuDraw(); break;
    case SCR_VISUAL:   visualDraw(); break;
  }

  if(toastUntil)
  {
    char buf[48];
    uiFit(buf, sizeof(buf), toastText, F_SMB, 270);
    int w = uiTextWidth(buf, F_SMB) + 32;
    uiRRect(160 - w / 2, 132, w, 30, 15, T.text);
    uiText(buf, 160, 147, F_SMB, T.bg, MC_DATUM);
  }
  uiPush();
}

void uiInit()
{
  screen = SCR_STATIONS;
  needDraw = true;
}
