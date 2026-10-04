#include "status_led.h"

#include <esp32_smartdisplay.h>
#include "settings.h"

static LedMode ledMode = LED_WIFI;
static bool powerR, powerG, powerB;

void setLedMode(LedMode mode) {
  ledMode = mode;
}

void setPowerLed(const PlantReading &r) {
  powerR = powerG = powerB = false;
  if (r.gridKw > IDLE_KW)       powerR = true;                 // importing
  else if (r.essKw < -IDLE_KW)  powerB = true;                 // discharging
  else if (r.essKw > IDLE_KW)   powerG = true;                 // charging
  else if (r.pvKw > IDLE_KW)    powerR = powerG = true;        // solar only
}

void ledTick() {
  if (!STATUS_LED) return;
  bool blink = (millis() / 400) % 2;
  switch (ledMode) {
    case LED_WIFI:   smartdisplay_led_set_rgb(false, false, blink); break;
    case LED_SEARCH: smartdisplay_led_set_rgb(blink, blink, false); break;
    case LED_ERROR:  smartdisplay_led_set_rgb(blink, false, false); break;
    case LED_OK:     smartdisplay_led_set_rgb(powerR, powerG, powerB); break;
  }
}
