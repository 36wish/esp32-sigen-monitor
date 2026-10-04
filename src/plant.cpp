#include "plant.h"

#include "dashboard.h"  // showReading(), showChargeOn(), chargeButtonOn(), showReleasePending()
#include "discovery.h"  // modbusPortOpen()
#include "modbus_tcp.h"
#include "settings.h"
#include "sigenergy.h"
#include "status_led.h"
#include "storage.h"

static const uint32_t CONNECT_TIMEOUT_MS = 2000;

static WiFiClient client;

// ── Charge ───────────────────────────────────────────────────────────────
// Toggle that holds the battery minimum at 100% (storm backup): pressed in =
// plant Backup SOC CHARGE_ON_SOC, released = CHARGE_OFF_SOC. Nothing is stored
// on the ESP; the button is set from the plant's Backup SOC on every connect
// and every CHARGE_SYNC_INTERVAL_MS.
static int      pendingReserve = -1;    // Backup SOC % to write to the plant, -1 = none
static bool     needChargeSync = true;  // read Backup SOC to set the Charge button
static uint32_t lastChargeSyncAt = 0;   // ...and again every CHARGE_SYNC_INTERVAL_MS

// ── Free hour ────────────────────────────────────────────────────────────
// The power company's free hour. Remote EMS is switched to "PCS remote
// control" with the inverter set to take FREE_HOUR_CHARGE_PCT of its rated
// power from the AC side, so the battery charges at a fixed rate and can't
// discharge: the house runs off the grid alone. ("Command charging, grid
// first" holds grid import at a fixed ~4 kW and lets the battery cover the
// house above that.) At the end the setpoint is zeroed and remote EMS turned
// off, so the mySigen app's own mode takes over again. NVS_FREE_HOUR stays set
// until remote EMS is handed back, so a reboot mid free hour still releases
// it: the protocol has no remote-EMS timeout. For the same reason a release
// the plant refuses is sent again every EMS_RELEASE_RETRY_MS, not left for a
// reboot that may be months away.
enum EmsStep { EMS_IDLE, EMS_ENABLE, EMS_SET_MODE, EMS_SET_POWER, EMS_ZERO_POWER, EMS_RELEASE };
static EmsStep  emsStep = EMS_IDLE;
static const uint32_t EMS_RELEASE_RETRY_MS = 60000;
static bool     retryRelease = false;   // the plant refused the release; queue it again...
static uint32_t releaseRefusedAt = 0;   // ...EMS_RELEASE_RETRY_MS after this

void plantBegin() {
  if (prefs.getBool(NVS_FREE_HOUR, false)) {
    // a free hour was running when the ESP last stopped; hand control back to the app
    Serial.println("Free hour was left on, releasing remote EMS once connected");
    emsStep = EMS_ZERO_POWER;
  }
}

bool plantConnect(IPAddress ip) {
  // check the port without blocking first, so the UI keeps animating while
  // the plant is unreachable; the real connect is then near-instant
  if (!modbusPortOpen(ip, CONNECT_TIMEOUT_MS) || !client.connect(ip, MODBUS_PORT, CONNECT_TIMEOUT_MS))
    return false;
  needChargeSync = true;  // the reserve may have changed (e.g. in the mySigen app)
  return true;
}

void applyChargeMode(bool on) {
  pendingReserve = on ? CHARGE_ON_SOC : CHARGE_OFF_SOC;
}

void applyFreeHourMode(bool on) {
  retryRelease = false;  // what this queues replaces a release waiting to be retried
  if (on) {
    prefs.putBool(NVS_FREE_HOUR, true);
    emsStep = EMS_ENABLE;
  } else {
    emsStep = EMS_ZERO_POWER;
  }
}

// Plant RW registers. The spec lists 0x04 for them but holding reads usually
// work too; try 0x03 first and fall back.
static ModbusResult readPlantRegs(uint16_t addr, uint16_t qty, uint16_t *out) {
  ModbusResult r = modbusReadRegisters(client, PLANT_SLAVE_ID, FC_READ_HOLDING, addr, qty, out);
  if (r == MB_EXCEPTION) r = modbusReadRegisters(client, PLANT_SLAVE_ID, FC_READ_INPUT, addr, qty, out);
  return r;
}

// Sends the next free-hour request and moves emsStep on. On MB_FAIL the step
// stays queued for after the reconnect. The plant refuses the power setpoint
// until remote EMS is on, hence the order.
//   start:  enable remote EMS, switch it to PCS remote control, then set the
//           charge setpoint (anything refused after the enable releases it)
//   end:    zero the setpoint (while remote EMS still takes it), then disable
//           remote EMS; if the plant refuses that, try again every
//           EMS_RELEASE_RETRY_MS
// The request keeps LVGL running while it waits, so free hour can be opened or
// closed meanwhile; the step that queues wins over this one's next step.
static ModbusResult emsTurn() {
  const EmsStep step = emsStep;
  EmsStep next = step;
  bool nothingToHandBack = false;  // clear NVS_FREE_HOUR
  bool releaseRefused = false;     // send EMS_RELEASE again later
  ModbusResult r = MB_OK;
  switch (step) {
    case EMS_IDLE:
      break;

    case EMS_ENABLE:
      r = modbusWriteRegister(client, PLANT_SLAVE_ID, REMOTE_EMS_ENABLE_REG, 1);
      if (r == MB_FAIL) break;
      if (r == MB_OK) {
        Serial.println("Remote EMS enabled");
        next = EMS_SET_MODE;
      } else {
        Serial.println("Plant refused remote EMS enable");
        nothingToHandBack = true;
        next = EMS_IDLE;
      }
      break;

    case EMS_SET_MODE:
      r = modbusWriteRegister(client, PLANT_SLAVE_ID, REMOTE_EMS_MODE_REG, EMS_MODE_PCS);
      if (r == MB_FAIL) break;
      if (r == MB_OK) {
        next = EMS_SET_POWER;
      } else {
        // don't leave remote EMS on in whatever mode it had
        Serial.println("Plant refused PCS remote control, releasing remote EMS");
        next = EMS_RELEASE;
      }
      break;

    case EMS_SET_POWER:
      r = modbusWriteRegister(client, PLANT_SLAVE_ID, ACTIVE_POWER_PCT_REG,
                              (uint16_t)(int16_t)(-FREE_HOUR_CHARGE_PCT * 100));
      if (r == MB_FAIL) break;
      if (r == MB_OK) {
        Serial.printf("Remote EMS: charging at %d%% from grid, house on grid\n", FREE_HOUR_CHARGE_PCT);
        next = EMS_IDLE;
      } else {
        Serial.println("Plant refused charge setpoint, releasing remote EMS");
        next = EMS_RELEASE;
      }
      break;

    case EMS_ZERO_POWER:
      // tidy only: the setpoint does nothing once remote EMS is off
      r = modbusWriteRegister(client, PLANT_SLAVE_ID, ACTIVE_POWER_PCT_REG, 0);
      if (r == MB_FAIL) break;
      if (r != MB_OK) Serial.println("Plant refused zeroing the charge setpoint");
      next = EMS_RELEASE;
      break;

    case EMS_RELEASE:
      r = modbusWriteRegister(client, PLANT_SLAVE_ID, REMOTE_EMS_ENABLE_REG, 0);
      if (r == MB_FAIL) break;
      if (r == MB_OK) {
        Serial.println("Remote EMS off, plant back on its own mode");
        nothingToHandBack = true;
      } else {
        Serial.printf("Plant refused remote EMS release, retrying in %lu s\n",
                      (unsigned long)(EMS_RELEASE_RETRY_MS / 1000));
        releaseRefused = true;
      }
      next = EMS_IDLE;
      break;
  }
  // Free hour opened or closed while the request was out: its step replaces
  // this sequence, and NVS_FREE_HOUR stays as that left it.
  if (emsStep != step) return r;
  emsStep = next;
  if (nothingToHandBack) {
    prefs.putBool(NVS_FREE_HOUR, false);
    showReleasePending(false);
  }
  if (releaseRefused) {
    retryRelease = true;
    releaseRefusedAt = millis();
    showReleasePending(true);  // on screen until a release goes through
  }
  return r;
}

// Sends the queued Charge write. On MB_FAIL it stays queued. The request keeps
// LVGL running while it waits; a tap meanwhile queues a new value, which goes next.
static ModbusResult writeBackupSoc() {
  const int reserve = pendingReserve;
  ModbusResult r = modbusWriteRegister(client, PLANT_SLAVE_ID, BACKUP_SOC_REG, reserve * 10);
  if (r == MB_FAIL) return r;
  if (r == MB_OK) {
    Serial.printf("Backup SOC set to %d%%\n", reserve);
    needChargeSync = false;
    lastChargeSyncAt = millis();  // the next periodic read confirms it stuck
  } else {
    Serial.printf("Plant refused Backup SOC %d%%\n", reserve);
    needChargeSync = true;  // put the button back to what the plant has
  }
  if (pendingReserve == reserve) pendingReserve = -1;
  return r;
}

// Sets the Charge button from the plant's Backup SOC (pressed in only at
// CHARGE_ON_SOC), so changes made in the mySigen app show up.
static ModbusResult syncChargeButton() {
  uint16_t socRaw;
  ModbusResult r = readPlantRegs(BACKUP_SOC_REG, 1, &socRaw);
  if (r == MB_FAIL) return r;
  if (r != MB_OK) {
    Serial.println("Plant refused Backup SOC read");
  } else if (pendingReserve >= 0) {
    // Charge was tapped while the read was out; this value predates the
    // tap, and the queued write goes next.
  } else {
    bool on = socRaw >= CHARGE_ON_SOC * 10;
    if (needChargeSync || on != chargeButtonOn()) {
      Serial.printf("Plant Backup SOC: %.0f%% -> Charge %s\n", socRaw / 10.0f, on ? "on" : "off");
    }
    showChargeOn(on);
  }
  needChargeSync = false;
  lastChargeSyncAt = millis();
  return r;
}

// Reads the live block and puts it on screen. Any failure, even a refusal,
// counts as a dropped link.
static ModbusResult readLiveValues() {
  uint16_t regs[LIVE_BLOCK_COUNT];
  if (modbusReadRegisters(client, PLANT_SLAVE_ID, FC_READ_HOLDING, LIVE_BLOCK_START, LIVE_BLOCK_COUNT,
                          regs) != MB_OK) {
    return MB_FAIL;
  }
  auto reg = [&regs](uint16_t addr) { return &regs[addr - LIVE_BLOCK_START]; };
  PlantReading reading;
  reading.gridKw = toS32(reg(GRID_POWER_REG)) / 1000.0f;
  reading.socPct = *reg(BATTERY_SOC_REG) / 10.0f;
  reading.pvKw   = toS32(reg(PV_POWER_REG)) / 1000.0f;
  reading.essKw  = toS32(reg(ESS_POWER_REG)) / 1000.0f;
  showReading(reading);
  setPowerLed(reading);
  return MB_OK;
}

bool plantTurn() {
  if (retryRelease && millis() - releaseRefusedAt >= EMS_RELEASE_RETRY_MS) {
    retryRelease = false;
    emsStep = EMS_RELEASE;
  }
  const char *what;
  ModbusResult r;
  if (emsStep != EMS_IDLE) {
    what = "Free hour request";
    r = emsTurn();
  } else if (pendingReserve >= 0) {
    what = "Backup SOC write";
    r = writeBackupSoc();
  } else if (needChargeSync || millis() - lastChargeSyncAt >= CHARGE_SYNC_INTERVAL_MS) {
    what = "Backup SOC read";
    r = syncChargeButton();
  } else {
    what = "Read";
    r = readLiveValues();
  }
  if (r != MB_FAIL) return true;

  Serial.print(what);
  Serial.println(" failed, reconnecting...");
  client.stop();
  return false;
}
