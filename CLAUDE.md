# espsolarmonitor

A wall-mounted energy dashboard for a Sigenergy home battery/solar system. An ESP32 touchscreen joins WiFi, finds the Sigenergy plant on the LAN, polls it over Modbus TCP every second, and shows house load, source mix, battery state and solar/grid/battery flows.

All firmware is in [src/main.cpp](src/main.cpp), a single file.

## Hardware

- **Sunton ESP32-3248S035R** ("Cheap Yellow Display"): 3.5" 480x320 landscape, ST7796 SPI LCD, **resistive** XPT2046 touch, ESP32-WROOM-32, CH340 USB-serial.
- **Touch calibration:** with the board definition as it is, touch comes out mirrored left-to-right in landscape and is also off by up to ~20 px. smartdisplay's `touch_calibration_data` corrects both. The correction acts on the raw portrait point before LVGL rotates it, and LVGL maps portrait (px, py) to landscape (479 − py, px).
  - **On-screen 3-point calibration:** hold the clock area (top right) for 5 s, or send `cal` over serial. Press and hold the centre of each of the 3 crosses; a 4th cross checks the fit (it has to land within 12 px). The result is saved in NVS (`tcal`) and loaded at boot. Serial logs each raw point and the final coefficients.
  - `TOUCH_CAL_FALLBACK` (settings block) is only a fallback for when nothing has been saved yet. They came from one of these calibrations, so update them if the board's touch drifts.
- **Light sensor:** a CdS LDR on GPIO34 drives the auto-brightness. In a fully dark room (`CDS_OFF_RAW`), with no touch for `SCREEN_OFF_IDLE_MS`, the backlight turns off completely. The ESP keeps running and polling. The screen comes back on when the room gets brighter (`CDS_ON_RAW`) or when it is touched. A `wakeGuard` object on `lv_layer_top()` absorbs the tap that wakes it.
- **Status LED:** the RGB LED is active-low and only switches on/off.
- **Board definition:** [boards/esp32-3248S035R.json](boards/esp32-3248S035R.json) is pinned to the commit that matches `esp32_smartdisplay@2.1.1`. Upgrade the two together or not at all.

## Build / flash

`pio` isn't on PATH. Use `~/.platformio/penv/Scripts/pio.exe` (Git Bash) or `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe` (PowerShell).

```sh
pio run                  # build
pio run -t upload        # flash: hold BOOT, tap RESET first (see below)
```

- **First build:** copy `include/secrets.h.example` to `include/secrets.h` (gitignored) and set `WIFI_SSID` and `WIFI_PASSWORD` there. All other user settings (`TZ_INFO`, off-peak hours, Charge SOCs, brightness thresholds, `CAL_HOLD_MS`, `TOUCH_CAL_FALLBACK`) are in the settings block at the top of `main.cpp`.
- **Uploading:** this board's CH340 auto-reset can't reliably enter download mode by itself. The user has to hold BOOT and tap RESET, otherwise you get "Wrong boot mode detected (0x13)". A normal reboot afterwards (an RTS pulse, DTR low) works fine.
- **Serial monitor:** `pio device monitor` needs an interactive terminal. To read serial output from a script, use `pyserial` at 115200.

## Build gotchas (keep these in platformio.ini)

- **`-Iinclude` is required.** PlatformIO doesn't pass the project's `include/` to `lib_deps` builds. Without it, LVGL silently ignores [include/lv_conf.h](include/lv_conf.h) and the link fails with `undefined reference to lv_font_montserrat_XX`.
- **`-D LVGL_BUFFER_PIXELS=19200` is required.** smartdisplay 2.1.1 sizes its buffer with LVGL 9's 3-byte `lv_color_t`. The default then asks for one 115 KB internal-RAM block, which fails, and the board hangs in `smartdisplay_init()` with no serial output at all. The "redefined" warning it causes is expected.

## Firmware structure (src/main.cpp)

- **App loop:** a non-blocking state machine in `loop()`, `ST_WIFI → ST_LOAD_CACHE → ST_SCAN → ST_CONNECT → ST_POLL`, with `ST_WAIT_RETRY` between failed attempts. Long operations call `waitTick()`/`pumpLvgl()` so touch and redraws stay responsive. Don't add blocking delays.
- **Finding the plant:** first it tries the IP cached in NVS (`Preferences` namespace `sigmon`, opened in `setup()`; key `ip`). If that fails, it scans the /24 subnet in parallel for port 502 and checks each candidate with a Modbus read.
- **Modbus client:** a minimal hand-written Modbus TCP client (slave 247): reads with FC 0x03 (`readPlantRegs()` falls back to 0x04 for the plant's RW registers), writes single registers with FC 0x06, and writes U32s with FC 0x10 (`modbusWriteU32()`, high word first). Each poll reads registers 30005–30038 as one block:
  - `30005` grid power (s32; positive = import)
  - `30014` battery SOC
  - `30035` PV power (s32)
  - `30037` battery power (s32; positive = charging)

  `40046` Backup SOC (u16, %×10) backs the Charge button. Free hour uses `40029` remote EMS enable, `40031` remote EMS mode and `40032` ESS max charging limit (u32, kW×1000). `ST_POLL` sends one request per turn (the plant wants ≥1 s between requests): a queued free-hour step first, then a queued Charge write, then the Backup SOC sync read (on connect and every 60 s), then the normal poll.
- **Derived values:** `updateUi()` calculates house load as `pv + grid - ess`.
- **UI:** `buildUi()` builds the screen once, and `updateUi()`/`updateClock()` change it in place.
  - Everything is **absolute pixel coordinates** on 480x320. Layout constants (`BATT_*`, `MIX_*`, `HERO_*`, `TILE_*`, `MODE_BTN_*`) and the palette (`COL_*`) are at the top of the file.
  - Icons are drawn from LVGL lines and boxes (`make*Icon`) rather than images.
  - **Top bar:** the clock is sized to its text and right-anchored. `layoutTopBar()` runs whenever the time changes and keeps the plant IP `IP_GAP` px to the clock's left, with the reconnect spinner centred in the gap.
  - **Off-peak:** from `OFFPEAK_START_HOUR` until `OFFPEAK_END_HOUR` (21:00–07:00, the cheaper overnight rate), `updateClock()` draws the hero number in `COL_OFFPEAK` grey. Nothing else changes colour.
  - A full-screen overlay covers the dashboard while connecting or scanning. Once data has arrived, a reconnect only shows a spinner, and the last readings stay on screen.
  - **Hero:** the icon tile sits in the top-left corner and the big number (`font_hero_144`) starts at the left edge under it.
- **Modes:** the home screen has two icon-only buttons stacked in a column between the big number and the battery. Charge (thundercloud) is on top and Free hour (clock) is below it; together they span the battery's height.
  - **Charge** is a toggle that stays pressed in until it's pressed again. It's meant to hold the battery minimum at 100% before a storm. Pressed in writes Backup SOC 100% (`CHARGE_ON_SOC`), released writes 30% (`CHARGE_OFF_SOC`). Nothing is stored on the ESP: on every connect, and every 60 s after that (`CHARGE_SYNC_INTERVAL_MS`), the button is set from the plant's Backup SOC (pressed only if it's 100%), so changes made in the mySigen app show up. A read that was in flight when the button was tapped is discarded.
  - **Free hour** switches the screen into free-hour mode. This isn't a separate LVGL screen: `setFreeHourMode()` hides and shows widgets and moves the hero tile. In this mode the close button takes the top-left corner, with the hero tile and the "FREE HOUR" title to its right. The big number shows grid import, the house load takes the grid's place in the bottom strip, and a countdown runs to the top of the hour. The countdown runs in the mix bar's slot, extended across the hidden button column (`FREE_W`). When it reaches zero, or the close button is tapped, the screen goes back to home.
  - `updateUi()` checks `freeHour` to decide which values go where. The latest reading is kept in `last*Kw` so a mode switch redraws straight away.
  - **System hooks:** `applyChargeMode()` queues the Backup SOC write (`pendingReserve`) for `ST_POLL`. `applyFreeHourMode()` queues the free-hour steps (`emsStep`), which `emsTurn()` sends one per `ST_POLL` turn:
    - **Start:** read `40032`. If it isn't `0xFFFFFFFF` (no limit), write `0xFFFFFFFF`. If the plant refuses that, write the rated charge power read from `30550` on inverter slave 1 instead. Then write `40029=1` and `40031=3` (command charging, grid first). A cap step that fails is logged and skipped. If the mode write is refused, remote EMS is released again.
    - **End:** write `40029=0`, so the mySigen app's own mode (max self-consumption) takes over. The charge cap isn't restored.
    - A later call replaces whatever is still queued. A dropped link keeps the step queued until the reconnect.
    - NVS key `fhems` is set when free hour starts and cleared once remote EMS is handed back. If it's still set at boot (the ESP rebooted mid free hour), `setup()` queues the release. The protocol has no remote-EMS timeout, so this is the only thing that hands control back.

## Fonts

The built-in Montserrat sizes are enabled in `lv_conf.h` (12/14/16/22/24/32/48). There are also two custom fonts that **contain only digits plus a few symbols**:

- `font_hero_144`: `-./0-9`
- `font_num_44`: `%+-./0-9:` (the `:` is for the free-hour countdown)

If you show any other character in them (for example `:` for a countdown), regenerate the font with that glyph added. Each generated `.c` file starts with an `Opts:` line holding the exact `lv_font_conv` command. Run it with `npx lv_font_conv …` from the repo root, adding the new code point to `-r`.

## Mockups

UI changes are mocked up as PNGs before any code is written. The scripts are in [tools/mockups/](tools/mockups/) and use Python + Pillow:

- **`redesign.py`** is the shared drawing library: 4x supersampled canvas, palette, vector icons. It also holds the original A–D design concepts.
- **`final.py`** and **`preview3.py`** render the dashboard with the Montserrat TTFs from `.pio/libdeps/.../lvgl@9.2.2`, so a `pio run` must have happened at least once.
- **`stack.py`** renders the current layout at the `buildUi()` coordinates: icon-only buttons stacked beside a 108px battery. The implemented layout is "C" (`stack-C-bigger-*`) for the home screen and "C2" (`stack-C2-free-hour*`) for free hour. A, B and D are the alternatives that were rejected.

  ```sh
  cd tools/mockups && python stack.py ../../mockups/stack
  ```
- **`modes.py`** renders the previous layout, with labelled Charge/Free-hour buttons under the hero. `stack.py` reuses its widgets.
- **`mock.py`** is the earliest prototype; it produced `ui-mockup*.png`.

Output goes to [mockups/](mockups/) at 2x (960x640). When the firmware layout changes, update the coordinates in the scripts to match `buildUi()`. `preview3.py` predates the current layout: it still has the 150px battery and a labelled mix bar.
