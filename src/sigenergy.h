#pragma once

#include <stdint.h>

// Sigenergy Modbus TCP register map (Sigenergy Modbus Protocol V2.9).

static const uint16_t MODBUS_PORT       = 502;
static const uint8_t  PLANT_SLAVE_ID    = 247;  // the plant (whole system)
static const uint8_t  INVERTER_SLAVE_ID = 1;    // first inverter

// The plant wants at least this long between requests.
static const uint32_t POLL_INTERVAL_MS = 1000;

// Live values, read as one block every poll.
static const uint16_t GRID_POWER_REG  = 30005;  // S32, W; positive = importing
static const uint16_t MAX_ACTIVE_REG  = 30010;  // U32, W; the plant's rated power (discovery checks it)
static const uint16_t BATTERY_SOC_REG = 30014;  // U16, %*10
static const uint16_t PV_POWER_REG    = 30035;  // S32, W
static const uint16_t ESS_POWER_REG   = 30037;  // S32, W; positive = charging
static const uint16_t LIVE_BLOCK_START = GRID_POWER_REG;
static const uint16_t LIVE_BLOCK_COUNT = ESS_POWER_REG + 2 - LIVE_BLOCK_START;  // 30005 .. 30038

// Charge button
static const uint16_t BACKUP_SOC_REG = 40046;  // RW U16, %*10

// Free hour (remote EMS, Appendix 6)
static const uint16_t REMOTE_EMS_ENABLE_REG = 40029;      // RW U16, 0 off (app's own mode) / 1 on
static const uint16_t REMOTE_EMS_MODE_REG   = 40031;      // RW U16, used while remote EMS is on
static const uint16_t EMS_MODE_GRID_CHARGE  = 3;          // command charging, grid power first
static const uint16_t ESS_MAX_CHARGE_REG    = 40032;      // RW U32, kW*1000, applies in every mode
static const uint32_t NO_CHARGE_LIMIT       = 0xFFFFFFFF; // factory default: no cap
static const uint16_t RATED_CHARGE_REG      = 30550;      // RO U32, kW*1000, on the inverter
