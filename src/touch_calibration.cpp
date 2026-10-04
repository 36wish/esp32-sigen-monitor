#include "touch_calibration.h"

#include <esp32_smartdisplay.h>
#include "settings.h"
#include "storage.h"
#include "theme.h"
#include "widgets.h"

// 3-point calibration: a cross is shown at each of the first CAL_POINTS
// targets, the raw XPT2046 point is averaged over each press, and smartdisplay
// fits the affine correction from them. One more cross checks the fit before
// it's saved to NVS; a fit that misses by more than CAL_VERIFY_TOL starts over.
//
// The correction acts on the portrait point before LVGL rotates it, and LVGL's
// 90-degree rotation maps portrait (px, py) to landscape (479 - py, px).
static const int        CAL_POINTS = 3;  // smartdisplay fits from exactly 3
static const lv_point_t CAL_TARGETS[CAL_POINTS + 1] = { {48, 40}, {432, 160}, {240, 280},
                                                        {380, 250} };  // last: the check
static const int      CAL_VERIFY_TOL  = 12;     // px
static const uint32_t CAL_TIMEOUT_MS  = 30000;  // no touch for this long: give up, keep the old one
static const int      CAL_SKIP        = 3;      // samples dropped at each end of a press (still settling)
static const int      CAL_MIN_KEEP    = 3;      // samples needed after trimming (~0.3s press at 33ms reads)
static const int      CAL_MAX_SAMPLES = 64;
static const int      SCREEN_W        = 480;
static const int      CROSS_SIZE      = 41;
static const int      CROSS_MID       = CROSS_SIZE / 2;

static lv_indev_read_cb_t touchReadCb;    // smartdisplay's: driver read + touch_calibration_data
static touch_calibration_data_t calPrev;  // what was in force before calibrating
static int        calStep = -1;           // -1 idle, then the index of the cross on screen
static bool       calArmed;               // false until the press that started it is released
static lv_point_t calSamples[CAL_MAX_SAMPLES];
static int        calCount;
static lv_point_t calTap;                 // averaged point of the last press
static bool       calTapReady;
static uint32_t   calLastTouch;
static lv_point_t calRaw[CAL_POINTS];
static lv_obj_t  *calLayer, *calCross, *calText;
static uint32_t   clockHeldSince;

// Wraps smartdisplay's read callback. While calibrating it collects the points
// and tells LVGL the screen isn't touched, so nothing underneath reacts.
static void touchRead(lv_indev_t *indev, lv_indev_data_t *data) {
  touchReadCb(indev, data);
  if (calStep < 0) return;
  bool down = data->state == LV_INDEV_STATE_PRESSED;
  data->state = LV_INDEV_STATE_RELEASED;
  if (down) {
    calLastTouch = millis();
    if (calArmed && calCount < CAL_MAX_SAMPLES) calSamples[calCount++] = data->point;
    return;
  }
  calArmed = true;
  if (calCount == 0) return;
  int keep = calCount - 2 * CAL_SKIP;
  if (keep >= CAL_MIN_KEEP) {
    int32_t sx = 0, sy = 0;
    for (int i = CAL_SKIP; i < CAL_SKIP + keep; i++) {
      sx += calSamples[i].x;
      sy += calSamples[i].y;
    }
    calTap = { sx / keep, sy / keep };
    calTapReady = true;
  }
  calCount = 0;
}

void touchCalibrationBegin() {
  touch_calibration_data = TOUCH_CAL_FALLBACK;  // until a saved one is loaded below
  lv_indev_t *touch = lv_indev_get_next(nullptr);
  touchReadCb = lv_indev_get_read_cb(touch);
  lv_indev_set_read_cb(touch, touchRead);
  touch_calibration_data_t savedCal;
  if (prefs.getBytes(NVS_TOUCH_CAL, &savedCal, sizeof(savedCal)) == sizeof(savedCal) && savedCal.valid) {
    touch_calibration_data = savedCal;
    Serial.println("Using saved touch calibration");
  }
}

static void showCalTarget(const char *note = "") {
  const lv_point_t &t = CAL_TARGETS[calStep];
  lv_obj_set_pos(calCross, t.x - CROSS_MID, t.y - CROSS_MID);
  char buf[96];
  if (calStep < CAL_POINTS)
    snprintf(buf, sizeof(buf), "%sPress and hold the centre\nof the cross  (%d/%d)", note, calStep + 1, CAL_POINTS);
  else
    snprintf(buf, sizeof(buf), "%sPress the cross once more\nto check", note);
  lv_label_set_text(calText, buf);
}

void startTouchCalibration() {
  if (calStep >= 0) return;
  Serial.println("Touch calibration started");
  calPrev = touch_calibration_data;
  touch_calibration_data.valid = false;  // collect raw points
  calStep = 0;
  calArmed = false;
  calCount = 0;
  calTapReady = false;
  calLastTouch = millis();
  lv_obj_remove_flag(calLayer, LV_OBJ_FLAG_HIDDEN);
  showCalTarget();
}

static void endTouchCalibration(bool keep) {
  if (keep) {
    prefs.putBytes(NVS_TOUCH_CAL, &touch_calibration_data, sizeof(touch_calibration_data));
    Serial.printf("Touch calibration saved: x = %.4f*rx %+.4f*ry %+.1f, y = %.4f*rx %+.4f*ry %+.1f\n",
                  touch_calibration_data.alphaX, touch_calibration_data.betaX, touch_calibration_data.deltaX,
                  touch_calibration_data.alphaY, touch_calibration_data.betaY, touch_calibration_data.deltaY);
  } else {
    touch_calibration_data = calPrev;
    Serial.println("Touch calibration abandoned, previous calibration kept");
  }
  calStep = -1;
  lv_obj_add_flag(calLayer, LV_OBJ_FLAG_HIDDEN);
}

// smartdisplay's fit divides by this (twice the area of the triangle the raw
// points make), so it's 0 when the presses lie on one line, e.g. two landed on
// the same raw point. Checked in integer maths before fitting: -Ofast compiles
// isfinite() to true, so a non-finite fit can't be caught afterwards.
static int32_t rawPointsArea2() {
  const lv_point_t &a = calRaw[0], &b = calRaw[1], &c = calRaw[2];
  return (a.x - c.x) * (b.y - c.y) - (b.x - c.x) * (a.y - c.y);
}

// A press on one of the fit crosses; after the last one, fits the correction.
static void addCalPoint(lv_point_t raw) {
  calRaw[calStep] = raw;
  Serial.printf("Cal point %d: target (%d,%d) raw (%d,%d)\n", calStep + 1,
                (int)CAL_TARGETS[calStep].x, (int)CAL_TARGETS[calStep].y, (int)raw.x, (int)raw.y);
  if (++calStep == CAL_POINTS) {
    if (rawPointsArea2() == 0) {
      calStep = 0;
      showCalTarget("Those presses didn't register\nproperly. Again:\n");
      return;
    }
    lv_point_t screen[CAL_POINTS];  // the targets in portrait, as the raw points are
    for (int i = 0; i < CAL_POINTS; i++) screen[i] = { CAL_TARGETS[i].y, SCREEN_W - 1 - CAL_TARGETS[i].x };
    touch_calibration_data = smartdisplay_compute_touch_calibration(screen, calRaw);
  }
  showCalTarget();
}

// The press on the check cross, which went through the new fit: keep the fit
// if it landed close enough, otherwise start over.
static void checkCalFit(lv_point_t tap) {
  const lv_point_t &t = CAL_TARGETS[CAL_POINTS];
  int lx = SCREEN_W - 1 - tap.y, ly = tap.x;  // rotate to landscape
  int err = max(abs(lx - t.x), abs(ly - t.y));
  Serial.printf("Cal check: target (%d,%d) got (%d,%d), off by %d px\n", (int)t.x, (int)t.y, lx, ly, err);
  if (err <= CAL_VERIFY_TOL) {
    endTouchCalibration(true);
    return;
  }
  touch_calibration_data.valid = false;
  calStep = 0;
  char note[48];
  snprintf(note, sizeof(note), "That was %d px off. Again:\n", err);
  showCalTarget(note);
}

void updateTouchCalibration() {
  if (calStep < 0) return;
  if (millis() - calLastTouch > CAL_TIMEOUT_MS) {
    endTouchCalibration(false);
    return;
  }
  if (!calTapReady) return;
  calTapReady = false;
  if (calStep < CAL_POINTS) addCalPoint(calTap);
  else                      checkCalFit(calTap);
}

static void onClockHold(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) clockHeldSince = millis();
  else if (code == LV_EVENT_PRESSING && clockHeldSince && millis() - clockHeldSince >= CAL_HOLD_MS) {
    clockHeldSince = 0;
    startTouchCalibration();
  }
}

void buildCalibrationUi() {
  // invisible hold target over the clock / IP / wifi area
  lv_obj_t *hold = makeBox(lv_screen_active());
  lv_obj_add_flag(hold, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(hold, 280, 0);
  lv_obj_set_size(hold, SCREEN_W - 280, 24);
  lv_obj_add_event_cb(hold, onClockHold, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(hold, onClockHold, LV_EVENT_PRESSING, nullptr);

  // on the top layer so it also covers the connecting overlay
  calLayer = makeFilledBox(lv_layer_top(), COL_BG, 0);
  lv_obj_set_size(calLayer, LV_PCT(100), LV_PCT(100));
  calText = makeLabel(calLayer, &lv_font_montserrat_16, COL_TEXT, "");
  lv_obj_set_style_text_align(calText, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(calText, LV_ALIGN_CENTER, 0, -20);

  calCross = makeBox(calLayer);
  lv_obj_set_size(calCross, CROSS_SIZE, CROSS_SIZE);
  lv_obj_t *h = makeFilledBox(calCross, COL_TEXT, 0);
  lv_obj_set_pos(h, 0, CROSS_MID);
  lv_obj_set_size(h, CROSS_SIZE, 1);
  lv_obj_t *v = makeFilledBox(calCross, COL_TEXT, 0);
  lv_obj_set_pos(v, CROSS_MID, 0);
  lv_obj_set_size(v, 1, CROSS_SIZE);
  lv_obj_t *ring = makeBox(calCross);
  lv_obj_set_pos(ring, 12, 12);
  lv_obj_set_size(ring, 17, 17);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(ring, 2, 0);
  lv_obj_set_style_border_color(ring, lv_color_hex(COL_FREE), 0);

  lv_obj_add_flag(calLayer, LV_OBJ_FLAG_HIDDEN);
}
