#include "game.h"
#include <assert.h>
#include <stdio.h>

static game_input_t none = {0};
static void run(game_t *g, int steps, game_input_t in) { for (int i = 0; i < steps; i++) game_step(g, in); }

static void test_tap_speed(void) {
    assert(game_taps_to_slices(0, 36) == 0);
    assert(game_taps_to_slices(1, 36) == 1);   // any second press fires at least one
    assert(game_taps_to_slices(2, 36) == 1);
    assert(game_taps_to_slices(3, 36) == 2);
    assert(game_taps_to_slices(4, 36) == 3);
    assert(game_taps_to_slices(5, 36) == 4);
    assert(game_taps_to_slices(6, 36) == 5);
    assert(game_taps_to_slices(20, 36) == 5);   // capped
}
static void test_jump(void) {
    game_t g; game_init(&g, 1, 0);
    game_input_t j = { .jump = true };
    game_step(&g, j);
    assert(!g.on_ground && g.cat_vy < 0);
    int top = g.cat_y; for (int i = 0; i < 20; i++) { game_step(&g, j); if (g.cat_y < top) top = g.cat_y; }
    int held = (GAME_GROUND_Y - 20) * GAME_Q - top;
    game_init(&g, 1, 0); game_step(&g, j); game_step(&g, none);   // released at once = short hop
    int top2 = g.cat_y; for (int i = 0; i < 40; i++) { game_step(&g, none); if (g.cat_y < top2) top2 = g.cat_y; }
    int tap = (GAME_GROUND_Y - 20) * GAME_Q - top2;
    assert(held > tap);
    run(&g, 80, none); assert(g.on_ground);
}
static void test_pizza_and_nails(void) {
    game_t g; game_init(&g, 1, 0);
    g.energy = 5;
    game_input_t s = { .strike = true };
    game_step(&g, s); game_step(&g, none);                       // swipe
    for (int i = 0; i < 6; i++) { game_step(&g, s); game_step(&g, none); }   // 6 fast taps
    uint32_t ev = 0; for (int i = 0; i < 40; i++) ev |= game_step(&g, none);
    assert(ev & EV_HELL_CLAW);
    assert(g.energy == 0 && g.last_shot_slices == 5);
}
static void test_no_energy_no_nails(void) {
    game_t g; game_init(&g, 1, 0); g.energy = 0;
    game_input_t s = { .strike = true };
    uint32_t ev = 0;
    for (int i = 0; i < 10; i++) { ev |= game_step(&g, s); ev |= game_step(&g, none); }
    assert(!(ev & EV_NAILS));
}
static void test_head_bounce(void) {
    game_t g; game_init(&g, 1, 2);
    g.on_ground = false; g.cat_vy = 3 * GAME_Q; g.cat_y = (GAME_GROUND_Y - 44 - 20 + 8) * GAME_Q;
    g.obj[0] = (game_obj_t){ OBJ_HUMAN, 48 * GAME_Q, (GAME_GROUND_Y - 44) * GAME_Q, 20, 44 };
    uint32_t ev = game_step(&g, none);
    assert(ev & EV_HEAD_BOUNCE); assert(g.cat_vy < 0); assert(g.lives == 3);
}
static void test_difficulty_grows(void) {
    for (int p = 1; p < GAME_PHASES; p++) { assert(game_speed_q8(p) > game_speed_q8(p - 1)); assert(game_spawn_gap_px(p) * GAME_Q / game_speed_q8(p) <= game_spawn_gap_px(p - 1) * GAME_Q / game_speed_q8(p - 1));   // less running time between obstacle groups }
    assert(game_speed_q8(11) == game_speed_q8(0) * 188 / 100);
}
static void test_phase_and_meme(void) {
    game_t g; game_init(&g, 7, 2);
    g.distance = g.phase_len - 1;
    uint32_t ev = game_step(&g, none);
    assert((ev & EV_PHASE_CLEAR) && (ev & EV_MEME_UNLOCK) && g.phase == 3 && (g.unlocked_memes & 1));
    game_init(&g, 7, 0); g.distance = g.phase_len - 1; ev = game_step(&g, none);
    assert((ev & EV_PHASE_CLEAR) && !(ev & EV_MEME_UNLOCK));
}
static void test_determinism(void) {
    game_t a, b; game_init(&a, 42, 0); game_init(&b, 42, 0);
    for (int i = 0; i < 3000; i++) { game_input_t in = { (i / 17) % 3 == 0, (i / 29) % 5 == 0, (i / 11) % 7 == 0 }; game_step(&a, in); game_step(&b, in); }
    assert(a.score == b.score && a.lives == b.lives && a.tick == b.tick && a.distance == b.distance);
}
static void test_game_over(void) {
    game_t g; game_init(&g, 3, 0);
    for (int i = 0; i < 60 * 120 && !g.over; i++) game_step(&g, none);    // never acts: must eventually lose
    assert(g.over && g.lives == 0);
}
int main(void) {
    test_tap_speed(); test_jump(); test_pizza_and_nails(); test_no_energy_no_nails(); test_head_bounce();
    test_difficulty_grows(); test_phase_and_meme(); test_determinism(); test_game_over();
    puts("all game_core tests passed"); return 0;
}
