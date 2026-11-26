#!/usr/bin/env bash
# Mac helper, round 2: copies the few SOURCE files that define the Rev 4 board setup (expander, touch, display)
# from a working rev-4 project into the branch demo-excerpts, so Claude can read them in full.
# GPL-3.0 source: used as a hardware reference only; it is NOT copied into the game code.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
cd "$W"
git clone -q --depth 1 --filter=blob:none --sparse https://github.com/FrancisSailor/NMEA2000-DISPLAY-on-ESP32S3 ref4 || { echo "clone failed"; exit 1; }
git -C ref4 sparse-checkout set src
OUT="$W/out"; mkdir -p "$OUT"
# the Rev 4 sketch folder files (no fonts/UI data) + any ST7701 init source in bundled libraries
find ref4 \( -ipath '*Version4*' -o -ipath '*Version 4*' \) -type f \( -iname 'rev4_board*' -o -iname 'gt911*' -o -iname 'lvgl_arduino_v4*' -o -iname 'lvgl_port*' -o -iname '*pins*' -o -iname '*board*' \) -size -300k -print0 | while IFS= read -r -d '' f; do cp "$f" "$OUT/$(basename "$f")"; done
find ref4 -type f \( -iname '*st7701*' -o -iname 'Arduino_RGB_Display*' -o -iname 'Arduino_ESP32RGBPanel*' \) -size -300k -print0 | while IFS= read -r -d '' f; do cp "$f" "$OUT/lib_$(basename "$f")"; done
# the main sketch's top 120 lines (pins/consts) if present
INO=$(find ref4 -ipath '*Version4*' -iname '*.ino' | head -1)
[ -n "$INO" ] && head -120 "$INO" > "$OUT/main_sketch_head.txt"
ls -la "$OUT"; du -sh "$OUT"
cd "$REPO"; CUR=$(git branch --show-current)
git switch -C demo-excerpts -q || exit 1
mkdir -p docs/rev4-source && rm -rf docs/rev4-source/* && cp "$OUT"/* docs/rev4-source/
git add docs/rev4-source && git commit -qm "Rev 4 reference sources" && git push -qf -u origin demo-excerpts || { echo "push failed"; exit 1; }
git switch -q "$CUR"; echo "Done: pushed branch demo-excerpts. Tell Claude."
