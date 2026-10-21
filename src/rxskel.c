/* RXSKELF.C: the boars and the skeleton boss (level 20). */
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

/* The skeleton boss (deadb, level 20). INITLEVEL makes two type 0x53 objects at (0xef,
 * 0x11e), under the floor: the skeleton (`_Boss`, priority 0x4a) and its head (`_Neck[0]`,
 * Animator DOSKELHEAD, GenericVar[0] 1 while it sits on the neck, hittable). Only the head can
 * hurt the skeleton: knocked off by the player's weapon it flies away from the player, and
 * while it is "armed" (GenericVar[1], 500 ticks, blinking) striking the skeleton is one hit
 * (its GenericVar[3]); it then bounces about the room for good. The skeleton rises out of the
 * ground with a spray of rocks, walks right after the player and hacks (from the left) or
 * lunges with its sword (from the right), sinks back, and after 300 ticks rises on the other
 * side. The SKEL* animators in RXDEAD.C call these. While it fights, the player is kept right
 * of the view's left edge (x _OrgX + 5). */

static void keep_player_right(void)
{
    obj_t *you = G.you;
    if (you && you->x < (uint16_t)(G.org_x + 5)) {
        player_right(1, you);
        if (you->xspeed < 0)
            you->xspeed = 0;
    }
}

/* A rock thrown up by the skeleton digging in or out (type 0x54, DeadRck4ID). */
static void spawn_rock(const obj_t *o, int16_t dx, int16_t jump, uint16_t up)
{
    obj_t *r = spawn_obj(0x54, LIST_MAIN, G.img[IMG_DEADRCK4],
                         (uint16_t)(o->x + frandom(0x3C) + dx), 0x100);
    r->gv[0] = 0;
    r->gv[4] = jump;
    r->xspeed = (int16_t)frandom(12000);
    r->yspeed = (int16_t)(-(int16_t)up - (int16_t)frandom(up));
    r->xdir = (uint8_t)frandom(2);
}

/* Four bones (type 0x38: DOPROJECTILE kind 7 with SkelHeadID[5..8] or [9..12]) from the
 * skeleton's box. */
static void skel_bones(const obj_t *o, uint16_t dy)
{
    for (int i = 0; i < 4; i++) {
        const int16_t k = frandom(2) == 0 ? 9 : 5;
        obj_t *b = spawn_obj(0x38, LIST_MAIN, G.img[IMG_SKELHEAD + k],
                             (uint16_t)(o->x + frandom(o->xsize)),
                             (uint16_t)(o->y + frandom((uint16_t)(o->ysize - dy))));
        b->xdir = (uint8_t)frandom(2);
        b->gv[0] = 7;
        b->gv[1] = k;
        b->max_frames = 3;
        b->frame_count = b->frame_count_max = (uint16_t)(frandom(0x1E) + 10);
    }
}

/* RXSKELF.C:750 SKELHIT: the skeleton struck by its own head: a rattle, four bones, and
 * ENEMYHIT with the boss's hits. */
static void skel_hit(obj_t *o)
{
    play_sample(0x82, NULL);
    play_sample(0x8F, NULL);
    skel_bones(o, 0x0F);
    enemy_hit(o, boss_max_hits(3));
}

static void skel_common(obj_t *o)
{
    hit_player(o);
    if (o->flags & OF_HIT)
        skel_hit(o);
}

/* RXSKELF.C:235 FDOSKELHEAD: the head (also run by GENERICUP / GENERICFALL in the air). It
 * looks at the player (SkelHeadID[0] far .. [4] near). On the neck it follows the skeleton's
 * image; knocked off (GenericVar[0] 0) it flies at its xSpeed, bounces at each landing (at a
 * ySpeed settling to -27000, xSpeed to 16000) and off the view's sides, and while armed
 * (GenericVar[1]) a strike on the skeleton counts a hit (with sparks and a blink). When the
 * skeleton dies, so does the head. */
int do_skel_head(obj_t *o)
{
    obj_t *you = G.you, *boss = G.boss;
    if (!you || !boss)
        return 0;
    o->xdir = o->x < you->x ? 1 : 0;
    const uint16_t d = (uint16_t)iabs16((int16_t)(you->x - o->x));
    obj_image(o, G.img[IMG_SKELHEAD + (d > 0x96 ? 0 : d > 0x7D ? 1 : d > 0x64 ? 2
                                        : d > 0x4B ? 3 : 4)]);
    if (boss->animator == A_SKELDIE) {
        o->gv[3] = 3;
        enemy_hit(o, 2);
        G.enemy_found--;
    }
    if (o->flags & OF_HIT) {
        play_sample(0x82, o);
        o->flags &= 0xFD;
        o->gv[0] = 0;
        o->gv[1] = 1;
        o->counter = 0;
        o->gv[3] = 0;
        o->gv[4] = 0x5F;
        o->yspeed = -16000;
        o->animator = A_GENERICBEGINJUMP;
        o->xspeed = you->x < o->x ? 32000 : -32000;
    }
    if (o->gv[0] == 0) {
        if (carry(&o->xpart, iabs16(o->xspeed))) {
            if (o->xspeed < 0)
                o->x--;
            else
                o->x++;
        }
        if (G.diff_mode > 1)
            hit_player(o);
        if (o->animator == A_DOSKELHEAD) {
            /* landed: bounce */
            play_sample(0x83, o);
            o->gv[4] = 0x78;
            o->yspeed = (int16_t)-o->yspeed;
            if (o->yspeed > -27000)
                o->yspeed = (int16_t)(o->yspeed - 1000);
            else if (o->yspeed < -27000)
                o->yspeed = (int16_t)(o->yspeed + 1000);
            if (o->xspeed < -16000)
                o->xspeed = (int16_t)(o->xspeed + 1000);
            else if (o->xspeed > 16000)
                o->xspeed = (int16_t)(o->xspeed - 1000);
            o->animator = A_GENERICBEGINJUMP;
        }
        if (o->x < G.org_x) {
            o->x = G.org_x;
            if (o->xspeed < 0)
                o->xspeed = (int16_t)-o->xspeed;
            if (o->xspeed > 16000)
                o->xspeed = (int16_t)(o->xspeed - 1000);
        } else if ((uint16_t)(G.org_x + 300) < o->x) {
            o->x = (uint16_t)(G.org_x + 300);
            if (o->xspeed > 0)
                o->xspeed = (int16_t)-o->xspeed;
            if (o->xspeed < -16000)
                o->xspeed = (int16_t)(o->xspeed + 1000);
        }
        if (o->gv[1] == 0)
            return 0;
        if (obj_collide(boss, o)) {
            o->xspeed = o->xspeed < 1 ? 9000 : -9000;
            o->gv[1] = 0;
            o->blinks = 0;
            if (!(boss->flags & OF_HIT)) {
                boss->flags |= OF_HIT;
                boss->gv[3]++;
                boss->blink_color = 0x2E;
                obj_blink(boss, 2, 3, 3);
                obj_t *s = spawn_obj(0x0D, LIST_MAIN, G.img[IMG_EXPLODE + 6],
                                     (uint16_t)(o->x + frandom(o->xsize)),
                                     (uint16_t)(o->y + frandom(o->ysize)));
                s->gv[0] = 1;
            }
        }
        if (o->counter > 500) {
            o->gv[1] = 0;
            o->blinks = 0;
        } else if (o->blinks == 0) {
            obj_blink(o, 2, 5, 5);
        }
        return 0;
    }
    /* On the neck: placed by the skeleton's image. Facing right it is pulled 5 pixels left,
     * twice for the hack's images (as in the original). */
    const sprite_t *im = boss->image;
    int16_t dx = 10, dy = 0;
    bool again = false;
    if (im == G.img[IMG_SKELHACK]) {
        dx = 10, dy = 1, again = true;
    } else if (im == G.img[IMG_SKELHACK + 1]) {
        dx = 5, dy = 1, again = true;
    } else if (im == G.img[IMG_SKELHACK + 2]) {
        dx = 0x10, dy = 6, again = true;
        if (boss->xdir == 0)
            dx = (int16_t)(dx - 0x10);
    } else if (im == G.img[IMG_SKELHACK + 3]) {
        dx = 0x23, dy = 0x10, again = true;
    } else if (im == G.img[IMG_SKELSTAB + 1]) {
        dx = -2, dy = 6;
    } else if (im == G.img[IMG_SKELSTAB + 2]) {
        dx = -7, dy = 10;
    } else if (im == G.img[IMG_SKELSTAB + 3]) {
        dx = -7, dy = 0x13;
    } else if (im == G.img[IMG_SKELTRAN]) {
        dx = 8, dy = 10;
    } else if (im == G.img[IMG_SKELWLK1]) {
        dx = 0x0C, dy = 2;
    } else if (im == G.img[IMG_SKELWLK1 + 1]) {
        dx = -1, dy = 3;
        if (boss->xdir != 0)
            dx = (int16_t)(dx + 0x11);
    } else if (im == G.img[IMG_SKELWLK1 + 2]) {
        dx = 0x0E, dy = -1;
    } else if (im == G.img[IMG_SKELWLK1 + 3]) {
        dx = 9, dy = 3;
    } else if (im == G.img[IMG_SKELWLK2]) {
        dx = 0x0E, dy = 1;
    } else if (im == G.img[IMG_SKELWLK2 + 1]) {
        dx = 9, dy = 1;
    }
    o->x = (uint16_t)(boss->x + dx);
    o->y = (uint16_t)(boss->y + dy);
    if (again && o->xdir != 0)
        o->x = (uint16_t)(o->x - 5);
    if (o->xdir != 0)
        o->x = (uint16_t)(o->x - 5);
    return 0;
}

/* RXSKELF.C:419 FSKELWAIT: at the start the camera lets go of the player vertically (a
 * dead zone the view's height); once the view reaches the room (_OrgX = Cols * 16 - 0x141)
 * the bar shows, the dead zone becomes the whole view, and the skeleton rises. */
int skel_wait(obj_t *o)
{
    if (o->gv[0] == 0) {
        o->gv[0]++;
        G.center_off = -1;
        G.window_ysize = 200;
        set_scroll_bounds(200, G.window_xsize);
    }
    if ((uint16_t)(G.lv->cols * 16 - 0x141) == G.org_x) {
        spawn_obj(0x34, LIST_FORE, G.img[IMG_BOSSHP + 10], (uint16_t)(G.org_x + 0x89),
                  (uint16_t)(G.org_y + 7));
        o->gv[2] = (int16_t)play_sample(0x37, NULL);
        play_sample(0x92, NULL);
        o->animator = A_SKELRISE;
        o->counter = 0;
        G.window_xsize = 0x140;
        G.window_ysize = 200;
        set_scroll_bounds(200, 0x140);
    }
    return 0;
}

/* RXSKELF.C:445 FSKELRISE: it leaps out of the ground (GENERICBEGINJUMP: ySpeed -30000 for
 * 0x136 ticks, SkelHackID[1]), throwing up a rock every 10 ticks while rising (GENERICUP calls
 * this too). GENERICDOWN lands it in SKELLUNGE (facing left) or SKELWALK (facing right). */
int skel_rise(obj_t *o)
{
    keep_player_right();
    if (o->counter == 1) {
        o->gv[1] = 0;
        o->animator = A_GENERICBEGINJUMP;
        o->gv[4] = 0x136;
        o->yspeed = -30000;
        o->max_frames = 5;
        o->frame_no = 0;
        o->anim_inc = 0;
        o->frame_count = o->frame_count_max = 0x7D;
        obj_image(o, G.img[IMG_SKELHACK + 1]);
    } else if (o->counter == 0x0C) {
        o->counter = 2;
        spawn_rock(o, -0x1E, 0x78, 15000);
    }
    skel_common(o);
    return 0;
}

/* GENERICFALL's part for the falling skeleton: just keep the player in. */
void skel_fall(obj_t *o)
{
    (void)o;
    keep_player_right();
}

/* RXSKELF.C:488 FSKELSINK: back into the ground, 2 pixels and a rock every 12 ticks; below y
 * 0x104 it hides at y 0x11e on the other side (x 0xef facing right, or 0x1d3 facing left) and
 * waits (SKELWAIT2). */
int skel_sink(obj_t *o)
{
    keep_player_right();
    if (o->counter == 0x0C) {
        o->counter = 0;
        spawn_rock(o, -0x19, 0x5A, 10000);
        o->y = (uint16_t)(o->y + 2);
        if (o->y > 0x104) {
            o->y = 0x11E;
            o->counter = 0;
            o->image = NULL;
            o->animator = A_SKELWAIT2;
            if (o->xdir == 0) {
                o->xdir = 1;
                o->x = 0xEF;
            } else {
                o->xdir = 0;
                o->x = 0x1D3;
            }
        }
    }
    skel_common(o);
    return 0;
}

/* RXSKELF.C:533 FSKELWALK: from the left, 9 pixels a frame after the player (rattling) until
 * within 0x5a pixels, then a roar and the hack (SKELHACK, SkelTranID first). */
int skel_walk(obj_t *o)
{
    obj_t *you = G.you;
    keep_player_right();
    if (you && o->frame_count == o->frame_count_max) {
        if ((uint16_t)(o->x + 0x5A) < you->x) {
            if (!sound_link_active(o->gv[2]))
                o->gv[2] = (int16_t)play_sample(0x52, G.boss);
            o->x = (uint16_t)(o->x + 9);
        } else {
            sound_link_kill(o->gv[2]);
            play_sample(0x81, G.boss);
            o->animator = A_SKELHACK;
            o->max_frames = 3;
            o->frame_no = 0;
            o->anim_inc = 0;
            o->frame_count = o->frame_count_max = 0x6E;
            obj_image(o, G.img[IMG_SKELTRAN]);
        }
    }
    if (o->animator != A_SKELHACK)
        next_frame(o, IMG_SKELWLK1);
    skel_common(o);
    return 0;
}

/* The hack and the lunge hurt a player in front of the blade from their third frame on,
 * whether or not the hit boxes meet (_FreeHit). */
static void blade_check(const obj_t *o, uint16_t left, uint16_t right)
{
    const obj_t *you = G.you;
    if (you && left < you->x && you->x < right &&
        (uint16_t)(o->y + (o->ysize >> 1)) < (uint16_t)(you->y + 0x32) &&
        you->y < (uint16_t)(o->y + o->ysize) && G.hitagain == 0 && o->frame_no > 2)
        G.free_hit = 1;
}

/* RXSKELF.C:577 FSKELHACK: four swings of SkelHackID stepping 8 pixels right each frame (not
 * past the map's end); at the end of the last it sinks (SKELSINK). */
int skel_hack(obj_t *o)
{
    keep_player_right();
    if (o->frame_count == o->frame_count_max && (uint16_t)(o->x + 0x3C) < G.lv->cols * 16 - 1)
        o->x = (uint16_t)(o->x + 8);
    next_frame(o, IMG_SKELHACK);
    if (o->frame_no == 2 && o->frame_count == o->frame_count_max) {
        play_sample(0x83, G.boss);
    } else if (o->frame_no == 3 && o->frame_count == 2) {
        o->image = G.img[IMG_SKELWLK1 + 1];
        o->counter = 0;
        o->animator = A_SKELSINK;
        o->gv[2] = (int16_t)play_sample(0x37, G.boss);
    }
    blade_check(o, o->x, (uint16_t)(o->x + 0x32));
    skel_common(o);
    return 0;
}

/* RXSKELF.C:620 FSKELLUNGE: from the right, gliding left at 18000 with the sword out
 * (SkelStabID, held at frame 4) until x 0x122, then it sinks. */
int skel_lunge(obj_t *o)
{
    keep_player_right();
    if (carry(&o->xpart, 18000))
        o->x--;
    if (o->frame_no != 4)
        next_frame(o, IMG_SKELSTAB);
    blade_check(o, (uint16_t)(o->x - 0x1E), (uint16_t)(o->x + o->xsize));
    hit_player(o);
    if (o->x < 0x122) {
        obj_image(o, G.img[IMG_SKELWLK1 + 1]);
        o->counter = 0;
        o->animator = A_SKELSINK;
        sound_link_kill(o->gv[2]);
        o->gv[2] = (int16_t)play_sample(0x37, G.boss);
    }
    skel_common(o);
    return 0;
}

/* RXSKELF.C:664 FSKELWAIT2: 300 ticks under the ground, then up again. */
int skel_wait2(obj_t *o)
{
    keep_player_right();
    if (o->counter > 300) {
        play_sample(0x92, NULL);
        sound_link_kill(o->gv[2]);
        o->gv[2] = (int16_t)play_sample(0x37, G.boss);
        o->counter = 0;
        o->animator = A_SKELRISE;
        if (!(o->flags & 1)) {
            o->max_frames = 4;
            o->frame_no = 0;
            o->anim_inc = 0;
            o->frame_count = o->frame_count_max = 0x55;
        }
    }
    return 0;
}

/* RXSKELF.C:692 FSKELDIE (ENEMYHIT starts it): the skeleton floats up (accelerating by 10 a
 * tick) until 0x14 pixels below the view's top, flickering: every 45 ticks it vanishes for 25
 * with the view bobbing a pixel, and there is a crash of two explosions and four bones (the
 * first 60 times); after 71 rounds the level is won. */
int skel_die(obj_t *o)
{
    keep_player_right();
    if ((uint16_t)(G.org_y + 0x14) < o->y) {
        if (o->yspeed < 32000)
            o->yspeed = (int16_t)(o->yspeed + 10);
        if (carry(&o->ypart, iabs16(o->yspeed)))
            o->y--;
    }
    if (++o->gv[1] == 0x2D) {
        if (frandom(7) == 0)
            play_sample(0xA3, NULL);
        else if (frandom(7) == 0)
            play_sample(0xA2, NULL);
        if (frandom(10) == 0)
            play_sample(0x8F, NULL);
        o->gv[1] = 0;
        if (o->gv[2] < 0x3C) {
            for (int i = 0; i < 2; i++)
                spawn_obj(0x0D, LIST_MAIN, G.img[IMG_EXPLODE],
                          (uint16_t)(o->x + frandom(o->xsize)),
                          (uint16_t)(o->y + frandom((uint16_t)(o->ysize - 0x14))));
            skel_bones(o, 0x0F);
        }
        if (++o->gv[2] > 0x46) {
            G.quitflag = 3;
            return 0;
        }
    }
    if (o->gv[1] == 1) {
        if (o->gv[2] < 0x2D)
            scroll_down(1);
        o->image = NULL;
    } else if (o->gv[1] == 0x1A && o->gv[2] < 0x2D) {
        scroll_up(1);
        obj_image(o, G.img[IMG_SKELHACK + 1]);
    }
    return 0;
}
