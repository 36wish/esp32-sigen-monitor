#pragma once

#include <lvgl.h>

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

// ── custom fonts (src/font_*.c): digits and a few symbols only ──────────
LV_FONT_DECLARE(font_hero_144); // Montserrat SemiBold 144px, "-./0-9" only
LV_FONT_DECLARE(font_num_44);   // Montserrat SemiBold 44px, "%+-./0-9:" only
