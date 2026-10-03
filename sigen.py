#!/usr/bin/env python3
"""
Sigenergy quick settings over local Modbus TCP.

    python sigen.py A       Battery reserve (Backup SOC) -> 100%
    python sigen.py B       Battery reserve (Backup SOC) -> 30%
    python sigen.py C       Maximum self consumption
    python sigen.py D       Charge battery from grid at full power (house runs from grid)

    python sigen.py status  Show current settings
    python sigen.py off     Turn off remote control; the mySigen app's own mode takes over again

A and B only change the reserve. C and D only change the operating mode.
So you can combine them, e.g. run D and then A.

Set INVERTER_IP below first.  Needs:  pip install pymodbus
Registers checked against Sigenergy Modbus Protocol V2.9.
"""
import sys
import time

from pymodbus.client import ModbusTcpClient

# ---- settings -------------------------------------------------------------
INVERTER_IP = "192.168.1.114"   # <-- change to your inverter / gateway IP
PORT = 502
RESERVE_A = 100                # % for setting A
RESERVE_B = 30                 # % for setting B
MAX_CHARGE_KW = None           # Only used if the inverter refuses "no limit" for the
                               # charge rate. None = read rated power from inverter 1;
                               # set a number (e.g. 10) if that fails.
# ---------------------------------------------------------------------------

PLANT = 247        # plant (whole-system) address
INVERTER = 1       # first inverter's address (used only to read rated charge power)

# Plant registers
EMS_WORK_MODE = 30003          # RO  7 = Remote EMS
REMOTE_EMS_ENABLE = 40029      # RW  0 off / 1 on
REMOTE_EMS_MODE = 40031        # RW  2 = max self consumption, 3 = charge from grid first
ESS_MAX_CHARGE = 40032         # RW  U32 kW*1000, applies in every mode
NO_LIMIT = 0xFFFFFFFF          # value meaning "no limit set" (shows as 4294967.29 kW)
BACKUP_SOC = 40046             # RW  U16 %*10
# Inverter register
RATED_CHARGE = 30550           # RO  U32 kW*1000  [ESS] rated charge power

WORK_MODES = {0: "Max self consumption", 1: "AI", 2: "TOU", 5: "Full feed-in",
              7: "Remote EMS", 9: "Custom"}
REMOTE_MODES = {0: "PCS remote control", 1: "Standby", 2: "Max self consumption",
                3: "Charge from grid (grid first)", 4: "Charge (PV first)",
                5: "Discharge (PV first)", 6: "Discharge (ESS first)", 8: "V2G"}


def call(func, *args, unit, **kw):
    """pymodbus renamed slave= to device_id= in 3.10; support both."""
    try:
        rr = func(*args, device_id=unit, **kw)
    except TypeError:
        rr = func(*args, slave=unit, **kw)
    time.sleep(1)  # spec: at least 1 s between requests
    return rr


def ok(rr):
    return rr is not None and not rr.isError()


def need(rr, what):
    if not ok(rr):
        sys.exit(f"Failed: {what} ({rr})")
    return rr


def read_holding(c, reg, n):
    rr = call(c.read_holding_registers, reg, count=n, unit=PLANT)
    if not ok(rr):  # spec lists 0x04 for RW registers; fall back to it
        rr = call(c.read_input_registers, reg, count=n, unit=PLANT)
    return need(rr, f"read {reg}").registers


def u32(regs):
    return (regs[0] << 16) | regs[1]


def write16(c, reg, val):
    need(call(c.write_register, reg, val, unit=PLANT), f"write {reg}")


def write32(c, reg, val):
    need(call(c.write_registers, reg, [(val >> 16) & 0xFFFF, val & 0xFFFF], unit=PLANT),
         f"write {reg}")


def full_charge_kw(c):
    if MAX_CHARGE_KW is not None:
        return float(MAX_CHARGE_KW)
    rr = call(c.read_input_registers, RATED_CHARGE, count=2, unit=INVERTER)
    if ok(rr) and u32(rr.registers) > 0:
        return u32(rr.registers) / 1000
    sys.exit("Couldn't read the battery's rated charge power. "
             "Set MAX_CHARGE_KW at the top of the script and try again.")


def uncap_charging(c):
    """Make sure no old charge-rate cap is left in 40032. 0xFFFFFFFF = no limit."""
    if u32(read_holding(c, ESS_MAX_CHARGE, 2)) == NO_LIMIT:
        return  # already unlimited (factory default) - leave it alone
    rr = call(c.write_registers, ESS_MAX_CHARGE, [0xFFFF, 0xFFFF], unit=PLANT)
    if not ok(rr):  # inverter refused "no limit"; use the rated maximum instead
        write32(c, ESS_MAX_CHARGE, int(round(full_charge_kw(c) * 1000)))


def set_mode(c, mode):
    uncap_charging(c)
    write16(c, REMOTE_EMS_ENABLE, 1)
    write16(c, REMOTE_EMS_MODE, mode)


def status(c):
    wm = need(call(c.read_input_registers, EMS_WORK_MODE, count=1, unit=PLANT),
              "read 30003").registers[0]
    en, _, mode = read_holding(c, REMOTE_EMS_ENABLE, 3)
    raw = u32(read_holding(c, ESS_MAX_CHARGE, 2))
    lim = "no limit" if raw == NO_LIMIT else f"{raw / 1000:.2f} kW"
    soc = read_holding(c, BACKUP_SOC, 1)[0] / 10
    print(f"Operating mode : {WORK_MODES.get(wm, wm)}"
          + (f" -> {REMOTE_MODES.get(mode, mode)}" if en else ""))
    print(f"Battery reserve: {soc:.0f}%")
    print(f"Max charge     : {lim}")


def main():
    cmd = (sys.argv[1] if len(sys.argv) > 1 else "").upper()
    if cmd not in {"A", "B", "C", "D", "STATUS", "OFF"}:
        sys.exit(__doc__)

    c = ModbusTcpClient(INVERTER_IP, port=PORT, timeout=5)
    if not c.connect():
        sys.exit(f"Could not connect to {INVERTER_IP}:{PORT}")
    try:
        if cmd == "A":
            write16(c, BACKUP_SOC, RESERVE_A * 10)
        elif cmd == "B":
            write16(c, BACKUP_SOC, RESERVE_B * 10)
        elif cmd == "C":
            set_mode(c, 2)
        elif cmd == "D":
            set_mode(c, 3)
        elif cmd == "OFF":
            write16(c, REMOTE_EMS_ENABLE, 0)
        status(c)
    finally:
        c.close()


if __name__ == "__main__":
    main()
