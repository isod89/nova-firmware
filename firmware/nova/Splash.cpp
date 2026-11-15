//
// Splash.cpp - the animated boot screen: the NOVA star bursts in, its radio
// waves open on both sides, then the name and the tagline appear.
//
// It runs on the other CPU core while the radio starts, so it costs no time:
// the radio is ready when the animation ends. A click skips it, and it can be
// turned off (Settings > Display > Boot animation).
//
#include "Nova.h"
#include "Ui.h"

#define SPLASH_LEN 1.9f          // seconds of animation

// Brand colours (RGB888) of the logo gradient
static const uint8_t G0[3] = { 0x5E, 0xE7, 0xFF }, G1[3] = { 0x7C, 0x6C, 0xFF }, G2[3] = { 0xE8, 0x79, 0xF9 };
static const uint8_t BG0[3] = { 0x16, 0x1D, 0x3A }, BG1[3] = { 0x07, 0x09, 0x12 };

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static inline float easeOut(float t) { t = clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }
static inline float easeBack(float t)
{
  t = clamp01(t);
  const float c1 = 1.70158f, c3 = c1 + 1;
  return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2);
}
static inline uint16_t rgb(float r, float g, float b) { return lgfx::color565((uint8_t)r, (uint8_t)g, (uint8_t)b); }

// Logo gradient along the diagonal, u in 0..1
static void grad(float u, float out[3])
{
  u = clamp01(u);
  const uint8_t *a = u < 0.52f ? G0 : G1, *b = u < 0.52f ? G1 : G2;
  float k = u < 0.52f ? u / 0.52f : (u - 0.52f) / 0.48f;
  for(int i = 0; i < 3; i++) out[i] = a[i] + (b[i] - a[i]) * k;
}

static void background(float y, float out[3])
{
  float k = clamp01(y / SCREEN_H);
  for(int i = 0; i < 3; i++) out[i] = BG0[i] + (BG1[i] - BG0[i]) * k;
}

// Blend a colour over the pixel already in the sprite
static inline void blendPixel(int x, int y, const float c[3], float a)
{
  if(x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H || a <= 0) return;
  if(a > 1) a = 1;
  uint16_t *buf = static_cast<uint16_t *>(spr.getBuffer());
  uint16_t p = buf[y * SCREEN_W + x];
  p = (p >> 8) | (p << 8);                         // sprite pixels are byte-swapped
  float r = ((p >> 11) & 31) * 255.0f / 31, g = ((p >> 5) & 63) * 255.0f / 63, b = (p & 31) * 255.0f / 31;
  uint16_t q = rgb(r + (c[0] - r) * a, g + (c[1] - g) * a, b + (c[2] - b) * a);
  buf[y * SCREEN_W + x] = (q >> 8) | (q << 8);
}

// The four-pointed star: concave sides (quadratic curves through the centre
// region), filled with the gradient, anti-aliased with 4 sub-scanlines
static void drawStar(float cx, float cy, float R, float rot, float bright)
{
  if(R < 1) return;
  const int SEG = 14, N = SEG * 4;
  float px[N], py[N];
  float k = R * 0.06f;                              // how much the sides curve in
  float cs = cosf(rot), sn = sinf(rot);
  for(int s = 0; s < 4; s++)
  {
    // tips at 0, 90, 180, 270 degrees
    float a0 = s * 1.5707963f, a1 = a0 + 1.5707963f;
    float x0 = cosf(a0) * R, y0 = sinf(a0) * R, x2 = cosf(a1) * R, y2 = sinf(a1) * R;
    float mx = cosf(a0 + 0.785398f) * k, my = sinf(a0 + 0.785398f) * k;
    for(int i = 0; i < SEG; i++)
    {
      float t = (float)i / SEG, u = 1 - t;
      float x = u * u * x0 + 2 * u * t * mx + t * t * x2, y = u * u * y0 + 2 * u * t * my + t * t * y2;
      px[s * SEG + i] = cx + x * cs - y * sn;
      py[s * SEG + i] = cy + x * sn + y * cs;
    }
  }
  int y0 = (int)floorf(cy - R - 1), y1 = (int)ceilf(cy + R + 1);
  int xa = (int)floorf(cx - R - 1), xb = (int)ceilf(cx + R + 1);
  if(xa < 0) xa = 0;
  if(xb >= SCREEN_W) xb = SCREEN_W - 1;
  static float cov[SCREEN_W];
  for(int y = y0 < 0 ? 0 : y0; y <= y1 && y < SCREEN_H; y++)
  {
    for(int x = xa; x <= xb; x++) cov[x] = 0;
    for(int sub = 0; sub < 4; sub++)
    {
      float sy = y + (sub + 0.5f) / 4;
      float xs[8]; int n = 0;
      for(int i = 0, j = N - 1; i < N && n < 8; j = i++)
        if((py[i] > sy) != (py[j] > sy)) xs[n++] = px[j] + (sy - py[j]) * (px[i] - px[j]) / (py[i] - py[j]);
      for(int a = 1; a < n; a++) { float v = xs[a]; int b = a - 1; while(b >= 0 && xs[b] > v) { xs[b + 1] = xs[b]; b--; } xs[b + 1] = v; }
      for(int p = 0; p + 1 < n; p += 2)
      {
        float l = xs[p], r = xs[p + 1];
        int il = (int)floorf(l), ir = (int)floorf(r);
        for(int x = il; x <= ir; x++)
        {
          if(x < xa || x > xb) continue;
          float c = 1;
          if(x == il) c -= l - il;
          if(x == ir) c -= (ir + 1) - r;
          if(il == ir) c = r - l;
          cov[x] += (c < 0 ? 0 : c) / 4;
        }
      }
    }
    for(int x = xa; x <= xb; x++)
    {
      if(cov[x] <= 0) continue;
      float col[3];
      grad(((x - (cx - R)) + (y - (cy - R))) / (4 * R), col);
      for(int i = 0; i < 3; i++) col[i] = col[i] + (255 - col[i]) * bright;
      blendPixel(x, y, col, cov[x]);
    }
  }
}

// Background and glow, computed once with ordered dithering (no banding),
// kept in PSRAM: each frame starts from a copy
static uint16_t *bgPlain = nullptr, *bgGlow = nullptr;
static const float GCX = 160, GCY = 66, GR = 62;
static void buildBackgrounds()
{
  if(bgPlain) return;
  size_t n = SCREEN_W * SCREEN_H;
  bgPlain = static_cast<uint16_t *>(ps_malloc(n * 2));
  bgGlow = static_cast<uint16_t *>(ps_malloc(n * 2));
  if(!bgPlain || !bgGlow) { free(bgPlain); free(bgGlow); bgPlain = bgGlow = nullptr; return; }
  static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
  for(int y = 0; y < SCREEN_H; y++)
    for(int x = 0; x < SCREEN_W; x++)
    {
      float c[3]; background(y, c);
      float d = sqrtf((x - GCX) * (x - GCX) + (y - GCY) * (y - GCY)) / GR;
      float a = d < 1 ? 0.55f * (1 - d) * (1 - d) : 0;
      float g[3] = { c[0] + (0x9F - c[0]) * a, c[1] + (0x8C - c[1]) * a, c[2] + (0xFF - c[2]) * a };
      float th = (bayer[y & 3][x & 3] + 0.5f) / 16.0f;
      auto q = [&](const float v[3]) {
        // 5-6-5 with dithering
        int r = (int)(v[0] * 31 / 255 + th), gg = (int)(v[1] * 63 / 255 + th), b = (int)(v[2] * 31 / 255 + th);
        if(r > 31) r = 31;
        if(gg > 63) gg = 63;
        if(b > 31) b = 31;
        uint16_t p = (r << 11) | (gg << 5) | b;
        return (uint16_t)((p >> 8) | (p << 8));
      };
      bgPlain[y * SCREEN_W + x] = q(c);
      bgGlow[y * SCREEN_W + x] = q(g);
    }
}

static void drawBackground(float glowA)
{
  uint16_t *buf = static_cast<uint16_t *>(spr.getBuffer());
  if(!bgPlain)
  {
    for(int y = 0; y < SCREEN_H; y++) { float c[3]; background(y, c); spr.drawFastHLine(0, y, SCREEN_W, rgb(c[0], c[1], c[2])); }
    return;
  }
  memcpy(buf, glowA >= 1 ? bgGlow : bgPlain, SCREEN_W * SCREEN_H * 2);
  if(glowA <= 0 || glowA >= 1) return;
  // Fade the glow in: blend the two inside its square only
  int x0 = (int)(GCX - GR), x1 = (int)(GCX + GR), y0 = (int)(GCY - GR), y1 = (int)(GCY + GR);
  int k = (int)(glowA * 256);
  for(int y = y0 < 0 ? 0 : y0; y <= y1 && y < SCREEN_H; y++)
    for(int x = x0; x <= x1; x++)
    {
      uint16_t a = bgPlain[y * SCREEN_W + x], b = bgGlow[y * SCREEN_W + x];
      if(a == b) continue;
      a = (a >> 8) | (a << 8); b = (b >> 8) | (b << 8);
      int r = ((a >> 11) * (256 - k) + (b >> 11) * k) >> 8;
      int g = (((a >> 5) & 63) * (256 - k) + ((b >> 5) & 63) * k) >> 8;
      int bl = ((a & 31) * (256 - k) + (b & 31) * k) >> 8;
      uint16_t p = (r << 11) | (g << 5) | bl;
      buf[y * SCREEN_W + x] = (p >> 8) | (p << 8);
    }
}

// Radio wave: a thick arc with round ends, anti-aliased from its distance field
static void wave(float cx, float cy, float r, float half, float side, float width, float alpha)
{
  if(half <= 0.01f || alpha <= 0) return;
  float base = side > 0 ? 0 : 3.14159265f, hw = width / 2;
  float ex0 = cx + r * cosf(base - half), ey0 = cy + r * sinf(base - half);
  float ex1 = cx + r * cosf(base + half), ey1 = cy + r * sinf(base + half);
  int xa = (int)floorf(fminf(fminf(ex0, ex1), side > 0 ? cx + r * cosf(half) : cx - r) - hw - 1);
  int xb = (int)ceilf(fmaxf(fmaxf(ex0, ex1), side > 0 ? cx + r : cx - r * cosf(half)) + hw + 1);
  int ya = (int)floorf(fminf(ey0, ey1) - hw - 1), yb = (int)ceilf(fmaxf(ey0, ey1) + hw + 1);
  for(int y = ya; y <= yb; y++)
    for(int x = xa; x <= xb; x++)
    {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      float ang = atan2f(dy, dx) - base;
      while(ang > 3.14159265f) ang -= 6.2831853f;
      while(ang < -3.14159265f) ang += 6.2831853f;
      float d;
      if(fabsf(ang) <= half) d = fabsf(sqrtf(dx * dx + dy * dy) - r);
      else
      {
        float ex = ang > 0 ? ex1 : ex0, ey = ang > 0 ? ey1 : ey0;
        d = sqrtf((x + 0.5f - ex) * (x + 0.5f - ex) + (y + 0.5f - ey) * (y + 0.5f - ey));
      }
      float cov = hw - d + 0.5f;
      if(cov <= 0) continue;
      float col[3];
      grad(((x - (cx - r)) + (y - (cy - r))) / (4 * r), col);
      blendPixel(x, y, col, (cov > 1 ? 1 : cov) * alpha);
    }
}

static void splashFrame(float t)
{
  // Background: the brand's deep navy, the glow fading in
  drawBackground(easeOut(t / 0.5f));
  const float cx = 160, cy = 66;

  // 1. star (0 - 0.55 s), with a short shine at the end
  float s = easeBack(t / 0.55f);
  float rot = (1 - easeOut(t / 0.7f)) * 0.9f;
  float shine = t > 1.35f && t < 1.75f ? sinf((t - 1.35f) / 0.4f * 3.14159f) * 0.35f : 0;
  drawStar(cx, cy, 34 * s, rot, shine);

  // 2. waves open, inner then outer (0.25 - 1.0 s)
  float w1 = easeOut((t - 0.25f) / 0.45f), w2 = easeOut((t - 0.45f) / 0.5f);
  for(float side : { 1.0f, -1.0f })
  {
    wave(cx, cy, 49, 0.66f * w1, side, 6.5f, w1);
    wave(cx, cy, 63, 0.66f * w2, side, 6.5f, w2 * 0.5f);
  }

  // 3. the name, letter by letter (0.7 - 1.25 s), then the tagline
  const char *name = "NOVA";
  spr.setFont(F_LG);
  int total = 0, adv[4];
  for(int i = 0; i < 4; i++) { char ch[2] = { name[i], 0 }; adv[i] = spr.textWidth(ch) + 7; total += adv[i]; }
  total -= 7;
  int x = 160 - total / 2;
  spr.setTextDatum(ML_DATUM);
  for(int i = 0; i < 4; i++)
  {
    float a = easeOut((t - 0.7f - i * 0.08f) / 0.3f);
    if(a > 0)
    {
      float bg[3]; background(126, bg);
      spr.setTextColor(rgb(bg[0] + (255 - bg[0]) * a, bg[1] + (255 - bg[1]) * a, bg[2] + (255 - bg[2]) * a));
      char ch[2] = { name[i], 0 };
      spr.drawString(ch, x, 126 + (int)((1 - a) * 6));
    }
    x += adv[i];
  }
  float ta = easeOut((t - 1.1f) / 0.4f);
  if(ta > 0)
  {
    float bg[3]; background(150, bg);
    uint16_t c = rgb(bg[0] + (0x8A - bg[0]) * ta, bg[1] + (0x93 - bg[1]) * ta, bg[2] + (0xB5 - bg[2]) * ta);
    uiText("OPEN SOURCE RADIO FIRMWARE", 160, 150, F_CAP, c, MC_DATUM);
    uint16_t v = rgb(bg[0] + (0x4A - bg[0]) * ta, bg[1] + (0x52 - bg[1]) * ta, bg[2] + (0x70 - bg[2]) * ta);
    uiText("v" NOVA_VERSION, 314, 162, F_CAP, v, MR_DATUM);
  }
  uiPush();
}

//
// Run it on the other core while setup() starts the radio
//
static volatile bool runDone = false, stopReq = false, skipReq = false;
static uint32_t startMs = 0;

#ifndef NOVA_SIM
static TaskHandle_t task = nullptr;
static void splashTask(void *)
{
  while(true)
  {
    float t = (millis() - startMs) / 1000.0f;
    if(skipReq || (stopReq && t >= SPLASH_LEN)) break;
    splashFrame(t > SPLASH_LEN + 0.6f ? SPLASH_LEN + 0.6f : t);
    vTaskDelay(1);
  }
  runDone = true;
  vTaskDelete(nullptr);
}
#endif

void splashStart()
{
  runDone = false; stopReq = false; skipReq = false;
  buildBackgrounds();
  splashFrame(0);                 // first frame before the backlight comes on
  startMs = millis();
  if(!cfg.bootAnim) { splashFrame(SPLASH_LEN); runDone = true; return; }
#ifndef NOVA_SIM
  cpuBoost(true);                 // smooth frames; back to 80 MHz at the end
  if(xTaskCreatePinnedToCore(splashTask, "splash", 6144, nullptr, 1, &task, 0) != pdPASS) { splashFrame(SPLASH_LEN); runDone = true; }
#else
  runDone = true;
#endif
}

#ifndef NOVA_SIM
// Waits for the animation to end (a click skips it). Call when the radio is ready.
void splashFinish()
{
  stopReq = true;
  while(!runDone)
  {
    if(digitalRead(ENCODER_PUSH) == LOW) skipReq = true;
    delay(5);
  }
  while(digitalRead(ENCODER_PUSH) == LOW) delay(5);     // do not turn the skip into a click
  cpuBoost(false);
}

// Stop at once (a fatal error needs the screen)
void splashAbort()
{
  skipReq = true; stopReq = true;
  while(!runDone) delay(5);
  cpuBoost(false);
}
#else
void splashFinish() {}
void splashAbort() {}
#endif

#ifdef NOVA_SIM
void splashRender(float t) { splashFrame(t); }
#endif
