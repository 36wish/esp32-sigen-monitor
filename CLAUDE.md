# espsolarmonitor

A wall-mounted energy dashboard for a Sigenergy home battery/solar system. An ESP32 touchscreen joins WiFi, finds the Sigenergy plant on the LAN, polls it over Modbus TCP every second, and shows house load, source mix, battery state and solar/grid/battery flows.

The firmware is in [src/](src/), one module per concern (see [Firmware structure](#firmware-structure)). User settings are in [include/settings.h](include/settings.h).

## Hardware

- **Sunton ESP32-3248S035R** ("Cheap Yellow Display"): 3.5" 480x320 landscape, ST7796 SPI LCD, **resistive** XPT2046 touch, ESP32-WROOM-32, CH340 USB-serial.
- **Touch calibration:** with the board definition as it is, touch comes out mirrored left-to-right in landscape and is also off by up to ~20 px. smartdisplay's `touch_calibration_data` corrects both. The correction acts on the raw portrait point before LVGL rotates it, and LVGL maps portrait (px, py) to landscape (479 − py, px).
  - **On-screen 3-point calibration:** hold the clock area (top right) for 5 s, or send `cal` over serial. Press and hold the centre of each of the 3 crosses; a 4th cross checks the fit (it has to land within 12 px). The result is saved in NVS (`tcal`) and loaded at boot. Serial logs each raw point and the final coefficients.
  - `TOUCH_CAL_FALLBACK` (settings.h) is only a fallback for when nothing has been saved yet. They came from one of these calibrations, so update them if the board's touch drifts.
- **Light sensor:** a CdS LDR on GPIO34 drives the auto-brightness. In a fully dark room (`CDS_OFF_RAW`), with no touch for `SCREEN_OFF_IDLE_MS`, the backlight turns off completely. The ESP keeps running and polling. The screen comes back on when the room gets brighter (`CDS_ON_RAW`) or when it is touched. A `wakeGuard` object on `lv_layer_top()` absorbs the tap that wakes it. The thresholds are in settings.h, the logic in display.cpp.
- **Status LED:** the RGB LED is active-low and only switches on/off. status_led.cpp drives it (connection state, then power flow) only when `STATUS_LED` is set in settings.h; otherwise it stays unlit.
- **Board definition:** [boards/esp32-3248S035R.json](boards/esp32-3248S035R.json) is pinned to the commit that matches `esp32_smartdisplay@2.1.1`. Upgrade the two together, along with the pinned platform (see [Build gotchas](#build-gotchas-keep-these-in-platformioini)), or not at all.

## Build / flash

`pio` isn't on PATH. Use `~/.platformio/penv/Scripts/pio.exe` (Git Bash) or `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe` (PowerShell).

```sh
pio run                  # build
pio run -t upload        # flash: hold BOOT, tap RESET first (see below)
```

- **First build:** copy `include/secrets.h.example` to `include/secrets.h` (gitignored) and set `WIFI_SSID` and `WIFI_PASSWORD` there. All other user settings (`TZ_INFO`, off-peak hours, Charge SOCs, `FREE_HOUR_CHARGE_PCT`, brightness and screen-off thresholds, `CAL_HOLD_MS`, `TOUCH_CAL_FALLBACK`, `STATUS_LED`) are in `include/settings.h`.
- **Uploading:** this board's CH340 auto-reset can't reliably enter download mode by itself. The user has to hold BOOT and tap RESET, otherwise you get "Wrong boot mode detected (0x13)". A normal reboot afterwards (an RTS pulse, DTR low) works fine.
- **Serial monitor:** `pio device monitor` needs an interactive terminal. To read serial output from a script, use `pyserial` at 115200.

## Build gotchas (keep these in platformio.ini)

- **`-Iinclude` is required.** PlatformIO doesn't pass the project's `include/` to `lib_deps` builds. Without it, LVGL silently ignores [include/lv_conf.h](include/lv_conf.h) and the link fails with `undefined reference to lv_font_montserrat_XX`.
- **`-D LVGL_BUFFER_PIXELS=19200` is required.** smartdisplay 2.1.1 sizes its buffer with LVGL 9's 3-byte `lv_color_t`. The default then asks for one 115 KB internal-RAM block, which fails, and the board hangs in `smartdisplay_init()` with no serial output at all. The "redefined" warning it causes is expected.
- **The platform is pinned to `espressif32@7.1.3`.** That's Arduino-ESP32 2.0.17 on ESP-IDF 4.4.7, the core smartdisplay 2.1.1 and the board definition are known to build with. Unpinned, a fresh clone would build against whatever platform is newest.
- **`-Ofast` turns on `-ffinite-math-only`.** GCC then compiles `isfinite()`/`isnan()` to constants, so they can't catch a division by zero. Check the divisor before dividing instead, as `rawPointsArea2()` in touch_calibration.cpp does.

## Firmware structure

| File (in `src/`) | Contents |
|---|---|
| `main.cpp` | `setup()`, and the connection state machine in `loop()` |
| `discovery.cpp` | finding the plant: cached IP, /24 scan, non-blocking port check |
| `plant.cpp` | the connected plant: `plantTurn()` (one request per turn) and the system hooks |
| `modbus_tcp.cpp` | the Modbus TCP client |
| `sigenergy.h` | register map |
| `dashboard.cpp` | the screen: layout constants, `buildUi()`, `updateUi()`, clock, status overlay, Charge/Free hour views and button handlers |
| `widgets.cpp` | LVGL building blocks (`makeBox()`, `makeButton()`, …) and the vector icons |
| `theme.h` | palette (`COL_*`) and the custom font declarations |
| `display.cpp` | LVGL pump (`pumpLvgl()`/`waitTick()`), auto-brightness, screen off in the dark |
| `touch_calibration.cpp` | on-screen touch calibration |
| `status_led.cpp` | RGB status LED (off unless `STATUS_LED`) |
| `storage.h` | NVS keys |

- **App loop:** a non-blocking state machine in `loop()`, `ST_WIFI → ST_LOAD_CACHE → ST_SCAN → ST_CONNECT → ST_POLL`, with `ST_WAIT_RETRY` between failed attempts. Long operations call `waitTick()`/`pumpLvgl()` so touch and redraws stay responsive. Don't add blocking delays.
- **Finding the plant:** first it tries the IP cached in NVS (`Preferences` namespace `sigmon`, opened in `setup()`; key `ip`; every key is listed in `storage.h`). If that fails, it scans the /24 subnet in parallel for port 502. Each candidate, the cached IP included, has to answer a read of the live block in a way that looks like a Sigenergy plant: a non-zero rated power (`30010`, u32 W) and an SOC of at most 100%. Any Modbus device can answer the read, and whatever is accepted gets the Charge and Free hour writes, so `probePlant()` logs the candidates it turns down.
- **Modbus client:** a minimal hand-written Modbus TCP client (slave 247): reads with FC 0x03 (`readPlantRegs()` falls back to 0x04 for the plant's RW registers), writes single registers with FC 0x06, and can write U32s with FC 0x10 (`modbusWriteU32()`, high word first; nothing uses it at the moment). Each request gets a new transaction ID. A reply has to echo it and the unit ID, carry protocol ID 0, and have an MBAP length that fits the reply; anything else is `MB_FAIL`, which drops the link. Each poll reads registers 30005–30038 as one block:
  - `30005` grid power (s32; positive = import)
  - `30014` battery SOC
  - `30035` PV power (s32)
  - `30037` battery power (s32; positive = charging)

  `40046` Backup SOC (u16, %×10) backs the Charge button. Free hour uses `40029` remote EMS enable, `40031` remote EMS mode and `40005` active power percentage (s16, %×100 of rated; negative = charge from AC). The plant refuses writes to `40001`–`40045` (exception 2) unless remote EMS is already on. It refuses `40001` (exception 3) even then, and `40032`/`40034` won't take `0xFFFFFFFF` back. The README's Modbus section has the live test results. `ST_POLL` calls `plantTurn()`, which sends one request per turn (the plant wants ≥1 s between requests): a queued free-hour step first, then a queued Charge write, then a free-hour energy read, then the Backup SOC sync read (on connect and every 60 s), then the normal poll.
  - The free-hour energy comes from the plant's lifetime counters, U64 kWh×100, read as one block `30200`–`30231`: `30200` charged into the battery, `30216` imported from the grid, `30228` house load (every source). They step by 0.01 kWh about every 5 s. `readFreeHourEnergy()` reads them when free hour starts (`ENERGY_START`), every 30 s while it runs (`ENERGY_RUNNING`) and once at the end (`ENERGY_END`), and subtracts the start values as raw integers: a float can't hold a lifetime total to 0.01 kWh. A reply that arrives after free hour has started or ended since is dropped.
- **Derived values:** `updateUi()` calculates house load as `pv + grid - ess`.
- **UI:** `buildUi()` builds the screen once, one `build*()` per region in back-to-front order (later widgets draw on top), and `updateUi()`/`updateClock()` change it in place.
  - Everything is **absolute pixel coordinates** on 480x320. Layout constants (`BATT_*`, `MIX_*`, `HERO_*`, `TILE_*`, `MODE_BTN_*`) are at the top of dashboard.cpp, the palette (`COL_*`) in theme.h.
  - Icons are drawn from LVGL lines and boxes (`make*Icon` in widgets.cpp) rather than images.
  - **Top bar:** the clock is sized to its text and right-anchored. `layoutTopBar()` runs whenever the time changes and keeps the plant IP `IP_GAP` px to the clock's left, with the reconnect spinner centred in the gap.
  - **Off-peak:** from `OFFPEAK_START_HOUR` until `OFFPEAK_END_HOUR` (21:00–07:00, the cheaper overnight rate), `updateClock()` draws the hero number in `COL_OFFPEAK` grey. Nothing else changes colour.
  - A full-screen overlay covers the dashboard while connecting or scanning. Once data has arrived, a reconnect only shows a spinner, and the last readings stay on screen.
  - **Hero:** the icon tile sits in the top-left corner and the big number (`font_hero_144`) starts at the left edge under it.
  - **Flow glow:** soft light bands travel through the battery fill: up while charging, down while discharging. Speed is `GLOW_SPEED_BASE + GLOW_SPEED_PER_KW·|kW|` px/s, capped at `GLOW_SPEED_MAX`, and the bands stop below `IDLE_KW`. Each band is two white gradient boxes inside `battFill`: transparent up to the peak, then back to transparent. `clip_corner` keeps them inside the rounded fill. Every fill colour gets the same band: 34 px high, peaking at 65% (`GLOW_W`, `GLOW_PEAK`). `glowTick()`, an LVGL timer, moves the bands and stops while the screen is off (`screenIsOff()`). It's the "glow++" mockup, applied to green as well.
- **Modes:** the home screen has two icon-only buttons stacked in a column between the big number and the battery. Charge (thundercloud) is on top and Free hour (clock) is below it; together they span the battery's height.
  - **Charge** is a toggle that stays pressed in until it's pressed again. It's meant to hold the battery minimum at 100% before a storm. Pressed in writes Backup SOC 100% (`CHARGE_ON_SOC`), released writes 30% (`CHARGE_OFF_SOC`). Nothing is stored on the ESP: on every connect, and every 60 s after that (`CHARGE_SYNC_INTERVAL_MS`), the button is set from the plant's Backup SOC (pressed only if it's 100%), so changes made in the mySigen app show up. A read that was in flight when the button was tapped is discarded; a tap while a write is in flight is sent after it.
  - **Free hour** switches the screen into free-hour mode. This isn't a separate LVGL screen: `setFreeHourMode()` hides and shows widgets and moves the hero tile. In this mode the close button takes the top-left corner, with the hero tile and the "FREE HOUR" title to its right. The big number shows grid import, the house load takes the grid's place in the bottom strip, and a countdown runs to the top of the hour. The countdown row is the mix bar's slot, extended across the hidden button column (`FREE_W`): a grid tile and the kWh imported so far (`showFreeHourEnergy()`, "-.-" until the start counters are in) on the left, the countdown on the right. When it reaches zero, or the close button is tapped, the screen goes back to home.
  - **Free hour summary:** once the end counters are read, `showFreeHourSummary()` opens a card over the home screen: the kWh from the grid, the start and end times (hh:mm, recorded in `onFreeHourOpen()`/`endFreeHour()`), and how much went into the battery and the house. A tap anywhere closes it. It only uses fonts the screen already links (`font_num_44`, Montserrat 14/16/24): flash is at ~88%, and Montserrat 48 alone added ~95 KB.
  - `updateUi()` checks `freeHour` to decide which values go where. The latest reading is kept in `reading` so a mode switch redraws straight away.
  - **System hooks** (plant.cpp): `applyChargeMode()` queues the Backup SOC write (`pendingReserve`) for `plantTurn()`. `applyFreeHourMode()` queues the free-hour steps (`emsStep`), which `emsTurn()` sends one per turn:
    - **Start:** write `40029=1`, then `40031=0` (PCS remote control), then `40005=-FREE_HOUR_CHARGE_PCT*100`. The inverter then takes a fixed share of its rated power from the grid into the battery, so the battery can't discharge and the house runs on the grid. Don't go back to mode 3 (command charging, grid first): on this plant it holds grid import at about 4 kW, and the battery covers any house load above that. If the mode or setpoint write is refused, remote EMS is released again.
    - **End:** write `40005=0` (only accepted while remote EMS is still on; a refusal is just logged), then `40029=0`, so the mySigen app's own mode (max self-consumption) takes over. If the plant refuses the release, it's sent again every 60 s (`EMS_RELEASE_RETRY_MS`) until it's accepted. Until then the plant goes on charging from the grid, so `showReleasePending()` shows it on the home screen: the Free hour button stays pressed in (`LV_STATE_CHECKED`, but not checkable) with a red badge, and "STILL GRID CHARGING" appears beside the hero tile. While it's pending, a tap on the Free hour button sends the release straight away instead of opening free hour.
    - A later call replaces whatever is still queued, even one made while a step's request is in flight (requests keep LVGL running, so taps are handled mid-request). A dropped link keeps the step queued until the reconnect.
    - NVS key `fhems` is set when free hour starts and cleared once remote EMS is handed back. If it's still set at boot (the ESP rebooted mid free hour), `plantBegin()` queues the release. The protocol has no remote-EMS timeout, so this is the only thing that hands control back.

## The Sigenergy plant

- **Spec:** the register spec is `Sigenergy Modbus Protocol EN_V2.9.pdf` in the repo root. `pdftotext -layout` (in Git Bash) turns it into searchable text.
- **The spec isn't the last word:** the plant often behaves differently from what the spec says (see the free-hour notes above and the README's "Free hour" section). Check a control change against the live plant before relying on it.
- **Probing:** the plant is reachable from the dev PC. Probe it with a short raw-socket Python script, at least 1 s between requests, because the ESP is polling it too.
  - Reads are always safe.
  - Writes change how the house battery behaves. Ask the user first.
  - Before a write, record the original values, and put them back in a `finally` block.
  - Read each value back after writing it. A register won't always accept its original value again.
- **Logging:** to see what a mode really does, log grid, PV, battery and plant active power (`30031`) together with the control registers while the user switches modes.

### Free Hour Mode

These register choices came from testing on a 5 kW SigenStor system (October 2026):

- **The obvious mode doesn't work.** Mode 3, "command charging (grid first)", held grid import at a fixed 4 kW. When the house used more than that, the battery discharged to cover the rest.
- **PCS remote control does what's wanted.** Mode 0 with 40005 = −100% made the inverter take a fixed 3.75 kW from the grid into the battery. The house ran entirely from the grid, whatever its load.
- **The power and limit registers (40001–40045) only accept writes while remote EMS is on.** With 40029 = 0, writes are refused with exception 2. That's why 40029 is written first.
- **40001 is refused.** The fixed active power setpoint (40001, kW) is refused with exception 3 even when remote EMS is on. The percentage register (40005) works.
- **40032 and 40034 don't accept "no limit" back.** The ESS max charging and discharging limits read `0xFFFFFFFF` (no limit) by default, but won't take that value as a write. Only values from 0 up to the rated power are accepted, so the firmware doesn't touch them.
- **The app's "grid only" mode isn't remote EMS.** It switches the system to its Custom mode (30003 = 9) with Backup SOC at 100%. The battery then sits idle rather than charging.

The built-in Montserrat sizes are enabled in `lv_conf.h` (12/14/16/22/24/32/48). There are also two custom fonts that **contain only digits plus a few symbols**:

- `font_hero_144`: `-./0-9`
- `font_num_44`: `%+-./0-9:` (the `:` is for the free-hour countdown)

If you show any other character in them (for example `:` for a countdown), regenerate the font with that glyph added. Each generated `.c` file starts with an `Opts:` line holding the exact `lv_font_conv` command. Run it with `npx lv_font_conv …` from the repo root, adding the new code point to `-r`.

## Mockups

UI changes are mocked up as PNGs before any code is written. The scripts are in [tools/mockups/](tools/mockups/) and use Python + Pillow:

- **`draw.py`** is the shared drawing library: 4x supersampled canvas, palette, vector icons.
- **`dashboard.py`** renders the current layout at the `buildUi()` coordinates (its layout constants mirror the ones at the top of dashboard.cpp): home, Charge on, off-peak, widest hero value, free hour, free hour summary. It uses the Montserrat TTFs from `.pio/libdeps/.../lvgl@9.2.2`, so a `pio run` must have happened at least once.

  ```sh
  cd tools/mockups && python dashboard.py ../../mockups/dashboard
  ```
- **`pending.py`** renders the pending-release alternatives on top of `dashboard.py`. The implemented look is A (button pressed in with a red badge) and B (red title) together; C (a banner in the mix bar's slot) was rejected.

- **`battery_wave.py`** renders animated WebPs of an animation in the battery fill: up while charging, down while discharging, speed from the power. There are three looks: A band, B chevrons, C ripples. It draws through the `fill_fx` hook on `dashboard.battery()`.
- **`band_variants.py`** has variations on look A: glint, glow, sawtooth, swell, sweep and sheen. Each one is a brightness profile behind the band's crest, with an optional tilt or single sweep.
- **`free_kwh.py`** renders the ways of showing a free hour's kWh. During it: A in the header, B beside the hero, C in the countdown row. After it: D a summary card, E a recap beside the hero tile. C and D are implemented (drawn by `dashboard.py`); A, B and E were rejected.
- **`glow_variants.py`** makes the glow stronger on the lighter yellow (20–49%) and red (<20%) fills, in three strengths: glow+, glow++ and glow+ edge. Green keeps the plain glow. glow++ is the one implemented (see Flow glow above).

Output goes to [mockups/](mockups/) at 2x (960x640), plus an `overview.png` contact sheet. When the firmware layout changes, update the constants in `dashboard.py` to match.
