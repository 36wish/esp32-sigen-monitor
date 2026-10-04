#pragma once

#include <WiFi.h>

// Finding the plant on the LAN: first the address saved in NVS, then a scan of
// the /24 the ESP32 is on. Progress shows on the status overlay.

// Attempts at a known address before falling back to a scan: right after WiFi
// associates, the first ARP/TCP exchange is often slow.
static const int CACHE_ATTEMPTS = 4;

// Tries the address saved in NVS. True (and `ip` set) if the plant answered.
bool findCachedPlant(IPAddress &ip);

// Scans the ESP32's own /24 for the plant. On success sets `ip` and saves it
// in NVS for next boot.
bool scanForPlant(IPAddress &ip);

// True if `ip` accepts a connection on the Modbus port. LVGL keeps running
// while it waits.
bool modbusPortOpen(IPAddress ip, uint32_t timeoutMs);
