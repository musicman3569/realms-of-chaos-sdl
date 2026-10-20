/* RXMUSH.C: the mushroom levels' spore holes. */
#include "behave.h"

/* RXMUSH.C:11 DOSPOREHOLE: an invisible vent (type 0x47, OF_RATE) on a 14-run cycle: it
 * puffs up (MushSporID[2], [1], [0]), spits a spore at the third run (type 0x48: thrown up and
 * to the right, then straight up, then to the left), closes again and vanishes from the
 * sixth. */
int do_spore_hole(obj_t *o)
{
    switch (++o->gv[0]) {
    case 1: case 5:
        o->image = G.img[IMG_MUSHSPOR + 2];
        break;
    case 2: case 4:
        o->image = G.img[IMG_MUSHSPOR + 1];
        break;
    case 3: {
        o->image = G.img[IMG_MUSHSPOR];
        play_sample(0x61, o);
        obj_t *s = spawn_obj(0x48, LIST_MAIN, G.img[IMG_GARGFIRE], (uint16_t)(o->x + 3),
                             (uint16_t)(o->y - 5));
        if (++o->gv[1] == 3) {
            o->gv[1] = 0;
            s->xdir = 0;
        } else {
            s->xdir = 1;
        }
        s->xspeed = o->gv[1] == 0 || o->gv[1] == 2 ? 8000 : 0;
        break;
    }
    case 6:
        o->image = NULL;
        break;
    case 0x0E:
        o->gv[0] = 0;
        break;
    default:
        break;
    }
    return 0;
}

/* RXMUSH.C:39 DOSPORE: a spore (or a statue bomb's flaming blob), priority 0x39. GENERICUP /
 * GENERICFALL call it in flight: it blinks, hurts the player it touches, drifts sideways at
 * xSpeed, and bursts against a wall, the map's edge, the player or the player's weapon;
 * landing (Animator DOSPORE) bursts it too. */
int do_spore(obj_t *o)
{
    if (G.you && obj_collide(o, G.you))
        hit_player(o);
    if (o->blinks == 0) {
        o->blink_color = 0x1D;
        obj_blink(o, 4, 2, 2);
    }
    const bool landed = o->animator == A_DOSPORE;
    if (carry(&o->xpart, o->xspeed) || landed) {
        can_move(o);
        if (o->xdir != 0 && can_go_right(o) && room_right(o))
            o->x++;
        else if (o->xdir == 0 && can_go_left(o) && room_left(o))
            o->x--;
        else
            o->up_offset = 0x0B;
        if (landed)
            o->up_offset = 0x0B;
    }
    if (o->up_offset > 10 || landed || (G.you && obj_collide(o, G.you)) || (o->flags & OF_HIT)) {
        play_sample(0x62, NULL);
        spawn_obj(0x0D, LIST_MAIN, G.img[IMG_EXPLODE], (uint16_t)(o->x - 3),
                  (uint16_t)(o->y - 3))->gv[0] = 0;
        obj_kill(o);
    }
    return 0;
}
