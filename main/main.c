// Cat from Hell: firmware main. Game logic runs at 60 Hz (game_core, logical 240x240); drawing is done at the panel's
// full 480x480 resolution into a hidden frame buffer that is flipped each frame (2 sprite pixels per logical unit).
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "board.h"
#include "game.h"
#include "sprites.h"
#include "font.h"

static const char *TAG = "cat";
#define W 480
#define H 480
#define PLAY_Y 384                                       // top of the touch-button strip (physical px)
#define FLOOR_Y (GAME_GROUND_Y * 2)                      // 360
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static uint16_t *fb;                                     // current back buffer

// ---------------------------------------------------------------- drawing helpers (physical pixels)
static void rect(int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) { w = W - x; }
    if (y + h > H) { h = H - y; }
    if (w <= 0 || h <= 0) { return; }
    for (int j = 0; j < h; j++) { uint16_t *p = &fb[(y + j) * W + x]; for (int i = 0; i < w; i++) { p[i] = c; } }
}
static void circle(int cx, int cy, int r, uint16_t c) {
    for (int j = -r; j <= r; j++) {
        int dx = 0; while ((dx + 1) * (dx + 1) + j * j <= r * r) { dx++; }
        rect(cx - dx, cy + j, 2 * dx + 1, 1, c);
    }
}
static void blit(const sprite_t *sp, int x, int y) {           // x,y physical top-left
    for (int j = 0; j < sp->h; j++) {
        int yy = y + j; if (yy < 0 || yy >= H) { continue; }
        for (int i = 0; i < sp->w; i++) {
            int xx = x + i; if (xx < 0 || xx >= W) { continue; }
            uint16_t p = sp->px[j * sp->w + i];
            if (p != 0xF81F) { fb[yy * W + xx] = p; }
        }
    }
}
static uint16_t mix(uint16_t d, uint16_t s, int a) {            // a 0..255 of s over d (RGB565)
    int dr = d >> 11, dg = (d >> 5) & 63, db = d & 31, sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
    return (uint16_t)((((dr * (255 - a) + sr * a) / 255) << 11) | (((dg * (255 - a) + sg * a) / 255) << 5) | ((db * (255 - a) + sb * a) / 255));
}
static int text_w(const font_t *f, const char *s) { int w = 0; for (; *s; s++) { if (*s >= 32 && *s < 127) { w += f->g[*s - 32].adv; } } return w; }
static void glyphs(const font_t *f, int x, int y, const char *s, uint16_t col) {   // y = top of the line
    int base = y + f->ascent;
    for (; *s; s++) {
        if (*s < 32 || *s > 126) { continue; }
        const glyph_t *g = &f->g[*s - 32];
        for (int j = 0; j < g->h; j++) {
            int yy = base + g->y + j; if (yy < 0 || yy >= H) { continue; }
            for (int i = 0; i < g->w; i++) {
                int xx = x + g->x + i; uint8_t a = f->a[g->off + j * g->w + i];
                if (!a || xx < 0 || xx >= W) { continue; }
                fb[yy * W + xx] = a > 200 ? col : mix(fb[yy * W + xx], col, a);
            }
        }
        x += g->adv;
    }
}
static void text(const font_t *f, int cx, int y, const char *s, uint16_t col, bool center) {   // black outline for readability
    int x = center ? cx - text_w(f, s) / 2 : cx;
    for (int dy = -1; dy <= 2; dy += 3) { for (int dx = -1; dx <= 2; dx += 3) { glyphs(f, x + dx, y + dy, s, RGB(20, 10, 10)); } }
    glyphs(f, x, y, s, col);
}

// ---------------------------------------------------------------- world: living room (world 1)
static void world_room(uint32_t scroll) {
    static uint16_t row[W];
    uint32_t par = scroll / 4;
    for (int x = 0; x < W; x++) { row[x] = (((x + par) / 48) & 1) ? RGB(250, 224, 182) : RGB(255, 238, 205); }
    for (int y = 0; y < 348; y++) { memcpy(&fb[y * W], row, sizeof row); }
    rect(0, 0, W, 14, RGB(214, 170, 120)); rect(0, 14, W, 3, RGB(176, 128, 84));            // ceiling edge
    // decorations scroll slowly (parallax); period 760 px
    const int items[3] = { 120, 380, 610 };
    for (int k = 0; k < 3; k++) {
        int x = (int)(((items[k] - (int)(scroll / 3)) % 760 + 760) % 760) - 130;
        if (x > W) { continue; }
        if (k == 0) {                                                                       // window with sun and cloud
            rect(x, 56, 118, 138, RGB(120, 76, 40)); rect(x + 8, 64, 102, 122, RGB(150, 212, 255));
            circle(x + 82, 98, 14, RGB(255, 214, 64)); rect(x + 18, 140, 44, 12, RGB(255, 255, 255)); rect(x + 30, 132, 26, 12, RGB(255, 255, 255));
            rect(x + 57, 64, 4, 122, RGB(120, 76, 40)); rect(x + 8, 122, 102, 4, RGB(120, 76, 40));
            rect(x - 6, 192, 130, 8, RGB(150, 100, 56));
        } else if (k == 1) {                                                                // picture frame (cat portrait)
            rect(x, 92, 84, 104, RGB(110, 62, 30)); rect(x + 7, 99, 70, 90, RGB(190, 230, 190));
            circle(x + 42, 150, 22, RGB(245, 138, 31)); rect(x + 24, 120, 8, 12, RGB(245, 138, 31)); rect(x + 52, 120, 8, 12, RGB(245, 138, 31));
            circle(x + 33, 148, 4, RGB(255, 255, 255)); circle(x + 51, 148, 4, RGB(255, 255, 255)); rect(x + 36, 160, 12, 3, RGB(20, 10, 10));
        } else {                                                                            // wall clock
            circle(x + 30, 100, 36, RGB(110, 62, 30)); circle(x + 30, 100, 30, RGB(255, 252, 240));
            rect(x + 28, 78, 4, 24, RGB(20, 10, 10)); rect(x + 30, 98, 18, 4, RGB(20, 10, 10)); circle(x + 30, 100, 4, RGB(200, 40, 40));
        }
    }
    rect(0, 340, W, 20, RGB(150, 98, 58)); rect(0, 340, W, 3, RGB(196, 140, 90));           // skirting board
    rect(0, FLOOR_Y, W, PLAY_Y - FLOOR_Y, RGB(205, 142, 80));                               // floor planks
    rect(0, FLOOR_Y, W, 3, RGB(120, 76, 40)); rect(0, FLOOR_Y + 12, W, 2, RGB(170, 112, 62));
    for (int half = 0; half < 2; half++) {
        for (int x = -(int)((scroll + half * 48) % 96); x < W; x += 96) { rect(x, FLOOR_Y + half * 12, 3, 12, RGB(120, 76, 40)); }
    }
}

// ---------------------------------------------------------------- HUD, buttons, screens
static void tri(int cx, int cy, int s, bool up, uint16_t c) {
    for (int j = 0; j < s; j++) { int w = up ? j : s - 1 - j; rect(cx - w, cy + j, 2 * w + 1, 1, c); }
}
static void button(int x, int y, int w, int h, uint16_t col, uint16_t dark) {
    rect(x, y, w, h, dark); rect(x, y, w, h - 6, col); rect(x, y, w, 3, RGB(255, 255, 255));
}
static void controls(const game_input_t *in) {
    rect(0, PLAY_Y, W, H - PLAY_Y, RGB(40, 30, 50));
    button(6, PLAY_Y + 6, 152, 84, in->jump ? RGB(120, 230, 120) : RGB(76, 175, 80), RGB(36, 110, 40));
    button(164, PLAY_Y + 6, 152, 84, in->duck ? RGB(110, 180, 255) : RGB(33, 150, 243), RGB(20, 90, 160));
    button(322, PLAY_Y + 6, 152, 84, in->strike ? RGB(255, 120, 100) : RGB(229, 57, 53), RGB(150, 30, 30));
    tri(82, PLAY_Y + 14, 18, true, RGB(255, 255, 255)); tri(240, PLAY_Y + 34, 18, false, RGB(255, 255, 255));
    for (int i = 0; i < 3; i++) { for (int j = 0; j < 22; j++) { rect(372 + i * 12 + j / 2, PLAY_Y + 14 + j, 5, 1, RGB(255, 255, 255)); } }
    text(&font_small, 82, PLAY_Y + 50, "PULAR", RGB(255, 255, 255), true);   text(&font_small, 82, PLAY_Y + 68, "JUMP", RGB(255, 240, 200), true);
    text(&font_small, 240, PLAY_Y + 50, "ABAIXAR", RGB(255, 255, 255), true); text(&font_small, 240, PLAY_Y + 68, "DUCK", RGB(255, 240, 200), true);
    text(&font_small, 398, PLAY_Y + 50, "ARRANHAR", RGB(255, 255, 255), true); text(&font_small, 398, PLAY_Y + 68, "STRIKE", RGB(255, 240, 200), true);
}
static void hud(const game_t *g, uint32_t hi) {
    char b[40];
    for (int i = 0; i < g->lives; i++) { blit(&spr_ui_heart, 10 + i * 24, 22); }
    snprintf(b, sizeof b, "FASE %d-%d", g->phase / 3 + 1, g->phase % 3 + 1); text(&font_small, 10, 44, b, RGB(255, 255, 255), false);
    snprintf(b, sizeof b, "%u", (unsigned)g->score); text(&font_big, 240, 18, b, RGB(255, 230, 90), true);
    snprintf(b, sizeof b, "RECORDE %u", (unsigned)(hi > g->score ? hi : g->score)); text(&font_small, 240, 54, b, RGB(255, 255, 255), true);
    for (int i = 0; i < GAME_MAX_SLICES; i++) {                                              // pizza energy
        if (i < g->energy) { blit(&spr_obj_pizza, 330 + i * 28, 18); } else { circle(330 + i * 28 + 16, 34, 6, RGB(200, 180, 150)); }
    }
    int pw = (int)((int64_t)g->distance * 120 / (g->phase_len ? g->phase_len : 1)); if (pw > 120) { pw = 120; }
    rect(330, 58, 124, 8, RGB(40, 30, 50)); rect(332, 60, pw, 4, RGB(76, 175, 80));          // phase progress
}
static void play_scene(const game_t *g, uint32_t hi, const game_input_t *in) {
    world_room((uint32_t)(g->distance / GAME_Q) * 2);
    for (int i = 0; i < GAME_MAX_OBJ; i++) {
        const game_obj_t *o = &g->obj[i]; if (!o->type) { continue; }
        const sprite_t *sp = o->type == OBJ_LOW ? &spr_obj_cushion : o->type == OBJ_HIGH ? &spr_obj_lamp : o->type == OBJ_BREAKABLE ? &spr_obj_vase
                           : o->type == OBJ_HUMAN ? &spr_obj_human : o->type == OBJ_PIZZA ? &spr_obj_pizza : &spr_obj_rocket;
        int ox = o->x / GAME_Q, oy = o->y / GAME_Q, lw = sp->w / 2, lh = sp->h / 2;
        int x = ox + (o->w - lw) / 2;
        int y = (o->type == OBJ_HIGH) ? oy : (o->type == OBJ_PIZZA || o->type == OBJ_ROCKET) ? oy + (o->h - lh) / 2 : oy + o->h - lh;   // lamp hangs, flyers centred, the rest stand on the floor
        blit(sp, x * 2, y * 2);
    }
    for (int i = 0; i < GAME_MAX_NAILS; i++) { if (g->nail[i].active) { blit(&spr_fx_nail, g->nail[i].x / GAME_Q * 2, (g->nail[i].y / GAME_Q - 1) * 2); } }
    if (!(g->invuln && (g->tick & 4))) {
        const sprite_t *cs = g->state == CAT_DUCK ? &spr_cat_duck : g->state == CAT_JUMP ? &spr_cat_jump : g->strike_timer ? &spr_cat_strike : ((g->tick / 6) & 1) ? &spr_cat_run1 : &spr_cat_run0;
        int hh = g->state == CAT_DUCK ? 10 : 20;
        blit(cs, 80, (g->cat_y / GAME_Q + hh - 30) * 2);
    }
    hud(g, hi);
    controls(in);
}
static void panel(int x, int y, int w, int h) {
    rect(x - 4, y - 4, w + 8, h + 8, RGB(20, 10, 10)); rect(x, y, w, h, RGB(58, 40, 78)); rect(x, y, w, 4, RGB(110, 80, 140));
}
static void title_screen(uint32_t tick, uint32_t hi) {
    char b[40];
    panel(50, 70, 380, 250);
    text(&font_big, 240, 92, "CAT FROM HELL", RGB(255, 140, 30), true);
    text(&font_small, 240, 136, "O GATO DO INFERNO", RGB(255, 255, 255), true);
    blit(((tick / 8) & 1) ? &spr_cat_run1 : &spr_cat_run0, 200, 168);
    if ((tick / 30) & 1) { text(&font_small, 240, 256, "TOQUE PARA JOGAR", RGB(255, 230, 90), true); }
    else { text(&font_small, 240, 256, "TAP TO PLAY", RGB(255, 230, 90), true); }
    snprintf(b, sizeof b, "RECORDE / BEST: %u", (unsigned)hi); text(&font_small, 240, 284, b, RGB(255, 255, 255), true);
}
static const int BTN_Y = 250, BTN_H = 70, BTN_AX = 70, BTN_BX = 250, BTN_W = 160;
static void over_screen(const game_t *g, uint32_t hi, bool new_best) {
    char b[48];
    panel(40, 60, 400, 290);
    text(&font_big, 240, 72, "FIM DE JOGO", RGB(255, 90, 80), true);
    text(&font_small, 240, 112, "GAME OVER", RGB(255, 255, 255), true);
    snprintf(b, sizeof b, "PONTOS / SCORE: %u", (unsigned)g->score); text(&font_small, 240, 146, b, RGB(255, 230, 90), true);
    if (new_best) { text(&font_small, 240, 176, "NOVO RECORDE! NEW BEST!", RGB(120, 255, 120), true); }
    else { snprintf(b, sizeof b, "RECORDE / BEST: %u", (unsigned)hi); text(&font_small, 240, 176, b, RGB(255, 255, 255), true); }
    button(BTN_AX, BTN_Y, BTN_W, BTN_H, RGB(76, 175, 80), RGB(36, 110, 40));
    button(BTN_BX, BTN_Y, BTN_W, BTN_H, RGB(229, 57, 53), RGB(150, 30, 30));
    text(&font_small, BTN_AX + 80, BTN_Y + 12, "CONTINUAR", RGB(255, 255, 255), true); text(&font_small, BTN_AX + 80, BTN_Y + 34, "CONTINUE", RGB(255, 240, 200), true);
    text(&font_small, BTN_BX + 80, BTN_Y + 12, "REINICIAR", RGB(255, 255, 255), true); text(&font_small, BTN_BX + 80, BTN_Y + 34, "RESTART", RGB(255, 240, 200), true);
}

// ---------------------------------------------------------------- input, sound, high score
static int read_touch(touch_pt_t *p) { return board_touch_read(p, 5); }
static game_input_t buttons_from(const touch_pt_t *p, int n) {
    game_input_t in = { 0 };
    for (int i = 0; i < n; i++) if (p[i].down && p[i].y >= PLAY_Y) {
        if (p[i].x < 160) { in.jump = true; } else if (p[i].x < 320) { in.duck = true; } else { in.strike = true; }
    }
    return in;
}
static void play_sound(uint32_t ev) {
    if (ev & EV_JUMP) { board_beep(0, 25); }
    if (ev & EV_HEAD_BOUNCE) { board_beep(0, 60); }
    if (ev & EV_PIZZA) { board_beep(0, 20); }
    if (ev & (EV_BREAK | EV_NAIL_HIT)) { board_beep(0, 40); }
    if (ev & EV_NAILS) { board_beep(0, 30); }
    if (ev & EV_HURT) { board_beep(0, 150); }
    if (ev & (EV_PHASE_CLEAR | EV_MEME_UNLOCK)) { board_beep(0, 250); }
    if (ev & EV_GAME_OVER) { board_beep(0, 500); }
}
static uint32_t hi_load(void) { nvs_handle_t h; uint32_t v = 0; if (nvs_open("cat", NVS_READONLY, &h) == ESP_OK) { nvs_get_u32(h, "hi", &v); nvs_close(h); } return v; }
static void hi_save(uint32_t v) { nvs_handle_t h; if (nvs_open("cat", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u32(h, "hi", v); nvs_commit(h); nvs_close(h); } }

typedef enum { UI_TITLE, UI_PLAY, UI_OVER } ui_t;

// What the render task needs, copied from the logic loop under a mutex so the two never touch the same data.
typedef struct { game_t g; ui_t ui; game_input_t in; uint32_t hi, idle; bool new_best; int banner; } view_t;
static view_t s_view; static SemaphoreHandle_t s_view_mx;

// Render task (core 1): draws the latest snapshot at the display's pace; game logic keeps its own steady 60 Hz on core 0.
static void render_task(void *arg) {
    static view_t v; int64_t fps_t = esp_timer_get_time(); int frames = 0;
    for (;;) {
        xSemaphoreTake(s_view_mx, portMAX_DELAY); v = s_view; xSemaphoreGive(s_view_mx);
        fb = board_back_buffer();
        if (fb) {
            if (v.ui == UI_TITLE) { world_room(v.idle * 3); controls(&v.in); title_screen(v.idle, v.hi); }
            else {
                play_scene(&v.g, v.hi, &v.in);
                if (v.banner > 0) { text(&font_big, 240, 150, "FASE COMPLETA!", RGB(255, 230, 90), true); text(&font_small, 240, 192, "PHASE CLEAR!", RGB(255, 255, 255), true); }
                if (v.ui == UI_OVER) { over_screen(&v.g, v.hi, v.new_best); }
            }
            board_flip(); frames++;
        } else { vTaskDelay(pdMS_TO_TICKS(10)); }
        int64_t now = esp_timer_get_time();
        if (now - fps_t >= 1000000) { ESP_LOGI(TAG, "fps %d score %u phase %d", frames, (unsigned)v.g.score, v.g.phase + 1); frames = 0; fps_t = now; }
    }
}

void app_main(void) {
    esp_err_t ne = nvs_flash_init();
    if (ne == ESP_ERR_NVS_NO_FREE_PAGES || ne == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }
    esp_err_t e = board_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "board_init failed (%s)", esp_err_to_name(e)); return; }
    static game_t g; game_init(&g, (uint32_t)esp_timer_get_time(), 0);
    uint32_t hi = hi_load(); bool new_best = false; ui_t ui = UI_TITLE;
    s_view_mx = xSemaphoreCreateMutex();
    s_view.g = g; s_view.ui = ui; s_view.hi = hi;
    xTaskCreatePinnedToCore(render_task, "render", 8192, NULL, 5, NULL, 1);
    int64_t next = esp_timer_get_time(), ui_t0 = next;
    bool prev_down = true; int banner = 0; uint32_t idle = 0;
    const int64_t dt = 1000000 / GAME_STEP_HZ;
    for (;;) {
        touch_pt_t tp[5]; int n = read_touch(tp);
        bool down = false; int tx = 0, ty = 0;
        for (int i = 0; i < n; i++) { if (tp[i].down) { down = true; tx = tp[i].x; ty = tp[i].y; } }
        bool tap = down && !prev_down; prev_down = down;
        int64_t now = esp_timer_get_time();
        game_input_t in = buttons_from(tp, n);

        if (ui == UI_PLAY) {
            uint32_t ev = game_step(&g, in);
            play_sound(ev);
            if (banner > 0) { banner--; }
            if (ev & EV_PHASE_CLEAR) { banner = 60; }
            if (ev & EV_GAME_OVER) {
                ui = UI_OVER; ui_t0 = now; new_best = g.score > hi;
                if (new_best) { hi = g.score; hi_save(hi); }
                ESP_LOGI(TAG, "game over, score %u", (unsigned)g.score);
            }
        } else if (ui == UI_TITLE) {
            idle++;
            if (tap && now - ui_t0 > 600000) { game_init(&g, (uint32_t)now, 0); ui = UI_PLAY; board_beep(0, 60); }
        } else if (tap && now - ui_t0 > 800000) {                    // game over: ignore taps right after dying
            if (tx >= BTN_AX && tx < BTN_AX + BTN_W && ty >= BTN_Y && ty < BTN_Y + BTN_H) { game_continue(&g); ui = UI_PLAY; board_beep(0, 60); }
            else if (tx >= BTN_BX && tx < BTN_BX + BTN_W && ty >= BTN_Y && ty < BTN_Y + BTN_H) { game_init(&g, (uint32_t)now, 0); ui = UI_PLAY; board_beep(0, 60); }
        }
        xSemaphoreTake(s_view_mx, portMAX_DELAY);
        s_view.g = g; s_view.ui = ui; s_view.in = in; s_view.hi = hi; s_view.idle = idle; s_view.new_best = new_best; s_view.banner = banner;
        xSemaphoreGive(s_view_mx);

        next += dt; int64_t wait = next - esp_timer_get_time();
        if (wait > 1000) { vTaskDelay(pdMS_TO_TICKS(wait / 1000)); } else if (wait < -100000) { next = esp_timer_get_time(); }
    }
}
