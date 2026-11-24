#include "game.h"
#include <string.h>

// ---- tunables (see plan: speed 1.0x .. 1.6x, shorter gaps each phase) ----
#define BASE_SPEED_Q8   (3 * GAME_Q)            // 3 px/step = 180 px/s at 1.0x
#define CAT_X           48
#define CAT_W           24
#define CAT_H           20
#define CAT_DUCK_H      10
#define GRAVITY_Q8      77                       // 0.30 px/step^2
#define JUMP_V_Q8       (-5 * GAME_Q)
#define BOUNCE_V_Q8     (-8 * GAME_Q)            // head bounce
#define STRIKE_STEPS    12
#define TAP_WINDOW_STEPS 36                      // 0.6 s
#define PHASE_SECONDS   60
#define NAIL_SPEED_Q8   (6 * GAME_Q)
#define SPEED_PCT(p)    (100 + (p) * 5 + ((p) >= 11 ? 5 : 0))   // 100..160

static uint32_t rnd(game_t *g) { uint32_t x = g->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g->rng = x ? x : 1; }

int game_speed_q8(uint8_t phase) { return (BASE_SPEED_Q8 * SPEED_PCT(phase)) / 100; }
int game_spawn_gap_px(uint8_t phase) { int gap = 150 - phase * 8; return gap < 70 ? 70 : gap; }

int game_taps_to_slices(int taps, int window_steps) {
    if (taps <= 0) return 0;
    int rate_x10 = taps * GAME_STEP_HZ * 10 / window_steps;     // taps per second * 10
    int slices = rate_x10 / 20;                                  // 2 taps/s per slice
    if (slices < 1) slices = 1;                                  // any second press fires at least 1
    return slices > GAME_MAX_SLICES ? GAME_MAX_SLICES : slices;
}

static void spawn(game_t *g, obj_type_t t, int w, int h, int y_px) {
    for (int i = 0; i < GAME_MAX_OBJ; i++) if (g->obj[i].type == OBJ_NONE) {
        g->obj[i] = (game_obj_t){ t, (GAME_W + 8) * GAME_Q, y_px * GAME_Q, (int16_t)w, (int16_t)h };
        return;
    }
}

static void spawn_next(game_t *g) {
    uint32_t r = rnd(g) % 100;
    int ph = g->phase;
    if (r < 28)       spawn(g, OBJ_LOW, 18, 16, GAME_GROUND_Y - 16);                 // jump over
    else if (r < 46 && ph >= 1) spawn(g, OBJ_HIGH, 24, 12, GAME_GROUND_Y - 34);       // duck under
    else if (r < 66)  spawn(g, OBJ_BREAKABLE, 16, 18, GAME_GROUND_Y - 18);            // strike it
    else if (r < 78 && ph >= 2) spawn(g, OBJ_HUMAN, 20, 44, GAME_GROUND_Y - 44);      // jump on its head
    else if (r < 90 && ph >= 3) spawn(g, OBJ_ROCKET, 22, 14, GAME_GROUND_Y - 30 - (int)(rnd(g) % 3) * 8);
    else              spawn(g, OBJ_PIZZA, 12, 12, GAME_GROUND_Y - 22 - (int)(rnd(g) % 3) * 14);
}

void game_init(game_t *g, uint32_t seed, uint8_t start_phase) {
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 0x9E3779B9u;
    g->lives = 3; g->phase = start_phase;
    g->cat_y = (GAME_GROUND_Y - CAT_H) * GAME_Q; g->on_ground = true; g->state = CAT_RUN;
    g->phase_len = (int32_t)((int64_t)game_speed_q8(start_phase) * GAME_STEP_HZ * PHASE_SECONDS);
    g->spawn_in = 60 * GAME_Q;
}

static bool overlap(int32_t ax, int32_t ay, int aw, int ah, const game_obj_t *o) {
    int32_t ax2 = ax + aw * GAME_Q, ay2 = ay + ah * GAME_Q;
    int32_t bx2 = o->x + o->w * GAME_Q, by2 = o->y + o->h * GAME_Q;
    return ax < bx2 && ax2 > o->x && ay < by2 && ay2 > o->y;
}

static void fire_nails(game_t *g, int slices) {
    // Power grows with slices: more nails, more range (see plan table).
    int count = slices > 4 ? 5 : slices;
    for (int n = 0; n < count && n < GAME_MAX_NAILS; n++) {
        for (int i = 0; i < GAME_MAX_NAILS; i++) if (!g->nail[i].active) {
            g->nail[i] = (game_nail_t){ true, (CAT_X + CAT_W) * GAME_Q, g->cat_y + (4 + n * 3 - count) * GAME_Q,
                                        NAIL_SPEED_Q8, (60 + slices * 40) * GAME_Q };
            break;
        }
    }
    g->last_shot_slices = (uint8_t)slices;
    g->events |= EV_NAILS | (slices >= 5 ? EV_HELL_CLAW : 0);
}

static void hurt(game_t *g) {
    if (g->invuln) return;
    g->events |= EV_HURT; g->invuln = 60; g->state = CAT_HURT;
    if (g->lives && --g->lives == 0) { g->over = true; g->events |= EV_GAME_OVER; }
}

uint32_t game_step(game_t *g, game_input_t in) {
    g->events = 0;
    if (g->over) return g->events;
    g->tick++;
    const int speed = game_speed_q8(g->phase);

    // ---- input: jump ----
    bool jump_edge = in.jump && !g->jump_prev;
    if (jump_edge && g->on_ground) { g->cat_vy = JUMP_V_Q8; g->on_ground = false; g->jump_cut = false; g->double_used = false; g->events |= EV_JUMP; }
    else if (jump_edge && !g->on_ground && !g->double_used) { g->cat_vy = JUMP_V_Q8 * 3 / 4; g->double_used = true; g->events |= EV_JUMP; }
    if (!in.jump && !g->on_ground && g->cat_vy < 0 && !g->jump_cut) { g->cat_vy /= 2; g->jump_cut = true; }   // short hop
    g->jump_prev = in.jump;

    // ---- input: strike, then fast taps release pizza energy ----
    bool strike_edge = in.strike && !g->strike_prev;
    if (strike_edge) {
        if (g->tap_window > 0) { g->taps++; }
        else { g->strike_timer = STRIKE_STEPS; g->tap_window = (g->energy > 0) ? TAP_WINDOW_STEPS : 0; g->taps = 0; g->events |= EV_STRIKE; }
    }
    g->strike_prev = in.strike;
    if (g->tap_window > 0 && --g->tap_window == 0) {
        if (g->taps > 0 && g->energy > 0) {
            int s = game_taps_to_slices(g->taps, TAP_WINDOW_STEPS);
            if (s > g->energy) s = g->energy;
            g->energy = (uint8_t)(g->energy - s);
            fire_nails(g, s);
        }
        g->taps = 0;
    }
    if (g->strike_timer) g->strike_timer--;

    // ---- cat physics ----
    int ch = CAT_H;
    if (in.duck && g->on_ground) ch = CAT_DUCK_H;
    if (!g->on_ground) {
        g->cat_vy += GRAVITY_Q8; if (in.duck) g->cat_vy += GRAVITY_Q8;   // duck in the air drops faster
        g->cat_y += g->cat_vy;
    }
    int32_t floor_y = (GAME_GROUND_Y - ch) * GAME_Q;
    if (g->cat_y >= floor_y) { g->cat_y = floor_y; g->cat_vy = 0; g->on_ground = true; }
    int32_t top = g->cat_y;
    if (g->invuln) g->invuln--;
    g->state = g->invuln > 40 ? CAT_HURT : !g->on_ground ? CAT_JUMP : in.duck ? CAT_DUCK : g->strike_timer ? CAT_STRIKE : CAT_RUN;

    // ---- world scroll, spawn ----
    g->distance += speed;
    if (g->spawn_in <= (uint32_t)speed) { spawn_next(g); g->spawn_in = (uint32_t)game_spawn_gap_px(g->phase) * GAME_Q + (rnd(g) % 40) * GAME_Q; }
    else g->spawn_in -= (uint32_t)speed;

    // ---- nails ----
    for (int i = 0; i < GAME_MAX_NAILS; i++) if (g->nail[i].active) {
        game_nail_t *n = &g->nail[i];
        n->x += n->vx; n->range_left -= n->vx;
        if (n->range_left <= 0 || n->x > GAME_W * GAME_Q) { n->active = false; continue; }
        game_obj_t nb = { OBJ_NONE, n->x, n->y, 6, 3 };
        for (int j = 0; j < GAME_MAX_OBJ; j++) {
            game_obj_t *o = &g->obj[j];
            if ((o->type == OBJ_BREAKABLE || o->type == OBJ_ROCKET || o->type == OBJ_HUMAN || o->type == OBJ_LOW) && overlap(nb.x, nb.y, 6, 3, o)) {
                if (o->type != OBJ_HUMAN) { g->score += 15; o->type = OBJ_NONE; g->events |= EV_NAIL_HIT | EV_BREAK; }
                else g->events |= EV_NAIL_HIT;
                if (g->last_shot_slices < 4) n->active = false;      // 4+ slices pierce
                break;
            }
        }
    }

    // ---- objects: move + collide ----
    int32_t cx = CAT_X * GAME_Q;
    int32_t claw_x = (CAT_X + CAT_W) * GAME_Q;
    for (int i = 0; i < GAME_MAX_OBJ; i++) {
        game_obj_t *o = &g->obj[i];
        if (o->type == OBJ_NONE) continue;
        o->x -= speed + (o->type == OBJ_ROCKET ? 2 * GAME_Q : 0);
        if (o->x + o->w * GAME_Q < 0) { o->type = OBJ_NONE; continue; }
        // claw swipe breaks things right in front
        if (g->strike_timer && (o->type == OBJ_BREAKABLE || o->type == OBJ_ROCKET || o->type == OBJ_LOW) &&
            o->x < claw_x + 14 * GAME_Q && o->x + o->w * GAME_Q > claw_x && o->y + o->h * GAME_Q > top - 6 * GAME_Q) {
            g->score += (o->type == OBJ_ROCKET) ? 30 : 10; o->type = OBJ_NONE; g->events |= EV_BREAK; continue;
        }
        if (!overlap(cx, top, CAT_W, ch, o)) continue;
        switch (o->type) {
        case OBJ_PIZZA: if (g->energy < GAME_MAX_SLICES) g->energy++; g->score += 5; o->type = OBJ_NONE; g->events |= EV_PIZZA; break;
        case OBJ_HUMAN:
            if (g->cat_vy > 0 && top + ch * GAME_Q - o->y < 14 * GAME_Q) {   // landing on the head
                g->cat_vy = BOUNCE_V_Q8; g->on_ground = false; g->double_used = false; g->score += 25; g->events |= EV_HEAD_BOUNCE;
            } else hurt(g);
            break;
        default: hurt(g); break;
        }
    }

    // ---- phase progress ----
    if (g->distance >= g->phase_len) {
        g->events |= EV_PHASE_CLEAR; g->score += 100;
        if (g->phase % 3 == 2) { g->unlocked_memes |= (uint16_t)(1u << (g->phase / 3)); g->events |= EV_MEME_UNLOCK; }
        if (g->phase + 1 < GAME_PHASES) g->phase++;
        g->distance = 0;
        g->phase_len = (int32_t)((int64_t)game_speed_q8(g->phase) * GAME_STEP_HZ * PHASE_SECONDS);
        memset(g->obj, 0, sizeof g->obj);
    }
    return g->events;
}
