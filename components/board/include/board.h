#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include "board_pins.h"

typedef struct { bool down; int16_t x, y; } touch_pt_t;       // x,y in 0..LCD_W/H

esp_err_t board_init(void);                                     // expander, touch, display, buzzer
int  board_touch_read(touch_pt_t *pts, int max);                // number of fingers (GT911: up to 5)
void board_draw_band(const uint16_t *px, int y0, int rows);    // RGB565 480-pixel-wide band to the panel at row y0
void board_beep(uint16_t hz, uint16_t ms);                      // on/off buzzer: hz is ignored, ms = length (non-blocking)
