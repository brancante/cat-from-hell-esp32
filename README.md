# Cat from Hell 🐱🔥

A kid-drawn auto-runner for the Waveshare ESP32-S3-Touch-LCD-4 (rev 4). The cat always runs; you tap Jump, Duck or Strike.
Fast Strike taps spend pizza energy to shoot claws. Twelve phases, a meme (picture + QR code) after every third.

## Status
- ✅ `components/game_core`: physics, controls, pizza/nail rules, phase and difficulty tables, meme unlock. Pure C, tested on the host.
- 🚧 `components/board`: display/touch/buzzer bring-up. **Pins are deliberately unset** until verified against the schematic (Milestone 0).
- 🚧 Sprites, sound, menu, EN/PT text, `catctl` console on the firmware side.

## Try it (no board needed)
    make test    # unit tests
    make demo    # autopilot plays the game core and prints progress

## Build and flash (Mac)
    . ~/esp/esp-idf-v5.3/export.sh   # your existing ESP-IDF
    make build
    make flash                  # finds /dev/cu.usbmodem* automatically

## Milestone 0 (in progress)
Pins are from the Waveshare wiki (see `board_pins.h`); touch + expander + buzzer are implemented. Display waits for the vendor ST7701S init sequence: run `tools/grab_waveshare_demo.sh`.

### Old text
Fill `components/board/include/board_pins.h` from the Waveshare wiki/schematic, set `BOARD_PINS_VERIFIED 1`, then implement
`board_init` (ST7701S via IO expander, RGB panel, GT911, buzzer) and show colour bars plus a touch dot.

Art credit: the cat, the Saint Dog and the level ideas come from drawings by Gustavo's 9-year-old son.
