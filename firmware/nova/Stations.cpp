//
// Stations.cpp - the station list: storage, favourites, blocked frequencies,
// names and the filtered view shown on the stations screen
//
#include "Nova.h"

#define LIST_VERSION 2

// Version 1 entry (8-character names), migrated on first start
struct __attribute__((packed)) StationV1 { uint32_t freq; uint8_t group, mode, rssi, snr, flags; char name[9]; };

Station *stations = nullptr;
uint16_t stationCount = 0;
uint16_t *view = nullptr;
uint16_t viewCount = 0;

static uint32_t blocked[BLOCK_MAX];
static uint8_t blockN = 0;
static bool dirty = false;
static uint32_t dirtyAt = 0;

static void markDirty() { dirty = true; dirtyAt = millis(); }

void stationsInit()
{
  if(!stations)
  {
    stations = static_cast<Station *>(ps_malloc(ST_MAX * sizeof(Station)));
    view = static_cast<uint16_t *>(ps_malloc(ST_MAX * sizeof(uint16_t)));
    if(!stations || !view) { stationCount = 0; return; }
  }
  stationCount = 0;
  uint8_t ver = 0;
  size_t got = 0;
  if(storeLoad("listv", &ver, 1, &got) && ver == LIST_VERSION &&
     storeLoad("list", stations, ST_MAX * sizeof(Station), &got) && !(got % sizeof(Station)))
    stationCount = got / sizeof(Station);
  else if(ver == 1)
  {
    StationV1 *old = static_cast<StationV1 *>(ps_malloc(ST_MAX * sizeof(StationV1)));
    if(old && storeLoad("list", old, ST_MAX * sizeof(StationV1), &got) && !(got % sizeof(StationV1)))
    {
      uint16_t n = got / sizeof(StationV1);
      for(uint16_t i = 0; i < n; i++)
      {
        Station &d = stations[i];
        memset(&d, 0, sizeof(d));
        d.freq = old[i].freq; d.group = old[i].group; d.mode = old[i].mode;
        d.rssi = old[i].rssi; d.snr = old[i].snr; d.flags = old[i].flags;
        memcpy(d.name, old[i].name, 8);
      }
      stationCount = n;
      dirty = true; dirtyAt = 0;
    }
    if(old) free(old);
  }
  if(storeLoad("block", blocked, sizeof(blocked), &got) && !(got % sizeof(uint32_t)))
    blockN = got / sizeof(uint32_t);

  // Drop anything that does not make sense
  uint16_t n = 0;
  for(uint16_t i = 0; i < stationCount; i++)
    if(stations[i].freq && stations[i].group < GROUP_COUNT && stations[i].mode < MODE_COUNT)
    {
      stations[i].name[ST_NAME_LEN - 1] = '\0';
      stations[n++] = stations[i];
    }
  stationCount = n;
  viewRebuild();
}

void stationsSave()
{
  if(!stations) return;
  uint8_t ver = LIST_VERSION;
  storeSave("listv", &ver, 1);
  storeSave("list", stations, stationCount * sizeof(Station));
  storeSave("block", blocked, blockN * sizeof(uint32_t));
  dirty = false;
}

void stationsTick()
{
  stationLearnName();
  if(dirty && millis() - dirtyAt > 15000) stationsSave();
}

int16_t stationFind(uint32_t hz, uint8_t group)
{
  for(uint16_t i = 0; i < stationCount; i++)
    if(stations[i].freq == hz && stations[i].group == group) return i;
  return -1;
}

int16_t stationTuned()
{
  return stationFind(radioFreq(), groupOf(radioFreq(), radioMode()));
}

// Display name: live RDS / schedule when tuned, else stored, else 24/7 list
void stationLabel(uint16_t i, char *buf, size_t size, bool live)
{
  const Station &s = stations[i];
  buf[0] = '\0';
  if((s.flags & ST_CUSTOM) && s.name[0]) { snprintf(buf, size, "%s", s.name); return; }
  if(live && radioMode() == MODE_FM && s.group == GROUP_FM && rdsName()[0])
  {
    snprintf(buf, size, "%s", rdsName());
    return;
  }
  if(s.name[0]) { snprintf(buf, size, "%s", s.name); return; }
  if(s.group != GROUP_FM)
  {
    const char *n = scheduleLookup(s.freq);
    if(!n) n = knownStation(s.freq);
    if(n) snprintf(buf, size, "%s", n);
  }
}

void stationToggleFav(uint16_t i)
{
  if(i >= stationCount) return;
  stations[i].flags ^= ST_FAV;
  stationsSave();
  viewRebuild();
}

static int cmpStation(const Station &a, const Station &b)
{
  if(a.group != b.group) return a.group < b.group ? -1 : 1;
  return a.freq < b.freq ? -1 : a.freq > b.freq ? 1 : 0;
}

static void sortStations()
{
  for(uint16_t i = 1; i < stationCount; i++)
  {
    Station key = stations[i];
    int j = i - 1;
    while(j >= 0 && cmpStation(stations[j], key) > 0) { stations[j + 1] = stations[j]; j--; }
    stations[j + 1] = key;
  }
}

int16_t stationAddManual(uint32_t hz, uint8_t mode)
{
  uint8_t g = groupOf(hz, mode);
  int16_t i = stationFind(hz, g);
  if(i >= 0)
  {
    stations[i].flags |= ST_FAV;
    stations[i].mode = mode;
  }
  else
  {
    if(stationCount >= ST_MAX) return -1;
    Station s;
    memset(&s, 0, sizeof(s));
    s.freq = hz; s.group = g; s.mode = mode;
    s.flags = ST_FAV | ST_MANUAL;
    Signal sg = radioSignal();
    s.rssi = sg.rssi ? sg.rssi : 1; s.snr = sg.snr;
    if(mode == MODE_FM && rdsName()[0]) snprintf(s.name, ST_NAME_LEN, "%s", rdsName());
    stations[stationCount++] = s;
    sortStations();
  }
  stationsSave();
  viewRebuild();
  return stationFind(hz, g);
}

bool stationIsBlocked(uint32_t hz)
{
  for(uint8_t k = 0; k < blockN; k++) if(blocked[k] == hz) return true;
  return false;
}

void stationBlock(uint16_t i)
{
  if(i >= stationCount) return;
  if(blockN < BLOCK_MAX && !stationIsBlocked(stations[i].freq)) blocked[blockN++] = stations[i].freq;
  for(uint16_t k = i; k + 1 < stationCount; k++) stations[k] = stations[k + 1];
  stationCount--;
  stationsSave();
  viewRebuild();
}

uint8_t blockedCount() { return blockN; }
void blockedClear() { blockN = 0; stationsSave(); }
void stationsClear() { stationCount = 0; stationsSave(); viewRebuild(); }

// Replace every station of a scanned range by fresh results. Names and
// favourites are inherited; favourites that were not heard stay (dimmed).
void stationReplaceRange(uint8_t group, uint32_t lo, uint32_t hi, const Station *fresh, uint16_t n, bool markNew)
{
  static Station merged[ST_MAX];
  uint16_t m = 0;
  auto inRange = [&](const Station &s) { return s.group == group && s.freq >= lo && s.freq <= hi; };

  for(uint16_t i = 0; i < stationCount && m < ST_MAX; i++)
    if(!inRange(stations[i])) merged[m++] = stations[i];

  for(uint16_t k = 0; k < n && m < ST_MAX; k++)
  {
    Station s = fresh[k];
    bool known = false;
    for(uint16_t i = 0; i < stationCount; i++)
      if(stations[i].group == group && stations[i].freq == s.freq)
      {
        s.flags |= stations[i].flags;
        if(!s.name[0]) memcpy(s.name, stations[i].name, ST_NAME_LEN);
        if(stations[i].flags & ST_MANUAL) s.mode = stations[i].mode;
        known = true;
        break;
      }
    if(!known && markNew) s.flags |= ST_NEW;
    merged[m++] = s;
  }

  for(uint16_t i = 0; i < stationCount && m < ST_MAX; i++)
  {
    if(!inRange(stations[i]) || !(stations[i].flags & ST_FAV)) continue;
    bool heard = false;
    for(uint16_t k = 0; k < n; k++) if(fresh[k].freq == stations[i].freq) { heard = true; break; }
    if(!heard) { merged[m] = stations[i]; merged[m].rssi = merged[m].snr = 0; m++; }
  }

  memcpy(stations, merged, m * sizeof(Station));
  stationCount = m;
  sortStations();
  markDirty();
}

uint16_t stationNewCount()
{
  uint16_t c = 0;
  for(uint16_t i = 0; i < stationCount; i++) c += !!(stations[i].flags & ST_NEW);
  return c;
}

// The user listened to it: it is not new any more
void stationSeen(int16_t i)
{
  if(i < 0 || i >= stationCount || !(stations[i].flags & ST_NEW)) return;
  stations[i].flags &= ~ST_NEW;
  markDirty();
}

void stationLearnName()
{
  if(radioMode() != MODE_FM || !rdsName()[0]) return;
  int16_t i = stationTuned();
  if(i < 0 || stations[i].name[0]) return;
  snprintf(stations[i].name, ST_NAME_LEN, "%s", rdsName());
  markDirty();
  uiRequestDraw();
}

//
// Filtered view
//
const char *filterName(uint8_t f)
{
  static const char *const names[FILTER_COUNT] = { "All", "Favorites", "New", "FM", "AM", "Shortwave" };
  return names[f < FILTER_COUNT ? f : 0];
}

void viewRebuild()
{
  viewCount = 0;
  if(!stations) return;
  for(uint16_t i = 0; i < stationCount; i++)
  {
    const Station &s = stations[i];
    bool ok;
    switch(cfg.listFilter)
    {
      case 1:  ok = s.flags & ST_FAV; break;
      case 2:  ok = s.flags & ST_NEW; break;
      case 3:  ok = s.group == GROUP_FM; break;
      case 4:  ok = s.group == GROUP_AM; break;
      case 5:  ok = s.group == GROUP_SW; break;
      default: ok = true; break;
    }
    if(ok) view[viewCount++] = i;
  }
  uiRequestDraw();
}

//
// Web editor: replace the whole list or the blocked frequencies
//
bool stationsReplaceAll(const Station *list, uint16_t n)
{
  if(!stations) return false;
  uint16_t m = 0;
  for(uint16_t k = 0; k < n && m < ST_MAX; k++)
  {
    Station s = list[k];
    if(s.mode >= MODE_COUNT || s.freq < radioMinFreq(s.mode) || s.freq > radioMaxFreq(s.mode)) continue;
    s.group = groupOf(s.freq, s.mode);
    s.name[ST_NAME_LEN - 1] = '\0';
    bool dup = false;
    for(uint16_t i = 0; i < m; i++) if(stations[i].freq == s.freq && stations[i].group == s.group) { dup = true; break; }
    if(!dup) stations[m++] = s;
  }
  stationCount = m;
  sortStations();
  stationsSave();
  viewRebuild();
  return true;
}

uint32_t blockedAt(uint8_t i) { return i < blockN ? blocked[i] : 0; }

void blockedReplace(const uint32_t *list, uint8_t n)
{
  blockN = 0;
  for(uint8_t k = 0; k < n && blockN < BLOCK_MAX; k++) if(list[k]) blocked[blockN++] = list[k];
  stationsSave();
}
