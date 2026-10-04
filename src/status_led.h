#pragma once

#include "plant.h"

// RGB status LED (active-low, on/off only). Only driven when STATUS_LED is
// set in settings.h; otherwise it stays unlit (smartdisplay_init() turns it off).
//   blue blink   connecting to WiFi / WiFi lost
//   amber blink  looking for / connecting to the Sigenergy plant
//   red blink    plant not found or read failed (waiting to retry)
//   solid        connected: power-flow status (see setPowerLed)
//                  red = importing from grid, blue = battery discharging,
//                  green = battery charging, yellow = solar only, off = idle
enum LedMode { LED_WIFI, LED_SEARCH, LED_ERROR, LED_OK };

void setLedMode(LedMode mode);
void setPowerLed(const PlantReading &r);

// Called from pumpLvgl(), so it keeps blinking through blocking network calls.
void ledTick();
