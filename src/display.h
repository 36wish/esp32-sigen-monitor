#pragma once

#include <stdint.h>

// LCD, touch and LVGL bring-up, the LVGL pump, and ambient-light backlight
// control (dimming, and switching off in a dark room).

// smartdisplay_init(), landscape rotation, auto-brightness.
void displayBegin();

// Runs LVGL: tick, timers, input, redraws. Call often, including from inside
// long network operations, so touch and redraws stay live.
void pumpLvgl();

// pumpLvgl() plus a 1 ms yield, for busy-wait loops.
void waitTick();

// Waits `ms` milliseconds with LVGL still running.
void waitMs(uint32_t ms);
