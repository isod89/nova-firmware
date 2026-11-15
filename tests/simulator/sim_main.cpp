#include "Nova.h"
#include "Ui.h"
#include <vector>
#include <string>
#include <algorithm>
#include <sys/select.h>
#include <unistd.h>
SimSerial Serial;
static bool serve = false;
#define LOG(...) fprintf(serve ? stderr : stdout, __VA_ARGS__)
extern uint64_t simMs;
extern bool poweredOff;
extern int simNoise; extern uint64_t simStormFrom;
struct Air { uint32_t f; int rssi, snr; const char *name; const char *rt; };
extern std::vector<Air> airFM, airAM;

static std::vector<std::pair<uint32_t, std::string>> want;
static int abortAtPct = -1;
static void save(const char *name)
{
  char p[160]; snprintf(p, sizeof(p), "out/%s.ppm", name);
  FILE *f = fopen(p, "wb"); fprintf(f, "P6\n320 170\n255\n");
  for(int y = 0; y < 170; y++) for(int x = 0; x < 320; x++)
  { uint16_t c = spr.readPixel(x, y); uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31) }; fwrite(rgb, 1, 3, f); }
  fclose(f);
}
void simPush()
{
  uint32_t pct = scanState.total ? scanState.done * 100 / scanState.total : 0;
  for(auto it = want.begin(); it != want.end(); ++it) if(pct >= it->first) { save(it->second.c_str()); want.erase(it); break; }
}
static void simFeed()
{
  fd_set fs; FD_ZERO(&fs); FD_SET(0, &fs);
  timeval tv = { 0, 0 };
  if(select(1, &fs, nullptr, nullptr, &tv) > 0) { char b[4096]; ssize_t n = ::read(0, b, sizeof(b) - 1); if(n > 0) { b[n] = 0; Serial.feed(b); } }
}
bool inputAbort() { if(serve) { simFeed(); bool st = usbScanPoll(); fflush(stdout); if(st) return true; } uint32_t pct = scanState.total ? scanState.done * 100 / scanState.total : 0; return abortAtPct >= 0 && (int)pct >= abortAtPct; }
static bool cleanShots = false;
static void shot(const char *n) { if(cleanShots) { simMs += 6000; uiTick(); } uiDraw(); save(n); }
void simSave(const char *n) { save(n); }

static Input I() { Input in; memset(&in, 0, sizeof(in)); return in; }
static void turn(int n, bool push = false) { Input in = I(); in.turn = n; in.turnFast = n; in.pushTurn = push; uiInput(in); delay(400); uiTick(); radioPoll(); }
static void click() { Input in = I(); in.click = true; uiInput(in); delay(50); }
static void hold() { Input in = I(); in.hold = true; uiInput(in); delay(50); }
static void longHold() { Input in = I(); in.longHold = true; uiInput(in); }

int main(int argc, char **argv)
{
  spr.setColorDepth(16); spr.createSprite(320, 170);
  configDefaults();
  serve = argc > 1 && !strcmp(argv[1], "serve");
  if(argc > 1 && !strcmp(argv[1], "splash"))
  {
    // Every frame of the boot animation, for a GIF
    extern void splashRender(float); extern void splashStart(); splashStart();
    for(int i = 0; i <= 60; i++) { splashRender(i / 25.0f); char n[32]; snprintf(n, sizeof(n), "splash_%03d", i); save(n); }
    return 0;
  }
  if(argc > 1 && !serve) cfg.theme = atoi(argv[1]);
  if(argc > 2 && !serve) cleanShots = true;
  stationsInit(); scheduleInit(); ghostInit(); wifiInit(); toolsInit();

  // EiBi import with fixed-column lines like eibi.txt
  scheduleImportBegin();
  const char *rows[][3] = {
    {"5960", "0000-2400", "Radio Monde"}, {"6070", "0000-2400", "CFRX Toronto"}, {"7325", "1300-1600", "Voix Internationale"},
    {"9650", "1400-1500", "World Radio Intl."}, {"9650", "1500-1600", "Evening Service"}, {"11780", "1200-2300", "Radio Nacional Amaz\xF4nia"},
    {"15140", "1400-1700", "Atlantic Radio"}, {"4747", "2200-1100", "Radio Huanta 2000"}, {"9580", "0000-0100", "Too late"},
  };
  int ok = 0;
  for(auto &r : rows) { char line[200]; snprintf(line, sizeof(line), "%-14s%-9s%-11s%-24s%s", r[0], r[1], " Mo-Fr CAN", r[2], "E  NAm"); ok += scheduleImportLine(line); }
  uint32_t cnt = 0; scheduleImportEnd(cnt);
  LOG("eibi parsed=%d stored=%u lookup 9650=%s 11780=%s 4747=%s 9580=%s\n", ok, cnt,
         scheduleLookup(9650000) ?: "-", scheduleLookup(11780000) ?: "-", scheduleLookup(4747000) ?: "-", scheduleLookup(9580000) ?: "-");

  uiFontsInit();
  uiInit();
  { extern void splashRender(float); extern void splashStart(); splashStart();
    if(!serve) for(float t : { 0.15f, 0.35f, 0.6f, 0.9f, 1.2f, 1.9f }) { splashRender(t); char n[32]; snprintf(n, sizeof(n), "00_splash_%03d", (int)(t * 100)); save(n); }
    splashRender(1.9f); if(!serve) save("00_splash"); }
  if(!serve) want = { {2, "01_scan_fm"}, {35, "02_scan_sw"}, {80, "03_scan_offband"} };
  uint64_t t0 = simMs;
  LOG("plan channels=%u\n", scanPlanChannels());
  uiStartScan();
  LOG("scan time %.0f s, stations=%u new=%u\n", (simMs - t0) / 1000.0, stationCount, stationNewCount());
  int miss = 0, ghosts = 0;
  for(auto &a : airFM) if(stationFind(a.f, GROUP_FM) < 0) { miss++; LOG("  missed FM %u\n", a.f); }
  for(auto &a : airAM) if(stationFind(a.f, groupOf(a.f, MODE_AM)) < 0) { miss++; LOG("  missed AM %u\n", a.f); }
  for(uint16_t i = 0; i < stationCount; i++) { bool real = false; for(auto &a : (stations[i].group == GROUP_FM ? airFM : airAM)) real |= a.f == stations[i].freq; if(!real) { ghosts++; LOG("  ghost %u\n", stations[i].freq); } }
  LOG("missed=%d ghost=%d\n", miss, ghosts);
  if(serve)
  {
    // Protocol test: commands on stdin, answers on stdout
    setvbuf(stdout, nullptr, _IOFBF, 1 << 16);
    char buf[4096];
    for(;;)
    {
      fd_set fs; FD_ZERO(&fs); FD_SET(0, &fs);
      timeval tv = { 0, 20000 };
      if(select(1, &fs, nullptr, nullptr, &tv) > 0)
      {
        ssize_t n = ::read(0, buf, sizeof(buf) - 1);
        if(n <= 0) break;
        buf[n] = 0; Serial.feed(buf);
      }
      usbTick();
      if(ghostTick()) ghostStepped();
      if(noiseTick()) toolStepped();
      if(stormTick()) toolStepped();
      if(getenv("SIM_STORM") && !simStormFrom) simStormFrom = simMs;
      simNoise = getenv("SIM_NOISE") ? (int)((simMs / 1000) % 20) : 0;
      simMs += 20; radioPoll(); uiTick(); uiDraw();
      fflush(stdout);
    }
    return 0;
  }

  simMs += 3000; uiTick(); radioPoll(); delay(500); radioPoll();
  shot("04_stations");
  turn(2); radioPoll(); delay(500); radioPoll(); uiTick(); shot("05_stations_fm_rds");
  turn(1, true); shot("06_filter_hud");
  for(int k = 0; k < 4; k++) turn(1, true);
  delay(2000); uiTick(); turn(6); radioPoll(); uiTick(); shot("07_stations_sw");
  click(); turn(3); shot("08_volume"); click();
  cfg.listFilter = 0; viewRebuild();

  hold(); shot("09_menu");
  hold();
  uiShow(SCR_TUNE); radioTune(9650000, MODE_AM); radioPoll(); shot("10_tune_am");
  click(); turn(1); turn(1); shot("11_tune_chips");
  for(int k = 0; k < 6; k++) turn(1);
  shot("12_tune_chips_scrolled");
  hold();
  click(); turn(1); click(); shot("13_band_picker"); turn(10); click(); radioPoll(); shot("14_tune_after_band");
  click(); for(int k = 0; k < 8; k++) turn(1); click(); shot("15_keypad"); turn(1); click(); shot("15b_keypad_digit2"); hold();
  radioTune(9650000, MODE_AM);
  click(); for(int k = 0; k < 7; k++) turn(1); click(); turn(3); shot("16_scope"); click(); radioPoll();
  uiShow(SCR_TUNE); radioTune(14230000, MODE_USB); radioPoll(); shot("17_tune_usb");
  radioTune(99100000, MODE_FM); delay(600); radioPoll(); uiShow(SCR_TUNE); shot("18_tune_fm");

  settingsOpen(); shot("19_settings");
  click(); shot("20_settings_scanner");
  hold(); turn(1); click(); shot("21_settings_audio");
  hold(); turn(1); turn(1); click(); click(); turn(1); shot("22_reception_toggle"); hold(); hold();
  uiShow(SCR_STATIONS); longHold(); turn(1); shot("23_power"); hold();
  uiConfirm("Clear the station list?", "Favorites are deleted too", "Clear", nullptr); turn(1); shot("24_confirm"); click();

  // Standby: a new station appears, the radio finds it while asleep
  airAM.push_back({ 9420000, 30, 16, "", "" });
  cfg.standbyPause = 0;
  standbyEnter();
  for(int k = 0; k < 400 && (scanActive() || k < 2); k++) { standbyTick(); }
  printf("standby cycle done, new=%u (9420 new=%d)\n", stationNewCount(), stationFind(9420000, GROUP_SW) >= 0 && (stations[stationFind(9420000, GROUP_SW)].flags & ST_NEW));
  turn(1); shot("25_after_standby");
  cfg.listFilter = 2; viewRebuild(); uiShow(SCR_STATIONS); shot("26_new_filter");
  cfg.listFilter = 0; viewRebuild();
  uint8_t theme0 = cfg.theme;
  for(int t : {1, 2}) { cfg.theme = t; uiShow(SCR_STATIONS); char n[40]; snprintf(n, sizeof(n), "27_theme%d_stations", t); shot(n);
    uiShow(SCR_TUNE); snprintf(n, sizeof(n), "28_theme%d_tune", t); shot(n); }
  for(uint8_t t = 0; t < themeCount(); t++) { cfg.theme = t; uiShow(SCR_STATIONS); char n[40]; snprintf(n, sizeof(n), "70_themes_%02u", t); shot(n); }
  cfg.theme = theme0;

  // Apps and Ghost Box
  uiShow(SCR_STATIONS); hold(); shot("30_menu_apps");
  for(int k = 0; k < 2; k++) turn(1);
  shot("30b_menu_apps_sel");
  appsOpen(SCR_STATIONS); shot("31_apps");
  click();
  for(int k = 0; k < 60; k++) { simMs += 20; if(ghostTick()) ghostStepped(); }
  uiDraw(); shot("32_ghost_fm");
  click(); for(int k = 0; k < 30; k++) { simMs += 20; if(ghostTick()) ghostStepped(); }
  printf("ghost marks=%u steps=%lu trace=%lu freq=%lu\n", ghostMarkCount(), (unsigned long)ghostSteps(), (unsigned long)ghostTraceTotal(), (unsigned long)radioFreq());
  shot("33_ghost_mark");
  hold(); turn(1); shot("34_ghost_controls");
  click();                                   // Controls: focus on Pause
  turn(1); click(); turn(-1); click(); shot("35_ghost_speed");
  turn(1); click(); turn(1); turn(1); turn(1); click();
  for(int k = 0; k < 80; k++) { simMs += 20; if(ghostTick()) ghostStepped(); }
  shot("36_ghost_am");
  turn(-1); turn(-1); click(); shot("37_ghost_paused");
  printf("ghost band=%u rate=%u paused=%d freq=%lu\n", ghost.band, ghost.rate, ghostPaused(), (unsigned long)radioFreq());
  click();                                   // resume
  // New controls: content, chaos, range, timer, blind
  for(int k = 0; k < 4; k++) turn(1);        // Play (content)
  click(); turn(1); turn(1); click();        // Stations only
  shot("37b_ghost_content_chip");
  for(int k = 0; k < 300; k++) { simMs += 20; if(ghostTick()) ghostStepped(); }
  printf("stations only: carriers=%u learning=%d freq=%lu\n", ghostCarriers(), ghostLearning(), (unsigned long)radioFreq());
  { int off = 0, n = 0; uint32_t last = 0;
    for(int k = 0; k < 400; k++) { simMs += 20; if(ghostTick()) { ghostStepped(); }
      uint32_t f = radioFreq(); if(f != last) { last = f; n++; bool real = false; for(auto &a : airAM) real |= a.f == f; off += !real; } }
    printf("stations only: %d steps, %d off a station\n", n, off); }
  for(int k = 0; k < 7; k++) turn(1);        // From
  click(); turn(1); turn(1); click(); turn(1); click(); turn(-1); turn(-1); click();
  for(int k = 0; k < 40; k++) { simMs += 20; if(ghostTick()) ghostStepped(); }
  printf("range lo=%lu hi=%lu band %lu..%lu freq=%lu\n", (unsigned long)ghost.lo, (unsigned long)ghost.hi, (unsigned long)ghostBandLo(), (unsigned long)ghostBandHi(), (unsigned long)radioFreq());
  shot("37c_ghost_range");
  turn(1); click(); turn(1); click(); shot("37d_ghost_timer_chip");
  hold();                                    // close the strip
  ghostMark(1); simMs += 2500; ghostMark(0);
  ghost.blind = 1; uiDraw(); shot("37e_ghost_blind");
  hold(); shot("37f_ghost_menu_blind");
  hold();
  simMs += 5UL * 60000UL; for(int k = 0; k < 3; k++) { simMs += 20; if(ghostTick()) ghostStepped(); }
  ghostUiTick(); uiDraw(); shot("37g_ghost_timer_done");
  printf("timer: paused=%d marks=%u kinds=%u,%u\n", ghostPaused(), ghostMarkCount(), ghostMarkKind(ghostMarkCount()-2), ghostMarkKind(ghostMarkCount()-1));
  ghost.blind = 0; ghost.timer = 0; ghost.lo = ghost.hi = 0; ghost.content = 0; ghostSettingsChanged();
  // ---- 1.8: sweep orders, clusters, reaction, hold, window, RF trigger, profiles, history
  {
    extern int simNoise;
    auto run = [](int ms) { for(int k = 0; k < ms / 10; k++) { simMs += 10; if(ghostTick()) ghostStepped(); } };
    ghostPause(false);
    ghost.band = 3; ghost.dir = 4; ghost.step = 0; ghost.flutter = 0; ghost.chaos = 0; ghost.rate = 50; ghostSettingsChanged(); ghostApply();
    uint32_t lo = ghostBandLo(), hi = ghostBandHi(), st = ghostStepHz(), n = (hi - lo) / st + 1;
    std::vector<int> seen(n, 0); uint32_t last = 0; int stepsSeen = 0;
    while(stepsSeen < (int)n) { simMs += 10; if(ghostTick()) { uint32_t f = radioFreq(); if(f != last) { last = f; seen[(f - lo) / st]++; stepsSeen++; } } }
    int once = 0; for(auto v : seen) once += v == 1;
    printf("shuffle: %u frequencies, %d played exactly once in one round\n", n, once);
    ghost.dir = 5; ghostSettingsChanged();
    { std::vector<uint32_t> fs; uint32_t l = 0; while(fs.size() < 8) { simMs += 10; if(ghostTick()) { uint32_t f = radioFreq(); if(f != l) { l = f; fs.push_back(f / 1000); } } }
      printf("dual:"); for(auto f : fs) printf(" %u", f); printf("\n"); }
    ghost.dir = 0; ghost.content = GC_CLUSTER; ghost.cluster = 2; ghost.step = 2; ghostSettingsChanged(); ghostApply();
    run(4000);
    { int far = 0, cnt = 0; uint32_t l = 0;
      for(int k = 0; k < 3000; k++) { simMs += 10; if(ghostTick()) { uint32_t f = radioFreq(); if(f == l) continue; l = f; cnt++; int best = 1 << 30; for(auto &a : airAM) best = std::min(best, (int)abs((int)a.f - (int)f)); if(best > 2 * (int)ghostStepHz()) far++; } }
      printf("clusters: %d steps, %d farther than 2 steps from a station, carriers=%u\n", cnt, far, ghostCarriers()); }
    shot("80_ghost_clusters");
    ghost.content = 0; ghost.step = 0; ghost.rate = 200; ghost.react = 4; ghost.onMark = 1; ghostSettingsChanged(); ghostApply();
    run(3000);
    uint32_t before = radioFreq(); run(450);
    uint32_t playing = radioFreq();
    uint16_t mk = ghostMark(0); const GhostPoint *m = ghostMarkAt(mk - 1);
    printf("reaction: playing %lu, mark %u stored %lu (playing 0.45 s before: %lu), hold=%lu ms, now %lu\n", (unsigned long)playing, mk, (unsigned long)m->hz, (unsigned long)before, (unsigned long)ghostHoldLeft(), (unsigned long)radioFreq());
    shot("81_ghost_hold");
    run(3500); printf("after hold: hold=%lu sweeping freq=%lu\n", (unsigned long)ghostHoldLeft(), (unsigned long)radioFreq());
    ghost.onMark = 0; ghost.window = 8; ghostSettingsChanged();
    ghostMark(1); run(1000); shot("82_ghost_window");
    printf("window left %lu\n", (unsigned long)ghostWindowLeft());
    run(8000); ghostUiTick(); printf("window after: %lu\n", (unsigned long)ghostWindowLeft());
    ghost.window = 0; ghost.trigger = 1; ghost.trigSens = 6; ghost.trigOpen = 5; ghostSettingsChanged();
    run(2500); shot("83_ghost_armed");
    printf("trigger: state=%u level=%u base=%u freq=%lu\n", ghostTrigState(), ghostTrigLevel(), ghostTrigBase(), (unsigned long)radioFreq());
    uint16_t mBefore = ghostMarkCount();
    simNoise = 20; run(200); simNoise = 0;
    printf("burst: state=%u marks %u->%u kind=%u\n", ghostTrigState(), mBefore, ghostMarkCount(), ghostMarkKind(ghostMarkCount() - 1));
    run(1000); shot("84_ghost_open");
    run(5000); printf("after open: state=%u\n", ghostTrigState());
    ghost.trigger = 0; ghostSettingsChanged(); run(500); printf("trigger off: state=%u\n", ghostTrigState());
    // Catch: stay on a frequency with a signal
    ghost.band = 3; ghost.dir = 0; ghost.step = 0; ghost.rate = 100; ghost.content = 0; ghost.catchT = 15; ghost.catchSnr = 10; ghost.gaps = 0;
    ghostSettingsChanged(); ghostApply();
    { uint32_t lastF = radioFreq(), since = simMs, longest = 0, longestF = 0; int catches0 = ghostCatches(); bool shotDone = false;
      for(int k = 0; k < 2000; k++) { simMs += 10; if(ghostTick()) ghostStepped(); uint32_t f = radioFreq();
        if(f != lastF) { if(simMs - since > longest) { longest = simMs - since; longestF = lastF; } lastF = f; since = simMs; }
        if(!shotDone && ghostHoldKind() == 2) { uiDraw(); save("87_ghost_catch"); shotDone = true; } }
      printf("catch: %d caught, longest stay %lu ms on %lu (rate 100 ms, catch 1.5 s)\n", ghostCatches() - catches0, (unsigned long)longest, (unsigned long)longestF); }
    ghost.catchT = 0; ghostSettingsChanged();
    hold(); for(int k = 0; k < 5; k++) turn(1); click(); shot("85_ghost_profiles");
    for(int k = 0; k < 5; k++) turn(1);
    click();
    printf("profile: band=%u rate=%u content=%u step=%u dir=%u\n", ghost.band, ghost.rate, ghost.content, ghost.step, ghost.dir);
    hold(); for(int k = 0; k < 5; k++) turn(1); click(); for(int k = 0; k < 10; k++) turn(1); click();   // Save as My profile 1
    printf("user profile 1 saved=%d\n", ghostUserProfile(0, nullptr));
    ghost.noise = 0; ghost.react = 4; ghost.content = 0; ghost.band = 3; ghost.rate = 175; ghost.dir = 0; ghost.step = 0; ghost.cluster = 3; ghostSettingsChanged(); ghostApply();
    hold(); turn(1); click(); for(int k = 0; k < 15; k++) turn(1); shot("86_ghost_react_chip");
    hold();
  }
  hold(); turn(1); click();                  // controls again
  for(int k = 0; k < 26; k++) turn(1);
  shot("38_ghost_exit_chip");
  click();
  printf("after exit: active=%d freq=%lu mode=%u screen=%u\n", ghostActive(), (unsigned long)radioFreq(), radioMode(), uiScreen());
  { uint32_t id, ep, dur, st; uint16_t mk; GhostCfg c; printf("history: %u sessions", ghostHistCount());
    if(ghostHistInfo(0, id, ep, dur, st, mk, c)) printf(", last id=%lu %lu s %lu steps %u marks band %u", (unsigned long)id, (unsigned long)dur / 1000, (unsigned long)st, mk, c.band);
    printf("\n"); }
  shot("39_after_exit");

  // Noise Hunter
  appsOpen(SCR_STATIONS); turn(1); shot("40_apps_noise");
  click();
  for(int k = 0; k < 200; k++) { simMs += 10; if(noiseTick()) toolStepped(); }
  shot("41_noise_start");
  click();                                     // zero
  simNoise = 14;
  for(int k = 0; k < 300; k++) { simMs += 10; if(noiseTick()) toolStepped(); }
  shot("42_noise_closer");
  printf("noise level=%u base=%d peak=%u max=%u hz=%lu\n", noiseLevel(), noiseBase(), noisePeak(), noiseMax(), (unsigned long)radioFreq());
  hold(); click(); shot("43_noise_spot");
  printf("noise spots=%u\n", noiseSpotCount());
  for(int k = 0; k < 7; k++) turn(1);
  click();
  printf("noise after exit: active=%d screen=%u freq=%lu\n", noiseActive(), uiScreen(), (unsigned long)radioFreq());
  simNoise = 0;

  // Storm Detector
  appsOpen(SCR_STATIONS); turn(1); shot("44_apps_storm");
  click();
  shot("45_storm_quiet");
  simStormFrom = simMs + 5000;
  uint64_t until = simMs + 14ULL * 60000;
  int alerts = 0;
  uint8_t maxAlert = 0;
  while(simMs < until)
  {
    if(stormTick()) { if(stormAlert() > maxAlert) maxAlert = stormAlert(); }
    if(stormTakeAlert()) alerts++;
    simMs += 2;
    if(simMs % 120000 < 3) { uiDraw(); }
  }
  printf("storm events=%lu n1=%lu n10=%lu n60=%lu alert=%u maxAlert=%u alerts=%d base=%u hz=%lu trend=%d\n", (unsigned long)stormEventTotal(),
         (unsigned long)stormCount(1), (unsigned long)stormCount(10), (unsigned long)stormCount(60), stormAlert(), maxAlert, alerts, stormBase(), (unsigned long)radioFreq(), stormTrend());
  uiDraw(); shot("46_storm_active");
  hold(); shot("47_storm_controls");
  simStormFrom = 0;
  for(int k = 0; k < 6; k++) turn(1);
  click();
  printf("storm after exit: active=%d screen=%u\n", stormActive(), uiScreen());

  // App menu: Hold, then "Back to the radio"
  appsOpen(SCR_STATIONS); click();             // Storm Detector still selected
  hold(); shot("50_app_menu");
  click();
  printf("app menu exit: storm=%d screen=%u\n", stormActive(), uiScreen());
  appsOpen(SCR_STATIONS); turn(-1); turn(-1); click(); hold(); turn(1); turn(1); turn(1); turn(1); shot("51_app_menu_other");
  click(); printf("other apps: ghost=%d screen=%u\n", ghostActive(), uiScreen());
  hold();

  // Search stations launcher, estimates, and a fast FM-only search
  for(uint8_t sp = 0; sp < 3; sp++)
  {
    uint32_t ch = 0, t = scanEstimate(SCAN_ALL, sp, &ch);
    printf("estimate all speed=%s: %lu s, %lu channels\n", scanSpeedName(sp), (unsigned long)t, (unsigned long)ch);
  }
  printf("estimate FM fast: %lu s\n", (unsigned long)scanEstimate(SCAN_FM, 0));
  uiShow(SCR_TUNE); hold();
  for(int k = 0; k < 5; k++) turn(1);   // Save, Visualizer, Stations, Free tuning, Apps, Search stations
  shot("52_menu_search");
  click(); shot("53_search_menu");
  turn(-1); turn(-1); shot("54_search_speed");
  click(); click(); shot("55_search_speed_fast");    // Normal -> Thorough -> Fast
  turn(1); turn(1); turn(1); shot("56_search_fm_row");   // wrap to FM
  {
    uint64_t t0 = simMs;
    uint16_t before = stationCount;
    click();
    printf("FM fast search: %.0f s, stations %u -> %u, speed setting=%s\n", (simMs - t0) / 1000.0, before, stationCount, scanSpeedName(cfg.scanSpeed));
  }
  cfg.scanSpeed = 0;
  { uint64_t t0 = simMs; uiStartScan(); printf("full fast search: %.0f s, stations %u\n", (simMs - t0) / 1000.0, stationCount); }
  int missF = 0; for(auto &a : airFM) missF += stationFind(a.f, GROUP_FM) < 0; for(auto &a : airAM) missF += stationFind(a.f, groupOf(a.f, MODE_AM)) < 0;
  printf("fast missed=%d\n", missF);
  cfg.scanSpeed = 2;
  { uint64_t t0 = simMs; uiStartScan(); printf("full thorough search: %.0f s, stations %u\n", (simMs - t0) / 1000.0, stationCount); }
  printf("estimate all after learning: fast %lu s, normal %lu s, thorough %lu s\n", (unsigned long)scanEstimate(SCAN_ALL, 0), (unsigned long)scanEstimate(SCAN_ALL, 1), (unsigned long)scanEstimate(SCAN_ALL, 2));
  cfg.scanSpeed = 1;
  // Visualizers
  radioTune(99100000, MODE_FM); uiShow(SCR_STATIONS);
  for(int k = 0; k < 20; k++) { simMs += 500; uiTick(); }
  visualOpen(SCR_STATIONS);
  for(int st = 0; st < 6; st++)
  {
    for(int k = 0; k < 40; k++) { simMs += 50; visualTick(); uiDraw(); }
    char nm[40]; snprintf(nm, sizeof(nm), "60_visual_%d", st); save(nm);
    Input in; memset(&in, 0, sizeof(in)); in.turn = 1; in.turnFast = 1; uiInput(in);
  }
  { Input in; memset(&in, 0, sizeof(in)); in.click = true; uiInput(in); }
  cfg.visStyle = 0; for(int k = 0; k < 40; k++) { simMs += 50; visualTick(); uiDraw(); }
  save("66_visual_pinned");
  hold();
  printf("visual exit: screen=%u\n", uiScreen());
  radioTune(9650000, MODE_AM); visualOpen(SCR_STATIONS); cfg.visStyle = 4;
  for(int k = 0; k < 60; k++) { simMs += 50; visualTick(); uiDraw(); }
  save("67_visual_waterfall_sw");
  cfg.visStyle = 5; for(int k = 0; k < 20; k++) { simMs += 50; visualTick(); uiDraw(); }
  save("68_visual_big_sw");
  hold();

  settingsOpen(); for(int k = 0; k < 6; k++) turn(1); click(); shot("59_settings_names_wifi");
  wifiSetCreds("Maison 2.4", "secret"); uiDraw(); shot("59b_settings_names_wifi_set");
  hold();
  settingsOpenScanner(); shot("57_settings_scanner");
  for(int k = 0; k < 5; k++) turn(1);
  shot("58_settings_scanner_parts");
  return 0;
}
