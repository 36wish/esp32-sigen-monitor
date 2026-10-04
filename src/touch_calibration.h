#pragma once

// On-screen 3-point touch calibration. Hold the clock for CAL_HOLD_MS, or send
// "cal" over serial, to start it. The result is saved in NVS and loaded at boot.

// Hooks smartdisplay's touch read callback and loads the saved calibration
// (TOUCH_CAL_FALLBACK until one has been saved). After displayBegin() and
// prefs.begin().
void touchCalibrationBegin();

// The hold target over the clock, and the (hidden) calibration screen. After
// buildUi(), so the hold target sits on top of the clock.
void buildCalibrationUi();

void startTouchCalibration();

// Call every loop.
void updateTouchCalibration();
