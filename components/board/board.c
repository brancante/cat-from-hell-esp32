// Rev 4 board support: CH32 expander, GT911 touch, ST7701S + RGB panel, buzzer.
#include "board.h"
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "board";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_ch32, s_gt;
static SemaphoreHandle_t s_i2c_lock;
static uint8_t s_out = CH32_OUT_SAFE;
static esp_timer_handle_t s_beep_timer;
static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_strip;                 // pixel-doubling strip in SRAM

// ---------------------------------------------------------------- I2C helpers (all calls serialised)
static esp_err_t i2c_tx(i2c_master_dev_handle_t d, const uint8_t *b, size_t n) {
    xSemaphoreTake(s_i2c_lock, portMAX_DELAY);
    esp_err_t e = i2c_master_transmit(d, b, n, 50);
    xSemaphoreGive(s_i2c_lock);
    return e;
}
static esp_err_t i2c_txrx(i2c_master_dev_handle_t d, const uint8_t *w, size_t wn, uint8_t *r, size_t rn) {
    xSemaphoreTake(s_i2c_lock, portMAX_DELAY);
    esp_err_t e = i2c_master_transmit_receive(d, w, wn, r, rn, 50);
    xSemaphoreGive(s_i2c_lock);
    return e;
}
static esp_err_t ch32_write(uint8_t reg, uint8_t v) { uint8_t b[2] = { reg, v }; return i2c_tx(s_ch32, b, 2); }
static void ch32_out(uint8_t out) { s_out = out; ch32_write(CH32_REG_OUT, out); }
static void ch32_bit(int bit, bool on) {
    xSemaphoreTake(s_i2c_lock, portMAX_DELAY);          // protect the shadow register too
    uint8_t v = on ? (s_out | (1u << bit)) : (s_out & ~(1u << bit));
    s_out = v;
    xSemaphoreGive(s_i2c_lock);
    ch32_write(CH32_REG_OUT, v);
}

// ---------------------------------------------------------------- buzzer
static void beep_off(void *arg) { (void)arg; ch32_bit(CH32_BIT_BEEP, false); }
void board_beep(uint16_t hz, uint16_t ms) {
    (void)hz;
    if (!s_beep_timer || !ms) return;
    esp_timer_stop(s_beep_timer);
    ch32_bit(CH32_BIT_BEEP, true);
    esp_timer_start_once(s_beep_timer, (uint64_t)ms * 1000);
}

// ---------------------------------------------------------------- GT911
#define GT_STATUS 0x814E
#define GT_CONFIG 0x8047
static esp_err_t gt_read(uint16_t reg, uint8_t *buf, size_t n) {
    const uint8_t a[2] = { reg >> 8, reg & 0xFF };
    return i2c_txrx(s_gt, a, 2, buf, n);
}
static esp_err_t gt_write(uint16_t reg, const uint8_t *buf, size_t n) {
    uint8_t tmp[2 + 28]; if (n > 28) return ESP_ERR_INVALID_SIZE;
    tmp[0] = reg >> 8; tmp[1] = reg & 0xFF; memcpy(&tmp[2], buf, n);
    return i2c_tx(s_gt, tmp, 2 + n);
}
// Same idea as the working Rev 4 project: rewrite the config with a 480x480 resolution and 5 touch points.
static bool gt_apply_config(void) {
    uint8_t cfg[186] = { 0 };
    for (int off = 0; off < 184; off += 28) {
        int n = 184 - off < 28 ? 184 - off : 28;
        if (gt_read(GT_CONFIG + off, &cfg[off], n) != ESP_OK) return false;
    }
    cfg[0] = (uint8_t)(cfg[0] + 1); if (!cfg[0]) cfg[0] = 1;
    cfg[1] = 0xE0; cfg[2] = 0x01; cfg[3] = 0xE0; cfg[4] = 0x01;      // 480 x 480
    cfg[5] = 5;                                                       // max touches
    if (!cfg[6]) cfg[6] = 0x0D;
    uint8_t sum = 0; for (int i = 0; i < 184; i++) sum = (uint8_t)(sum + cfg[i]);
    cfg[184] = (uint8_t)(~sum + 1); cfg[185] = 0x01;
    for (int off = 0; off < 186; off += 28) {
        int n = 186 - off < 28 ? 186 - off : 28;
        if (gt_write(GT_CONFIG + off, &cfg[off], n) != ESP_OK) return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    uint8_t z = 0; gt_write(GT_STATUS, &z, 1);
    return true;
}

int board_touch_read(touch_pt_t *pts, int max) {
    if (!s_gt) return 0;
    uint8_t st = 0;
    if (gt_read(GT_STATUS, &st, 1) != ESP_OK) return 0;
    int n = 0;
    if (st & 0x80) {
        int cnt = st & 0x0F; if (cnt > 5) cnt = 5;
        if (cnt > 0) {
            uint8_t buf[40];
            if (gt_read(0x8150, buf, cnt * 8) == ESP_OK)           // x at +0, y at +2 (little endian), 8 bytes per point
                for (int i = 0; i < cnt && n < max; i++) {
                    const uint8_t *p = &buf[i * 8];
                    pts[n++] = (touch_pt_t){ true, (int16_t)(p[0] | (p[1] << 8)), (int16_t)(p[2] | (p[3] << 8)) };
                }
        }
        uint8_t z = 0; gt_write(GT_STATUS, &z, 1);
    }
    return n;
}

// ---------------------------------------------------------------- ST7701S init (3-wire SPI, 9-bit frames, bit-banged)
static void spi_bit(int b) {
    gpio_set_level(PIN_LCD_SPI_SDA, b); esp_rom_delay_us(1);
    gpio_set_level(PIN_LCD_SPI_SCL, 1); esp_rom_delay_us(1);
    gpio_set_level(PIN_LCD_SPI_SCL, 0);
}
static void spi_word(int dc, uint8_t v) {
    spi_bit(dc);
    for (int i = 7; i >= 0; i--) spi_bit((v >> i) & 1);
}
static void st_cmd(uint8_t cmd, const uint8_t *d, int n) {
    gpio_set_level(PIN_LCD_SPI_CS, 0);
    spi_word(0, cmd);
    for (int i = 0; i < n; i++) spi_word(1, d[i]);
    gpio_set_level(PIN_LCD_SPI_CS, 1);
    esp_rom_delay_us(2);
}
#define C(cmd, ...) do { const uint8_t _d[] = { __VA_ARGS__ }; st_cmd(cmd, _d, sizeof _d); } while (0)
#define C0(cmd) st_cmd(cmd, NULL, 0)
// Command sequence that works on this panel (same values as the Arduino_GFX "st7701 type1" table used by the Rev 4 project).
static void st7701_init(void) {
    gpio_config_t io = { .pin_bit_mask = (1ULL << PIN_LCD_SPI_SDA) | (1ULL << PIN_LCD_SPI_SCL) | (1ULL << PIN_LCD_SPI_CS), .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    gpio_set_level(PIN_LCD_SPI_CS, 1); gpio_set_level(PIN_LCD_SPI_SCL, 0);
    C(0xFF, 0x77, 0x01, 0x00, 0x00, 0x10);
    C(0xC0, 0x3B, 0x00); C(0xC1, 0x0D, 0x02); C(0xC2, 0x31, 0x05); C(0xCD, 0x08);
    C(0xB0, 0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18);
    C(0xB1, 0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18);
    C(0xFF, 0x77, 0x01, 0x00, 0x00, 0x11);
    C(0xB0, 0x60); C(0xB1, 0x32); C(0xB2, 0x07); C(0xB3, 0x80); C(0xB5, 0x49); C(0xB7, 0x85); C(0xB8, 0x21);
    C(0xC1, 0x78); C(0xC2, 0x78);
    C(0xE0, 0x00, 0x1B, 0x02);
    C(0xE1, 0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44);
    C(0xE2, 0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00);
    C(0xE3, 0x00, 0x00, 0x11, 0x11);
    C(0xE4, 0x44, 0x44);
    C(0xE5, 0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0);
    C(0xE6, 0x00, 0x00, 0x11, 0x11);
    C(0xE7, 0x44, 0x44);
    C(0xE8, 0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0);
    C(0xEB, 0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40);
    C(0xEC, 0x3C, 0x00);
    C(0xED, 0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA);
    C(0xFF, 0x77, 0x01, 0x00, 0x00, 0x13);
    C(0xE5, 0xE4);
    C(0xFF, 0x77, 0x01, 0x00, 0x00, 0x00);
    C0(0x21);                    // IPS
    C(0x3A, 0x60);
    C0(0x11);                    // sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    C0(0x29);                    // display on
}

// ---------------------------------------------------------------- init
static esp_err_t add_dev(uint8_t addr, i2c_master_dev_handle_t *out, uint32_t hz) {
    i2c_device_config_t d = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = hz };
    return i2c_master_bus_add_device(s_bus, &d, out);
}

esp_err_t board_init(void) {
    s_i2c_lock = xSemaphoreCreateMutex();
    i2c_master_bus_config_t bc = { .i2c_port = I2C_NUM_0, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
                                   .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &s_bus));
    for (int a = 0x08; a < 0x78; a++) if (i2c_master_probe(s_bus, a, 20) == ESP_OK) ESP_LOGI(TAG, "I2C device at 0x%02X", a);
    if (i2c_master_probe(s_bus, CH32_ADDR, 50) != ESP_OK) { ESP_LOGE(TAG, "CH32 expander not found at 0x%02X: is this a Rev 4 board?", CH32_ADDR); return ESP_ERR_NOT_FOUND; }
    ESP_ERROR_CHECK(add_dev(CH32_ADDR, &s_ch32, 100000));

    // Startup sequence used by the working Rev 4 project: safe outputs, LCD reset pulse, backlight.
    ch32_out(CH32_OUT_SAFE); ch32_write(CH32_REG_DIR, CH32_DIR_SAFE);
    vTaskDelay(pdMS_TO_TICKS(20));
    ch32_out(CH32_OUT_SAFE & ~(1u << CH32_BIT_LCD_RST));
    vTaskDelay(pdMS_TO_TICKS(100));
    ch32_out(CH32_OUT_SAFE);
    vTaskDelay(pdMS_TO_TICKS(500));
    ch32_write(CH32_REG_BL_PWM, BL_PWM_DEFAULT);

    // Touch: GT911 answers at 0x5D or 0x14.
    uint8_t gt = 0;
    if (i2c_master_probe(s_bus, 0x5D, 50) == ESP_OK) gt = 0x5D; else if (i2c_master_probe(s_bus, 0x14, 50) == ESP_OK) gt = 0x14;
    if (gt) {
        ESP_ERROR_CHECK(add_dev(gt, &s_gt, 100000));
        ESP_LOGI(TAG, "GT911 at 0x%02X, config %s", gt, gt_apply_config() ? "applied" : "FAILED");
    } else ESP_LOGW(TAG, "GT911 not found");
    ch32_write(CH32_REG_DIR, CH32_DIR_RUN);                 // buzzer bit becomes an output

    const esp_timer_create_args_t ta = { .callback = beep_off, .name = "beep" };
    ESP_ERROR_CHECK(esp_timer_create(&ta, &s_beep_timer));

    // Display: ST7701S init over 3-wire SPI, then the RGB panel.
    st7701_init();
    esp_lcd_rgb_panel_config_t pc = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = { .pclk_hz = LCD_PCLK_HZ, .h_res = LCD_W, .v_res = LCD_H,
                     .hsync_pulse_width = LCD_HS_PW, .hsync_back_porch = LCD_HS_BP, .hsync_front_porch = LCD_HS_FP,
                     .vsync_pulse_width = LCD_VS_PW, .vsync_back_porch = LCD_VS_BP, .vsync_front_porch = LCD_VS_FP,
                     .flags = { .hsync_idle_low = 0, .vsync_idle_low = 0, .de_idle_high = 0, .pclk_active_neg = 0, .pclk_idle_high = 0 } },
        .data_width = 16, .bits_per_pixel = 16, .num_fbs = 1, .bounce_buffer_size_px = LCD_W * 10,
        .hsync_gpio_num = PIN_LCD_HSYNC, .vsync_gpio_num = PIN_LCD_VSYNC, .de_gpio_num = PIN_LCD_DE,
        .pclk_gpio_num = PIN_LCD_PCLK, .disp_gpio_num = -1, .data_gpio_nums = PIN_LCD_D,
        .flags = { .fb_in_psram = 1 },
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&pc, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    s_strip = heap_caps_malloc(LCD_W * 32 * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_strip) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "display ready");
    board_beep(0, 120);                                      // boot beep
    return ESP_OK;
}

// 240x240 RGB565 -> 480x480, 16 source rows (32 output rows) per strip.
void board_present(const uint16_t *fb) {
    if (!s_panel || !s_strip) return;
    for (int y0 = 0; y0 < 240; y0 += 16) {
        for (int r = 0; r < 16; r++) {
            const uint16_t *src = fb + (y0 + r) * 240;
            uint16_t *d0 = s_strip + (r * 2) * LCD_W, *d1 = d0 + LCD_W;
            for (int x = 0; x < 240; x++) { uint16_t p = src[x]; d0[2 * x] = d0[2 * x + 1] = p; }
            memcpy(d1, d0, LCD_W * sizeof(uint16_t));
        }
        esp_lcd_panel_draw_bitmap(s_panel, 0, y0 * 2, LCD_W, y0 * 2 + 32, s_strip);
    }
}
