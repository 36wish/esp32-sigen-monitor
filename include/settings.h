#pragma once

// User settings. WiFi credentials are in secrets.h.

#include <esp32_smartdisplay.h>  // touch_calibration_data_t

// POSIX TZ string for the on-screen clock (default: New Zealand).
#define TZ_INFO "NZST-12NZDT,M9.5.0,M4.1.0/3"

// Cheaper overnight rate (local time): the hero number is drawn grey from
// OFFPEAK_START_HOUR:00 until OFFPEAK_END_HOUR:00.
static const int OFFPEAK_START_HOUR = 21;
static const int OFFPEAK_END_HOUR   = 7;

// Charge button: the battery reserve (Backup SOC, %) written to the plant when
// it's pressed in and when it's released.
static const uint16_t CHARGE_ON_SOC  = 100;
static const uint16_t CHARGE_OFF_SOC = 25;
// Reread it this often so the button follows changes made elsewhere (mySigen app).
static const uint32_t CHARGE_SYNC_INTERVAL_MS = 60000;

// Free hour: how hard the inverter charges the battery from the grid, as % of
// its rated power (100 = full rate). The house runs off the grid either way.
static const int FREE_HOUR_CHARGE_PCT = 100;

// Full-scale value for the solar / grid / battery bars in the bottom strip.
static const float BAR_MAX_KW = 8.0f;

// Ambient-light auto brightness (CdS on GPIO34). The reading is low in bright
// light and rises as it gets darker. Tune the raw thresholds (see the
// "CdS raw" serial log) to suit the room.
static const float    BL_MIN         = 0.08f;  // never fully black, so the UI stays readable
static const float    BL_MAX         = 1.0f;
static const uint16_t CDS_BRIGHT_RAW = 30;     // at/below this -> BL_MAX
static const uint16_t CDS_DARK_RAW   = 400;    // at/above this -> BL_MIN
// Screen off in a totally dark room: once the reading reaches CDS_OFF_RAW and
// nothing has touched the screen for SCREEN_OFF_IDLE_MS. It comes back when
// the reading drops below CDS_ON_RAW, or on a touch.
static const uint16_t CDS_OFF_RAW        = 800;
static const uint16_t CDS_ON_RAW         = 600;
static const uint32_t SCREEN_OFF_IDLE_MS = 30000;

// Touch calibration: hold the clock this long to start it.
static const uint32_t CAL_HOLD_MS = 5000;
// Used until a calibration has been saved to NVS. The board definition's touch
// mapping comes out mirrored left-to-right in landscape, and the uncalibrated
// XPT2046 readings are 10-20px off on top of that; this corrects both on the
// raw (portrait) point before LVGL rotates it (raw y runs the opposite way to
// landscape x, so mirroring x means flipping y). From an on-screen 3-point
// calibration of one board (3px off at the check point).
static const touch_calibration_data_t TOUCH_CAL_FALLBACK = {
  .valid = true,
  .alphaX = 1.1148f, .betaX = -0.0107f, .deltaX = -19.5f,
  .alphaY = 0.0206f, .betaY = -1.1100f, .deltaY = 502.0f,
};

// RGB status LED (see status_led.h for what it shows). Off: the LED stays unlit.
static const bool STATUS_LED = false;
