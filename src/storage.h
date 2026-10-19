#pragma once

#include <Preferences.h>

// NVS storage, opened on NVS_NAMESPACE in setup(). All keys are listed here so
// they can't collide.
extern Preferences prefs;

static const char *const NVS_NAMESPACE = "sigmon";
static const char *const NVS_PLANT_IP  = "ip";     // last address the plant was found at
static const char *const NVS_TOUCH_CAL = "tcal";   // saved touch_calibration_data_t
static const char *const NVS_FREE_HOUR = "fhems";  // set while remote EMS is ours to hand back
static const char *const NVS_RESTARTS     = "rstcnt";  // how many times the ESP has restarted itself
static const char *const NVS_LAST_RESTART = "rstwhy";  // why it last did, with a status line
