/**
 * ESP32 Sigenergy monitor - LVGL touchscreen dashboard.
 *
 * Board: Sunton ESP32-3248S035R (3.5" 480x320 landscape, ST7796 SPI LCD,
 * resistive XPT2046 touch), driven via the rzeldent/esp32_smartdisplay
 * board+driver package.
 *
 * First build: copy include/secrets.h.example to include/secrets.h and
 * fill in your WiFi SSID/password.
 */

#include <Arduino.h>
#include <esp32_smartdisplay.h>
#include <WiFi.h>
#include <Preferences.h>
#include <lwip/sockets.h>
#include <errno.h>
#include <math.h>
#include "secrets.h"

// ── Sigenergy Modbus layout  ─────────────────
static const uint8_t  PLANT_SLAVE_ID   = 247;
static const uint16_t START_ADDRESS    = 30005; // first register we read
static const uint16_t REGISTER_COUNT   = 34;    // covers 30005 .. 30038 inclusive
static const uint16_t MODBUS_PORT      = 502;
static const uint32_t POLL_INTERVAL_MS = 1000;
static const size_t   MAX_MODBUS_BYTES = 128;   // supports up to 64 registers

// Charge button: battery reserve (Backup SOC) on the plant, as in sigen.py
// modes A (on) and B (off).
static const uint16_t BACKUP_SOC_REG = 40046;  // RW U16, %*10 (Sigenergy V2.9)
static const uint16_t CHARGE_ON_SOC  = 100;    // sigen.py mode A
static const uint16_t CHARGE_OFF_SOC = 30;     // sigen.py mode B
// Reread it this often so the button follows changes made elsewhere (mySigen app).
static const uint32_t CHARGE_SYNC_INTERVAL_MS = 60000;

// POSIX TZ string for the on-screen clock; override in secrets.h if needed.
#ifndef TZ_INFO
#define TZ_INFO "NZST-12NZDT,M9.5.0,M4.1.0/3"
#endif

// Cheaper overnight rate (local time): the hero number is drawn grey from
// OFFPEAK_START_HOUR:00 until OFFPEAK_END_HOUR:00.
static const int OFFPEAK_START_HOUR = 21;
static const int OFFPEAK_END_HOUR   = 7;

// Gap between the end of the plant IP and the start of the clock text; the
// reconnect spinner (16px) sits centred in it.
static const int IP_GAP = 30;

// Full-scale value for the solar / grid / battery bars in the bottom strip.
static const float BAR_MAX_KW = 8.0f;

// Battery graphic geometry (top-right). 108px wide still fits "100%".
static const int BATT_X = 360, BATT_Y = 34, BATT_W = 108, BATT_H = 170;

// Source-mix bar geometry.
static const int MIX_X   = 16;
static const int MIX_W   = 286;
static const int MIX_H   = 36;
static const int MIX_GAP = 3;

// Hero: icon tile in the top-left corner, the number under it on a baseline at
// y=152 (font_hero_144: line height 136, baseline 14 up from the bottom).
// Under the free-hour header the tile shifts right to make room for the close button.
static const int HERO_X = 16, HERO_Y = 30;
static const int TILE_SIZE = 32;
static const int TILE_X = 16, TILE_Y = 4;
static const int FREE_TILE_X = 58, FREE_TILE_Y = 6;

// Mode buttons: icon-only, stacked in a column between the hero and the
// battery, together spanning the battery's height.
static const int MODE_BTN_X = 314, MODE_BTN_W = 40, MODE_BTN_H = 79;
static const int CHARGE_BTN_Y = BATT_Y, FREE_BTN_Y = 122;
static const int BTN_DROP   = 3;   // how far a button sinks when pressed in

// Free-hour countdown: "mm:ss" right-aligned in the mix bar's slot, which runs
// on across the (hidden) button column to the battery, with a bar beside it
// that drains to the top of the hour. font_num_44 digits are proportional
// ("1" is 17px, "4" is 30px), so each digit gets its own fixed cell; otherwise
// the text changes width every second and jumps around.
static const int CD_DIGIT_W  = 30;  // widest digit in font_num_44
static const int CD_COLON_W  = 11;
static const int COUNTDOWN_W = 4 * CD_DIGIT_W + CD_COLON_W;
static const int FREE_W      = BATT_X - 14 - MIX_X;
static const int FREE_BAR_W  = FREE_W - COUNTDOWN_W - 16;

// ── palette ──────────────────────────────────────────────────────────────
static const uint32_t COL_BG        = 0x080c16;
static const uint32_t COL_CARD      = 0x111828;
static const uint32_t COL_CARD2     = 0x161f33;
static const uint32_t COL_BORDER    = 0x202c44;
static const uint32_t COL_TRACK     = 0x0c121f;
static const uint32_t COL_MUTED     = 0x8091b2;
static const uint32_t COL_TEXT      = 0xeef3fb;
static const uint32_t COL_ON_FILL   = 0x06281e;  // dark text drawn on the battery fill
static const uint32_t COL_SOLAR     = 0xfbbf24;
static const uint32_t COL_SOLAR_HI  = 0xfde047;
static const uint32_t COL_IMPORT    = 0xf87171;
static const uint32_t COL_EXPORT    = 0x34d399;
static const uint32_t COL_IDLE      = 0x8091b2;
static const uint32_t COL_BATT_HI   = 0x34d399;
static const uint32_t COL_BATT_MID  = 0xfbbf24;
static const uint32_t COL_BATT_LOW  = 0xf87171;
static const uint32_t COL_CHARGE    = 0x34d399;
static const uint32_t COL_DISCHARGE = 0x60a5fa;
static const uint32_t COL_HOME      = 0xa78bfa;
static const uint32_t COL_SHADOW    = 0x03050a;  // raised-button drop shadow
static const uint32_t COL_RESERVE   = 0x818cf8;  // Charge mode (hold the battery at 100%)
static const uint32_t COL_FREE      = 0x22d3ee;  // free power hour
static const uint32_t COL_OFFPEAK   = 0x9ca3b0;  // hero number during the cheap overnight rate

LV_FONT_DECLARE(font_hero_144); // Montserrat SemiBold 144px, "-./0-9" only
LV_FONT_DECLARE(font_num_44);   // Montserrat SemiBold 44px, "%+-./0-9:" only

Preferences prefs;
WiFiClient  modbusClient;
IPAddress   sigenergyIp;
bool        haveIp = false;
bool        haveData = false;  // once true, reconnects keep the last readings on screen

// ── LVGL objects ─────────────────────────────────────────────────────────
static lv_obj_t *timeLabel;
static lv_obj_t *ipLabel;
static lv_obj_t *reconnectIcon;
static lv_obj_t *overlay;
static lv_obj_t *overlayLabel;

static lv_obj_t *houseTile, *heroHouseIcon, *heroGridIcon;
static lv_obj_t *heroRow;
static lv_obj_t *heroValue;
static lv_obj_t *mixBar;
static lv_obj_t *mixSolar, *mixGrid, *mixBatt;

static lv_obj_t *battNub;
static lv_obj_t *battBody;
static lv_obj_t *battFill;
static lv_obj_t *socRow;
static lv_obj_t *socValue;
static lv_obj_t *socPctLabel;

struct StripSection {
  lv_obj_t *tile;
  lv_obj_t *icon;
  lv_obj_t *value;
  lv_obj_t *bar;
  lv_obj_t *altIcon;  // grid section only: house icon shown during free hour
};
static StripSection secSolar, secGrid, secBatt;

// mode buttons (home screen) and the free-hour header/countdown
static lv_obj_t *chargeBtn, *chargeIcon;
static lv_obj_t *freeBtn;
static lv_obj_t *closeBtn, *freeTitle, *freeBar, *freeCountdown;
static lv_obj_t *cdDigits[4];  // m m : s s, inside freeCountdown

// ── modes ────────────────────────────────────────────────────────────────
// Charge: toggle that holds the battery minimum at 100% (storm backup). Stays
//   on until pressed again. Pressed in = plant Backup SOC 100%, released = 30%;
//   the button is set from the plant's Backup SOC on every connect and every 60s.
// Free hour: the power company's free hour. The dashboard swaps grid import into
//   the hero and house load into the strip, and counts down to the top of the
//   hour, then returns home by itself (or via the close button).
static bool     chargeOn = false;
static int      pendingReserve = -1;    // Backup SOC % to write to the plant, -1 = none
static bool     needChargeSync = true;  // read Backup SOC to set the Charge button
static uint32_t lastChargeSyncAt = 0;   // ...and again every CHARGE_SYNC_INTERVAL_MS
static bool     freeHour = false;
static uint32_t freeHourEndMs = 0;
static int      shownSecsLeft = -1;

// last plant reading, so a mode switch can redraw without waiting for a poll
static float lastGridKw, lastSocPct, lastPvKw, lastEssKw;

// ── ambient-light auto brightness (CdS on GPIO34, ADC1 so it works with WiFi) ──
// Reading is low in bright light and rises as it gets darker. Tune the two
// raw thresholds (see the "CdS raw" serial log) to suit the room.
static const float    BL_MIN         = 0.08f;  // never fully black, so the UI stays readable
static const float    BL_MAX         = 1.0f;
static const uint16_t CDS_BRIGHT_RAW = 30;     // at/below this -> BL_MAX
static const uint16_t CDS_DARK_RAW   = 400;    // at/above this -> BL_MIN
static const uint32_t BL_INTERVAL_MS = 100;

static float adaptiveBrightness() {
  static float avg = -1;
  static uint32_t lastLog = 0;
  float raw = analogRead(CDS);
  avg = avg < 0 ? raw : avg + (raw - avg) * 0.05f;  // ~2s smoothing at 100ms
  if (millis() - lastLog >= 10000) {
    lastLog = millis();
    Serial.printf("CdS raw avg: %.0f\n", avg);
  }
  float t = (avg - CDS_BRIGHT_RAW) / (float)(CDS_DARK_RAW - CDS_BRIGHT_RAW);
  t = fminf(fmaxf(t, 0.0f), 1.0f);
  return BL_MAX - t * (BL_MAX - BL_MIN);
}

/* RGB status LED disabled for now (stays unlit; smartdisplay_init() turns it off).
// ── RGB status LED (active-low, on/off only) ────────────────────────────────
//   blue blink   connecting to WiFi / WiFi lost
//   amber blink  looking for / connecting to the Sigenergy plant
//   red blink    plant not found or read failed (waiting to retry)
//   solid        connected: power-flow status (see setPowerLed)
//                  red = importing from grid, blue = battery discharging,
//                  green = battery charging, yellow = solar only, off = idle
enum LedMode { LED_WIFI, LED_SEARCH, LED_ERROR, LED_OK };
static LedMode ledMode = LED_WIFI;
static bool powerR, powerG, powerB;

static void setPowerLed(float gridKw, float pvKw, float essKw) {
  powerR = powerG = powerB = false;
  if (gridKw > 0.05f)       powerR = true;                 // importing
  else if (essKw < -0.05f)  powerB = true;                 // discharging
  else if (essKw > 0.05f)   powerG = true;                 // charging
  else if (pvKw > 0.05f)    powerR = powerG = true;        // solar only
}

static void ledTick() {
  uint32_t now = millis();
  bool blink = (now / 400) % 2;
  switch (ledMode) {
    case LED_WIFI:   smartdisplay_led_set_rgb(false, false, blink); break;
    case LED_SEARCH: smartdisplay_led_set_rgb(blink, blink, false); break;
    case LED_ERROR:  smartdisplay_led_set_rgb(blink, false, false); break;
    case LED_OK:     smartdisplay_led_set_rgb(powerR, powerG, powerB); break;
  }
}
*/

// ── LVGL tick/handler pump (call often so touch + redraws stay live even
//    during the occasional blocking network call) ──────────────────────────
static uint32_t lvLastTick = 0;
static void pumpLvgl() {
  uint32_t now = millis();
  lv_tick_inc(now - lvLastTick);
  lvLastTick = now;
  lv_timer_handler();
  // ledTick();
}
static void waitTick() {
  pumpLvgl();
  delay(1);
}

static int32_t toS32(uint16_t hiWord, uint16_t loWord) {
  uint32_t combined = ((uint32_t)hiWord << 16) | loWord;
  return (int32_t)combined;
}

// ── Modbus TCP (minimal client: MBAP header + read/write registers) ────────
// MB_EXCEPTION means the plant answered but refused the request; MB_FAIL means
// no usable answer (timeout, dropped link, garbled reply).
enum ModbusResult { MB_OK, MB_EXCEPTION, MB_FAIL };

static bool waitForBytes(WiFiClient &client, int n, uint32_t start, uint32_t timeoutMs) {
  while (client.connected() && client.available() < n && millis() - start < timeoutMs) waitTick();
  return client.available() >= n;
}

// Sends one request PDU (function code + 4 bytes: address and quantity/value)
// and reads the response header up to and including the function code.
static ModbusResult modbusRequest(WiFiClient &client, uint8_t slaveId, uint8_t funcCode,
                                  uint16_t addr, uint16_t arg, uint32_t start, uint32_t timeoutMs) {
  uint16_t txId = 1;
  uint8_t req[12] = {
    (uint8_t)(txId >> 8), (uint8_t)(txId & 0xFF),
    0, 0,                              // protocol id
    0, 6,                              // length: unitId + PDU
    slaveId,
    funcCode,
    (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
    (uint8_t)(arg >> 8), (uint8_t)(arg & 0xFF),
  };
  client.write(req, sizeof(req));

  if (!waitForBytes(client, 8, start, timeoutMs)) return MB_FAIL;
  uint8_t hdr[8];
  client.readBytes(hdr, 8);
  if (hdr[0] != req[0] || hdr[1] != req[1]) return MB_FAIL; // stale/mismatched response

  if (hdr[7] == (funcCode | 0x80)) {
    if (waitForBytes(client, 1, start, timeoutMs)) client.read(); // consume exception code
    return MB_EXCEPTION;
  }
  return hdr[7] == funcCode ? MB_OK : MB_FAIL;
}

// FC 0x03 (holding) or 0x04 (input) registers.
static ModbusResult modbusReadRegisters(WiFiClient &client, uint8_t slaveId, uint8_t funcCode,
                                        uint16_t startAddr, uint16_t qty,
                                        uint16_t *outRegs, uint32_t timeoutMs = 2000) {
  if (qty * 2 > MAX_MODBUS_BYTES) return MB_FAIL;

  uint32_t start = millis();
  ModbusResult r = modbusRequest(client, slaveId, funcCode, startAddr, qty, start, timeoutMs);
  if (r != MB_OK) return r;

  if (!waitForBytes(client, 1, start, timeoutMs)) return MB_FAIL;
  uint8_t byteCount = client.read();
  if (byteCount != qty * 2) return MB_FAIL;
  if (!waitForBytes(client, byteCount, start, timeoutMs)) return MB_FAIL;

  uint8_t data[MAX_MODBUS_BYTES];
  client.readBytes(data, byteCount);
  for (uint16_t i = 0; i < qty; i++) {
    outRegs[i] = ((uint16_t)data[i * 2] << 8) | data[i * 2 + 1];
  }
  return MB_OK;
}

static bool modbusReadHoldingRegisters(WiFiClient &client, uint8_t slaveId,
                                        uint16_t startAddr, uint16_t qty,
                                        uint16_t *outRegs, uint32_t timeoutMs = 2000) {
  return modbusReadRegisters(client, slaveId, 0x03, startAddr, qty, outRegs, timeoutMs) == MB_OK;
}

// FC 0x06 write single register; the reply echoes address and value.
static ModbusResult modbusWriteRegister(WiFiClient &client, uint8_t slaveId,
                                        uint16_t addr, uint16_t value, uint32_t timeoutMs = 2000) {
  uint32_t start = millis();
  ModbusResult r = modbusRequest(client, slaveId, 0x06, addr, value, start, timeoutMs);
  if (r != MB_OK) return r;

  if (!waitForBytes(client, 4, start, timeoutMs)) return MB_FAIL;
  uint8_t echo[4];
  client.readBytes(echo, 4);
  bool match = echo[0] == (addr >> 8) && echo[1] == (addr & 0xFF) &&
               echo[2] == (value >> 8) && echo[3] == (value & 0xFF);
  return match ? MB_OK : MB_FAIL;
}

// The spec lists 0x04 for RW registers but holding reads usually work too;
// try 0x03 first and fall back, as sigen.py does.
static ModbusResult readBackupSoc(WiFiClient &client, uint16_t &raw) {
  ModbusResult r = modbusReadRegisters(client, PLANT_SLAVE_ID, 0x03, BACKUP_SOC_REG, 1, &raw);
  if (r == MB_EXCEPTION) r = modbusReadRegisters(client, PLANT_SLAVE_ID, 0x04, BACKUP_SOC_REG, 1, &raw);
  return r;
}

// ── autoscan ─────────────────────────────────────────────────────────────
// The cached address gets several patient attempts before we fall back to a
// scan: right after WiFi associates, the first ARP/TCP exchange is often slow.
static const int      CACHE_ATTEMPTS   = 4;
static const uint32_t CACHE_CONNECT_MS = 2000;
static const uint32_t CACHE_READ_MS    = 2000;

// Subnet sweep: this many non-blocking port-502 connects in flight at once
// (lwIP has 16 sockets in total), all sharing one timeout.
static const int      SCAN_PARALLEL   = 12;
static const uint32_t SCAN_CONNECT_MS = 400;

static bool probeSigenergy(IPAddress ip, uint32_t connectMs, uint32_t readMs) {
  WiFiClient client;
  if (!client.connect(ip, MODBUS_PORT, connectMs)) return false;
  uint16_t regs[2];
  bool ok = modbusReadHoldingRegisters(client, PLANT_SLAVE_ID, START_ADDRESS, 2, regs, readMs);
  client.stop();
  return ok;
}

// Starts a non-blocking connect to port 502 on each host at once and reports
// which accepted. Absent hosts and closed ports cost one shared timeout
// instead of one each, and LVGL keeps running while we wait.
static void findOpenPorts(const IPAddress *hosts, int count, bool *open, uint32_t timeoutMs) {
  int fds[SCAN_PARALLEL];
  int pending = 0;
  for (int i = 0; i < count; i++) {
    open[i] = false;
    fds[i] = socket(AF_INET, SOCK_STREAM, 0);
    if (fds[i] < 0) continue;
    fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL, 0) | O_NONBLOCK);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(MODBUS_PORT);
    addr.sin_addr.s_addr = (uint32_t)hosts[i];
    if (lwip_connect(fds[i], (sockaddr *)&addr, sizeof(addr)) < 0 && errno != EINPROGRESS) {
      close(fds[i]);
      fds[i] = -1;
      continue;
    }
    pending++;
  }

  uint32_t start = millis();
  while (pending > 0 && millis() - start < timeoutMs) {
    fd_set wr;
    FD_ZERO(&wr);
    int maxFd = -1;
    for (int i = 0; i < count; i++) {
      if (fds[i] < 0) continue;
      FD_SET(fds[i], &wr);
      if (fds[i] > maxFd) maxFd = fds[i];
    }
    timeval tv = { 0, 20000 };
    int n = select(maxFd + 1, nullptr, &wr, nullptr, &tv);
    if (n < 0) break;
    for (int i = 0; n > 0 && i < count; i++) {
      if (fds[i] < 0 || !FD_ISSET(fds[i], &wr)) continue;
      int err = 0;
      socklen_t len = sizeof(err);
      getsockopt(fds[i], SOL_SOCKET, SO_ERROR, &err, &len);
      open[i] = (err == 0);  // a refused connect also wakes select, with an error set
      close(fds[i]);
      fds[i] = -1;
      pending--;
    }
    pumpLvgl();
  }
  for (int i = 0; i < count; i++) {
    if (fds[i] >= 0) close(fds[i]);
  }
}

static void spinCb(void *obj, int32_t angle) {
  lv_obj_set_style_transform_rotation((lv_obj_t *)obj, angle, 0);
}

// Small spinning arrow beside the plant IP while we reconnect.
static void showReconnecting(bool on) {
  if (on == !lv_obj_has_flag(reconnectIcon, LV_OBJ_FLAG_HIDDEN)) return;
  if (on) {
    lv_obj_clear_flag(reconnectIcon, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, reconnectIcon);
    lv_anim_set_exec_cb(&a, spinCb);
    lv_anim_set_values(&a, 0, 3600);
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
  } else {
    lv_anim_delete(reconnectIcon, spinCb);
    lv_obj_add_flag(reconnectIcon, LV_OBJ_FLAG_HIDDEN);
  }
}

// Before the first reading this is a full-screen status message; after that
// the last data stays on screen and only the reconnect spinner shows.
static void showOverlay(const String &text) {
  if (haveData) {
    showReconnecting(true);
    return;
  }
  lv_label_set_text(overlayLabel, text.c_str());
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
}

static void hideOverlay() {
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  showReconnecting(false);
}

// Scans the /24 the ESP32 itself is on. Home/office LANs are almost always
// /24; if yours is larger, narrow it down manually via the cached-IP path.
static bool scanForSigenergy(IPAddress &found) {
  IPAddress local = WiFi.localIP();
  String subnet = local.toString().substring(0, local.toString().lastIndexOf('.') + 1);
  Serial.printf("Scanning %s0/24 for a Sigenergy plant (Modbus TCP, slave %u)...\n",
                subnet.c_str(), PLANT_SLAVE_ID);
  uint32_t scanStart = millis();
  int h = 1;
  while (h <= 254) {
    IPAddress batch[SCAN_PARALLEL];
    int n = 0;
    for (; h <= 254 && n < SCAN_PARALLEL; h++) {
      if (h != local[3]) batch[n++] = IPAddress(local[0], local[1], local[2], (uint8_t)h);
    }
    showOverlay("Searching for Sigenergy plant...\n" + subnet + "0/24  (" + String(h - 1) + "/254)");

    bool open[SCAN_PARALLEL];
    findOpenPorts(batch, n, open, SCAN_CONNECT_MS);
    for (int i = 0; i < n; i++) {
      if (!open[i]) continue;
      Serial.printf("Port %u open on %s, checking Modbus...\n", MODBUS_PORT, batch[i].toString().c_str());
      if (probeSigenergy(batch[i], 1000, 1500)) {
        found = batch[i];
        Serial.printf("Found Sigenergy plant at %s (%lu ms)\n", found.toString().c_str(),
                      (unsigned long)(millis() - scanStart));
        return true;
      }
    }
  }
  return false;
}

static void tryLoadCachedIp() {
  String cached = prefs.getString("ip", "");
  if (cached.length() == 0) return;
  IPAddress candidate;
  if (!candidate.fromString(cached)) return;
  for (int attempt = 1; attempt <= CACHE_ATTEMPTS; attempt++) {
    Serial.printf("Trying cached Sigenergy IP %s (attempt %d/%d)...\n", cached.c_str(), attempt, CACHE_ATTEMPTS);
    showOverlay("Checking last known address\n" + cached + "  (try " + String(attempt) + "/" +
                String(CACHE_ATTEMPTS) + ")");
    if (probeSigenergy(candidate, CACHE_CONNECT_MS, CACHE_READ_MS)) {
      sigenergyIp = candidate;
      haveIp = true;
      Serial.println("Cached IP confirmed.");
      return;
    }
    if (attempt < CACHE_ATTEMPTS) {
      uint32_t until = millis() + 1000;
      while ((int32_t)(millis() - until) < 0) waitTick();
    }
  }
  Serial.println("Cached IP did not respond, will rescan.");
}

// ── UI construction ─────────────────────────────────────────────────────
// Layout (480x320 landscape):
//   top right        plant IP  hh:mm  wifi
//   hero (left)      house icon + big kW, source-mix bar
//   battery (right)  large battery graphic with SOC
//   bottom strip     solar | grid | battery: icon tile + bar, big value below

// Unstyled rectangle we fully control (no theme padding/borders/scrolling).
static lv_obj_t *makeBox(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return o;
}

static lv_obj_t *makeFilledBox(lv_obj_t *parent, uint32_t col, int radius) {
  lv_obj_t *o = makeBox(parent);
  lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  return o;
}

static lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t col, const char *text) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  return l;
}

// Fixed-width label so changing text never needs re-alignment.
static lv_obj_t *makeAlignedLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t col,
                                  int x, int y, int w, lv_text_align_t align) {
  lv_obj_t *l = makeLabel(parent, font, col, "");
  lv_obj_set_width(l, w);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_align(l, align, 0);
  return l;
}

// Row that bottom-aligns a value label and a smaller unit label after it.
static lv_obj_t *makeValueRow(lv_obj_t *parent, int x, int y) {
  lv_obj_t *row = makeBox(parent);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_pos(row, x, y);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  return row;
}

static lv_obj_t *makeBar(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *bar = lv_bar_create(parent);
  lv_obj_set_pos(bar, x, y);
  lv_obj_set_size(bar, w, h);
  lv_bar_set_range(bar, 0, 1000);
  lv_obj_set_style_bg_color(bar, lv_color_hex(COL_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, h / 2, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, h / 2, LV_PART_INDICATOR);
  return bar;
}

static void setBarKw(lv_obj_t *bar, float kw) {
  float frac = fabsf(kw) / BAR_MAX_KW;
  if (frac > 1) frac = 1;
  int32_t v = (int32_t)(frac * 1000);
  if (fabsf(kw) > 0.05f && v < 25) v = 25;  // keep a visible nub for small flows
  lv_bar_set_value(bar, v, LV_ANIM_ON);
}

// ── vector icons, drawn in a 28x28 container (house: 32x32) ──
// Icons are built from lv_line segments plus a few filled boxes so they can
// be recoloured at runtime with setIconColor().
static const int ICON = 28;

static lv_obj_t *iconLine(lv_obj_t *icon, const lv_point_precise_t *pts, uint32_t n, int width) {
  lv_obj_t *l = lv_line_create(icon);
  lv_line_set_points(l, pts, n);
  lv_obj_set_style_line_width(l, width, 0);
  lv_obj_set_style_line_rounded(l, true, 0);
  return l;
}

static lv_obj_t *makeIconBox(lv_obj_t *tile, int size = ICON) {
  lv_obj_t *icon = makeBox(tile);
  lv_obj_set_size(icon, size, size);
  lv_obj_center(icon);
  return icon;
}

static lv_obj_t *makeSunIcon(lv_obj_t *tile) {
  static lv_point_precise_t rays[8][2];
  lv_obj_t *icon = makeIconBox(tile);
  lv_obj_t *core = makeFilledBox(icon, 0, LV_RADIUS_CIRCLE);
  lv_obj_set_size(core, 12, 12);
  lv_obj_center(core);
  for (int i = 0; i < 8; i++) {
    float a = i * (float)M_PI / 4;
    rays[i][0] = { (lv_value_precise_t)lroundf(14 + cosf(a) * 9.5f), (lv_value_precise_t)lroundf(14 + sinf(a) * 9.5f) };
    rays[i][1] = { (lv_value_precise_t)lroundf(14 + cosf(a) * 12.5f), (lv_value_precise_t)lroundf(14 + sinf(a) * 12.5f) };
    iconLine(icon, rays[i], 2, 2);
  }
  return icon;
}

static lv_obj_t *makeGridIcon(lv_obj_t *tile) {
  // transmission tower: A-frame legs, two cross-arms, X bracing
  static const lv_point_precise_t legs[]   = { {7, 26}, {14, 2}, {21, 26} };
  static const lv_point_precise_t armTop[] = { {5, 9}, {23, 9} };
  static const lv_point_precise_t armLow[] = { {7, 16}, {21, 16} };
  static const lv_point_precise_t brace1[] = { {11, 9}, {19, 16} };
  static const lv_point_precise_t brace2[] = { {17, 9}, {9, 16} };
  lv_obj_t *icon = makeIconBox(tile);
  iconLine(icon, legs, 3, 2);
  iconLine(icon, armTop, 2, 2);
  iconLine(icon, armLow, 2, 2);
  iconLine(icon, brace1, 2, 1);
  iconLine(icon, brace2, 2, 1);
  return icon;
}

// Drawn on a 32px grid and scaled to `size` (32 in the hero tile, ICON in the strip).
static lv_obj_t *makeHouseIcon(lv_obj_t *tile, int size = 32) {
  static lv_point_precise_t roofs[2][3];  // lv_line keeps the pointer: one per instance
  static int made = 0;
  lv_point_precise_t *roof = roofs[made++ % 2];
  auto k = [size](float v) { return (int32_t)lroundf(v * size / 32); };
  roof[0] = { k(3), k(16) };
  roof[1] = { k(16), k(4) };
  roof[2] = { k(29), k(16) };
  lv_obj_t *icon = makeIconBox(tile, size);
  iconLine(icon, roof, 3, 3);
  lv_obj_t *walls = makeBox(icon);
  lv_obj_set_size(walls, k(18), k(16));
  lv_obj_set_pos(walls, k(7), k(14));
  lv_obj_set_style_radius(walls, 2, 0);
  lv_obj_set_style_border_width(walls, 3, 0);
  lv_obj_set_style_border_side(walls, (lv_border_side_t)(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT |
                                                         LV_BORDER_SIDE_BOTTOM), 0);
  lv_obj_t *door = makeFilledBox(icon, 0, 1);
  lv_obj_set_size(door, k(6), k(9));
  lv_obj_set_pos(door, k(13), k(21));
  return icon;
}

// ── button icons, drawn in a 24x24 container (storm: 32x32) ──
static lv_obj_t *makeStormIcon(lv_obj_t *parent) {
  // thundercloud: three round puffs on a flat base, a bolt dropping out of it
  static const lv_point_precise_t bolt[] = { {18, 18}, {12, 24}, {19, 24}, {13, 30} };
  static const int16_t puffs[][3] = { {5, 6, 10}, {11, 2, 14}, {19, 7, 9} };  // x, y, diameter
  lv_obj_t *icon = makeBox(parent);
  lv_obj_set_size(icon, 32, 32);
  for (const auto &p : puffs) {
    lv_obj_t *puff = makeFilledBox(icon, 0, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(puff, p[0], p[1]);
    lv_obj_set_size(puff, p[2], p[2]);
  }
  lv_obj_t *base = makeFilledBox(icon, 0, 3);
  lv_obj_set_pos(base, 4, 11);
  lv_obj_set_size(base, 25, 6);
  iconLine(icon, bolt, 4, 3);
  return icon;
}

static lv_obj_t *makeClockIcon(lv_obj_t *parent) {
  static const lv_point_precise_t hands[] = { {12, 6}, {12, 12}, {16, 14} };
  lv_obj_t *icon = makeBox(parent);
  lv_obj_set_size(icon, 24, 24);
  lv_obj_t *face = makeBox(icon);
  lv_obj_set_size(face, 20, 20);
  lv_obj_set_pos(face, 2, 2);
  lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(face, 2, 0);
  iconLine(icon, hands, 3, 2);
  return icon;
}

static lv_obj_t *makeCloseIcon(lv_obj_t *parent) {
  static const lv_point_precise_t a[] = { {5, 5}, {19, 19} };
  static const lv_point_precise_t b[] = { {5, 19}, {19, 5} };
  lv_obj_t *icon = makeBox(parent);
  lv_obj_set_size(icon, 24, 24);
  iconLine(icon, a, 2, 2);
  iconLine(icon, b, 2, 2);
  return icon;
}

static lv_obj_t *battIconFill;
static lv_obj_t *makeBattIcon(lv_obj_t *tile) {
  lv_obj_t *icon = makeIconBox(tile);
  lv_obj_t *nub = makeFilledBox(icon, 0, 1);
  lv_obj_set_size(nub, 6, 3);
  lv_obj_set_pos(nub, 11, 2);
  lv_obj_t *body = makeBox(icon);
  lv_obj_set_size(body, 12, 21);
  lv_obj_set_pos(body, 8, 5);
  lv_obj_set_style_radius(body, 3, 0);
  lv_obj_set_style_border_width(body, 2, 0);
  battIconFill = makeFilledBox(icon, 0, 1);
  lv_obj_set_size(battIconFill, 6, 12);
  lv_obj_set_pos(battIconFill, 11, 11);
  return icon;
}

static void setIconColor(lv_obj_t *icon, uint32_t col) {
  lv_color_t c = lv_color_hex(col);
  uint32_t n = lv_obj_get_child_count(icon);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *ch = lv_obj_get_child(icon, i);
    if (lv_obj_check_type(ch, &lv_line_class)) {
      lv_obj_set_style_line_color(ch, c, 0);
    } else {
      lv_obj_set_style_bg_color(ch, c, 0);
      lv_obj_set_style_border_color(ch, c, 0);
    }
  }
}

static lv_color_t tint(uint32_t col, uint32_t base, uint8_t amount) {
  return lv_color_mix(lv_color_hex(col), lv_color_hex(base), amount);
}

// Raised button: light-to-dark gradient over a hard drop shadow. While pressed
// (and while checked, for toggles) it sinks BTN_DROP px onto where its shadow was.
// Children are laid out in a centred row.
static lv_obj_t *makeButton(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *b = makeBox(parent);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, tint(COL_TEXT, COL_CARD2, 18), 0);
  lv_obj_set_style_bg_grad_color(b, lv_color_hex(COL_CARD2), 0);
  lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_shadow_color(b, lv_color_hex(COL_SHADOW), 0);
  lv_obj_set_style_shadow_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(b, 2, 0);  // LVGL skips shadows narrower than 2px
  lv_obj_set_style_shadow_offset_y(b, BTN_DROP, 0);
  for (lv_style_selector_t st : { LV_STATE_PRESSED, LV_STATE_CHECKED }) {
    lv_obj_set_style_translate_y(b, BTN_DROP, st);
    lv_obj_set_style_shadow_opa(b, LV_OPA_TRANSP, st);
  }
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(b, 8, 0);
  return b;
}

static void setSectionColor(StripSection &s, uint32_t col) {
  lv_obj_set_style_bg_color(s.tile, tint(col, COL_CARD, 41), 0);
  setIconColor(s.icon, col);
  lv_obj_set_style_bg_color(s.bar, lv_color_hex(col), LV_PART_INDICATOR);
}

static StripSection makeSection(lv_obj_t *strip, int x, int w,
                                lv_obj_t *(*makeIcon)(lv_obj_t *), uint32_t col) {
  StripSection s = {};
  // top row: icon tile with the bar beside it; big value underneath
  s.tile = makeFilledBox(strip, COL_CARD, 9);
  lv_obj_set_size(s.tile, 34, 34);
  lv_obj_set_pos(s.tile, x + 12, 12);
  s.icon = makeIcon(s.tile);

  s.bar = makeBar(strip, x + 56, 26, w - 56 - 14, 6);

  // sized so the widest realistic value ("-10.5 kW") still fits the section
  lv_obj_t *row = makeValueRow(strip, x + 12, 48);
  s.value = makeLabel(row, &font_num_44, COL_TEXT, "--");
  lv_obj_t *unit = makeLabel(row, &lv_font_montserrat_16, COL_MUTED, "kW");
  lv_obj_set_style_pad_left(unit, 5, 0);
  lv_obj_set_style_pad_bottom(unit, 1, 0);  // baseline-align with the 44px value
  setSectionColor(s, col);
  return s;
}

static void onChargeToggled(lv_event_t *e);
static void onFreeHourOpen(lv_event_t *e);
static void onFreeHourClose(lv_event_t *e);

static void buildUi() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // ── plant IP + clock + wifi (top right) ──
  //    clock is sized to its text and right-anchored; layoutTopBar() keeps the
  //    IP and spinner a fixed distance left of whatever the clock reads
  timeLabel = makeLabel(scr, &lv_font_montserrat_14, COL_MUTED, "");
  lv_obj_align(timeLabel, LV_ALIGN_TOP_RIGHT, -38, 5);
  ipLabel = makeAlignedLabel(scr, &lv_font_montserrat_14, COL_MUTED, 0, 5, 130, LV_TEXT_ALIGN_RIGHT);
  reconnectIcon = makeAlignedLabel(scr, &lv_font_montserrat_14, COL_MUTED, 0, 5, 16, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(reconnectIcon, LV_SYMBOL_REFRESH);
  lv_obj_set_height(reconnectIcon, 16);
  lv_obj_set_style_transform_pivot_x(reconnectIcon, 8, 0);
  lv_obj_set_style_transform_pivot_y(reconnectIcon, 8, 0);
  lv_obj_add_flag(reconnectIcon, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *wifi = makeLabel(scr, &lv_font_montserrat_14, COL_MUTED, LV_SYMBOL_WIFI);
  lv_obj_align(wifi, LV_ALIGN_TOP_RIGHT, -12, 5);

  // ── hero: current house load ──
  //    (shows grid import instead during free hour; setFreeHourMode() swaps them)
  houseTile = makeFilledBox(scr, COL_CARD, 10);
  lv_obj_set_size(houseTile, TILE_SIZE, TILE_SIZE);
  lv_obj_set_pos(houseTile, TILE_X, TILE_Y);
  lv_obj_set_style_bg_color(houseTile, tint(COL_HOME, COL_CARD, 41), 0);
  heroHouseIcon = makeHouseIcon(houseTile, ICON);
  setIconColor(heroHouseIcon, COL_HOME);
  heroGridIcon = makeGridIcon(houseTile);
  setIconColor(heroGridIcon, COL_IMPORT);
  lv_obj_add_flag(heroGridIcon, LV_OBJ_FLAG_HIDDEN);

  heroRow = makeValueRow(scr, HERO_X, HERO_Y);
  heroValue = makeLabel(heroRow, &font_hero_144, COL_TEXT, "-.-");
  lv_obj_t *heroUnit = makeLabel(heroRow, &lv_font_montserrat_24, COL_MUTED, "kW");
  lv_obj_set_style_pad_left(heroUnit, 8, 0);
  lv_obj_set_style_pad_bottom(heroUnit, 9, 0);  // baseline-align with the 144px value

  // ── source mix: segmented bar, always solar | grid | battery left to right,
  //    bottom-aligned with the battery graphic ──
  const int mixX = MIX_X, mixW = MIX_W, mixY = BATT_Y + BATT_H - MIX_H;
  mixBar = makeFilledBox(scr, COL_CARD2, 10);
  lv_obj_set_pos(mixBar, mixX, mixY);
  lv_obj_set_size(mixBar, mixW, MIX_H);
  lv_obj_set_flex_flow(mixBar, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(mixBar, MIX_GAP, 0);
  // segment widths are set in pixels by setMixShares()
  mixSolar = makeFilledBox(mixBar, COL_SOLAR_HI, 10);
  lv_obj_set_style_bg_grad_color(mixSolar, lv_color_hex(COL_SOLAR), 0);
  lv_obj_set_style_bg_grad_dir(mixSolar, LV_GRAD_DIR_HOR, 0);
  mixGrid = makeFilledBox(mixBar, COL_IMPORT, 10);
  mixBatt = makeFilledBox(mixBar, COL_DISCHARGE, 10);
  for (lv_obj_t *seg : { mixSolar, mixGrid, mixBatt }) {
    lv_obj_set_size(seg, 0, LV_PCT(100));
    lv_obj_add_flag(seg, LV_OBJ_FLAG_HIDDEN);
  }

  // ── mode buttons (home screen): icon-only, stacked between the hero and the battery ──
  chargeBtn = makeButton(scr, MODE_BTN_X, CHARGE_BTN_Y, MODE_BTN_W, MODE_BTN_H);
  lv_obj_add_flag(chargeBtn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_style_bg_color(chargeBtn, tint(COL_RESERVE, COL_BG, 46), LV_STATE_CHECKED);
  lv_obj_set_style_bg_grad_color(chargeBtn, tint(COL_RESERVE, COL_BG, 128), LV_STATE_CHECKED);
  lv_obj_set_style_border_color(chargeBtn, lv_color_hex(COL_RESERVE), LV_STATE_CHECKED);
  lv_obj_set_style_border_width(chargeBtn, 2, LV_STATE_CHECKED);
  chargeIcon = makeStormIcon(chargeBtn);
  setIconColor(chargeIcon, COL_RESERVE);
  lv_obj_add_event_cb(chargeBtn, onChargeToggled, LV_EVENT_VALUE_CHANGED, nullptr);

  freeBtn = makeButton(scr, MODE_BTN_X, FREE_BTN_Y, MODE_BTN_W, MODE_BTN_H);
  setIconColor(makeClockIcon(freeBtn), COL_FREE);
  lv_obj_add_event_cb(freeBtn, onFreeHourOpen, LV_EVENT_CLICKED, nullptr);

  // ── free-hour header + countdown (hidden until free hour starts) ──
  //    close button in the corner, then the hero tile (moved by setFreeHourMode()), then the title
  closeBtn = makeButton(scr, 10, 4, 38, 36);
  setIconColor(makeCloseIcon(closeBtn), COL_TEXT);
  lv_obj_set_ext_click_area(closeBtn, 10);  // small target in the corner, resistive touch
  lv_obj_add_event_cb(closeBtn, onFreeHourClose, LV_EVENT_CLICKED, nullptr);

  freeTitle = makeLabel(scr, &lv_font_montserrat_14, COL_FREE, "FREE HOUR");
  lv_obj_set_style_text_letter_space(freeTitle, 2, 0);
  lv_obj_set_pos(freeTitle, FREE_TILE_X + TILE_SIZE + 12, FREE_TILE_Y + (TILE_SIZE - 16) / 2);

  freeBar = makeBar(scr, mixX, mixY + 6, FREE_BAR_W, MIX_H - 6);
  lv_bar_set_range(freeBar, 0, 3600);
  lv_obj_set_style_radius(freeBar, 9, LV_PART_MAIN);
  lv_obj_set_style_radius(freeBar, 9, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(freeBar, lv_color_hex(COL_CARD2), LV_PART_MAIN);
  lv_obj_set_style_bg_color(freeBar, tint(0xffffff, COL_FREE, 90), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(freeBar, lv_color_hex(COL_FREE), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(freeBar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);

  // baseline on the mix bar's bottom edge (font_num_44: line height 42, baseline 4)
  freeCountdown = makeBox(scr);
  lv_obj_set_size(freeCountdown, COUNTDOWN_W, 42);
  lv_obj_set_pos(freeCountdown, mixX + FREE_W - COUNTDOWN_W, mixY + MIX_H - (42 - 4));
  for (int i = 0, x = 0; i < 4; i++, x += CD_DIGIT_W) {
    if (i == 2) {
      lv_label_set_text(makeAlignedLabel(freeCountdown, &font_num_44, COL_FREE, x, 0, CD_COLON_W,
                                         LV_TEXT_ALIGN_CENTER), ":");
      x += CD_COLON_W;
    }
    cdDigits[i] = makeAlignedLabel(freeCountdown, &font_num_44, COL_FREE, x, 0, CD_DIGIT_W,
                                   LV_TEXT_ALIGN_CENTER);
  }

  for (lv_obj_t *o : { closeBtn, freeTitle, freeBar, freeCountdown })
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);

  // ── battery graphic ──
  battNub = makeFilledBox(scr, COL_BORDER, 4);
  lv_obj_set_pos(battNub, BATT_X + (BATT_W - 44) / 2, BATT_Y - 9);
  lv_obj_set_size(battNub, 44, 11);

  battBody = makeFilledBox(scr, COL_CARD, 18);
  lv_obj_set_pos(battBody, BATT_X, BATT_Y);
  lv_obj_set_size(battBody, BATT_W, BATT_H);
  lv_obj_set_style_border_color(battBody, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(battBody, 2, 0);

  battFill = makeFilledBox(battBody, COL_BATT_HI, 12);
  lv_obj_set_width(battFill, BATT_W - 2 * 2 - 2 * 5);
  lv_obj_set_height(battFill, 0);
  lv_obj_align(battFill, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_grad_dir(battFill, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_shadow_width(battFill, 18, 0);
  lv_obj_set_style_shadow_opa(battFill, LV_OPA_40, 0);

  socRow = makeBox(battBody);
  lv_obj_set_size(socRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(socRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(socRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  socValue = makeLabel(socRow, &font_num_44, COL_TEXT, "--");
  socPctLabel = makeLabel(socRow, &lv_font_montserrat_16, COL_TEXT, "%");
  lv_obj_set_style_pad_left(socPctLabel, 2, 0);
  lv_obj_set_style_pad_bottom(socPctLabel, 1, 0);  // baseline-align with the 44px value
  lv_obj_center(socRow);

  // ── bottom strip ──
  lv_obj_t *strip = makeFilledBox(scr, COL_CARD, 14);
  lv_obj_set_pos(strip, 10, 212);
  lv_obj_set_size(strip, 460, 100);
  lv_obj_set_style_border_color(strip, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(strip, 1, 0);

  const int secW = 458 / 3;  // strip content width / 3
  secSolar = makeSection(strip, 0, secW, makeSunIcon, COL_SOLAR);
  secGrid  = makeSection(strip, secW, secW, makeGridIcon, COL_IDLE);
  setIconColor(secGrid.icon, COL_IMPORT);  // icon stays red to match the mix bar
  secGrid.altIcon = makeHouseIcon(secGrid.tile, ICON);
  setIconColor(secGrid.altIcon, COL_HOME);
  lv_obj_add_flag(secGrid.altIcon, LV_OBJ_FLAG_HIDDEN);
  secBatt  = makeSection(strip, secW * 2, secW, makeBattIcon, COL_IDLE);
  for (int i = 1; i <= 2; i++) {
    lv_obj_t *div = makeFilledBox(strip, COL_BORDER, 0);
    lv_obj_set_pos(div, secW * i, 14);
    lv_obj_set_size(div, 1, 70);
  }

  // ── full-screen status overlay (shown while (re)connecting/scanning) ──
  overlay = lv_obj_create(scr);
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(overlay, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_90, 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_set_style_radius(overlay, 0, 0);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(overlay);

  overlayLabel = lv_label_create(overlay);
  lv_label_set_text(overlayLabel, "Starting...");
  lv_label_set_long_mode(overlayLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(overlayLabel, LV_PCT(80));
  lv_obj_set_style_text_align(overlayLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(overlayLabel, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_font(overlayLabel, &lv_font_montserrat_16, 0);
  lv_obj_center(overlayLabel);
}

// Sizes the source-mix segments in pixels. (LVGL's flex_grow divides the free
// space with integer maths, which left a lone 100% segment ~36px short.)
static void setMixShares(float solar, float grid, float batt) {
  lv_obj_t *segs[3] = { mixSolar, mixGrid, mixBatt };
  float shares[3] = { solar, grid, batt };
  int visible = 0;
  for (float sh : shares) if (sh > 0.005f) visible++;
  int avail = MIX_W - MIX_GAP * (visible > 0 ? visible - 1 : 0);
  int used = 0, seen = 0;
  for (int i = 0; i < 3; i++) {
    if (shares[i] <= 0.005f) {
      lv_obj_add_flag(segs[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    // last visible segment takes the remainder so the bar always ends flush
    int w = (++seen == visible) ? avail - used : (int)lroundf(avail * shares[i]);
    if (w < 4) w = 4;
    used += w;
    lv_obj_set_width(segs[i], w);
    lv_obj_clear_flag(segs[i], LV_OBJ_FLAG_HIDDEN);
  }
}

static void updateUi(float gridKw, float socPct, float pvKw, float essKw) {
  lastGridKw = gridKw;
  lastSocPct = socPct;
  lastPvKw   = pvKw;
  lastEssKw  = essKw;

  char buf[32];
  float homeKw = pvKw + gridKw - essKw;
  if (homeKw < 0) homeKw = 0;

  // hero: house load, or grid import during free hour
  // (whole kW from 10 up so "12.0 kW" doesn't run into the buttons)
  float heroKw = freeHour ? fmaxf(gridKw, 0) : homeKw;
  snprintf(buf, sizeof(buf), heroKw >= 9.95f ? "%.0f" : "%.1f", heroKw);
  lv_label_set_text(heroValue, buf);

  // where the house load is coming from
  float fromSolar = fminf(homeKw, fmaxf(pvKw, 0));
  float fromBatt  = fminf(homeKw - fromSolar, fmaxf(-essKw, 0));
  float fromGrid  = fmaxf(homeKw - fromSolar - fromBatt, 0);
  if (homeKw > 0.05f) setMixShares(fromSolar / homeKw, fromGrid / homeKw, fromBatt / homeKw);
  else                 setMixShares(0, 0, 0);

  // battery graphic
  int soc = (int)lroundf(socPct);
  if (soc < 0) soc = 0;
  if (soc > 100) soc = 100;
  uint32_t battCol = soc >= 50 ? COL_BATT_HI : (soc >= 20 ? COL_BATT_MID : COL_BATT_LOW);
  const int innerH = BATT_H - 2 * 2;         // body height minus border
  const int fillMax = innerH - 2 * 5;        // 5px inset top and bottom
  int fillH = fillMax * soc / 100;
  lv_obj_set_height(battFill, fillH);
  lv_obj_set_style_bg_color(battFill, tint(0xffffff, battCol, 90), 0);
  lv_obj_set_style_bg_grad_color(battFill, tint(battCol, 0x000000, 204), 0);
  lv_obj_set_style_shadow_color(battFill, lv_color_hex(battCol), 0);

  snprintf(buf, sizeof(buf), "%d", soc);
  lv_label_set_text(socValue, buf);
  // centre the number in the fill when it fits, otherwise in the empty space above it
  bool inFill = fillH >= 52;
  int centreY = inFill ? innerH - 5 - fillH / 2 : (5 + innerH - 5 - fillH) / 2;
  uint32_t socCol = inFill ? COL_ON_FILL : COL_TEXT;
  lv_obj_set_style_text_color(socValue, lv_color_hex(socCol), 0);
  lv_obj_set_style_text_color(socPctLabel, lv_color_hex(socCol), 0);
  lv_obj_align(socRow, LV_ALIGN_CENTER, 0, centreY - innerH / 2);

  uint32_t flowCol = essKw > 0.05f ? COL_CHARGE : (essKw < -0.05f ? COL_DISCHARGE : COL_IDLE);

  // bottom strip
  snprintf(buf, sizeof(buf), "%.1f", pvKw);
  lv_label_set_text(secSolar.value, buf);
  setBarKw(secSolar.bar, pvKw);

  if (freeHour) {
    // grid and house swap places: the house load takes the grid's strip section
    snprintf(buf, sizeof(buf), "%.1f", homeKw);
    lv_label_set_text(secGrid.value, buf);
    setSectionColor(secGrid, COL_HOME);
    setBarKw(secGrid.bar, homeKw);
  } else {
    snprintf(buf, sizeof(buf), "%.1f", fabsf(gridKw));
    lv_label_set_text(secGrid.value, buf);
    bool importing = gridKw > 0.05f, exporting = gridKw < -0.05f;
    setSectionColor(secGrid, importing ? COL_IMPORT : (exporting ? COL_EXPORT : COL_IDLE));
    setIconColor(secGrid.icon, COL_IMPORT);
    setBarKw(secGrid.bar, gridKw);
  }

  // no sign when the battery is idle, so it reads "0.0" rather than "+0.0"/"-0.0"
  if (fabsf(essKw) < 0.05f) snprintf(buf, sizeof(buf), "0.0");
  else snprintf(buf, sizeof(buf), "%+.1f", essKw);
  lv_label_set_text(secBatt.value, buf);
  setSectionColor(secBatt, flowCol);
  lv_obj_set_height(battIconFill, 1 + 11 * soc / 100);
  lv_obj_set_y(battIconFill, 23 - (1 + 11 * soc / 100));
  setBarKw(secBatt.bar, essKw);
}

static void layoutTopBar() {
  lv_obj_update_layout(timeLabel);  // clock width follows its digits
  lv_obj_align_to(ipLabel, timeLabel, LV_ALIGN_OUT_LEFT_TOP, -IP_GAP, 0);
  lv_obj_align_to(reconnectIcon, timeLabel, LV_ALIGN_OUT_LEFT_TOP, -(IP_GAP - 16) / 2, 0);
}

static void updateClock() {
  static int lastMinute = -1;
  time_t now = time(nullptr);
  if (now < 1700000000) return;  // not synced yet
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_min == lastMinute) return;
  lastMinute = t.tm_min;
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", &t);
  lv_label_set_text(timeLabel, buf);
  layoutTopBar();

  bool offPeak = t.tm_hour >= OFFPEAK_START_HOUR || t.tm_hour < OFFPEAK_END_HOUR;
  lv_obj_set_style_text_color(heroValue, lv_color_hex(offPeak ? COL_OFFPEAK : COL_TEXT), 0);
}

// ── system hooks ─────────────────────────────────────────────────────────
// Where the plant gets told about a mode change.

// Charge pressed in (on) or released (off). Queues the Backup SOC write;
// ST_POLL sends it on its next turn (or after reconnecting, if the link is down).
static void applyChargeMode(bool on) {
  pendingReserve = on ? CHARGE_ON_SOC : CHARGE_OFF_SOC;
}

// Free hour started (on) or ended (off: close button or countdown reached 0).
static void applyFreeHourMode(bool on) {
  if (on) {
    // TODO(system): free hour started.
  } else {
    // TODO(system): free hour ended.
  }
}

// ── mode UI ──────────────────────────────────────────────────────────────
static void showChargeOn(bool on) {
  chargeOn = on;
  if (on) lv_obj_add_state(chargeBtn, LV_STATE_CHECKED);
  else    lv_obj_remove_state(chargeBtn, LV_STATE_CHECKED);
  setIconColor(chargeIcon, on ? COL_TEXT : COL_RESERVE);
  // battery outline picks up the Charge colour so the mode shows across the room
  lv_color_t edge = on ? tint(COL_RESERVE, COL_BG, 191) : lv_color_hex(COL_BORDER);
  lv_obj_set_style_border_color(battBody, edge, 0);
  lv_obj_set_style_bg_color(battNub, edge, 0);
}

static void onChargeToggled(lv_event_t *e) {
  bool on = lv_obj_has_state(chargeBtn, LV_STATE_CHECKED);
  showChargeOn(on);
  Serial.printf("Charge mode %s\n", on ? "on" : "off");
  applyChargeMode(on);
}

static void showSecsLeft(int secs) {
  if (secs == shownSecsLeft) return;
  shownSecsLeft = secs;
  int digits[4] = { secs / 600, secs / 60 % 10, secs % 60 / 10, secs % 10 };
  for (int i = 0; i < 4; i++) {
    char d[2] = { (char)('0' + digits[i]), 0 };
    lv_label_set_text(cdDigits[i], d);
  }
  lv_bar_set_value(freeBar, secs, LV_ANIM_OFF);
}

static void setFreeHourMode(bool on) {
  freeHour = on;
  for (lv_obj_t *o : { chargeBtn, freeBtn, mixBar }) {
    if (on) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
  for (lv_obj_t *o : { closeBtn, freeTitle, freeBar, freeCountdown }) {
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }

  // hero tile moves right of the close button and shows the grid; strip shows the house
  lv_obj_set_pos(houseTile, on ? FREE_TILE_X : TILE_X, on ? FREE_TILE_Y : TILE_Y);
  lv_obj_set_style_bg_color(houseTile, tint(on ? COL_IMPORT : COL_HOME, COL_CARD, 41), 0);
  lv_obj_t *show[] = { on ? heroGridIcon : heroHouseIcon, on ? secGrid.altIcon : secGrid.icon };
  lv_obj_t *hide[] = { on ? heroHouseIcon : heroGridIcon, on ? secGrid.icon : secGrid.altIcon };
  for (lv_obj_t *o : show) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  for (lv_obj_t *o : hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);

  if (haveData) updateUi(lastGridKw, lastSocPct, lastPvKw, lastEssKw);
}

// Counts down to the top of the current hour by the wall clock.
static void onFreeHourOpen(lv_event_t *e) {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < 1700000000) {
    Serial.println("Free hour: clock not synced yet, ignoring");
    return;
  }
  struct tm t;
  localtime_r(&tv.tv_sec, &t);
  uint32_t msLeft = (3600 - (t.tm_min * 60 + t.tm_sec)) * 1000UL - tv.tv_usec / 1000;
  freeHourEndMs = millis() + msLeft;
  shownSecsLeft = -1;
  showSecsLeft((msLeft + 999) / 1000);
  setFreeHourMode(true);
  Serial.printf("Free hour started, %lu s to go\n", (unsigned long)(msLeft / 1000));
  applyFreeHourMode(true);
}

static void endFreeHour() {
  setFreeHourMode(false);
  Serial.println("Free hour ended");
  applyFreeHourMode(false);
}

static void onFreeHourClose(lv_event_t *e) {
  endFreeHour();
}

static void updateCountdown() {
  if (!freeHour) return;
  int32_t msLeft = (int32_t)(freeHourEndMs - millis());
  if (msLeft <= 0) {
    endFreeHour();
    return;
  }
  showSecsLeft((msLeft + 999) / 1000);
}

// ── touch calibration ────────────────────────────────────────────────────
// 3-point calibration: a cross is shown at CAL_TARGETS[0..2], the raw XPT2046
// point is averaged over each press, and smartdisplay fits the affine
// correction from them. A 4th cross checks the fit before it's saved to NVS
// (key "tcal"); a fit that misses by more than CAL_VERIFY_TOL starts over.
// Start it by holding the clock for CAL_HOLD_MS, or by sending "cal" over serial.
//
// The correction acts on the portrait point before LVGL rotates it, and LVGL's
// 90-degree rotation maps portrait (px, py) to landscape (479 - py, px).
static const lv_point_t CAL_TARGETS[4] = { {48, 40}, {432, 160}, {240, 280}, {380, 250} };
static const int      CAL_VERIFY_TOL = 12;       // px
static const uint32_t CAL_HOLD_MS    = 5000;
static const uint32_t CAL_TIMEOUT_MS = 30000;    // no touch for this long: give up, keep the old one
static const int      CAL_SKIP       = 3;        // samples dropped at each end of a press (still settling)
static const int      CAL_MIN_KEEP   = 3;        // samples needed after trimming (~0.3s press at 33ms reads)
static const int      SCREEN_W       = 480;

static lv_indev_read_cb_t touchReadCb;  // smartdisplay's: driver read + touch_calibration_data
static touch_calibration_data_t calPrev;  // what was in force before calibrating
static int        calStep = -1;          // -1 idle, 0..2 collecting, 3 verifying
static bool       calArmed;              // false until the press that started it is released
static lv_point_t calSamples[64];
static int        calCount;
static lv_point_t calTap;                // averaged point of the last press
static bool       calTapReady;
static uint32_t   calLastTouch;
static lv_point_t calRaw[3];
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
    if (calArmed && calCount < (int)(sizeof(calSamples) / sizeof(calSamples[0])))
      calSamples[calCount++] = data->point;
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

static void showCalTarget(const char *note = "") {
  const lv_point_t &t = CAL_TARGETS[calStep];
  lv_obj_set_pos(calCross, t.x - 20, t.y - 20);
  char buf[96];
  if (calStep < 3) snprintf(buf, sizeof(buf), "%sPress and hold the centre\nof the cross  (%d/3)", note, calStep + 1);
  else             snprintf(buf, sizeof(buf), "%sPress the cross once more\nto check", note);
  lv_label_set_text(calText, buf);
}

static void startTouchCalibration() {
  if (calStep >= 0) return;
  Serial.println("Touch calibration started");
  calPrev = touch_calibration_data;
  touch_calibration_data.valid = false;  // collect raw points
  calStep = 0;
  calArmed = false;
  calCount = 0;
  calTapReady = false;
  calLastTouch = millis();
  lv_obj_clear_flag(calLayer, LV_OBJ_FLAG_HIDDEN);
  showCalTarget();
}

static void endTouchCalibration(bool keep) {
  if (keep) {
    prefs.putBytes("tcal", &touch_calibration_data, sizeof(touch_calibration_data));
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

static void updateTouchCalibration() {
  if (calStep < 0) return;
  if (millis() - calLastTouch > CAL_TIMEOUT_MS) {
    endTouchCalibration(false);
    return;
  }
  if (!calTapReady) return;
  calTapReady = false;

  if (calStep < 3) {
    calRaw[calStep] = calTap;
    Serial.printf("Cal point %d: target (%d,%d) raw (%d,%d)\n", calStep + 1,
                  (int)CAL_TARGETS[calStep].x, (int)CAL_TARGETS[calStep].y, (int)calTap.x, (int)calTap.y);
    if (++calStep == 3) {
      lv_point_t screen[3];
      for (int i = 0; i < 3; i++) screen[i] = { CAL_TARGETS[i].y, SCREEN_W - 1 - CAL_TARGETS[i].x };
      touch_calibration_data_t fit = smartdisplay_compute_touch_calibration(screen, calRaw);
      bool finite = isfinite(fit.alphaX) && isfinite(fit.betaX) && isfinite(fit.deltaX) &&
                    isfinite(fit.alphaY) && isfinite(fit.betaY) && isfinite(fit.deltaY);
      if (!finite) {  // two presses landed on the same raw point
        calStep = 0;
        showCalTarget("Those presses didn't register\nproperly. Again:\n");
        return;
      }
      touch_calibration_data = fit;
    }
    showCalTarget();
    return;
  }

  // verifying: calTap went through the new fit, so rotate it to landscape
  const lv_point_t &t = CAL_TARGETS[3];
  int lx = SCREEN_W - 1 - calTap.y, ly = calTap.x;
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

static void onClockHold(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) clockHeldSince = millis();
  else if (code == LV_EVENT_PRESSING && clockHeldSince && millis() - clockHeldSince >= CAL_HOLD_MS) {
    clockHeldSince = 0;
    startTouchCalibration();
  }
}

static void buildCalibrationUi() {
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
  lv_obj_set_size(calCross, 41, 41);
  lv_obj_t *h = makeFilledBox(calCross, COL_TEXT, 0);
  lv_obj_set_pos(h, 0, 20);
  lv_obj_set_size(h, 41, 1);
  lv_obj_t *v = makeFilledBox(calCross, COL_TEXT, 0);
  lv_obj_set_pos(v, 20, 0);
  lv_obj_set_size(v, 1, 41);
  lv_obj_t *ring = makeBox(calCross);
  lv_obj_set_pos(ring, 12, 12);
  lv_obj_set_size(ring, 17, 17);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(ring, 2, 0);
  lv_obj_set_style_border_color(ring, lv_color_hex(COL_FREE), 0);

  lv_obj_add_flag(calLayer, LV_OBJ_FLAG_HIDDEN);
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
      len = 0;
    } else if (len < (int)sizeof(line) - 1) {
      line[len++] = c;
    }
  }
}

// ── app state machine (non-blocking so LVGL/touch stay responsive) ─────────
enum AppState { ST_WIFI, ST_LOAD_CACHE, ST_SCAN, ST_WAIT_RETRY, ST_CONNECT, ST_POLL };
static AppState state = ST_WIFI;
static AppState retryState = ST_SCAN;  // where ST_WAIT_RETRY goes when it expires
static uint32_t nextActionAt = 0;
static int connectFailures = 0;

void setup() {
  Serial.begin(115200);
  delay(300);

  smartdisplay_init();
  // The board definition's touch mapping comes out mirrored left-to-right in
  // landscape, and the uncalibrated XPT2046 readings are 10-20px off on top of
  // that. Correct both on the raw (portrait) point before LVGL rotates it:
  // raw y runs the opposite way to landscape x, so mirroring x means flipping y.
  // From an on-screen 3-point calibration (3px off at the check point). Used
  // until one has been saved to NVS (see "touch calibration").
  touch_calibration_data = {
    .valid = true,
    .alphaX = 1.1148f, .betaX = -0.0107f, .deltaX = -19.5f,
    .alphaY = 0.0206f, .betaY = -1.1100f, .deltaY = 502.0f,
  };
  lv_display_set_rotation(lv_display_get_default(), LV_DISPLAY_ROTATION_90);
  lvLastTick = millis();
  smartdisplay_lcd_set_brightness_cb(adaptiveBrightness, BL_INTERVAL_MS);
  lv_indev_t *touch = lv_indev_get_next(nullptr);
  touchReadCb = lv_indev_get_read_cb(touch);
  lv_indev_set_read_cb(touch, touchRead);
  prefs.begin("sigmon", false);
  touch_calibration_data_t savedCal;
  if (prefs.getBytes("tcal", &savedCal, sizeof(savedCal)) == sizeof(savedCal) && savedCal.valid) {
    touch_calibration_data = savedCal;
    Serial.println("Using saved touch calibration");
  }
  buildUi();
  buildCalibrationUi();
  layoutTopBar();
  showOverlay("Connecting to WiFi\n\"" WIFI_SSID "\"...");

  Serial.printf("Connecting to WiFi \"%s\"...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // modem sleep adds 100ms+ latency, which made LAN probes time out
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {
  // LED mode follows the state machine; a dropped WiFi link overrides it.
  // switch (state) {
  //   case ST_WIFI:       ledMode = LED_WIFI; break;
  //   case ST_LOAD_CACHE:
  //   case ST_SCAN:
  //   case ST_CONNECT:    ledMode = LED_SEARCH; break;
  //   case ST_WAIT_RETRY: ledMode = LED_ERROR; break;
  //   case ST_POLL:       ledMode = LED_OK; break;
  // }
  // if (state != ST_WIFI && WiFi.status() != WL_CONNECTED) ledMode = LED_WIFI;

  pumpLvgl();

  static uint32_t lastClockCheck = 0;
  if (millis() - lastClockCheck >= 1000) {
    lastClockCheck = millis();
    updateClock();
  }
  updateCountdown();
  updateTouchCalibration();
  pollSerial();

  switch (state) {
    case ST_WIFI: {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("WiFi connected, IP: ");
        Serial.println(WiFi.localIP());
        configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
        state = ST_LOAD_CACHE;
      }
      break;
    }

    case ST_LOAD_CACHE: {
      tryLoadCachedIp();
      state = haveIp ? ST_CONNECT : ST_SCAN;
      break;
    }

    case ST_SCAN: {
      showOverlay("Searching for Sigenergy plant...");
      IPAddress found;
      if (scanForSigenergy(found)) {
        sigenergyIp = found;
        haveIp = true;
        prefs.putString("ip", sigenergyIp.toString());
        state = ST_CONNECT;
      } else {
        showOverlay("No Sigenergy plant found.\nRetrying in 30s...");
        nextActionAt = millis() + 30000;
        retryState = ST_SCAN;
        state = ST_WAIT_RETRY;
      }
      break;
    }

    case ST_WAIT_RETRY: {
      if ((int32_t)(millis() - nextActionAt) >= 0) state = retryState;
      break;
    }

    case ST_CONNECT: {
      showOverlay("Connecting to Sigenergy plant\n" + sigenergyIp.toString() + " ...");
      // check the port without blocking first, so the UI keeps animating while
      // the plant is unreachable; the real connect is then near-instant
      bool open = false;
      findOpenPorts(&sigenergyIp, 1, &open, 2000);
      if (open && modbusClient.connect(sigenergyIp, MODBUS_PORT, 2000)) {
        connectFailures = 0;
        lv_label_set_text(ipLabel, sigenergyIp.toString().c_str());
        needChargeSync = true;  // the reserve may have changed (e.g. in the mySigen app)
        nextActionAt = 0;
        state = ST_POLL;
      } else if (++connectFailures < CACHE_ATTEMPTS) {
        // keep trying the known address before paying for a subnet scan
        Serial.printf("Could not reach Sigenergy plant (attempt %d/%d), retrying...\n",
                      connectFailures, CACHE_ATTEMPTS);
        nextActionAt = millis() + 1000;
        retryState = ST_CONNECT;
        state = ST_WAIT_RETRY;
      } else {
        Serial.println("Could not reach Sigenergy plant, rescanning...");
        connectFailures = 0;
        haveIp = false;
        state = ST_SCAN;
      }
      break;
    }

    case ST_POLL: {
      if ((int32_t)(millis() - nextActionAt) < 0) break;

      // One request per turn, so the plant always gets the 1s gap it asks for.
      // A queued Charge write goes first, then the Backup SOC sync, then the poll.
      if (pendingReserve >= 0) {
        ModbusResult r = modbusWriteRegister(modbusClient, PLANT_SLAVE_ID, BACKUP_SOC_REG,
                                             pendingReserve * 10);
        if (r == MB_FAIL) {
          Serial.println("Backup SOC write failed, reconnecting...");
          modbusClient.stop();
          state = ST_CONNECT;  // write stays queued
          break;
        }
        if (r == MB_OK) {
          Serial.printf("Backup SOC set to %d%%\n", pendingReserve);
          needChargeSync = false;
          lastChargeSyncAt = millis();  // the next periodic read confirms it stuck
        } else {
          Serial.printf("Plant refused Backup SOC %d%%\n", pendingReserve);
          needChargeSync = true;  // put the button back to what the plant has
        }
        pendingReserve = -1;
        nextActionAt = millis() + POLL_INTERVAL_MS;
        break;
      }

      if (needChargeSync || millis() - lastChargeSyncAt >= CHARGE_SYNC_INTERVAL_MS) {
        uint16_t socRaw;
        ModbusResult r = readBackupSoc(modbusClient, socRaw);
        if (r == MB_FAIL) {
          Serial.println("Backup SOC read failed, reconnecting...");
          modbusClient.stop();
          state = ST_CONNECT;
          break;
        }
        if (r != MB_OK) {
          Serial.println("Plant refused Backup SOC read");
        } else if (pendingReserve >= 0) {
          // Charge was tapped while the read was out; this value predates the
          // tap, and the queued write goes next.
        } else {
          bool on = socRaw >= CHARGE_ON_SOC * 10;
          if (needChargeSync || on != chargeOn) {
            Serial.printf("Plant Backup SOC: %.0f%% -> Charge %s\n", socRaw / 10.0f, on ? "on" : "off");
          }
          showChargeOn(on);
        }
        needChargeSync = false;
        lastChargeSyncAt = millis();
        nextActionAt = millis() + POLL_INTERVAL_MS;
        break;
      }

      uint16_t regs[REGISTER_COUNT];
      if (!modbusReadHoldingRegisters(modbusClient, PLANT_SLAVE_ID, START_ADDRESS, REGISTER_COUNT, regs)) {
        Serial.println("Read failed, reconnecting...");
        // ledMode = LED_ERROR;
        modbusClient.stop();
        state = ST_CONNECT;
        break;
      }

      haveData = true;
      hideOverlay();

      auto idx = [](uint16_t addr) { return (size_t)(addr - START_ADDRESS); };
      int32_t gridRaw = toS32(regs[idx(30005)], regs[idx(30005) + 1]);
      uint16_t socRaw = regs[idx(30014)];
      int32_t pvRaw   = toS32(regs[idx(30035)], regs[idx(30035) + 1]);
      int32_t essRaw  = toS32(regs[idx(30037)], regs[idx(30037) + 1]);

      updateUi(gridRaw / 1000.0f, socRaw / 10.0f, pvRaw / 1000.0f, essRaw / 1000.0f);
      // setPowerLed(gridRaw / 1000.0f, pvRaw / 1000.0f, essRaw / 1000.0f);

      nextActionAt = millis() + POLL_INTERVAL_MS;
      break;
    }
  }
}
