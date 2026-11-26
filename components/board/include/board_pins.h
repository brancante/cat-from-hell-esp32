// Waveshare ESP32-S3-Touch-LCD-4, hardware Rev 4.
// Sources: Waveshare wiki pin table + the working Rev 4 NMEA display project (read for hardware facts only):
// the Rev 4 board has a CH32 IO expander at 0x24 (NOT the TCA9554 of older revisions) on the SAME I2C bus as the touch chip.
#pragma once
#define BOARD_PINS_VERIFIED 1
#define BOARD_LCD_INIT_KNOWN 1

#define LCD_W 480
#define LCD_H 480

// ---- RGB565 panel bus (esp_lcd order: B0..B4, G0..G5, R0..R4) ----
#define PIN_LCD_PCLK  41
#define PIN_LCD_DE    40
#define PIN_LCD_VSYNC 39
#define PIN_LCD_HSYNC 38
#define PIN_LCD_D { 5, 45, 48, 47, 21,   14, 13, 12, 11, 10, 9,   46, 3, 8, 18, 17 }
// timings that work on Rev 4
#define LCD_PCLK_HZ 12000000
#define LCD_HS_FP 5
#define LCD_HS_PW 8
#define LCD_HS_BP 50
#define LCD_VS_FP 10
#define LCD_VS_PW 8
#define LCD_VS_BP 30

// ---- ST7701S 3-wire SPI (init only), 9-bit frames, direct GPIOs ----
#define PIN_LCD_SPI_SDA 1
#define PIN_LCD_SPI_SCL 2
#define PIN_LCD_SPI_CS  42

// ---- one shared I2C bus: CH32 expander + GT911 touch ----
#define PIN_I2C_SDA 15
#define PIN_I2C_SCL 7
#define PIN_TP_INT  16

// ---- CH32 expander (address 0x24) ----
#define CH32_ADDR       0x24
#define CH32_REG_OUT    0x02
#define CH32_REG_DIR    0x03          // 1 = output
#define CH32_REG_BL_PWM 0x05          // backlight PWM value (72 is the default used by the Rev 4 project, max 247)
#define CH32_OUT_SAFE   0xBF          // all high except the buzzer bit
#define CH32_DIR_SAFE   0x3A          // TP_RST, LCD_RST, SD_CS, SYS_EN as outputs
#define CH32_DIR_RUN    0x7A          // plus BEE_EN as output
#define CH32_BIT_TP_RST  1
#define CH32_BIT_LCD_RST 3
#define CH32_BIT_BEEP    6            // buzzer on when high (plain on/off: treated as an active buzzer)
#define BL_PWM_DEFAULT   72
