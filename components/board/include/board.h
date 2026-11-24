#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include "board_pins.h"

typedef struct { bool down; int16_t x, y; } touch_pt_t;       // x,y in 0..LCD_W/H

esp_err_t board_init(void);                                     // display, touch, buzzer
int  board_touch_read(touch_pt_t *pts, int max);                // returns number of fingers (GT911: up to 5)
void board_present(const uint16_t *fb240);                      // 240x240 RGB565 -> pixel-doubled 480x480
void board_beep(uint16_t hz, uint16_t ms);                      // non-blocking; hz=0 is silence
