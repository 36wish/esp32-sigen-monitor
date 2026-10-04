#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include "plant.h"  // PlantReading

// The dashboard screen. buildUi() builds it once; everything else changes it
// in place. Also runs the Charge and Free hour modes: button handlers and the
// free-hour countdown, which tell the plant through plant.h's system hooks.

void buildUi();

// Connection status. Before the first reading it's a full-screen message;
// after that the last readings stay on screen and only a spinner shows.
void showStatus(const String &text);
void showPlantIp(IPAddress ip);

// New live values from the plant.
void showReading(const PlantReading &r);

// The Charge button: set from the plant's Backup SOC, or by a tap.
void showChargeOn(bool on);
bool chargeButtonOn();

// Free hour is over but the plant refused the remote EMS release, so it's
// still charging from the grid: the Free hour button stays pressed in with a
// red badge (a tap sends the release again) and a red title shows.
void showReleasePending(bool on);

// Clock and off-peak shading; call about once a second.
void updateClock();

// Free-hour countdown; call every loop.
void updateCountdown();
