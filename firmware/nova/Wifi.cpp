//
// Wifi.cpp - Wi-Fi, used only on request: download the EiBi schedule and set
// the clock (NTP). The radio joins the network saved from the web editor,
// does the job with its audio muted, and turns Wi-Fi off again: Wi-Fi is never
// on while you listen.
//
#include "Nova.h"
#ifndef NOVA_SIM
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

#define WIFI_VERSION 1
#define EIBI_URL     "http://www.eibispace.de/dx/eibi.txt"

struct WifiCfg { uint8_t version; char ssid[33]; char pass[65]; };
static WifiCfg wcfg;

void wifiInit()
{
  size_t got = 0;
  if(!storeLoad("wifi", &wcfg, sizeof(wcfg), &got) || got != sizeof(wcfg) || wcfg.version != WIFI_VERSION) memset(&wcfg, 0, sizeof(wcfg));
  wcfg.ssid[sizeof(wcfg.ssid) - 1] = '\0';
  wcfg.pass[sizeof(wcfg.pass) - 1] = '\0';
}

bool wifiConfigured() { return wcfg.ssid[0] != '\0'; }
const char *wifiSsid() { return wcfg.ssid; }

void wifiSetCreds(const char *ssid, const char *pass)
{
  memset(&wcfg, 0, sizeof(wcfg));
  wcfg.version = WIFI_VERSION;
  snprintf(wcfg.ssid, sizeof(wcfg.ssid), "%s", ssid ? ssid : "");
  snprintf(wcfg.pass, sizeof(wcfg.pass), "%s", pass ? pass : "");
  storeSave("wifi", &wcfg, sizeof(wcfg));
}

void wifiForget() { wifiSetCreds("", ""); }

static WifiProgress progressFn = nullptr;
static void progress(const char *stage, uint32_t n)
{
  if(progressFn) progressFn(stage, n);
  char line[48];
  if(n) snprintf(line, sizeof(line), "%s  %lu", stage, (unsigned long)n); else snprintf(line, sizeof(line), "%s", stage);
  uiMessage("Wi-Fi", line);
}

#ifndef NOVA_SIM
static bool join(char *err, size_t en)
{
  if(!wifiConfigured()) { snprintf(err, en, "No Wi-Fi network saved"); return false; }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wcfg.ssid, wcfg.pass);
  uint32_t t0 = millis();
  while(WiFi.status() != WL_CONNECTED)
  {
    if(inputAbort()) { snprintf(err, en, "Cancelled"); return false; }
    if(millis() - t0 > 20000)
    {
      wl_status_t s = WiFi.status();
      snprintf(err, en, s == WL_NO_SSID_AVAIL ? "Network %s not found" : s == WL_CONNECT_FAILED ? "Wrong password for %s" : "Could not join %s", wcfg.ssid);
      return false;
    }
    delay(150);
    progress("Joining the network", 0);
  }
  return true;
}

static void leave()
{
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

static bool syncClock()
{
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  uint32_t t0 = millis();
  while(millis() - t0 < 8000)
  {
    time_t now = time(nullptr);
    if(now > 1700000000) { clockSet((uint32_t)now); return true; }
    delay(200);
  }
  return false;
}

static bool download(const char *url, uint32_t &count, char *err, size_t en)
{
  String where = url;
  for(int hop = 0; hop < 3; hop++)
  {
    bool tls = where.startsWith("https://");
    WiFiClient plain;
    WiFiClientSecure secure;
    secure.setInsecure();          // public schedule data; nothing secret is sent
    HTTPClient http;
    http.setTimeout(15000);
    const char *keep[] = { "Location" };
    http.collectHeaders(keep, 1);
    if(!(tls ? http.begin(secure, where) : http.begin(plain, where))) { snprintf(err, en, "Bad address"); return false; }
    progress("Asking eibispace.de", 0);
    int code = http.GET();
    if(code == 301 || code == 302 || code == 307 || code == 308)
    {
      where = http.header("Location");
      http.end();
      if(!where.length()) break;
      continue;
    }
    if(code != 200) { snprintf(err, en, code < 0 ? "No answer from eibispace.de" : "eibispace.de answered %d", code); http.end(); return false; }

    if(!scheduleImportBegin()) { snprintf(err, en, "Not enough memory"); http.end(); return false; }
    WiFiClient *s = http.getStreamPtr();
    char line[256];
    size_t len = 0;
    uint32_t lines = 0, lastByte = millis();
    int32_t left = http.getSize();          // -1 when unknown
    while(http.connected() && (left > 0 || left == -1))
    {
      int avail = s->available();
      if(!avail)
      {
        if(millis() - lastByte > 15000) break;
        if(inputAbort()) { snprintf(err, en, "Cancelled"); http.end(); return false; }
        delay(2);
        continue;
      }
      lastByte = millis();
      while(avail--)
      {
        int c = s->read();
        if(c < 0) break;
        if(left > 0) left--;
        if(c == '\n' || len == sizeof(line) - 1)
        {
          line[len] = '\0';
          if(len && line[len - 1] == '\r') line[len - 1] = '\0';
          scheduleImportLine(line);
          len = 0;
          if(!(++lines % 500)) progress("Downloading lines", lines);
        }
        else line[len++] = (char)c;
      }
    }
    if(len) { line[len] = '\0'; scheduleImportLine(line); }
    http.end();
    progress("Sorting and saving", 0);
    if(!scheduleImportEnd(count) || !count) { snprintf(err, en, "The file had no schedules"); return false; }
    return true;
  }
  snprintf(err, en, "Too many redirections");
  return false;
}

bool wifiRun(uint8_t job, WifiProgress fn, char *msg, size_t mn)
{
  progressFn = fn;
  appsStopAll();
  radioMute(true);
  bool ok = join(msg, mn);
  if(ok)
  {
    String ip = WiFi.localIP().toString();
    int rssi = WiFi.RSSI();
    progress("Setting the clock", 0);
    bool clock = syncClock();
    if(job == WIFI_JOB_TEST)
      snprintf(msg, mn, "%s\t%d\t%u", ip.c_str(), rssi, clock);
    else
    {
      uint32_t count = 0;
      ok = download(EIBI_URL, count, msg, mn);
      if(ok) snprintf(msg, mn, "%lu", (unsigned long)count);
    }
  }
  leave();
  radioMute(false);
  progressFn = nullptr;
  uiRequestDraw();
  return ok;
}
#else
// The simulator pretends: the clock is set and a few schedules arrive
bool wifiRun(uint8_t job, WifiProgress fn, char *msg, size_t mn)
{
  progressFn = fn;
  if(!wifiConfigured()) { snprintf(msg, mn, "No Wi-Fi network saved"); return false; }
  progress("Joining the network", 0);
  if(job == WIFI_JOB_TEST) { snprintf(msg, mn, "192.168.1.42\t-58\t1"); progressFn = nullptr; return true; }
  scheduleImportBegin();
  char line[80];
  snprintf(line, sizeof(line), "%-14s%-9s%-11s%s", "9650", "0000-2400", "", "KBS World Radio"); scheduleImportLine(line);
  snprintf(line, sizeof(line), "%-14s%-9s%-11s%s", "6070", "0000-2400", "", "CFRX Toronto"); scheduleImportLine(line);
  progress("Downloading lines", 2);
  uint32_t count = 0;
  bool ok = scheduleImportEnd(count);
  snprintf(msg, mn, "%lu", (unsigned long)count);
  progressFn = nullptr;
  return ok;
}
#endif
