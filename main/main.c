#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board.h"
#include "game.h"

static const char *TAG = "cat";

// Touch -> the three buttons along the bottom strip (y >= 384). Several fingers may be down at once.
static game_input_t read_buttons(void) {
    touch_pt_t p[5]; game_input_t in = {0};
    int n = board_touch_read(p, 5);
    for (int i = 0; i < n; i++) if (p[i].down && p[i].y >= 384) {
        if (p[i].x < 160) in.jump = true; else if (p[i].x < 320) in.duck = true; else in.strike = true;
    }
    return in;
}

static void play_sound(uint32_t ev) {
    if (ev & EV_JUMP) board_beep(660, 40);
    if (ev & EV_HEAD_BOUNCE) { board_beep(500, 50); board_beep(800, 70); }
    if (ev & EV_PIZZA) board_beep(1200, 40);
    if (ev & EV_BREAK) board_beep(180, 70);
    if (ev & EV_HURT) board_beep(300, 150);
    if (ev & EV_NAILS) board_beep(1500, 30);
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    esp_err_t e = board_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "board_init failed (%s). Game logic is tested on the host; finish Milestone 0.", esp_err_to_name(e)); return; }
    static game_t g; game_init(&g, (uint32_t)esp_timer_get_time(), 0);
    int64_t next = esp_timer_get_time();
    const int64_t dt = 1000000 / GAME_STEP_HZ;
    for (;;) {
        uint32_t ev = game_step(&g, read_buttons());
        play_sound(ev);
        // TODO(M2): draw world + sprites into the 240x240 buffer, board_present()
        next += dt; int64_t wait = next - esp_timer_get_time();
        if (wait > 0) vTaskDelay(pdMS_TO_TICKS(wait / 1000)); else next = esp_timer_get_time();
    }
}
