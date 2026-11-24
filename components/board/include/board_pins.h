// Waveshare ESP32-S3-Touch-LCD-4 (rev 4) pin map.
// !! UNVERIFIED !!  Fill from the board schematic / Waveshare demo (see README "Milestone 0").
// Every value below is -1 on purpose: board_init() refuses to drive the panel until they are set,
// so a wrong guess can never be flashed by accident. Set BOARD_PINS_VERIFIED to 1 afterwards.
#pragma once
#define BOARD_PINS_VERIFIED 0

#define LCD_W 480
#define LCD_H 480
// 16-bit RGB bus (ST7701S)
#define PIN_LCD_PCLK  -1
#define PIN_LCD_DE    -1
#define PIN_LCD_VSYNC -1
#define PIN_LCD_HSYNC -1
#define PIN_LCD_D { -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 }
// I2C bus shared by touch (GT911) and the IO expander (reset/CS/backlight)
#define PIN_I2C_SDA -1
#define PIN_I2C_SCL -1
#define PIN_TOUCH_INT -1
// Buzzer
#define PIN_BUZZER -1          // may be behind the IO expander instead: check schematic
