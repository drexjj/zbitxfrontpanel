# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

Front panel controller firmware for the [zBitx HF transceiver](https://github.com/afarhan/zbitx), running on a Raspberry Pi Pico (RP2040) with a 480×320 TFT touchscreen. It connects over WiFi to the main radio (host `192.168.4.1`, port `8081`) and exchanges control updates.

There is also a `package.json` referencing `express` and `sqlite3` — this appears to be scaffolding for a companion Node.js logbook server, not yet implemented as a separate file.

## Build & Flash

This project supports both **PlatformIO** and **Arduino IDE**.

### PlatformIO (recommended)
```sh
pio run                  # build
pio run -t upload        # build and flash via USB
pio device monitor       # serial monitor at 115200 baud
```
Target board is `rpipico` (see `platformio.ini`). Libraries: `TFT_eSPI`, `Wire`.

### Arduino IDE
Copy `platform.local.txt` into the RP2040 board package folder (see `Read_Me.ino` for the exact path procedure), then open `zbitx_front_panel_v2.ino`.

## Architecture

### Core UI Model — `struct field`
Everything on screen is a `struct field` (defined in `zbitx.h`). Fields have a `type` (BUTTON, NUMBER, SELECTION, TEXT, FREQ, WATERFALL, FT8, LOGBOOK, SMETER, etc.), pixel coordinates/size, a `label` (the key used in the radio protocol), and a `value` string.

- `fields_list.h` — static array `main_list[]` declaring every field; first `FIELDS_ALWAYS_ON` (20) fields are permanently visible.
- `fields.ino` — field lifecycle: init, lookup by label (`field_get`), selection, input dispatch, draw, and posting updates back to the radio (`field_post_to_radio`).
- `screen_gx.cpp` — low-level drawing primitives wrapping TFT_eSPI.

### Radio Communication Protocol
Plain-text over TCP WiFi socket. Each update is one line: `LABEL VALUE\n`. The front panel parses incoming lines in `command_tokenize()` (in `zbitx_front_panel_v2.ino`) and calls `field_set()`. Outgoing updates are batched in `on_request()` by scanning `field->update_to_radio`.

### Dual-Core Loop
- **Core 0** (`loop()`): encoder reading, touchscreen polling, field input dispatch, WiFi/TCP management.
- **Core 1** (`loop1()`): display rendering (`field_draw_all`), waterfall updates.

### Persistent Storage
`storage.cpp` — EEPROM-backed `struct saved` (magic `0x00C0FFEE`) storing WiFi AP credentials (up to 5) and calibration data. Use `block_read()` / `block_write()`.

### Subsystems
| File | Purpose |
|---|---|
| `waterfall.cpp` | Scrolling FFT waterfall display |
| `ft8.cpp / ft8.h` | FT8 decode display and QSO logging |
| `logbook.cpp / logbook.h` | On-screen QSO logbook |
| `console.cpp / console.h` | Scrolling text console field |
| `text_field.cpp / text_field.h` | Editable text input with on-screen keyboard |
| `queue.cpp` | Simple fixed-size ring buffer (`struct Queue`, capacity 4000) |

## Key Conventions

- Fields are looked up by string label (`field_get("FREQ")`), not by index.
- `f_selected` (global in `fields.ino`) tracks the one currently active field.
- The `update_to_radio` flag on a field is set by `field_post_to_radio()` and cleared after transmission in `on_request()`.
- `last_user_change` timestamp prevents radio updates from overwriting in-flight user edits (1 s debounce, except for `FIELD_TEXT`).
- Screen is 480×320; font sizes are 1 (small), 2 (normal), 4 (large) — corresponding width tables are `font_width2[]` and `font_width4[]`.
