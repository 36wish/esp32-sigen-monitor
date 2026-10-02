# espsolarmonitor

A wall-mounted energy dashboard for a Sigenergy home battery/solar system. An ESP32 touchscreen joins WiFi, finds the Sigenergy plant on the LAN, polls it over Modbus TCP every second, and shows house load, source mix, battery state and solar/grid/battery flows.

All firmware is in [src/main.cpp](src/main.cpp), a single file.

## Hardware

- **Sunton ESP32-3248S035R** ("Cheap Yellow Display"): 3.5" 480x320 landscape, ST7796 SPI LCD, **resistive** XPT2046 touch, ESP32-WROOM-32, CH340 USB-serial.
- **Touch calibration:** with the board definition as it is, touch comes out mirrored left-to-right in landscape, and readings are 10–20 px off on top of that. `setup()` fixes both by setting smartdisplay's `touch_calibration_data` right after `smartdisplay_init()`. The correction acts on the raw portrait point before LVGL rotates it, so landscape X is controlled by raw Y. To re-fit it, log each tap's raw point and compare it with the button centre you aimed at.
- **Light sensor:** a CdS LDR on GPIO34 drives the auto-brightness.
- **Status LED:** the RGB LED is active-low and only switches on/off.
- **Board definition:** [boards/esp32-3248S035R.json](boards/esp32-3248S035R.json) is pinned to the commit that matches `esp32_smartdisplay@2.1.1`. Upgrade the two together or not at all.

## Build / flash

`pio` isn't on PATH. Use `~/.platformio/penv/Scripts/pio.exe` (Git Bash) or `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe` (PowerShell).

```sh
pio run                  # build
pio run -t upload        # flash: hold BOOT, tap RESET first (see below)
```

- **First build:** copy `include/secrets.h.example` to `include/secrets.h` (gitignored) and set the WiFi credentials. You can also override `TZ_INFO` there; it defaults to New Zealand.
- **Uploading:** this board's CH340 auto-reset can't reliably enter download mode by itself. The user has to hold BOOT and tap RESET, otherwise you get "Wrong boot mode detected (0x13)". A normal reboot afterwards (an RTS pulse, DTR low) works fine.
- **Serial monitor:** `pio device monitor` needs an interactive terminal. To read serial output from a script, use `pyserial` at 115200.

## Build gotchas (keep these in platformio.ini)

- **`-Iinclude` is required.** PlatformIO doesn't pass the project's `include/` to `lib_deps` builds. Without it, LVGL silently ignores [include/lv_conf.h](include/lv_conf.h) and the link fails with `undefined reference to lv_font_montserrat_XX`.
- **`-D LVGL_BUFFER_PIXELS=19200` is required.** smartdisplay 2.1.1 sizes its buffer with LVGL 9's 3-byte `lv_color_t`. The default then asks for one 115 KB internal-RAM block, which fails, and the board hangs in `smartdisplay_init()` with no serial output at all. The "redefined" warning it causes is expected.

## Firmware structure (src/main.cpp)

- **App loop:** a non-blocking state machine in `loop()`, `ST_WIFI → ST_LOAD_CACHE → ST_SCAN → ST_CONNECT → ST_POLL`, with `ST_WAIT_RETRY` between failed attempts. Long operations call `waitTick()`/`pumpLvgl()` so touch and redraws stay responsive. Don't add blocking delays.
- **Finding the plant:** first it tries the IP cached in NVS (`Preferences` namespace `sigmon`, opened in `setup()`; keys `ip` and `charge`). If that fails, it scans the /24 subnet in parallel for port 502 and checks each candidate with a Modbus read.
- **Modbus client:** a minimal hand-written Modbus TCP client that only reads (FC 0x03, slave 247). Each poll reads registers 30005–30038 as one block:
  - `30005` grid power (s32; positive = import)
  - `30014` battery SOC
  - `30035` PV power (s32)
  - `30037` battery power (s32; positive = charging)
- **Derived values:** `updateUi()` calculates house load as `pv + grid - ess`.
- **UI:** `buildUi()` builds the screen once, and `updateUi()`/`updateClock()` change it in place.
  - Everything is **absolute pixel coordinates** on 480x320. Layout constants (`BATT_*`, `MIX_*`) and the palette (`COL_*`) are at the top of the file.
  - Icons are drawn from LVGL lines and boxes (`make*Icon`) rather than images.
  - **Top bar:** the clock is sized to its text and right-anchored. `layoutTopBar()` runs whenever the time changes and keeps the plant IP `IP_GAP` px to the clock's left, with the reconnect spinner centred in the gap.
  - **Off-peak:** from `OFFPEAK_START_HOUR` until `OFFPEAK_END_HOUR` (21:00–07:00, the cheaper overnight rate), `updateClock()` draws the hero number in `COL_OFFPEAK` grey. Nothing else changes colour.
  - A full-screen overlay covers the dashboard while connecting or scanning. Once data has arrived, a reconnect only shows a spinner, and the last readings stay on screen.
- **Modes:** the home screen has two buttons between the big number and the mix bar.
  - **Charge** is a toggle that stays pressed in until it's pressed again. It's meant to hold the battery minimum at 100% before a storm. Its state is saved in NVS, so it survives a reboot.
  - **Free hour** switches the screen into free-hour mode. This isn't a separate LVGL screen: `setFreeHourMode()` hides and shows widgets and moves the big number. In this mode the big number shows grid import, the house load takes the grid's place in the bottom strip, and a countdown runs to the top of the hour. When it reaches zero, or the close button is tapped, the screen goes back to home.
  - `updateUi()` checks `freeHour` to decide which values go where. The latest reading is kept in `last*Kw` so a mode switch redraws straight away.
  - **System hooks:** `applyChargeMode()` and `applyFreeHourMode()` are placeholders (`TODO(system)`). The UI calls them, but they don't send anything to the plant yet. Sigenergy write support (Modbus FC 0x06/0x10) is still to come.

## Fonts

The built-in Montserrat sizes are enabled in `lv_conf.h` (12/14/16/22/24/32/48). There are also two custom fonts that **contain only digits plus a few symbols**:

- `font_hero_112`: `-./0-9`
- `font_num_44`: `%+-./0-9:` (the `:` is for the free-hour countdown)

If you show any other character in them (for example `:` for a countdown), regenerate the font with that glyph added. Each generated `.c` file starts with an `Opts:` line holding the exact `lv_font_conv` command. Run it with `npx lv_font_conv …` from the repo root, adding the new code point to `-r`.

## Mockups

UI changes are mocked up as PNGs before any code is written. The scripts are in [tools/mockups/](tools/mockups/) and use Python + Pillow:

- **`redesign.py`** is the shared drawing library: 4x supersampled canvas, palette, vector icons. It also holds the original A–D design concepts.
- **`final.py`** and **`preview3.py`** render the dashboard with the Montserrat TTFs from `.pio/libdeps/.../lvgl@9.2.2`, so a `pio run` must have happened at least once.
- **`modes.py`** renders the Charge button and Free-hour screen mockups at the current `buildUi()` coordinates. The implemented layout is "B": `modes-B-*` and `free-hour-B-*`.

  ```sh
  cd tools/mockups && python modes.py ../../mockups/modes
  ```
- **`mock.py`** is the earliest prototype; it produced `ui-mockup*.png`.

Output goes to [mockups/](mockups/) at 2x (960x640). When the firmware layout changes, update the coordinates in the scripts to match `buildUi()`. `preview3.py` predates the current layout: it still has the 150px battery and a labelled mix bar.
