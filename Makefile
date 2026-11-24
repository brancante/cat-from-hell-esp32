# Cat from Hell: everyday commands. Run `. ~/esp/esp-idf-v5.3/export.sh` once per terminal first.
PORT ?= $(firstword $(wildcard /dev/cu.usbmodem*))
.PHONY: test demo build flash monitor
test:     ; cc -std=c11 -Wall -Wextra -Werror -Icomponents/game_core/include tests/test_game.c components/game_core/game.c -o /tmp/cfh_test && /tmp/cfh_test
demo:     ; cc -std=c11 -O2 -Icomponents/game_core/include tools/demo.c components/game_core/game.c -o /tmp/cfh_demo && /tmp/cfh_demo
build:    ; idf.py set-target esp32s3 && idf.py build
flash:    ; idf.py -p $(PORT) flash monitor
monitor:  ; idf.py -p $(PORT) monitor
