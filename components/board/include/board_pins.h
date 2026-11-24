// Waveshare ESP32-S3-Touch-LCD-4 (rev 4) pin map.
// Source: Waveshare wiki https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4 (pin table), checked 2026-11-24.
// Items marked INFERRED or UNKNOWN are not stated on the wiki; see README "Milestone 0".
#pragma once
#define BOARD_PINS_VERIFIED 1          // GPIO numbers below come from the vendor pin table
#define BOARD_LCD_INIT_KNOWN 0         // 0 until the ST7701S init sequence + timings are copied from the vendor demo

#define LCD_W 480
#define LCD_H 480

// ---- RGB565 panel bus ----
#define PIN_LCD_PCLK  41
#define PIN_LCD_DE    40
#define PIN_LCD_VSYNC 39
#define PIN_LCD_HSYNC 38
// INFERRED data order for esp_lcd data_gpio_nums[16]: B1..B5 (blue bits 0..4), G0..G5, R1..R5 (red bits 0..4)
#define PIN_LCD_D { 5, 45, 48, 47, 21,   14, 13, 12, 11, 10, 9,   46, 3, 8, 18, 17 }
#define LCD_PCLK_HZ 21000000           // vendor LVGL benchmark value

// ---- ST7701S 3-wire SPI (init only), direct GPIOs ----
#define PIN_LCD_SPI_SDA 1
#define PIN_LCD_SPI_SCL 2
#define PIN_LCD_SPI_CS  42

// ---- I2C ----
#define PIN_EXP_SDA 8                  // bus for the TCA9554 IO expander
#define PIN_EXP_SCL 9
#define PIN_TP_SDA  15                 // bus for GT911 touch, RTC
#define PIN_TP_SCL  7
#define PIN_TP_INT  16

// ---- TCA9554 expander bits (EXIOn) ----
#define EXIO_TP_RST  0
#define EXIO_BL_EN   1                 // backlight enable (UNKNOWN: EXIO4 "BLC" may also matter)
#define EXIO_LCD_RST 2
#define EXIO_SD_CS   3
#define EXIO_BLC     4
#define EXIO_BEE_EN  5                 // buzzer enable: plain on/off through the expander => treated as ACTIVE buzzer
#define EXIO_RTC_INT 6
#define EXIO_DO1     7
