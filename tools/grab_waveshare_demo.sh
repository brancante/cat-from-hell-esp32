#!/usr/bin/env bash
# Mac helper (small and disk-friendly): reads ONLY the source folder of a working rev-4 project for this board
# (GPL-3.0: we read hardware facts only: pins, timings, init array) and pushes a short excerpt file to the
# branch demo-excerpts so Claude can read it. Run from the repo folder:  bash tools/grab_waveshare_demo.sh
# Optional: WITH_ZIP=1 also tries Waveshare's own demo zip (large; needs ~1 GB free).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
cd "$W"
echo "free disk: $(df -h . | awk 'NR==2{print $4}')"
git clone -q --depth 1 --filter=blob:none --sparse https://github.com/FrancisSailor/NMEA2000-DISPLAY-on-ESP32S3 ref4
git -C ref4 sparse-checkout set src
echo "downloaded: $(du -sh ref4 | cut -f1)"
if [ "${WITH_ZIP:-0}" = 1 ]; then
  curl -fsSL -o z.zip https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4/ESP32-S3-Touch-LCD-4_democode.zip &&
  unzip -q z.zip '*.c' '*.h' '*.cpp' '*.hpp' '*.ino' -x '*/libraries/*' '*.pdf' -d zipsrc || echo "skip zip"
  rm -f z.zip
fi
OUT="$W/waveshare-demo-excerpts.txt"
CODE=(--include=*.h --include=*.c --include=*.cpp --include=*.hpp --include=*.ino)
{
  echo "# files mentioning st7701 / rgb timing"; grep -rliE "st7701|porch|pclk_hz" ref4 zipsrc 2>/dev/null | head -40
  echo; echo "# init command arrays"; grep -rnE -B2 -A70 "init_cmd|lcd_init|st7701.*init|ST7701.*init" "${CODE[@]}" ref4 zipsrc 2>/dev/null | head -500
  echo; echo "# timings and panel config"; grep -rnE "hsync|vsync|porch|pulse_width|pclk|data_width|bits_per_pixel|num_fbs|bounce|flags" "${CODE[@]}" ref4 zipsrc 2>/dev/null | head -150
  echo; echo "# i2c, expander, touch, buzzer"; grep -rnE "0x20|0x21|0x38|0x5D|0x14|TCA9554|GT911|BEE|buzzer|EXIO|backlight|BL_EN" "${CODE[@]}" ref4 zipsrc 2>/dev/null | head -150
  echo; echo "# port/board header heads"
  for f in $(find ref4 zipsrc -iname 'lvgl_port*.h' -o -iname '*pins*.h' -o -iname '*expander*' -o -iname '*board*.h' 2>/dev/null | head -6); do echo "=== $f"; head -250 "$f"; done
} > "$OUT"
echo "excerpt: $(wc -l < "$OUT") lines, $(du -h "$OUT" | cut -f1)"
cd "$REPO"
CUR=$(git branch --show-current)
git switch -C demo-excerpts -q
mkdir -p docs && cp "$OUT" docs/waveshare-demo-excerpts.txt
git add docs/waveshare-demo-excerpts.txt && git commit -qm "Demo excerpts for display bring-up" && git push -qf -u origin demo-excerpts
git switch -q "$CUR"
echo "Done: pushed branch demo-excerpts. Tell Claude."
