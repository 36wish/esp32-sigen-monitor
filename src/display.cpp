#include "display.h"

#include <esp32_smartdisplay.h>
#include <math.h>
#include "settings.h"
#include "status_led.h"

static_assert(CDS_BRIGHT_RAW < CDS_DARK_RAW, "CDS_BRIGHT_RAW must be below CDS_DARK_RAW");
static_assert(CDS_ON_RAW < CDS_OFF_RAW, "CDS_ON_RAW must be below CDS_OFF_RAW, or the screen flickers");

// ── LVGL pump ────────────────────────────────────────────────────────────
static uint32_t lvLastTick = 0;

void pumpLvgl() {
  uint32_t now = millis();
  lv_tick_inc(now - lvLastTick);
  lvLastTick = now;
  lv_timer_handler();
  ledTick();
}

void waitTick() {
  pumpLvgl();
  delay(1);
}

void waitMs(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) waitTick();
}

// ── ambient-light auto brightness (CdS on GPIO34, ADC1 so it works with WiFi) ──
// Thresholds are in settings.h.
static const uint32_t BL_INTERVAL_MS = 100;

// Screen off in a totally dark room. The ESP keeps running and polling, so the
// readings are current the moment it wakes.
static bool     screenOff = false;
static lv_obj_t *wakeGuard;  // full-screen catcher so the waking tap doesn't press a button

static void wakeGuardEvent(lv_event_t *e) {
  lv_obj_add_flag(wakeGuard, LV_OBJ_FLAG_HIDDEN);  // released: input goes to the UI again
}

static void setScreenOff(bool off) {
  if (off == screenOff) return;
  screenOff = off;
  Serial.printf("Screen %s\n", off ? "off (dark)" : "on");
  if (!wakeGuard) {
    wakeGuard = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(wakeGuard);
    lv_obj_set_size(wakeGuard, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(wakeGuard, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wakeGuard, wakeGuardEvent, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(wakeGuard, wakeGuardEvent, LV_EVENT_PRESS_LOST, NULL);
  }
  // Shown while off so the tap that wakes the screen lands on it. Woken by
  // light instead, there's no tap to swallow.
  if (off) lv_obj_remove_flag(wakeGuard, LV_OBJ_FLAG_HIDDEN);
}

bool screenIsOff() {
  return screenOff;
}

// smartdisplay's brightness callback, every BL_INTERVAL_MS. Returns the
// backlight level, 0 while the screen is off.
static float adaptiveBrightness() {
  static float avg = -1;
  static uint32_t lastLog = 0;
  float raw = analogRead(CDS);
  avg = avg < 0 ? raw : avg + (raw - avg) * 0.05f;  // ~2s smoothing at 100ms
  if (millis() - lastLog >= 10000) {
    lastLog = millis();
    Serial.printf("CdS raw avg: %.0f\n", avg);
  }
  bool touched = lv_display_get_inactive_time(NULL) < SCREEN_OFF_IDLE_MS;
  if (screenOff) {
    if (touched) setScreenOff(false);  // guard stays up until the finger lifts
    else if (avg < CDS_ON_RAW) {
      setScreenOff(false);
      lv_obj_add_flag(wakeGuard, LV_OBJ_FLAG_HIDDEN);
    }
  } else if (!touched && avg >= CDS_OFF_RAW) {
    setScreenOff(true);
  }
  if (screenOff) return 0.0f;

  float t = (avg - CDS_BRIGHT_RAW) / (float)(CDS_DARK_RAW - CDS_BRIGHT_RAW);
  t = fminf(fmaxf(t, 0.0f), 1.0f);
  return BL_MAX - t * (BL_MAX - BL_MIN);
}

void displayBegin() {
  smartdisplay_init();
  lv_display_set_rotation(lv_display_get_default(), LV_DISPLAY_ROTATION_90);
  lvLastTick = millis();
  smartdisplay_lcd_set_brightness_cb(adaptiveBrightness, BL_INTERVAL_MS);
}
