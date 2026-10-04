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

- **First build:** copy `include/secrets.h.example` to `include/secrets.h` (gitignored) and set `WIFI_SSID` and `WIFI_PASSWORD` there. All other user settings (`TZ_INFO`, off-peak hours, Charge SOCs, brightness and screen-off thresholds, `CAL_HOLD_MS`, `TOUCH_CAL_FALLBACK`, `STATUS_LED`) are in `include/settings.h`.
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
- **Modbus client:** a minimal hand-written Modbus TCP client (slave 247): reads with FC 0x03 (`readPlantRegs()` falls back to 0x04 for the plant's RW registers), writes single registers with FC 0x06, and writes U32s with FC 0x10 (`modbusWriteU32()`, high word first). Each request gets a new transaction ID. A reply has to echo it and the unit ID, carry protocol ID 0, and have an MBAP length that fits the reply; anything else is `MB_FAIL`, which drops the link. Each poll reads registers 30005–30038 as one block:
  - `30005` grid power (s32; positive = import)
  - `30014` battery SOC
  - `30035` PV power (s32)
  - `30037` battery power (s32; positive = charging)

  `40046` Backup SOC (u16, %×10) backs the Charge button. Free hour uses `40029` remote EMS enable, `40031` remote EMS mode and `40032` ESS max charging limit (u32, kW×1000). `ST_POLL` calls `plantTurn()`, which sends one request per turn (the plant wants ≥1 s between requests): a queued free-hour step first, then a queued Charge write, then the Backup SOC sync read (on connect and every 60 s), then the normal poll.
- **Derived values:** `updateUi()` calculates house load as `pv + grid - ess`.
- **UI:** `buildUi()` builds the screen once, one `build*()` per region in back-to-front order (later widgets draw on top), and `updateUi()`/`updateClock()` change it in place.
  - Everything is **absolute pixel coordinates** on 480x320. Layout constants (`BATT_*`, `MIX_*`, `HERO_*`, `TILE_*`, `MODE_BTN_*`) are at the top of dashboard.cpp, the palette (`COL_*`) in theme.h.
  - Icons are drawn from LVGL lines and boxes (`make*Icon` in widgets.cpp) rather than images.
  - **Top bar:** the clock is sized to its text and right-anchored. `layoutTopBar()` runs whenever the time changes and keeps the plant IP `IP_GAP` px to the clock's left, with the reconnect spinner centred in the gap.
  - **Off-peak:** from `OFFPEAK_START_HOUR` until `OFFPEAK_END_HOUR` (21:00–07:00, the cheaper overnight rate), `updateClock()` draws the hero number in `COL_OFFPEAK` grey. Nothing else changes colour.
  - A full-screen overlay covers the dashboard while connecting or scanning. Once data has arrived, a reconnect only shows a spinner, and the last readings stay on screen.
  - **Hero:** the icon tile sits in the top-left corner and the big number (`font_hero_144`) starts at the left edge under it.
- **Modes:** the home screen has two icon-only buttons stacked in a column between the big number and the battery. Charge (thundercloud) is on top and Free hour (clock) is below it; together they span the battery's height.
  - **Charge** is a toggle that stays pressed in until it's pressed again. It's meant to hold the battery minimum at 100% before a storm. Pressed in writes Backup SOC 100% (`CHARGE_ON_SOC`), released writes 30% (`CHARGE_OFF_SOC`). Nothing is stored on the ESP: on every connect, and every 60 s after that (`CHARGE_SYNC_INTERVAL_MS`), the button is set from the plant's Backup SOC (pressed only if it's 100%), so changes made in the mySigen app show up. A read that was in flight when the button was tapped is discarded; a tap while a write is in flight is sent after it.
  - **Free hour** switches the screen into free-hour mode. This isn't a separate LVGL screen: `setFreeHourMode()` hides and shows widgets and moves the hero tile. In this mode the close button takes the top-left corner, with the hero tile and the "FREE HOUR" title to its right. The big number shows grid import, the house load takes the grid's place in the bottom strip, and a countdown runs to the top of the hour. The countdown runs in the mix bar's slot, extended across the hidden button column (`FREE_W`). When it reaches zero, or the close button is tapped, the screen goes back to home.
  - `updateUi()` checks `freeHour` to decide which values go where. The latest reading is kept in `reading` so a mode switch redraws straight away.
  - **System hooks** (plant.cpp): `applyChargeMode()` queues the Backup SOC write (`pendingReserve`) for `plantTurn()`. `applyFreeHourMode()` queues the free-hour steps (`emsStep`), which `emsTurn()` sends one per turn:
    - **Start:** read `40032`. If it isn't `0xFFFFFFFF` (no limit), write `0xFFFFFFFF`. If the plant refuses that, write the rated charge power read from `30550` on inverter slave 1 instead. Then write `40029=1` and `40031=3` (command charging, grid first). A cap step that fails is logged and skipped. If the mode write is refused, remote EMS is released again.
    - **End:** write `40029=0`, so the mySigen app's own mode (max self-consumption) takes over. The charge cap isn't restored. If the plant refuses the release, it's sent again every 60 s (`EMS_RELEASE_RETRY_MS`) until it's accepted. Until then the plant goes on charging from the grid, so `showReleasePending()` shows it on the home screen: the Free hour button stays pressed in (`LV_STATE_CHECKED`, but not checkable) with a red badge, and "STILL GRID CHARGING" appears beside the hero tile. While it's pending, a tap on the Free hour button sends the release straight away instead of opening free hour.
    - A later call replaces whatever is still queued, even one made while a step's request is in flight (requests keep LVGL running, so taps are handled mid-request). A dropped link keeps the step queued until the reconnect.
    - NVS key `fhems` is set when free hour starts and cleared once remote EMS is handed back. If it's still set at boot (the ESP rebooted mid free hour), `plantBegin()` queues the release. The protocol has no remote-EMS timeout, so this is the only thing that hands control back.

## Fonts

The built-in Montserrat sizes are enabled in `lv_conf.h` (12/14/16/22/24/32/48). There are also two custom fonts that **contain only digits plus a few symbols**:

- `font_hero_144`: `-./0-9`
- `font_num_44`: `%+-./0-9:` (the `:` is for the free-hour countdown)

If you show any other character in them (for example `:` for a countdown), regenerate the font with that glyph added. Each generated `.c` file starts with an `Opts:` line holding the exact `lv_font_conv` command. Run it with `npx lv_font_conv …` from the repo root, adding the new code point to `-r`.

## Mockups

UI changes are mocked up as PNGs before any code is written. The scripts are in [tools/mockups/](tools/mockups/) and use Python + Pillow:

- **`draw.py`** is the shared drawing library: 4x supersampled canvas, palette, vector icons.
- **`dashboard.py`** renders the current layout at the `buildUi()` coordinates (its layout constants mirror the ones at the top of dashboard.cpp): home, Charge on, off-peak, widest hero value, free hour. It uses the Montserrat TTFs from `.pio/libdeps/.../lvgl@9.2.2`, so a `pio run` must have happened at least once.

  ```sh
  cd tools/mockups && python dashboard.py ../../mockups/dashboard
  ```
- **`pending.py`** renders the pending-release alternatives on top of `dashboard.py`. The implemented look is A (button pressed in with a red badge) and B (red title) together; C (a banner in the mix bar's slot) was rejected.

Output goes to [mockups/](mockups/) at 2x (960x640), plus an `overview.png` contact sheet. When the firmware layout changes, update the constants in `dashboard.py` to match.
