// Headless autopilot: plays the game core for a while and prints what happened (no board needed).
#include "game.h"
#include <stdio.h>
int main(void) {
    game_t g; game_init(&g, 12345, 0);
    for (int t = 0; t < 60 * 200 && !g.over; t++) {
        game_input_t in = {0};
        for (int i = 0; i < GAME_MAX_OBJ; i++) { const game_obj_t *o = &g.obj[i]; int dx = o->x / GAME_Q - 48;
            if (o->type == OBJ_LOW && dx > -26 && dx < 50) in.jump = true;
            if (o->type == OBJ_HIGH && dx > -20 && dx < 40) in.duck = true;
            if ((o->type == OBJ_BREAKABLE || o->type == OBJ_ROCKET) && dx > 20 && dx < 36) in.strike = (t & 1);
            if (o->type == OBJ_HUMAN && dx > -10 && dx < 52) in.jump = true; }
        uint32_t ev = game_step(&g, in);
        if (ev & (EV_PHASE_CLEAR | EV_MEME_UNLOCK | EV_GAME_OVER)) printf("t=%5.1fs phase=%d score=%u lives=%d%s%s\n", t / 60.0, g.phase, g.score, g.lives, (ev & EV_MEME_UNLOCK) ? "  MEME UNLOCKED" : "", (ev & EV_GAME_OVER) ? "  GAME OVER" : "");
    }
    printf("end: phase=%d score=%u lives=%d\n", g.phase, g.score, g.lives); return 0;
}
