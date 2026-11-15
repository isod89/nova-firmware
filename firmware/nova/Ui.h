//
// Ui.h - internal interface shared by the screen modules
//
#ifndef NOVA_UI_H
#define NOVA_UI_H

#include "Nova.h"

#ifndef NOVA_SIM
extern LGFX_Sprite spr;
#else
#include "SimDisplay.h"
#endif

struct Theme
{
  const char *name;
  uint16_t bg, surface, surface2, line, text, dim, accent, accentText, sel, selText;
  uint16_t good, warn, bad, fm, am, sw, barOff, fav;
};
extern const Theme themes[];
uint8_t themeCount();
#define T (themes[cfg.theme < themeCount() ? cfg.theme : 0])

// System tile colours (icons)
#define C_BLUE   0x0C3F
#define C_ORANGE 0xFCE1
#define C_GREEN  0x368B
#define C_INDIGO 0x5AFC
#define C_GRAY   0x8C72
#define C_PURPLE 0xBADE
#define C_RED    0xFA27
#define C_YELLOW 0xFEA1
#define C_TEAL   0x465C
#define C_PINK   0xF9AB

// Smooth Inter fonts (anti-aliased)
extern const lgfx::IFont *F_CAP;   // 11 px medium  - captions
extern const lgfx::IFont *F_SM;    // 13 px regular - body
extern const lgfx::IFont *F_SMB;   // 13 px semibold
extern const lgfx::IFont *F_MD;    // 16 px semibold - titles
extern const lgfx::IFont *F_LG;    // 22 px bold    - large titles
extern const lgfx::IFont *F_XL;    // 44 px display - frequency
extern const lgfx::IFont *F_XLS;   // 30 px display - digits
void uiFontsInit();

// Primitives
void uiStatusBar(const char *title, uint16_t titleColor);
void uiBattery(int x, int y);
void uiPill(int x, int y, int w, int h, uint16_t bg, uint16_t fg, const char *text, const lgfx::IFont *font);
void uiBadge(int x, int y, uint16_t bg, uint16_t fg, const char *text);   // small caption pill, x = left
void uiBars(int x, int y, int h, uint8_t level, uint16_t on, uint16_t off, int bw = 4, int gap = 2);
void uiStar(int cx, int cy, int r, uint16_t color);
void uiFit(char *buf, size_t size, const char *text, const lgfx::IFont *font, int maxw);
void uiLine(const char *s, int x, int y, int maxw, const lgfx::IFont *f, uint16_t color, uint8_t datum);
void uiText(const char *s, int x, int y, const lgfx::IFont *f, uint16_t color, uint8_t datum);
int  uiTextWidth(const char *s, const lgfx::IFont *f);
void uiToggle(int x, int y, bool on);          // iOS style switch, 34 x 20, x = left
void uiTile(int cx, int cy, int size, uint16_t color, uint8_t icon);
void uiRing(int cx, int cy, int r, int thick, uint8_t pct, uint16_t color);
uint8_t uiSignalLevel(uint8_t rssi, uint8_t snr);
uint16_t uiGroupColor(uint8_t group);
uint16_t uiOnColor(uint16_t bg);   // black or white text for a coloured background
const char *uiGroupShort(uint8_t group);
const char *uiShortBand(uint32_t hz, uint8_t mode);
void uiFormatFreq(char *buf, size_t size, uint32_t hz, uint8_t mode, bool unit);
void uiScrollbar(int x, int y, int h, uint16_t total, uint16_t visible, uint16_t first);
void uiIcon(uint8_t id, int cx, int cy, uint16_t color);
void uiPush();
const char *uiCaps(const char *s);
void uiRRect(int x, int y, int w, int h, int r, uint16_t c);
void uiGloss(int x, int y, int w, int h, int r, uint16_t c);
uint16_t uiMix(uint16_t a, uint16_t b, uint8_t t);

enum Icon : uint8_t { IC_LIST, IC_DIAL, IC_RADAR, IC_STAR, IC_GEAR, IC_MOON, IC_POWER, IC_INFO, IC_BLOCK, IC_PLUS,
                      IC_CLOCK, IC_SPEAKER, IC_ANTENNA, IC_BRUSH, IC_BATTERY, IC_BOOK, IC_RESET, IC_STANDBY,
                      IC_APPS, IC_GHOST, IC_WAVE, IC_BOLT, IC_BACK };

// Screen modules
void stationsEnter();
void stationsInput(const Input &in);
void stationsDraw();
bool stationsUiTick();
void stationsAfterScan(uint32_t oldFreq, uint8_t oldMode);

void tuneEnter();
void tuneInput(const Input &in);
void tuneDraw();
bool tuneTick();

void menuOpen(uint8_t from);
void menuInput(const Input &in);
void menuDraw();

void settingsOpen();
void settingsOpenScanner();

// Search stations (UiScan.cpp)
void scanMenuOpen(uint8_t back);
void scanMenuInput(const Input &in);
void scanMenuDraw();
void settingsInput(const Input &in);
void settingsDraw();
bool settingsTick();

typedef void (*ConfirmFn)();
void uiConfirm(const char *title, const char *text, const char *yes, ConfirmFn fn);

void volumeOpen();
bool volumeActive();
void volumeInput(const Input &in);
void volumeDraw();

void uiStartScan();
void uiGoHome();

// Apps (UiApps.cpp)
void appsOpen(uint8_t from);
void appsInput(const Input &in);
void appsDraw();
void ghostScreenEnter();
void ghostInput(const Input &in);
void ghostDraw();
bool ghostUiTick();
void ghostStepped();

// Measurement apps (UiTools.cpp)
void noiseScreenEnter();
void noiseInput(const Input &in);
void noiseDraw();
bool noiseUiTick();
void stormScreenEnter();
void stormInput(const Input &in);
void stormDraw();
bool stormUiTick();
void toolStepped();

// Visualizers (UiVisual.cpp)

bool visualActive();
void visualInput(const Input &in);
void visualDraw();
bool visualTick();
const char *visualStyleName(uint8_t s);

// App menu (hold in an app): back to the radio first
void appMenuBegin(const char *title);
void appMenuAdd(const char *label, uint8_t icon, uint16_t color, void (*fn)());
void appMenuShow();
bool appMenuActive();
void appMenuInput(const Input &in);
void appMenuDraw();
void appsOpenList();

#endif
