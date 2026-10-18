#include "diagnostics.h"

#include <WiFi.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <time.h>
#include "storage.h"

static esp_reset_reason_t bootResetReason;
// Set from the WiFi event task. Our own radio restarts count too (reason 8,
// "left"); the interesting ones are e.g. 200 beacon timeout, 201 no AP found.
static volatile uint32_t wifiDrops = 0;
static volatile uint8_t  lastDropReason = 0;  // wifi_err_reason_t

static const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "power on";
    case ESP_RST_EXT:       return "reset pin";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "crash";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "unknown";
  }
}

static const char *wifiStatusName(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:     return "idle";
    case WL_NO_SSID_AVAIL:   return "no SSID";
    case WL_SCAN_COMPLETED:  return "scan done";
    case WL_CONNECTED:       return "connected";
    case WL_CONNECT_FAILED:  return "connect failed";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED:    return "disconnected";
    default:                 return "off";
  }
}

static void onWifiDisconnected(arduino_event_id_t event, arduino_event_info_t info) {
  wifiDrops++;
  lastDropReason = info.wifi_sta_disconnected.reason;
  Serial.printf("WiFi disconnected, reason %u\n", lastDropReason);
}

static String statusLine(const String &event) {
  char when[24] = "time not set";
  time_t now = time(nullptr);
  if (now > 1600000000) {  // set by NTP
    tm local;
    localtime_r(&now, &local);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
  }
  char line[256];
  snprintf(line, sizeof(line),
           " | %s, up %lus | WiFi %s, IP %s, RSSI %d, %lu drops (last reason %u)"
           " | heap %u free, %u min, %u largest",
           when, (unsigned long)(esp_timer_get_time() / 1000000), wifiStatusName(WiFi.status()),
           WiFi.localIP().toString().c_str(), WiFi.RSSI(), (unsigned long)wifiDrops, lastDropReason,
           (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  return event + line;
}

static void printLastRestart() {
  uint32_t count = prefs.getUInt(NVS_RESTARTS, 0);
  if (count == 0) {
    Serial.println("No self-restarts recorded");
    return;
  }
  Serial.printf("Self-restarts: %lu, the last one: %s\n", (unsigned long)count,
                prefs.getString(NVS_LAST_RESTART, "").c_str());
}

void diagnosticsBegin() {
  bootResetReason = esp_reset_reason();
  Serial.printf("Reset reason: %s\n", resetReasonName(bootResetReason));
  printLastRestart();
  WiFi.onEvent(onWifiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

void logStatus(const String &event) {
  Serial.println(statusLine(event));
}

void printDiagnostics(const String &event) {
  logStatus(event);
  Serial.printf("Reset reason at boot: %s\n", resetReasonName(bootResetReason));
  printLastRestart();
}

void restartWithRecord(const String &why) {
  String line = statusLine(why);
  Serial.println("Restarting: " + line);
  prefs.putString(NVS_LAST_RESTART, line);
  prefs.putUInt(NVS_RESTARTS, prefs.getUInt(NVS_RESTARTS, 0) + 1);
  Serial.flush();
  ESP.restart();
}
