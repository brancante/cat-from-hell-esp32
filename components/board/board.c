#include "board.h"
#include "esp_log.h"
static const char *TAG = "board";

// Milestone 0 fills this in: ST7701S init over the IO expander, esp_lcd RGB panel (bounce buffers in SRAM,
// 240x240 source pixel-doubled on the way out), GT911 over I2C, buzzer via LEDC (passive) or GPIO (active).
esp_err_t board_init(void) {
#if !BOARD_PINS_VERIFIED
    ESP_LOGE(TAG, "board_pins.h is not verified yet: refusing to drive the panel. See README, Milestone 0.");
    return ESP_ERR_NOT_SUPPORTED;
#else
    return ESP_ERR_NOT_FINISHED;     // TODO(M0): panel + touch + buzzer bring-up
#endif
}
int  board_touch_read(touch_pt_t *pts, int max) { (void)pts; (void)max; return 0; }
void board_present(const uint16_t *fb) { (void)fb; }
void board_beep(uint16_t hz, uint16_t ms) { (void)hz; (void)ms; }
