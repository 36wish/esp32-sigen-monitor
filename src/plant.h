#pragma once

#include <WiFi.h>

// The Modbus link to the plant once it's been found. Each turn sends one
// request: a queued Free hour step or Charge write first, then the Charge
// button sync, otherwise the live poll, which goes to the dashboard.

// One poll's worth of live values.
struct PlantReading {
  float gridKw;  // positive = importing
  float socPct;
  float pvKw;
  float essKw;   // battery; positive = charging
};

// Flows smaller than this (kW) count as idle.
static const float IDLE_KW = 0.05f;

// Call once at boot, after prefs.begin().
void plantBegin();

// Opens the link. LVGL keeps running while the port is checked.
bool plantConnect(IPAddress ip);

// Sends the one request that's due; call it every POLL_INTERVAL_MS. Returns
// false if the link failed: it's been closed, and anything queued is sent
// after the next plantConnect().
bool plantTurn();

// ── system hooks: where the plant gets told about a mode change ──

// Requests keep LVGL running while they wait, so these can be called while one
// is out. A later call always wins.

// Charge pressed in (on) or released (off): queues the Backup SOC write.
void applyChargeMode(bool on);

// Free hour started (on) or ended (off: close button, countdown reached 0, or
// a tap on the Free hour button while a release is pending): queues the remote
// EMS steps, replacing whatever is still queued.
void applyFreeHourMode(bool on);
