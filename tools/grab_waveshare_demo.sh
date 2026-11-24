#!/usr/bin/env bash
# Mac helper: downloads Waveshare's demo code, extracts the ST7701 init sequence + RGB timings + I2C addresses,
# and pushes ONLY those excerpts to a branch of this repo so Claude can read them. Run from the repo folder:
#   bash tools/grab_waveshare_demo.sh
set -euo pipefail
cd "$(dirname "$0")/.."
W=$(mktemp -d); cd "$W"
# 1) A working rev-4 project for this board (GPL-3.0: we only READ hardware facts from it: pins, timings, init array)
git clone -q --depth 1 https://github.com/FrancisSailor/NMEA2000-DISPLAY-on-ESP32S3 ref4 || echo "skip ref4"
# 2) Waveshare's own demos
for z in ESP32-S3-Touch-LCD-4_democode.zip ESP32-S3-Touch-LCD-4_democode_V2.zip ESP32-S3-LCD-4_Demo.zip; do
  curl -fsSL -o "$z" "https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4/$z" && unzip -qo "$z" -d "${z%.zip}" || echo "skip $z"
done
OUT="$W/waveshare-demo-excerpts.txt"
{
  echo "# files mentioning st7701 / rgb timings"
  grep -rliE "st7701|hsync_back_porch|back_porch|hsync_pulse|pclk_hz|ST7701" . 2>/dev/null | head -40
  echo; echo "# excerpts (code files only)"
  grep -rnE -B2 -A60 "st7701_lcd_init_cmd|lcd_init_cmds|vendor_specific_init|init_cmds\[\]|st7701.*init|ST7701_.*init" --include=*.h --include=*.c --include=*.cpp --include=*.hpp --include=*.ino . 2>/dev/null | head -600
  echo; echo "# timings"
  grep -rnE "hsync|vsync|porch|pulse_width|pclk|flags|data_width|bits_per_pixel|num_fbs|bounce" --include=*.h --include=*.c --include=*.cpp --include=*.hpp . 2>/dev/null | grep -iE "lcd|rgb|panel|st7701" | head -120
  echo; echo "# i2c addresses / expander / gt911 / buzzer"
  grep -rnE "0x20|0x21|0x38|0x5D|0x14|TCA9554|GT911|BEE|buzzer|EXIO" --include=*.h --include=*.c --include=*.cpp --include=*.hpp --include=*.ino . 2>/dev/null | head -120
  echo; echo "# lvgl_port / board config files (rev 4 project)"
  for f in $(find ref4 -iname 'lvgl_port*.h' -o -iname '*board*.h' -o -iname '*pins*.h' -o -iname '*expander*' 2>/dev/null | head -8); do echo "=== $f"; head -300 "$f"; done
} > "$OUT"
wc -l "$OUT"
cd - >/dev/null
git switch -c demo-excerpts
cp "$OUT" docs/waveshare-demo-excerpts.txt 2>/dev/null || { mkdir -p docs && cp "$OUT" docs/waveshare-demo-excerpts.txt; }
git add docs/waveshare-demo-excerpts.txt && git commit -qm "Waveshare demo excerpts for display bring-up" && git push -u origin demo-excerpts
git switch -
echo "Done: excerpts pushed to branch demo-excerpts. Tell Claude."
