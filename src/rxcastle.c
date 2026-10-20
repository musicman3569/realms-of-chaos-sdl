/* RXCASTLE.C: the castle's creatures (only the bomb's explosion so far). */
#include "behave.h"

/* RXCASTLE.C:238 BOMBEXPLODER: an invisible spawner (OF_RATE, xSpeed 9000: a run every 7
 * ticks) that throws 9 burning puffs (DOTORCHPUFF), the first standing still, the others at
 * random speeds and directions, then goes. */
int bomb_exploder(obj_t *o)
{
    if (++o->gv[1] >= 10) {
        obj_kill(o);
        return 0;
    }
    obj_t *p = spawn_obj(0x24, LIST_MAIN, G.img[IMG_EXPLODE], o->x, o->y);
    if (o->gv[1] == 1) {
        p->xspeed = 0;
        p->yspeed = 0;
    } else {
        p->xspeed = (int16_t)frandom(10000);
        p->yspeed = (int16_t)frandom(10000);
    }
    p->animator = A_DOTORCHPUFF;
    p->xdir = (uint8_t)frandom(2);
    p->max_frames = 6;
    p->anim_inc = 0;
    p->frame_count_max = 0x19;
    p->frame_count = 0x19;
    p->gv[0] = 1;
    p->gv[1] = (int16_t)frandom(2);
    return 0;
}
