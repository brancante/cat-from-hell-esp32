#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board.h"
#include "game.h"

static const char *TAG = "cat";
static uint16_t s_fb[GAME_W * GAME_H];                  // 240x240 draw buffer (shown pixel-doubled on 480x480)

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define PLAY_H 192                                      // logical rows above the button strip (physical 384)

static void rect(int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
    if (x + w > GAME_W) w = GAME_W - x; if (y + h > GAME_H) h = GAME_H - y;
    for (int j = 0; j < h; j++) { uint16_t *p = &s_fb[(y + j) * GAME_W + x]; for (int i = 0; i < w; i++) p[i] = c; }
}

// Placeholder boxes until the real sprites (from the kid's drawings) arrive in milestone 2.
static void draw(const game_t *g, int fps) {
    rect(0, 0, GAME_W, PLAY_H, RGB(255, 233, 184));                          // sky
    rect(0, GAME_GROUND_Y, GAME_W, PLAY_H - GAME_GROUND_Y, RGB(201, 138, 75)); // floor
    for (int x = -(int)((g->distance / GAME_Q) % 24); x < GAME_W; x += 24) rect(x, GAME_GROUND_Y, 2, PLAY_H - GAME_GROUND_Y, RGB(120, 80, 40));
    for (int i = 0; i < GAME_MAX_OBJ; i++) {
        const game_obj_t *o = &g->obj[i]; if (!o->type) continue;
        uint16_t c = o->type == OBJ_LOW ? RGB(150, 100, 60) : o->type == OBJ_HIGH ? RGB(90, 90, 160) : o->type == OBJ_BREAKABLE ? RGB(230, 90, 90)
                   : o->type == OBJ_HUMAN ? RGB(240, 200, 170) : o->type == OBJ_PIZZA ? RGB(255, 200, 40) : RGB(120, 40, 160);
        rect(o->x / GAME_Q, o->y / GAME_Q, o->w, o->h, c);
    }
    for (int i = 0; i < GAME_MAX_NAILS; i++) if (g->nail[i].active) rect(g->nail[i].x / GAME_Q, g->nail[i].y / GAME_Q, 6, 3, RGB(255, 255, 255));
    int h = g->state == CAT_DUCK ? 10 : 20;                                    // cat
    if (!(g->invuln && (g->tick & 4))) {
        rect(48, g->cat_y / GAME_Q, 24, h, RGB(245, 138, 31));
        rect(60, g->cat_y / GAME_Q + 2, 6, 6, RGB(255, 255, 255));              // eye
        if (g->strike_timer) rect(72, g->cat_y / GAME_Q + 4, 14, 3, RGB(255, 255, 255));   // claw swipe
        rect(40, g->cat_y / GAME_Q + 3, 8, 3, RGB(31, 138, 59));                // tail
    }
    for (int i = 0; i < g->lives; i++) rect(4 + i * 10, 4, 8, 8, RGB(229, 57, 53));       // lives
    for (int i = 0; i < GAME_MAX_SLICES; i++) rect(170 + i * 12, 4, 10, 8, i < g->energy ? RGB(255, 190, 0) : RGB(180, 160, 120));  // pizza energy
    for (int i = 0; i < g->phase + 1 && i < GAME_PHASES; i++) rect(4 + i * 6, 16, 4, 3, RGB(31, 138, 59));
    rect(0, PLAY_H, 80, 48, RGB(76, 175, 80)); rect(80, PLAY_H, 80, 48, RGB(33, 150, 243)); rect(160, PLAY_H, 80, 48, RGB(229, 57, 53));  // buttons
    rect(79, PLAY_H, 2, 48, 0); rect(159, PLAY_H, 2, 48, 0);
    if (fps) rect(100, 4, fps > 30 ? 30 : fps, 3, RGB(0, 0, 0));               // fps bar (30 px = 30 fps)
}

static game_input_t read_buttons(void) {
    touch_pt_t p[5]; game_input_t in = { 0 };
    int n = board_touch_read(p, 5);
    for (int i = 0; i < n; i++) if (p[i].down && p[i].y >= 384) {
        if (p[i].x < 160) in.jump = true; else if (p[i].x < 320) in.duck = true; else in.strike = true;
    }
    return in;
}

static void play_sound(uint32_t ev) {
    if (ev & EV_JUMP) board_beep(0, 25);
    if (ev & EV_HEAD_BOUNCE) board_beep(0, 60);
    if (ev & EV_PIZZA) board_beep(0, 20);
    if (ev & (EV_BREAK | EV_NAIL_HIT)) board_beep(0, 40);
    if (ev & EV_NAILS) board_beep(0, 30);
    if (ev & EV_HURT) board_beep(0, 150);
    if (ev & (EV_PHASE_CLEAR | EV_MEME_UNLOCK)) board_beep(0, 250);
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    esp_err_t e = board_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "board_init failed (%s)", esp_err_to_name(e)); return; }
    static game_t g; game_init(&g, (uint32_t)esp_timer_get_time(), 0);
    int64_t next = esp_timer_get_time(), last_present = next; int frames = 0, fps = 0; int64_t fps_t = next;
    const int64_t dt = 1000000 / GAME_STEP_HZ;
    for (;;) {
        uint32_t ev = game_step(&g, read_buttons());
        play_sound(ev);
        if (g.over && (ev & EV_GAME_OVER)) { ESP_LOGI(TAG, "game over, score %u", (unsigned)g.score); }
        if (g.over && (g.tick % 180 == 0)) { game_init(&g, (uint32_t)esp_timer_get_time(), 0); }   // auto restart
        int64_t now = esp_timer_get_time();
        if (now - last_present >= 33000) {                  // draw at ~30 fps, logic stays at 60 Hz
            draw(&g, fps); board_present(s_fb); last_present = now; frames++;
        }
        if (now - fps_t >= 1000000) { fps = frames; frames = 0; fps_t = now; ESP_LOGI(TAG, "fps %d score %u phase %d", fps, (unsigned)g.score, g.phase + 1); }
        next += dt; int64_t wait = next - esp_timer_get_time();
        if (wait > 1000) vTaskDelay(pdMS_TO_TICKS(wait / 1000)); else if (wait < -100000) next = esp_timer_get_time();
    }
}
