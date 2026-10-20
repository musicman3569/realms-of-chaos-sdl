/* RXSKELF.C: the boars (the skeletons in this module aren't ported yet). */
#include "behave.h"

/* The boar's sprite set: Boar3ID for the fire-breathing boar (xSpeed 8000), Boar1ID for a
 * boar with its spear (GenericVar[0] 0), Boar2ID once it has thrown it. */
static int boar_set(const obj_t *o)
{
    if (o->xspeed == 8000)
        return IMG_BOAR3;
    return o->gv[0] == 0 ? IMG_BOAR1 : IMG_BOAR2;
}

static uint8_t boar_hits(const obj_t *o)
{
    return o->xspeed == 8000 ? 12 : 4;
}

/* RXSKELF.C:11 FDOBOAR (DOBOAR calls it): the boar walks a step at each xSpeed carry (0x1060,
 * or 8000 for the fire boar), following slopes, and turns round (grunting) at walls, the
 * map's edges and ledges, and anyway every 0x8fd ticks. When it's wholly in view, at the
 * player's height, it attacks (DOBOARATTACK, facing the player): with a spear or fire from
 * more than 60 pixels away, without one from closer than 30. */
int do_boar(obj_t *o)
{
    obj_t *you = G.you;
    can_move(o);
    if (o->down_offset > 10)
        o->y = (uint16_t)(o->y + (o->down_offset - 10));
    if (o->up_offset > 10)
        o->y = (uint16_t)(o->y - (o->up_offset - 10));
    if (o->down_offset != 1) {
        if (o->xdir == 0)
            o->x--;
        else
            o->x++;
    }
    next_frame(o, o->xspeed == 8000 ? IMG_BOAR3 : boar_set(o) + 1);
    bool turn = true;
    if (o->counter < 0x8FD) {
        const bool way = o->xdir == 0 ? can_go_left(o) && room_left(o)
                                      : can_go_right(o) && room_right(o);
        turn = !way || o->down_offset == 1;
    }
    if (turn) {
        play_sample(0x5F, o);
        o->frame_count = 1;
        o->counter = 0;
        if (o->xdir == 0) {
            o->x = (uint16_t)(o->x + 2);
            o->xdir = 1;
        } else {
            o->x = (uint16_t)(o->x - 2);
            o->xdir = 0;
        }
    }
    if (you && obj_onscreen(o) && o->y < (uint16_t)(you->y + you->ysize - 5) &&
        (uint16_t)(you->y - 0x10) < o->y && o->x > 0x28) {
        const uint16_t d = (uint16_t)iabs16((int16_t)(o->x - you->x));
        if (o->gv[0] == 0 ? d > 0x3C : d < 0x1E) {
            o->xdir = you->x < o->x ? 0 : 1;
            o->counter = 0;
            obj_image(o, G.img[boar_set(o)]);
            o->animator = A_DOBOARATTACK;
        }
    }
    hit_player(o);
    if (o->flags & OF_HIT) {
        play_sample(0x89, NULL);
        enemy_hit(o, boar_hits(o));
    }
    return 0;
}

/* RXSKELF.C:84 FDOBOARATTACK: the attack, by Counter (ticks): from 101 the wind-up frame
 * (7), from 201 the lunge (8, with a grunt and a roar), from 221 the throw, once: a spear
 * (type 0x52) flying at 32000, or three fireballs (type 6, the enemies' kinds 3-5; on the
 * hard difficulties they drop towards a player below). After 500 ticks it walks again,
 * towards the player; a boar that has thrown its spear charges from then on (GenericVar[0]
 * 1), the fire boar may breathe again (GenericVar[1] 0). The charging boar's lunge
 * (Boar2ID[8]) touching the player makes a noise of its own. */
int do_boar_attack(obj_t *o)
{
    obj_t *you = G.you;
    const int set = boar_set(o);
    if (o->counter < 300) {
        if (o->counter < 0xDD) {
            if (o->counter >= 0xC9) {
                if (o->image != G.img[set + 8]) {
                    play_sample(0x5F, o);
                    if (o->xspeed == 8000)
                        play_sample(0x61, NULL);
                    else
                        play_sample(0x80, o);
                }
                obj_image(o, G.img[set + 8]);
            } else if (o->counter >= 0x65) {
                obj_image(o, G.img[set + 7]);
            }
        } else if (o->gv[0] == 0 && o->gv[1] == 0) {
            o->gv[1]++;
            const int16_t max_x = (int16_t)(G.lv->cols * 16 - 0x29);
            if (o->xspeed == 8000) {
                int16_t x = (int16_t)(o->x + (o->xdir == 0 ? -0x2A : 0x25));
                if (x < 0x12)
                    x = 0x12;
                else if (x > max_x)
                    x = max_x;
                static const uint8_t frame[3] = {6, 3, 0};
                for (int i = 0; i < 3; i++) {
                    obj_t *f = spawn_obj(0x06, LIST_MAIN, G.img[IMG_FIREBALL + frame[i]],
                                         (uint16_t)x, (uint16_t)(o->y - 1));
                    f->gv[1] = o->xdir == 0 ? 3 : 4;
                    if (G.diff_mode > 2 && you && (uint16_t)(o->y + 10) < you->y)
                        f->gv[1] += 10;
                    f->gv[2] = (int16_t)(3 + i);
                }
            } else {
                int16_t x = (int16_t)(o->x + (o->xdir == 0 ? -0x32 : 0x2D));
                if (x < 0x12)
                    x = 0x12;
                else if (x > max_x)
                    x = max_x;
                obj_t *s = spawn_obj(0x52, LIST_MAIN, G.img[IMG_SPEAR], (uint16_t)x,
                                     (uint16_t)(o->y + 3));
                s->xspeed = o->xdir == 0 ? -32000 : 32000;
                s->xdir = o->xdir;
            }
        }
    } else if (o->counter > 500) {
        o->frame_count = o->frame_count_max;
        o->frame_no = 0;
        o->counter = 0;
        o->animator = A_DOBOAR;
        if (o->xspeed < 8000)
            o->gv[0] = 1;
        else
            o->gv[1] = 0;
        o->xdir = you && o->x < you->x ? 1 : 0;
    }
    if (you && obj_collide(you, o) && G.hitagain == 0 && !(G.game_flags & 8) &&
        o->image == G.img[IMG_BOAR2 + 8])
        play_sample(0x7A, NULL);
    hit_player(o);
    if (o->flags & OF_HIT) {
        play_sample(0x89, NULL);
        enemy_hit(o, boar_hits(o));
    }
    return 0;
}

/* RXSKELF.C:170 FDOBOARSPEAR: the thrown spear (type 0x52, priority 0x3e). In flight
 * (GENERICUP / GENERICFALL call this) it moves at its xSpeed and hurts; a wall, the map's edge
 * or the player (with a thud or a stab) bounces it back at 10000 and makes it harmless
 * (GenericVar[1]). When it lands (Animator DOBOARSPEAR) it hops once more, then lies there
 * for about 500 ticks and blinks out. */
int do_boar_spear(obj_t *o)
{
    obj_t *you = G.you;
    if (o->gv[1] == 0)
        hit_player(o);
    if (o->animator == A_DOBOARSPEAR) {
        if (o->gv[2] == 0) {
            o->gv[2] = 1;
            o->gv[1] = 1;
            o->gv[4] = 0x55;
            o->yspeed = -7000;
            o->animator = A_GENERICBEGINJUMP;
        } else if (o->counter < 0x29) {
            if (o->counter == 0x14)
                o->image = G.img[IMG_SPEAR];
        } else {
            o->counter = 0;
            if (++o->gv[0] >= 0x13)
                obj_kill(o);
            else if (o->gv[0] > 0x0C)
                o->image = NULL;
        }
        return 0;
    }
    if (!carry(&o->xpart, iabs16(o->xspeed)))
        return 0;
    can_move(o);
    const bool touching = you && obj_collide(you, o);
    if (o->xspeed < 0) {
        if (can_go_left(o) && room_left(o) && o->up_offset < 0x0B &&
            (o->gv[1] != 0 || !touching)) {
            o->x--;
            return 0;
        }
        if (o->gv[1] != 0)
            return 0;
        play_sample(touching ? 0xA9 : 0x83, o);
        o->gv[1]++;
        o->xspeed = 10000;
    } else {
        if (can_go_right(o) && room_right(o) && o->up_offset < 0x0B &&
            (o->gv[1] != 0 || !touching)) {
            o->x++;
            return 0;
        }
        if (o->gv[1] != 0)
            return 0;
        o->gv[1]++;
        o->xspeed = -10000;
    }
    o->yspeed = 0;
    return 0;
}
