//
// NOVA - world band receiver firmware for ESP32-S3 + SI4732 (ATS Mini hardware)
//
// One knob, one button:
//   turn          main action of the screen (change station, tune, move)
//   push + turn   secondary action (filter, fast tuning)
//   click         context action (volume, controls, select)
//   hold          menu / back
//   hold 2 s      power
//
#include "Nova.h"
#include "Ui.h"
#include "Rotary.h"
#include "Button.h"
#include <driver/rtc_io.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

LGFX tft;
LGFX_Sprite spr(&tft);
void uiFontsInit();

static Rotary encoder = Rotary(ENCODER_PIN_B, ENCODER_PIN_A, false);
static ButtonTracker button;
static volatile int16_t encCount = 0;
static volatile int16_t encFast = 0;

//
// Encoder with acceleration
//
static int16_t accelerate(int8_t dir)
{
  static const uint32_t thresholds[] = { 350, 60, 45, 35, 25 };
  static const uint16_t factors[]    = { 1, 2, 4, 8, 16 };
  static uint32_t lastTime = 0, speed = 350;
  static uint16_t factor = 1;
  static int8_t lastDir = 0;
  uint32_t now = millis();
  speed = ((now - lastTime) * 7 + speed * 3) / 10;
  if(speed > thresholds[0] || lastDir != dir) { speed = thresholds[0]; factor = 1; }
  else
    for(int i = 4; i >= 0; i--)
      if(speed <= thresholds[i] && factor < factors[i]) { factor = factors[i]; break; }
  lastTime = now;
  lastDir = dir;
  return dir * factor;
}

static void IRAM_ATTR onEncoder()
{
  uint8_t st = encoder.process();
  if(!st) return;
  int8_t d = st == DIR_CW ? 1 : -1;
  if(abs(encCount) < 8)
  {
    encCount += d;
    encFast += accelerate(d);
  }
}

static void takeCounts(int16_t &n, int16_t &fast)
{
  noInterrupts();
  n = encCount; fast = encFast;
  encCount = 0;
  encFast = 0;
  interrupts();
}

// Called by long blocking operations (scan, seek): any input stops them
bool inputAbort()
{
  if(usbScanPoll()) return true;    // "SCAN STOP" from the web editor
  int16_t n, f;
  takeCounts(n, f);
  if(n) return true;
  if(digitalRead(ENCODER_PUSH) == LOW)
  {
    while(digitalRead(ENCODER_PUSH) == LOW) delay(10);
    button.reset();
    return true;
  }
  return false;
}

static void fatal(const char *title, const char *line)
{
  splashAbort();
  backlight(255);
  uiMessage(title, line);
  while(true) delay(1000);
}

void setup()
{
  // Release the pins held low during power-off
  gpio_hold_dis((gpio_num_t)PIN_LCD_BL);
  gpio_hold_dis((gpio_num_t)PIN_AMP_EN);
  gpio_hold_dis((gpio_num_t)PIN_POWER_ON);
  gpio_deep_sleep_hold_dis();
  rtc_gpio_deinit((gpio_num_t)ENCODER_PUSH);

  pinMode(ENCODER_PUSH, INPUT_PULLUP);
  pinMode(ENCODER_PIN_A, INPUT_PULLUP);
  pinMode(ENCODER_PIN_B, INPUT_PULLUP);

  ledcAttach(PIN_LCD_BL, 16000, 8);
  ledcWrite(PIN_LCD_BL, 0);

  tft.init();
  tft.setRotation(3);
  spr.setPsram(true);
  spr.setColorDepth(16);
  spr.createSprite(SCREEN_W, SCREEN_H);

  usbInit();

  // Hold the knob while switching on (not when waking up with the knob): factory reset
  bool wokeByKnob = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
  if(wokeByKnob) while(digitalRead(ENCODER_PUSH) == LOW) delay(10);
  bool reset = !wokeByKnob && digitalRead(ENCODER_PUSH) == LOW;
  if(reset) storeErase();
  configLoad();

  uiFontsInit();
  splashStart();
  backlight(cfg.brightness);

  if(!psramSize()) fatal("Memory not found", "Use the other firmware file (QSPI/OSPI)");
  if(!radioInit()) fatal("Radio chip not found", "Check the hardware");

  stationsInit();
  scheduleInit();
  ghostInit();
  wifiInit();
  toolsInit();
  encoder.setHalfStep(cfg.encoderHalf);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), onEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), onEncoder, CHANGE);
  sleepTimerRestart();
  screenWake();

  splashFinish();
  if(reset)
  {
    uiMessage("Factory reset", "All settings were erased");
    while(digitalRead(ENCODER_PUSH) == LOW) delay(10);
    delay(800);
  }

  uiInit();
  bool scan = cfg.scanBoot == 0 || (cfg.scanBoot == 1 && !stationCount);
  if(scan) uiStartScan();
  else uiGoHome();
  uiDraw();
}

void loop()
{
  static bool turnedWhilePressed = false;
  static bool longFired = false;

  Input in;
  memset(&in, 0, sizeof(in));
  takeCounts(in.turn, in.turnFast);
  ButtonTracker::State b = button.update(digitalRead(ENCODER_PUSH) == LOW);

  if(in.turn && b.isPressed) { turnedWhilePressed = true; in.pushTurn = true; }
  if(!turnedWhilePressed)
  {
    in.click = b.wasClicked;
    in.hold = b.wasShortPressed;
    if(b.isLongPressed && !longFired) { in.longHold = true; longFired = true; }
  }
  if(!b.isPressed) { turnedWhilePressed = false; longFired = false; }

  uiInput(in);
  standbyTick();
  if(ghostTick()) ghostStepped();
  if(noiseTick()) toolStepped();
  if(stormTick()) toolStepped();

  if(radioPoll() && (uiScreen() == SCR_STATIONS || uiScreen() == SCR_TUNE)) uiRequestDraw();
  stationsTick();
  configTick();
  systemTick();
  usbTick();

  if(uiTick()) uiDraw();
  delay(5);
}
