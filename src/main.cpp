/**
 * ESP32 Sigenergy monitor - LVGL touchscreen dashboard.
 *
 * Board: Sunton ESP32-3248S035R (3.5" 480x320 landscape, ST7796 SPI LCD,
 * resistive XPT2046 touch), driven via the rzeldent/esp32_smartdisplay
 * board+driver package.
 *
 * First build: copy include/secrets.h.example to include/secrets.h and
 * fill in your WiFi SSID/password. Other settings are in include/settings.h.
 *
 * This file boots the board and runs the connection state machine, which also
 * gets the link back by itself (see checkLink()). The rest:
 *   discovery.cpp          finds the plant on the LAN (cached IP, then a /24 scan)
 *   plant.cpp              the connected plant: live poll, Charge and Free hour requests
 *   modbus_tcp.cpp         minimal Modbus TCP client
 *   sigenergy.h            Sigenergy register map
 *   dashboard.cpp          the screen: layout, live values, Charge and Free hour modes
 *   widgets.cpp            LVGL building blocks and vector icons
 *   theme.h                palette and custom fonts
 *   display.cpp            LVGL pump, auto-brightness, screen off in the dark
 *   touch_calibration.cpp  on-screen touch calibration
 *   status_led.cpp         RGB status LED (off unless STATUS_LED is set)
 *   diagnostics.cpp        connection diagnostics on the serial port, self-restart record
 *   storage.h              NVS keys
 */

#include <Arduino.h>
#include <WiFi.h>
#include "dashboard.h"
#include "diagnostics.h"
#include "discovery.h"
#include "display.h"
#include "plant.h"
#include "secrets.h"  // WIFI_SSID, WIFI_PASSWORD
#include "settings.h"
#include "sigenergy.h"
#include "status_led.h"
#include "storage.h"
#include "touch_calibration.h"

Preferences prefs;

// ── app state machine (non-blocking so LVGL/touch stay responsive) ─────────
// ST_WIFI → ST_LOAD_CACHE → ST_SCAN → ST_CONNECT → ST_POLL, with ST_WAIT_RETRY
// between failed attempts. Long operations keep LVGL running themselves
// (pumpLvgl()/waitTick()), so don't add blocking delays.
enum AppState { ST_WIFI, ST_LOAD_CACHE, ST_SCAN, ST_WAIT_RETRY, ST_CONNECT, ST_POLL };
static AppState  state = ST_WIFI;
static AppState  retryState = ST_SCAN;  // where ST_WAIT_RETRY goes when it expires
static uint32_t  nextActionAt = 0;
static int       connectFailures = 0;
static IPAddress plantIp;

static const uint32_t SCAN_RETRY_MS    = 30000;
static const uint32_t CONNECT_RETRY_MS = 1000;

// Getting the link back without a power cycle (checkLink()).
static const uint32_t WIFI_LOST_MS        = 10000;   // WiFi down this long: restart it ourselves...
static const uint32_t WIFI_RETRY_MS       = 30000;   // ...and again this often until it connects
static const uint32_t NO_REPLY_WIFI_MS    = 120000;  // plant silent this long: restart WiFi even if it says connected
static const uint32_t NO_REPLY_RESTART_MS = 600000;  // ...and this long: restart the ESP (not during a free hour)
static const uint32_t STATUS_LOG_MS       = 600000;  // status line on the serial port

static uint32_t wifiBegunAt = 0;   // last WiFi.begin()
static bool     wifiDown = false;  // WiFi lost outside ST_WIFI...
static uint32_t wifiDownAt = 0;    // ...since this
static uint32_t lastReplyAt = 0;   // the plant last answered a request
static bool     linkDown = false;  // it hasn't since the link dropped

static bool timeReached(uint32_t t) {
  return (int32_t)(millis() - t) >= 0;
}

static const char *stateName(AppState s) {
  switch (s) {
    case ST_WIFI:       return "wifi";
    case ST_LOAD_CACHE: return "load cache";
    case ST_SCAN:       return "scan";
    case ST_WAIT_RETRY: return "wait retry";
    case ST_CONNECT:    return "connect";
    case ST_POLL:       return "poll";
  }
  return "?";
}

// `what`, with the state and how long the plant has been silent, for logStatus().
static String linkEvent(const char *what) {
  return String(what) + " [" + stateName(state) + ", last reply " + String((millis() - lastReplyAt) / 1000) +
         "s ago]";
}

static void retryAfter(uint32_t ms, AppState next) {
  nextActionAt = millis() + ms;
  retryState = next;
  state = ST_WAIT_RETRY;
}

// LED mode follows the state machine; a dropped WiFi link overrides it.
static LedMode ledModeFor(AppState s) {
  if (s != ST_WIFI && WiFi.status() != WL_CONNECTED) return LED_WIFI;
  switch (s) {
    case ST_WIFI:       return LED_WIFI;
    case ST_LOAD_CACHE:
    case ST_SCAN:
    case ST_CONNECT:    return LED_SEARCH;
    case ST_WAIT_RETRY: return LED_ERROR;
    case ST_POLL:       return LED_OK;
  }
  return LED_WIFI;
}

// "cal" on the serial port starts calibration (a fallback for when touch is too
// far off to hold the clock).
static void pollSerial() {
  static char line[8];
  static int len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      line[len] = 0;
      if (strcmp(line, "cal") == 0) startTouchCalibration();
      if (strcmp(line, "diag") == 0) printDiagnostics(linkEvent("Diagnostics"));
      len = 0;
    } else if (len < (int)sizeof(line) - 1) {
      line[len++] = c;
    }
  }
}

static void beginWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // modem sleep adds 100ms+ latency, which made LAN probes time out
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiBegunAt = millis();
}

// Turns the radio off and on again and reconnects, as a power cycle would.
// That also clears a link that still says connected but passes nothing, where
// WiFi.begin() on its own does nothing. The arduino-esp32 2.0.x auto-reconnect
// doesn't retry after every kind of disconnect, so this is what brings WiFi
// back.
static void restartWifi(const char *why) {
  logStatus(linkEvent(why));
  plantDisconnect();
  WiFi.disconnect(true);  // radio off
  beginWifi();
  wifiDown = false;
  linkDown = true;
  state = ST_WIFI;
  showStatus("Reconnecting to WiFi\n\"" WIFI_SSID "\"...");
}

// Gets the link back without a power cycle: restarts WiFi when it has been
// down a while, or when the plant has stopped answering although WiFi says it's
// connected, and restarts the ESP when the plant has been silent for longer
// still. A restart would end a free hour (plantBegin() hands remote EMS back),
// so it waits until the free hour is over.
static void checkLink() {
  uint32_t now = millis();
  bool connected = WiFi.status() == WL_CONNECTED;
  if (state == ST_WIFI) {
    if (!connected && now - wifiBegunAt >= WIFI_RETRY_MS) restartWifi("WiFi still not connected, restarting it");
  } else if (!connected) {
    if (!wifiDown) {
      wifiDown = true;
      wifiDownAt = now;
      logStatus(linkEvent("WiFi lost"));
    } else if (now - wifiDownAt >= WIFI_LOST_MS) {
      restartWifi("WiFi still lost, restarting it");
    }
  } else {
    wifiDown = false;
    if (now - lastReplyAt >= NO_REPLY_WIFI_MS && now - wifiBegunAt >= NO_REPLY_WIFI_MS)
      restartWifi("Plant silent though WiFi is connected, restarting WiFi");
  }
  if (now - lastReplyAt >= NO_REPLY_RESTART_MS && !freeHourOn())
    restartWithRecord(linkEvent("Plant silent too long, restarting the ESP"));

  static uint32_t lastStatusLog = 0;
  if (now - lastStatusLog >= STATUS_LOG_MS) {
    lastStatusLog = now;
    logStatus(linkEvent("Status"));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  displayBegin();
  prefs.begin(NVS_NAMESPACE, false);
  diagnosticsBegin();
  touchCalibrationBegin();
  plantBegin();
  buildUi();
  buildCalibrationUi();
  showStatus("Connecting to WiFi\n\"" WIFI_SSID "\"...");

  Serial.printf("Connecting to WiFi \"%s\"...\n", WIFI_SSID);
  beginWifi();
  lastReplyAt = millis();  // the no-reply timeouts count from boot
}

void loop() {
  if (STATUS_LED) setLedMode(ledModeFor(state));
  pumpLvgl();

  static uint32_t lastClockCheck = 0;
  if (millis() - lastClockCheck >= 1000) {
    lastClockCheck = millis();
    updateClock();
  }
  updateCountdown();
  updateTouchCalibration();
  pollSerial();
  checkLink();

  switch (state) {
    case ST_WIFI:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("WiFi connected, IP: ");
        Serial.println(WiFi.localIP());
        configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
        state = ST_LOAD_CACHE;
      }
      break;

    case ST_LOAD_CACHE:
      state = findCachedPlant(plantIp) ? ST_CONNECT : ST_SCAN;
      break;

    case ST_SCAN:
      if (scanForPlant(plantIp)) {
        state = ST_CONNECT;
      } else {
        showStatus("No Sigenergy plant found.\nRetrying in " + String(SCAN_RETRY_MS / 1000) + "s...");
        retryAfter(SCAN_RETRY_MS, ST_SCAN);
      }
      break;

    case ST_WAIT_RETRY:
      if (timeReached(nextActionAt)) state = retryState;
      break;

    case ST_CONNECT:
      showStatus("Connecting to Sigenergy plant\n" + plantIp.toString() + " ...");
      if (plantConnect(plantIp)) {
        connectFailures = 0;
        showPlantIp(plantIp);
        nextActionAt = millis();  // poll straight away (0 isn't "now" once millis() passes 2^31)
        state = ST_POLL;
      } else if (++connectFailures < CACHE_ATTEMPTS) {
        // keep trying the known address before paying for a subnet scan
        Serial.printf("Could not reach Sigenergy plant (attempt %d/%d), retrying...\n",
                      connectFailures, CACHE_ATTEMPTS);
        retryAfter(CONNECT_RETRY_MS, ST_CONNECT);
      } else {
        Serial.println("Could not reach Sigenergy plant, rescanning...");
        connectFailures = 0;
        state = ST_SCAN;
      }
      break;

    case ST_POLL:
      if (!timeReached(nextActionAt)) break;
      if (plantTurn()) {
        if (linkDown) logStatus(linkEvent("Plant answering again"));
        linkDown = false;
        lastReplyAt = millis();
        nextActionAt = millis() + POLL_INTERVAL_MS;
      } else {
        // link dropped; queued requests wait for the reconnect
        logStatus(linkEvent("Link to the plant dropped"));
        linkDown = true;
        state = ST_CONNECT;
      }
      break;
  }
}
