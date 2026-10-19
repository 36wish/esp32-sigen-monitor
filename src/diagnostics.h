#pragma once

#include <Arduino.h>

// Connection diagnostics on the serial port: WiFi disconnects with their
// reason code, a status line on link events, and a record of the last
// self-restart kept in NVS, so a failure can still be looked into after the
// ESP has restarted itself ("diag" on the serial port prints it).

// Call once at boot, after prefs.begin() and before WiFi.begin(): logs why
// the ESP last reset and the last self-restart, and hooks WiFi disconnects.
void diagnosticsBegin();

// Logs `event` with the time, uptime, WiFi state and free heap.
void logStatus(const String &event);

// The "diag" serial command: logStatus(`event`), the boot reset reason and the
// last self-restart record.
void printDiagnostics(const String &event);

// Saves `why` with the status line to NVS, then restarts the ESP.
void restartWithRecord(const String &why);
