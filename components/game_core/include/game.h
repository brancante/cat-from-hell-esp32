// Cat from Hell: platform-independent game core.
// Pure C11, integer maths only, no allocation: runs identically on the ESP32-S3 and on the host (tests).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define GAME_STEP_HZ 60          // fixed timestep
#define GAME_W 240               // logical render size; shown pixel-doubled on 480x480
#define GAME_H 240
#define GAME_GROUND_Y 180        // y of the floor line
#define GAME_MAX_OBJ 24
#define GAME_MAX_NAILS 6
#define GAME_MAX_SLICES 5
#define GAME_PHASES 12
#define GAME_Q 256               // fixed point: 1 px = 256 units

typedef enum { OBJ_NONE, OBJ_LOW, OBJ_HIGH, OBJ_BREAKABLE, OBJ_HUMAN, OBJ_PIZZA, OBJ_ROCKET } obj_type_t;
typedef enum { CAT_RUN, CAT_JUMP, CAT_DUCK, CAT_STRIKE, CAT_HURT } cat_state_t;

typedef struct { bool jump, duck, strike; } game_input_t;      // held state of the 3 buttons

typedef enum {
    EV_NONE = 0, EV_JUMP = 1 << 0, EV_STRIKE = 1 << 1, EV_NAILS = 1 << 2, EV_HELL_CLAW = 1 << 3,
    EV_PIZZA = 1 << 4, EV_BREAK = 1 << 5, EV_HEAD_BOUNCE = 1 << 6, EV_HURT = 1 << 7,
    EV_PHASE_CLEAR = 1 << 8, EV_MEME_UNLOCK = 1 << 9, EV_GAME_OVER = 1 << 10, EV_NAIL_HIT = 1 << 11,
} game_event_t;

typedef struct { obj_type_t type; int32_t x, y; int16_t w, h; } game_obj_t;   // x,y in Q8 px (top-left)
typedef struct { bool active; int32_t x, y; int32_t vx; int32_t range_left; } game_nail_t;

typedef struct {
    // cat
    int32_t cat_y, cat_vy;          // Q8 px, cat_y = top of the cat
    cat_state_t state;
    bool on_ground, jump_prev, strike_prev, jump_cut, double_used;
    int16_t invuln;                  // steps of blinking safety
    int16_t strike_timer;            // steps left of the claw swipe animation
    int16_t tap_window;              // steps left in the nail-shot tap window (0 = closed)
    int16_t taps;                    // Strike taps counted in the window
    uint8_t lives, energy;           // energy = pizza slices 0..5
    uint32_t score;
    // world
    uint8_t phase;                   // 0..11
    int32_t distance;                // Q8 px scrolled in this phase
    int32_t phase_len;               // Q8 px
    uint32_t rng, spawn_in;          // deterministic generator
    uint32_t tick;
    bool duck_prev;                  // duck button edge detection
    int16_t duck_hold;               // a tap keeps the cat down for a moment
    bool over;
    uint16_t unlocked_memes;         // bit n = meme n unlocked
    game_obj_t obj[GAME_MAX_OBJ];
    game_nail_t nail[GAME_MAX_NAILS];
    uint32_t events;                 // bitmask of game_event_t raised by the last step
    uint8_t last_shot_slices;
} game_t;

void game_init(game_t *g, uint32_t seed, uint8_t start_phase);
void game_continue(game_t *g);   // after game over: restart the current phase, full lives, score kept (-10%)
// Advance one fixed step (1/60 s). Returns the event mask for this step (sound, effects).
uint32_t game_step(game_t *g, game_input_t in);

// Tunables, exposed for tests and for the level file.
int  game_speed_q8(uint8_t phase);                 // px per step * Q8
int  game_taps_to_slices(int taps, int window_steps);
int  game_spawn_gap_px(uint8_t phase);
