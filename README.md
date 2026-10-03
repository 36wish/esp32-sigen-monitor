# espsolarmonitor

A wall-mounted energy dashboard for a **Sigenergy** home battery and solar system, running on a 3.5" ESP32 touchscreen.

The display joins your WiFi, finds the Sigenergy system on your network by itself, and reads it over local Modbus TCP once a second. It needs no cloud account, app or Home Assistant.

## What it shows

- **House load** as a big number, so you can see what the house is using from across the room.
- **Source mix**: a bar showing how much of that load comes from solar, the battery and the grid.
- **Battery** state of charge.
- **Solar, grid and battery flows** in a strip along the bottom, showing import/export and charge/discharge.
- **Clock** and the system's IP address in the top bar.

## Features

- **Finds the system by itself.** On first boot it scans your /24 subnet for a Modbus TCP device on port 502 and checks each one with a test read. It will save the IP address so it can connect later without rescanning.
- **Charge button.** A toggle for holding the battery full, for example before a big weather event. When pressed it sets the system's backup reserve (Backup SOC) to 100%. Released, it sets it back to 30%. The button follows the system: it rereads the reserve every 60 seconds, so changes made in the mySigen app show up on the screen.
- **Free hour mode.** For power plans that include a free hour of electricity. Tap the clock button when the free hour starts. When activated it switches the system to draw maximum power from the grid. It will countdown to the start of the next hour, and automatically switces back to normal when it ends. The big number on screen switches to grid import, with a countdown to the top of the hour.
- **Off-peak display** Between 21:00 and 07:00, when the overnight rate is cheaper, the big number turns grey.
- **Auto-brightness.** The board's light sensor dims the backlight in a dark room.

## Hardware

| Part | Notes |
|---|---|
| **Sunton ESP32-3248S035R** (a "Cheap Yellow Display") | 3.5" 480×320 ST7796 LCD, resistive XPT2046 touch, ESP32-WROOM-32, CH340 USB-serial. |
| USB cable and 5 V supply | To power it on the wall. |

On the Sigenergy side, the system's **Modbus TCP server** has to be enabled, and the display must be on the same LAN. The firmware talks to the system address (slave ID 247) on port 502. If Modbus TCP isn't on, ask your installer to enable it.

## Building and flashing

The firmware is a [PlatformIO](https://platformio.org/) project for the Arduino framework, using [LVGL](https://lvgl.io/) 9 and [esp32_smartdisplay](https://github.com/rzeldent/esp32-smartdisplay).

1. Install PlatformIO, either the VS Code extension or the CLI.
2. Copy the secrets template and fill in your WiFi network name and password:

   ```sh
   cp include/secrets.h.example include/secrets.h
   ```

   `secrets.h` is gitignored, so your WiFi details stay out of the repo.

3. Optionally, change any other settings at the top of [src/main.cpp](src/main.cpp). See [Configuration](#configuration).
4. Build:

   ```sh
   pio run
   ```

5. Flash. The board's CH340 auto-reset doesn't reliably enter download mode by itself, so **hold BOOT, tap RESET, then release BOOT** before uploading:

   ```sh
   pio run -t upload
   ```

   If you see `Wrong boot mode detected (0x13)`, the board wasn't in download mode. Do the BOOT/RESET step again and retry.

6. Optionally, watch the log at 115200 baud:

   ```sh
   pio device monitor
   ```

On boot it connects to WiFi, then either reconnects to the saved system IP or scans for the system. A full-screen message shows progress until the first reading arrives.

## Touch calibration

These boards' resistive touch panels vary from unit to unit. If taps land in the wrong place:

1. Hold the clock (top right) for 5 seconds, or send `cal` over the serial port.
2. Press and hold the centre of each of the three crosses in turn.
3. A fourth cross checks the result. If your tap lands within 12 px of it, the calibration is saved to flash and loaded on every boot.

## Configuration

All settings are in one block at the top of [src/main.cpp](src/main.cpp). The only exceptions are the WiFi network name and password, which go in `include/secrets.h`.

| Setting | Default | What it does |
|---|---|---|
| `TZ_INFO` | New Zealand | POSIX timezone string for the clock, e.g. `"AEST-10AEDT,M10.1.0,M4.1.0/3"` for Sydney |
| `OFFPEAK_START_HOUR`, `OFFPEAK_END_HOUR` | 21, 7 | When the big number is shaded grey for the off-peak rate |
| `CHARGE_ON_SOC`, `CHARGE_OFF_SOC` | 100, 30 | Backup reserve (%) the Charge button sets when pressed in and when released |
| `CHARGE_SYNC_INTERVAL_MS` | 60000 | How often the Charge button rereads the reserve from the system |
| `BAR_MAX_KW` | 8.0 | Full-scale value for the solar/grid/battery bars |
| `CDS_BRIGHT_RAW`, `CDS_DARK_RAW` | 30, 400 | Light-sensor thresholds for auto-brightness. The serial log prints the raw reading every 10 s to help you tune them for your room. |
| `BL_MIN`, `BL_MAX` | 0.08, 1.0 | Dimmest and brightest backlight levels |
| `CAL_HOLD_MS` | 5000 | How long to hold the clock to start touch calibration |
| `TOUCH_CAL_FALLBACK` | | Touch calibration used until one has been saved on the device |

## Modbus registers

All values are read from the system address (slave 247) in a single block read of 30005–30038 each second:

| Register | Value | Notes |
|---|---|---|
| 30005 | Grid power | s32, W; positive = importing |
| 30014 | Battery SOC | %×10 |
| 30035 | PV power | s32, W |
| 30037 | Battery power | s32, W; positive = charging |
| 40046 | Backup SOC (read/write) | u16, %×10; used by the Charge button |

House load is calculated as `PV + grid − battery`. It sends one request per poll cycle.


## Project layout

```
src/main.cpp              all firmware and settings: Modbus client, discovery, UI, state machine
src/font_hero_144.c       custom font for the big number (digits only)
src/font_num_44.c         custom font for secondary numbers and the countdown
include/lv_conf.h         LVGL configuration
include/secrets.h.example WiFi credentials template
boards/                   board definition for the ESP32-3248S035R
```

## Acknowledgements

The Modbus register map and control logic draw on the work of these projects:

- [TypQxQ/Sigenergy-Local-Modbus](https://github.com/TypQxQ/Sigenergy-Local-Modbus)
- [Rocket-Search/sigenergy-modbus](https://github.com/Rocket-Search/sigenergy-modbus)

Thanks also to [rzeldent/esp32-smartdisplay](https://github.com/rzeldent/esp32-smartdisplay) for the board support package, and to the [LVGL](https://lvgl.io/) project.

## Licence

MIT. See [LICENCE.md](LICENCE.md).
