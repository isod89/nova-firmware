//
// Schedule.cpp - offline station names (EiBi schedules) and band labels
//
// The EiBi list comes from the web editor over USB (Usb.cpp) or is downloaded
// over Wi-Fi on request (Wifi.cpp). It is sorted, stored in LittleFS and held
// in PSRAM for instant lookups.
//
#include "Nova.h"
#ifndef NOVA_SIM
#include <LittleFS.h>
#endif

#define SCHED_PATH  "/eibi.bin"
#define SCHED_MAX   20000

struct __attribute__((packed)) Sched
{
  uint16_t khz;
  uint16_t start;      // minutes UTC
  uint16_t end;        // minutes UTC, end <= start means across midnight
  char     name[26];
};

static Sched *sched = nullptr;
static uint32_t schedCount = 0;
static Sched *importBuf = nullptr;
static uint32_t importCount = 0;

uint32_t scheduleCount() { return schedCount; }

#ifndef NOVA_SIM
void scheduleInit()
{
  if(!LittleFS.begin(false, "/littlefs", 10, "littlefs"))
  {
    LittleFS.format();
    if(!LittleFS.begin(false, "/littlefs", 10, "littlefs")) return;
  }
  File f = LittleFS.open(SCHED_PATH, "r");
  if(!f) return;
  size_t n = f.size() / sizeof(Sched);
  if(n && n <= SCHED_MAX)
  {
    sched = static_cast<Sched *>(ps_malloc(n * sizeof(Sched)));
    if(sched && f.read((uint8_t *)sched, n * sizeof(Sched)) == n * sizeof(Sched)) schedCount = n;
    else { free(sched); sched = nullptr; }
  }
  f.close();
}

static bool scheduleWrite(const Sched *data, uint32_t n)
{
  File f = LittleFS.open("/eibi.tmp", "w");
  if(!f) return false;
  bool ok = f.write((const uint8_t *)data, n * sizeof(Sched)) == n * sizeof(Sched);
  f.close();
  if(!ok) { LittleFS.remove("/eibi.tmp"); return false; }
  LittleFS.remove(SCHED_PATH);
  return LittleFS.rename("/eibi.tmp", SCHED_PATH);
}
#else
void scheduleInit() {}
static bool scheduleWrite(const Sched *, uint32_t) { return true; }
#endif

static bool isNow(const Sched &s, uint16_t now)
{
  if(s.start == s.end) return true;
  if(s.start < s.end) return now >= s.start && now < s.end;
  return now >= s.start || now < s.end;
}

const char *scheduleLookup(uint32_t hz)
{
  uint8_t h, m;
  if(!schedCount || !clockUTCHM(h, m)) return nullptr;
  uint16_t khz = (hz + 500) / 1000;
  uint16_t now = h * 60 + m;

  // First entry with this frequency
  uint32_t lo = 0, hi = schedCount;
  while(lo < hi)
  {
    uint32_t mid = (lo + hi) / 2;
    if(sched[mid].khz < khz) lo = mid + 1; else hi = mid;
  }
  for(uint32_t i = lo; i < schedCount && sched[i].khz == khz; i++)
    if(isNow(sched[i], now)) return sched[i].name;
  return nullptr;
}

//
// Import (one text line of eibi.txt at a time)
//
static char asciiOf(uint8_t c)
{
  static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  if(c >= 0xC0) return latin1[c - 0xC0];
  if(c >= 0x20 && c < 0x7F) return c;
  return ' ';
}

bool scheduleImportBegin()
{
  if(!importBuf) importBuf = static_cast<Sched *>(ps_malloc(SCHED_MAX * sizeof(Sched)));
  importCount = 0;
  return importBuf != nullptr;
}

// eibi.txt fixed columns: kHz (14) | time "hhmm-hhmm" (9) | days+ITU (11) | station (24) ...
bool scheduleImportLine(char *line)
{
  if(!importBuf || importCount >= SCHED_MAX) return false;

  // Convert UTF-8 two-byte Latin letters to Latin-1 first, in place
  char *w = line;
  for(char *r = line; *r; r++)
  {
    uint8_t c = *r;
    if(c == 0xC3 && ((uint8_t)r[1] & 0xC0) == 0x80) { *w++ = (char)(0xC0 + ((uint8_t)r[1] & 0x3F)); r++; }
    else *w++ = *r;
  }
  *w = '\0';

  char *p = line;
  while(*p == ' ') p++;
  if(*p < '0' || *p > '9') return false;
  size_t len = strlen(line);
  if(len < 24) return false;

  float khz = atof(line);
  if(khz < 100 || khz > 30000) return false;

  int sh, sm, eh, em;
  if(sscanf(line + 14, "%2d%2d-%2d%2d", &sh, &sm, &eh, &em) != 4) return false;

  char name[26];
  size_t o = 0;
  if(len > 34)
    for(size_t i = 34; i < len && i < 34 + 24 && o + 1 < sizeof(name); i++) name[o++] = asciiOf((uint8_t)line[i]);
  name[o] = '\0';
  while(o && name[o - 1] == ' ') name[--o] = '\0';
  char *n = name;
  while(*n == ' ') n++;
  if(!*n || strstr(n, "Jammer")) return false;

  Sched &s = importBuf[importCount++];
  s.khz = (uint16_t)(khz + 0.5f);
  s.start = (sh % 24) * 60 + sm;
  s.end = (eh >= 24 ? 0 : eh * 60) + em;
  if(eh >= 24 && sh == 0 && sm == 0) s.end = s.start;   // 0000-2400 = all day
  snprintf(s.name, sizeof(s.name), "%s", n);
  return true;
}

static int cmpSched(const void *a, const void *b)
{
  const Sched *x = (const Sched *)a, *y = (const Sched *)b;
  if(x->khz != y->khz) return x->khz < y->khz ? -1 : 1;
  return x->start < y->start ? -1 : x->start > y->start ? 1 : 0;
}

bool scheduleImportEnd(uint32_t &count)
{
  count = 0;
  if(!importBuf || !importCount) return false;
  qsort(importBuf, importCount, sizeof(Sched), cmpSched);
  if(!scheduleWrite(importBuf, importCount)) return false;
  free(sched);
  sched = importBuf;
  schedCount = importCount;
  importBuf = nullptr;
  count = schedCount;
  return true;
}

//
// Names that need no schedule
//
const char *knownStation(uint32_t hz)
{
  if(hz % 1000) return nullptr;
  switch(hz / 1000)
  {
    case 3330: case 7850: case 14670:             return "CHU Ottawa - time";
    case 2500: case 5000: case 10000: case 15000:  return "WWV/WWVH - time";
    case 20000: case 25000:                        return "WWV - time";
    default:                                       return nullptr;
  }
}

struct Label { uint32_t lo, hi; const char *name; };
static const Label labels[] =
{
  {   148000,   283000, "Longwave" },
  {   472000,   479000, "630m Ham" },
  {   490000,   519000, "NAVTEX / Maritime" },
  {   520000,  1710000, "Medium wave" },
  {  1800000,  2000000, "160m Ham" },
  {  2300000,  2495000, "120m Broadcast" },
  {  3200000,  3400000, "90m Broadcast" },
  {  3500000,  3900000, "80m Ham" },
  {  3900000,  4000000, "75m Broadcast" },
  {  4750000,  5060000, "60m Broadcast" },
  {  5330000,  5410000, "60m Ham" },
  {  5900000,  6200000, "49m Broadcast" },
  {  7000000,  7200000, "40m Ham" },
  {  7200000,  7600000, "41m Broadcast" },
  {  9400000,  9900000, "31m Broadcast" },
  { 10100000, 10150000, "30m Ham" },
  { 11600000, 12100000, "25m Broadcast" },
  { 13570000, 13870000, "22m Broadcast" },
  { 14000000, 14350000, "20m Ham" },
  { 15100000, 15800000, "19m Broadcast" },
  { 17480000, 17900000, "16m Broadcast" },
  { 18068000, 18168000, "17m Ham" },
  { 18900000, 19020000, "15m Broadcast" },
  { 21000000, 21450000, "15m Ham" },
  { 21450000, 21850000, "13m Broadcast" },
  { 24890000, 24990000, "12m Ham" },
  { 25670000, 26100000, "11m Broadcast" },
  { 26965000, 27405000, "CB" },
  { 28000000, 29700000, "10m Ham" },
};

const char *bandLabel(uint32_t hz, uint8_t mode)
{
  if(mode == MODE_FM)
  {
    if(hz >= 87500000) return "FM Broadcast";
    if(hz >= 76000000) return "FM Japan / Extended";
    if(hz >= 65800000 && hz <= 74000000) return "OIRT FM";
    return "VHF";
  }
  for(size_t i = 0; i < ITEM_COUNT(labels); i++)
    if(hz >= labels[i].lo && hz < labels[i].hi) return labels[i].name;
  return hz < 1711000 ? "Medium wave" : "Shortwave";
}
