#include "board.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "board";
static i2c_master_bus_handle_t s_exp_bus, s_tp_bus;
static i2c_master_dev_handle_t s_exp, s_gt;
static uint8_t s_out;                       // shadow of the expander output register
static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_beep_timer;

#define TCA_OUT 0x01
#define TCA_CFG 0x03

static void exp_write(uint8_t reg, uint8_t v) {
    uint8_t b[2] = { reg, v };
    i2c_master_transmit(s_exp, b, 2, 50);
}
static void exp_bit(int bit, bool on) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_out = on ? (s_out | (1u << bit)) : (s_out & ~(1u << bit));
    exp_write(TCA_OUT, s_out);
    xSemaphoreGive(s_lock);
}
static void beep_off(void *arg) { (void)arg; exp_bit(EXIO_BEE_EN, false); }

static void scan(i2c_master_bus_handle_t bus, const char *name) {
    for (int a = 0x08; a < 0x78; a++)
        if (i2c_master_probe(bus, a, 20) == ESP_OK) ESP_LOGI(TAG, "I2C %s: device at 0x%02X", name, a);
}

static esp_err_t new_bus(int sda, int scl, i2c_port_num_t port, i2c_master_bus_handle_t *out) {
    i2c_master_bus_config_t c = { .i2c_port = port, .sda_io_num = sda, .scl_io_num = scl,
                                  .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true };
    return i2c_new_master_bus(&c, out);
}
static esp_err_t add_dev(i2c_master_bus_handle_t bus, uint8_t addr, i2c_master_dev_handle_t *out) {
    i2c_device_config_t d = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000 };
    return i2c_master_bus_add_device(bus, &d, out);
}

esp_err_t board_init(void) {
#if !BOARD_PINS_VERIFIED
    ESP_LOGE(TAG, "board_pins.h is not verified yet"); return ESP_ERR_NOT_SUPPORTED;
#else
    s_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(new_bus(PIN_EXP_SDA, PIN_EXP_SCL, I2C_NUM_0, &s_exp_bus));
    ESP_ERROR_CHECK(new_bus(PIN_TP_SDA, PIN_TP_SCL, I2C_NUM_1, &s_tp_bus));
    scan(s_exp_bus, "expander bus (SDA8/SCL9)");
    scan(s_tp_bus, "touch bus (SDA15/SCL7)");

    // TCA9554: address 0x20..0x27 depending on A0..A2; use the first one that answers.
    uint8_t exp_addr = 0;
    for (uint8_t a = 0x20; a <= 0x27 && !exp_addr; a++) if (i2c_master_probe(s_exp_bus, a, 20) == ESP_OK) exp_addr = a;
    if (!exp_addr) { ESP_LOGE(TAG, "IO expander (TCA9554) not found on the expander bus"); return ESP_ERR_NOT_FOUND; }
    ESP_ERROR_CHECK(add_dev(s_exp_bus, exp_addr, &s_exp));
    ESP_LOGI(TAG, "TCA9554 at 0x%02X", exp_addr);
    // EXIO6 (RTC_INT) and EXIO7 (unknown) stay inputs; everything else is an output, all low except SD_CS.
    s_out = 1u << EXIO_SD_CS;
    exp_write(TCA_OUT, s_out); exp_write(TCA_CFG, (1u << EXIO_RTC_INT) | (1u << EXIO_DO1));

    // Reset sequence: touch + LCD, then backlight on.
    vTaskDelay(pdMS_TO_TICKS(20));
    exp_bit(EXIO_TP_RST, true); exp_bit(EXIO_LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(60));
    exp_bit(EXIO_BL_EN, true);

    // GT911 answers at 0x5D or 0x14.
    uint8_t gt = 0;
    if (i2c_master_probe(s_tp_bus, 0x5D, 30) == ESP_OK) gt = 0x5D; else if (i2c_master_probe(s_tp_bus, 0x14, 30) == ESP_OK) gt = 0x14;
    if (gt) { ESP_ERROR_CHECK(add_dev(s_tp_bus, gt, &s_gt)); ESP_LOGI(TAG, "GT911 touch at 0x%02X", gt); }
    else ESP_LOGW(TAG, "GT911 touch not found");

    const esp_timer_create_args_t ta = { .callback = beep_off, .name = "beep" };
    ESP_ERROR_CHECK(esp_timer_create(&ta, &s_beep_timer));
    board_beep(0, 120);                                   // boot beep proves the buzzer wiring
#if !BOARD_LCD_INIT_KNOWN
    ESP_LOGW(TAG, "LCD init sequence not available yet (BOARD_LCD_INIT_KNOWN=0): touch + buzzer only");
#endif
    return ESP_OK;
#endif
}

int board_touch_read(touch_pt_t *pts, int max) {
    if (!s_gt) return 0;
    const uint8_t st_reg[2] = { 0x81, 0x4E };
    uint8_t st = 0;
    if (i2c_master_transmit_receive(s_gt, st_reg, 2, &st, 1, 20) != ESP_OK) return 0;
    int n = 0;
    if (st & 0x80) {
        int cnt = st & 0x0F; if (cnt > 5) cnt = 5;
        if (cnt > 0) {
            const uint8_t pt_reg[2] = { 0x81, 0x4F };
            uint8_t buf[5 * 8];
            if (i2c_master_transmit_receive(s_gt, pt_reg, 2, buf, cnt * 8, 20) == ESP_OK)
                for (int i = 0; i < cnt && n < max; i++) {
                    const uint8_t *p = &buf[i * 8];
                    pts[n++] = (touch_pt_t){ true, (int16_t)(p[1] | (p[2] << 8)), (int16_t)(p[3] | (p[4] << 8)) };
                }
        }
        const uint8_t clr[3] = { 0x81, 0x4E, 0x00 };
        i2c_master_transmit(s_gt, clr, 3, 20);
    }
    return n;
}

void board_present(const uint16_t *fb) { (void)fb; }       // TODO: RGB panel once BOARD_LCD_INIT_KNOWN

void board_beep(uint16_t hz, uint16_t ms) {
    (void)hz;
    if (!s_beep_timer || !ms) return;
    esp_timer_stop(s_beep_timer);
    exp_bit(EXIO_BEE_EN, true);
    esp_timer_start_once(s_beep_timer, (uint64_t)ms * 1000);
}
