/* RXYOU.C / RXYOUF.C: the player. Each YOU* animator calls its FYOU* worker; the port names
 * them after the YOU* animator and ports the worker.
 *
 * GenericVar[5] bits: 1 on the ground, 2 falling, 4 jumping, 8 the jump key has been released
 * (so holding it doesn't jump again), 0x10 holds the special item. _GameFlags: 1 climbing,
 * 2 attacking (whip / fireball in progress), 4 the male character, 8 recovering from a hit.
 * The female character shoots fireballs, the male uses the whip. GenericVar[0] is the
 * attack's recovery (LEVELTIMER counts it down).
 *
 * The special item (key 6) changes the hero (DOTRANSFORMER). */
#include "behave.h"

#include <stdio.h>
#include <string.h>

/* The special item key (6, Space) with the item held (GenericVar[5] bit 0x10), not attacking
 * or recovering from a hit: the change of hero (DOTRANSFORMER, type 0x37, which takes the item
 * until it's done). True when it started. */
static bool special_item(obj_t *o)
{
    obj_t *you = G.you;
    if (!(you->gv[5] & 0x10) || !key(K_ITEM) || (G.game_flags & 2) || (G.game_flags & 8))
        return false;
    you->blinks = 0;
    obj_t *t = spawn_obj(0x37, LIST_FORE, you->image, o->x, o->y);
    t->image = G.img[IMG_SWORD3];
    return true;
}

static bool male(void)
{
    return (G.game_flags & 4) != 0;
}

static const sprite_t *img(int base, int n)
{
    const int i = base + n;
    return i >= 0 && i < IMG_COUNT ? G.img[i] : NULL;
}

/* The tile type (word) of a cell's foreground / bonus tile, and of its background tile. */
static uint16_t fg_type_at(unsigned row, unsigned col)
{
    const uint8_t f = (uint8_t)map_fg(row, col);
    return (f & 0x80) ? G.lv->fg_type[f & 0x7F] : G.lv->bon_type[f & 0x7F];
}

static uint16_t bg_type_at(unsigned row, unsigned col)
{
    return G.lv->bg_type[map_cell(row, col) & 0x1FF];
}

/* Vines: 0x6a climbable, 0x6b the top (scramble up onto the ledge), 0x6c the bottom. */
enum { T_VINE = 0x6A, T_VINE_TOP = 0x6B, T_VINE_BOTTOM = 0x6C };

static bool platform_over(int reach)
{
    /* A riding platform (SurfaceFlags 0x10, GenericVar[5] 0x40) just above the player. */
    const obj_t *you = G.you;
    for (const obj_t *p = G.list[LIST_MAIN]; p; p = p->next)
        if ((p->surface_flags & 0x10) && (p->gv[5] & 0x40) &&
            you->x < (uint16_t)(p->x + p->xsize) && p->x < (uint16_t)(you->x + you->xsize) &&
            p->y < you->y && (uint16_t)(you->y - reach) < (uint16_t)(p->y + p->ysize))
            return true;
    return false;
}

static bool ceiling_over(const obj_t *o)
{
    /* Room to stand up: the cells 16 pixels up at the player's left and right sides. */
    const unsigned row = (unsigned)(uint16_t)(o->y - 0x10) >> 4;
    return (map_cell(row, o->x >> 4) & 0x1000) ||
           (map_cell(row, (unsigned)(uint16_t)(o->x + 0x0D) >> 4) & 0x1000);
}

/* ------------------------------------------------------------------------------------------ */

/* RXYOUF.C:45 FYOUBEGINRUN: back on the ground. */
int you_begin_run(obj_t *o)
{
    obj_t *you = G.you;
    if (G.center_off >= 0)
        G.center_off = 'V';
    you->gv[5] = (int16_t)((you->gv[5] | 1) & ~2 & ~4);
    you->frame_count_max = male() ? 9 : 7;
    you->frame_count = you->frame_count_max;
    you->max_frames = male() ? 8 : 7;
    you->friction = 199;
    you->animator = A_YOURUN;
    if (!key(K_LEFT) && !key(K_RIGHT) && you->gv[0] != 0 && male())
        obj_image(o, img(G.you_attack, 0));
    else
        obj_image(o, img(G.you_walk, 8));
    return 0;
}

/* RXYOUF.C:71 FYOURUN: on the ground: jump, crouch, fall off an edge, or walk. */
int you_run(obj_t *o)
{
    if (key(K_JUMP) && (G.you->gv[5] & 8))
        return you_begin_jump(o);
    if (key(K_DOWN) && !G.looking) {
        o->counter = 40000;
        return you_begin_crouch(o);
    }
    can_move(o);
    if (G.you->down_offset == 1)
        return you_begin_fall(o);
    return you_horiz(o);
}

/* RXYOUF.C:92 FYOUBEGINJUMP: holding the jump key keeps the upward speed for up to 0xa0
 * (male) / 0xc3 (female) ticks. */
int you_begin_jump(obj_t *o)
{
    obj_t *you = G.you;
    if (G.center_off >= 0) {
        set_scroll_bounds(0x8C, G.window_xsize);
        G.window_ysize = 0x8C;
        G.center_off = 0;
    }
    you->gv[5] = (int16_t)((you->gv[5] | 4) & ~8 & ~1);
    you->max_frames = male() ? 8 : 7;
    you->frame_count_max = 0x2D;
    you->friction = 95;
    you->gv[4] = male() ? 0xA0 : 0xC3;
    you->yspeed = -16000;
    o->animator = A_YOUJUMP;
    return 0;
}

/* RXYOUF.C:122 FYOUJUMP */
int you_jump(obj_t *o)
{
    obj_t *you = G.you;
    if (key(K_JUMP) && --you->gv[4] != 0)
        you->yspeed = (int16_t)(you->yspeed - 0xB4);
    else
        you->animator = A_YOUUP;
    return you_up(o);
}

/* RXYOUF.C:134 FYOUUP: rising; gravity 0xb4 a tick. */
int you_up(obj_t *o)
{
    if (!(G.game_flags & 2))
        obj_image(o, img(G.you_jump, o->yspeed < -8999 ? 0 : 1));
    else
        o->ysize = 0x29;
    const int32_t v = o->yspeed + 0xB4;
    o->yspeed = (int16_t)v;
    if (v <= 0) {
        if (carry(&o->ypart, o->yspeed))
            return you_horiz(o);
        if (can_go_up(o) && o->y > 2) {
            player_up(1, o);
            return 0;
        }
        o->yspeed = 0;
    }
    return you_begin_fall(o);
}

/* RXYOUF.C:163 FYOUBEGINFALL */
int you_begin_fall(obj_t *o)
{
    obj_t *you = G.you;
    if (G.center_off >= 0) {
        set_scroll_bounds(0x8C, G.window_xsize);
        G.window_ysize = 0x8C;
        G.center_off = 0;
    }
    you->gv[5] = (int16_t)((you->gv[5] | 2) & ~4 & ~1);
    you->friction = 95;
    o->animator = A_YOUFALL;
    you->gv[1] = 0; /* fall length */
    you->max_frames = male() ? 8 : 7;
    you->frame_count_max = 0x2D;
    return 0;
}

/* RXYOUF.C:186 FYOUFALL: gravity 0xb4 a tick; land (stunned after a long fall), and a
 * crouching player stands up in mid-air when there's room. */
int you_fall(obj_t *o)
{
    obj_t *you = G.you;
    const int16_t v = (int16_t)(o->yspeed + 0xB4);
    if (v >= 0)
        o->yspeed = v;
    const uint16_t old = o->ypart;
    o->ypart = (uint16_t)(o->ypart + o->yspeed);
    if (old < o->ypart)
        return you_horiz(o);
    can_move(o);
    if (you->down_offset == 0) {
        if ((!male() || you->gv[1] < 0x97) && (male() || you->gv[1] < 0x74)) {
            if (you->gv[1] > 0x20)
                play_sample(male() ? 0x5A : 0x5B, o);
            o->animator = o->ysize == 0x29 ? A_YOUBEGINRUN : A_YOUBEGINCROUCH;
        } else {
            o->animator = A_YOUBEGINSTUN;
        }
        return 0;
    }
    you->gv[1]++;
    if (o->ysize == 0x19) {
        if (!ceiling_over(o) && !platform_over(0x10) && (uint16_t)(G.org_y + 0x4B) < o->y) {
            o->frame_count = 1;
            player_up(0x10, you);
            if (!(G.game_flags & 2))
                obj_image(o, img(G.you_jump, 1));
            else
                o->ysize = 0x29;
        }
    } else if (!(G.game_flags & 2)) {
        obj_image(o, img(G.you_jump, o->yspeed < 9000 ? 1 : 2));
    } else {
        o->ysize = 0x29;
    }
    player_down(1, o);
    if ((G.level == 8 || G.level == 9) &&
        (uint16_t)(G.lv->rows * 16 - 0x21) < (uint16_t)(you->y + you->ysize) && you->image &&
        Inv.hit_points[0])
        return you_drown(o);
    return you_horiz(o);
}

/* RXYOUF.C:1619 FYOUBOREDOM: standing still, the idle animations run off the Counter. After
 * running (Counter 40000) a short stop: frames 0x16-0x1b, then Counter 0x9c0e. Transcribed
 * as decompiled. */
static void you_boredom(void)
{
    obj_t *you = G.you;
    const uint16_t c = you->counter;
    const bool m = male();
    int f;
    if (c > 0x9C0E) {
        if (c > 0x9D3A) {
            obj_image(you, img(G.you_walk, 0x1B));
            you->counter = 0x9C0E;
            return;
        }
        if (c >= 0x9D09) f = 0x1A;
        else if (c >= 0x9CD7) f = 0x19;
        else if (c >= 0x9CA5) f = 0x18;
        else if (c >= 0x9C73) f = 0x17;
        else if (c >= 0x9C41) f = 0x16;
        else return;
        goto set;
    }
    if (c < 0x6D61 || !m) {
        if (c < 0x6D61 || m) {
            if (c <= (m ? 0x578 : 0x960)) {
                if (c > (m ? 0x514 : 0x708))
                    goto f0e;
                if (c <= (m ? 0x4B0 : 0x6A4)) {
                    if (c > (m ? 600 : 0x44C)) {
                        f = 0x10;
                        goto set;
                    }
                    if (c <= (m ? 500 : 1000)) {
                        if (c < 0x191)
                            goto f09;
                        goto f0e;
                    }
                }
                f = 0x0F;
                goto set;
            }
        } else if (c < 0x717B) {
            if (c < 0x70E5) {
                if (c > 0x704E) goto f15;
                if (c > 0x6FB8) goto f14;
                if (c > 0x6F22) goto f13;
                if (c > 0x6E8C) goto f12;
                if (c < 0x6DF7) goto f09;
            }
            goto f11;
        }
    } else if (c < 0x797D) {
        if (c > 0x78E6) goto f0a;
        if (c > 0x77EC) goto f11;
        if (c > 0x7756) goto f12;
        if (c > 0x76C0) goto f13;
        if (c < 0x762B) {
            if (c > 0x7594) goto f15;
            if (c < 0x74FF) {
                if (c > 0x7468) goto f13;
                if (c < 0x73D3) {
                    if (c > 0x733C) goto f11;
                    if (c < 0x72A7) {
                        if (c > 0x7210) goto f13;
                        if (c < 0x717B) {
                            if (c > 0x70E4) goto f15;
                            if (c < 0x704F) {
                                if (c < 0x6FB9) {
                                    if (c < 0x6F23) {
                                        if (c < 0x6E8D) {
                                            if (c < 0x6DF7) goto f09;
                                            goto f0a;
                                        }
                                        goto f11;
                                    }
                                    goto f12;
                                }
                                goto f13;
                            }
                        }
                        goto f14;
                    }
                }
                goto f12;
            }
        }
        goto f14;
    }
f09: f = 0x09; goto set;
f0a: f = 0x0A; goto set;
f0e: f = 0x0E; goto set;
f11: f = 0x11; goto set;
f12: f = 0x12; goto set;
f13: f = 0x13; goto set;
f14: f = 0x14; goto set;
f15: f = 0x15; goto set;
set:
    obj_image(you, img(G.you_walk, f));
}

/* RXYOUF.C:264 FYOUHORIZ: horizontal movement (on the ground and in the air), the standing
 * poses, grabbing a vine, and starting an attack. */
int you_horiz(obj_t *o)
{
    obj_t *you = G.you;
    const bool left = key(K_LEFT), right = key(K_RIGHT);
    if (!key(K_JUMP))
        you->gv[5] |= 8;
    if (G.wisp_walk == 0 && !left && !right) {
        /* Standing. */
        if (!(you->gv[5] & 2) && !(you->gv[5] & 4) && !(G.game_flags & 2) &&
            (!male() || you->gv[0] == 0)) {
            const uint16_t foot = (uint16_t)(you->y + you->ysize - 1);
            const uint16_t below = (uint16_t)(you->y + you->ysize + 1);
            const unsigned col = (unsigned)(uint16_t)(you->x + (you->xsize >> 1)) >> 4;
            uint16_t w = map_cell(foot >> 4, col);
            unsigned f = (uint8_t)map_fg(foot >> 4, col);
            uint8_t k = (uint8_t)G.lv->bg_type[w & 0x1FF];
            if (k < 200)
                k = (uint8_t)((f & 0x80) ? G.lv->fg_type[f & 0x7F] : G.lv->bon_type[f & 0x7F]);
            if (k < 200) {
                w = map_cell(below >> 4, col);
                f = (uint8_t)map_fg(below >> 4, col);
                k = (uint8_t)G.lv->bg_type[w & 0x1FF];
            }
            if (k < 200)
                k = (uint8_t)((f & 0x80) ? G.lv->fg_type[f & 0x7F] : G.lv->bon_type[f & 0x7F]);
            const sprite_t *pose = NULL;
            bool idle = true;
            if (!G.looking || !key(K_UP)) {
                const uint16_t c = o->counter;
                if (c > 0x9C3 && (c < 0x6D61 || c > 32999) &&
                    ((you->surface_flags & 2) || k > 199 || (w & 0x800))) {
                    /* Standing where boredom isn't allowed (platforms, slopes, thin
                     * floors): the plain standing pose; the male catches his breath first. */
                    if (you->image != img(G.you_walk, 9))
                        o->counter = 40000;
                    if (you->image == img(IMG_MALWALK, 10)) {
                        pose = img(IMG_MALWALK, 9);
                        if (you->gv[2] > 0) {
                            you->gv[2]--;
                            idle = false;
                        }
                    } else if (you->image == img(IMG_MALWALK, 11)) {
                        pose = img(IMG_MALWALK, 10);
                        if (you->gv[2] > 0x28) {
                            you->gv[2]--;
                            idle = false;
                        }
                    } else if (you->image == img(IMG_MALWALK, 12)) {
                        pose = img(IMG_MALWALK, 11);
                        if (you->gv[2] > 0x50) {
                            you->gv[2]--;
                            idle = false;
                        }
                    } else if (!male() || you->image == img(IMG_MALWALK, 9) ||
                               you->image == img(IMG_MALWALK, 0x1C)) {
                        if (male() && you->image != img(G.you_walk, 0x1C))
                            idle = false;
                        else
                            pose = img(G.you_walk, 9);
                    } else {
                        you->gv[2] = 0x78;
                        pose = img(IMG_MALWALK, 12);
                    }
                    if (idle)
                        obj_image(you, pose);
                } else {
                    you_boredom();
                }
            } else {
                obj_image(you, img(G.you_walk, 0x1C)); /* looking up */
            }
            player_right(0, o);
        }
    } else {
        o->counter = 40000;
        if (G.wisp_walk == -1)
            you->xdir = 0;
        else if (G.wisp_walk == 1)
            you->xdir = 1;
        else if (left)
            you->xdir = 0;
        else if (right)
            you->xdir = 1;
        if (!male() || !(G.game_flags & 2) || !(you->gv[5] & 1)) {
            if (!(you->gv[5] & 1)) {
                /* in the air: accelerate by Friction, up to 18000 */
                const int16_t v = (int16_t)(o->xspeed + (you->xdir == 0 ? -you->friction
                                                                        : you->friction));
                if (iabs16(v) <= 17999)
                    o->xspeed = v;
            } else {
                you->anim_inc = 0;
                o->xspeed = you->xdir == 0 ? -12000 : 12000;
            }
        }
    }

    /* Up or down at a vine. */
    if ((key(K_UP) || key(K_DOWN)) && !G.looking) {
        const unsigned row = (unsigned)(uint16_t)(o->y + 6) >> 4;
        const unsigned col = (unsigned)(uint16_t)(o->x + 7) >> 4;
        if (fg_type_at(row, col) == T_VINE || bg_type_at(row, col) == T_VINE) {
            G.vine_x = (uint16_t)col;
            o->animator = A_YOUBEGINCLIMB;
            return 0;
        }
        if (fg_type_at(row, col) == T_VINE_TOP || bg_type_at(row, col) == T_VINE_TOP) {
            G.vine_x = (uint16_t)col;
            o->y = (uint16_t)(row << 4);
            G.vine_y = (uint16_t)row;
            you->gv[1] = 1;
            o->animator = A_YOUBEGINSCRAMBLE;
            return 0;
        }
    }
    if (special_item(o))
        you->frame_count_max = male() ? 7 : 9;

    /* Slowing down: in the air by Friction / 2, on the ground at once. */
    const bool keep = (!male() || !(G.game_flags & 2) || !(you->gv[5] & 1)) &&
                      (o->xspeed >= 0 || left || G.wisp_walk == -1) &&
                      (o->xspeed < 1 || right || G.wisp_walk == 1);
    if (!keep) {
        if (!(you->gv[5] & 1)) {
            if (o->xspeed < 0) {
                o->xspeed = (int16_t)(o->xspeed + (you->friction >> 1));
                if (o->xspeed > 0)
                    o->xspeed = 0;
            } else if (o->xspeed > 0) {
                o->xspeed = (int16_t)(o->xspeed - (you->friction >> 1));
                if (o->xspeed < 0)
                    o->xspeed = 0;
            }
        } else {
            o->xspeed = 0;
        }
    }
    if (G.game_flags & 2) {
        if (male())
            you_whip(o);
        else
            you_shoot(o);
    }
    if (you->gv[0] < 10 && o->xspeed != 0 && male())
        you->gv[0] = 0;

    if (o->xspeed != 0) {
        const uint16_t old = o->xpart;
        o->xpart = (uint16_t)(o->xpart + iabs16(o->xspeed));
        if (o->xpart <= old) {
            can_move(o);
            if (you->up_offset > 10)
                player_up((uint8_t)(you->up_offset - 10), o);
            if (you->down_offset > 10)
                player_down((uint8_t)(you->down_offset - 10), o);
            bool moved = false;
            if (o->xspeed < 0) {
                if (can_go_left(you) &&
                    (G.level == 0x0F ? (uint16_t)(G.org_x + 0x12) < you->x : you->x >= 0x19)) {
                    player_left(1, o);
                    moved = true;
                }
            } else {
                if (o->xspeed < 1)
                    return 0;
                if (can_go_right(you) &&
                    (G.level == 0x0F
                         ? (uint16_t)(you->x + 0x0E) < (uint16_t)(G.org_x + 0x130)
                         : (uint16_t)(you->x + 0x0E) < (uint16_t)(G.lv->cols * 16 - 1))) {
                    player_right(1, o);
                    moved = true;
                }
            }
            if (!(you->gv[5] & 1))
                return 0;
            if (!moved) {
                if (!(G.game_flags & 2))
                    obj_image(you, img(G.you_walk, 9));
                return 0;
            }
            const sprite_t *im = you->image;
            if ((im == img(G.you_walk, 9) || im == img(G.you_walk, 10) ||
                 im == img(G.you_walk, 11) || im == img(G.you_walk, 12)) &&
                you->frame_count != you->frame_count_max) {
                /* starting to walk from a standing pose */
                you->frame_count = you->frame_count_max;
                you->frame_no = 0;
                obj_image(o, img(G.you_walk, 0));
                return 0;
            }
            if (G.game_flags & 2)
                return 0;
            next_frame(o, G.you_walk);
            return 0;
        }
    }
    if (key(K_ATTACK) && !(G.game_flags & 8) && G.wisp_walk == 0) {
        if (!male() && you->gv[0] == 0)
            return you_begin_shoot(o);
        if (!(G.game_flags & 2) && male() && you->gv[0] < 10)
            return you_begin_whip(o);
    }
    return 0;
}

/* RXYOUF.C:487 FYOUBEGINCROUCH */
int you_begin_crouch(obj_t *o)
{
    obj_t *you = G.you;
    if (G.center_off >= 0)
        G.center_off = 'f';
    you->anim_inc = 1;
    you->frame_no = 0;
    you->max_frames = 2;
    you->frame_count_max = 10;
    you->frame_count = 10;
    o->animator = A_YOUCROUCH;
    if (o->ysize == 0x29)
        o->y = (uint16_t)(o->y + 0x10);
    obj_image(o, img(G.you_duck, 0));
    return 0;
}

/* RXYOUF.C:513 FYOUBEGINSTUN: a hard landing: crouched and helpless for GenericVar[1]
 * ticks (YOUCROUCH counts it down). */
int you_begin_stun(obj_t *o)
{
    obj_t *you = G.you;
    if (G.center_off >= 0)
        G.center_off = 'f';
    you->gv[5] = (int16_t)((you->gv[5] | 1) & ~2 & ~4);
    you->friction = 199;
    you->anim_inc = 1;
    you->frame_no = 0;
    you->max_frames = 2;
    you->frame_count_max = 10;
    you->frame_count = 10;
    o->animator = A_YOUCROUCH;
    if (o->ysize == 0x29)
        o->y = (uint16_t)(o->y + 0x10);
    if (!(G.game_flags & 8)) {
        you->gv[1] = male() ? 0xB4 : 0xF0;
        play_sample(male() ? 0x6D : 0x6C, o);
    } else {
        you->gv[1] = 0xAA;
    }
    obj_image(o, img(G.you_duck, 0));
    if (!Inv.hit_points[1] || !Inv.hit_points[0]) {
        o->y = (uint16_t)(o->y - 0x10);
        o->animator = A_YOUDIE;
    }
    return 0;
}

/* RXYOUF.C:561 FYOUCROUCH: crouching (and the stun after a hard landing, while
 * GenericVar[1] counts down): crawl, stand up when the key is let go and there's room,
 * jump, or drop onto a vine below. */
int you_crouch(obj_t *o)
{
    obj_t *you = G.you;
    if (you->gv[1] > 0)
        you->gv[1]--;
    if (!G.blood_on && (G.game_flags & 8) && o->blinks == 0) {
        you->blink_color = 0x2E;
        obj_blink(you, 0x19, 3, 3);
    }
    if (you->gv[1] == 0) {
        if (G.game_flags & 8) {
            /* up again after a hit: protected a while longer */
            G.game_flags &= 0xFFF7;
            G.hitagain = 0x0B;
            if (!G.blood_on) {
                you->blink_color = 0x2E;
                obj_blink(you, 0x19, 3, 3);
            }
        }
        if (!key(K_JUMP))
            you->gv[5] |= 8;
        if (!ceiling_over(o) && !platform_over(0x11)) {
            if (!key(K_DOWN) && !G.looking) {
                if (G.center_off >= 0) {
                    set_scroll_bounds(200, G.window_xsize);
                    G.window_ysize = 200;
                }
                obj_image(o, img(G.you_walk, 8));
                o->frame_count = 1;
                o->animator = A_YOUBEGINRUN;
                o->y = (uint16_t)(o->y - 0x10);
                return 0;
            }
            if (key(K_JUMP) && (you->gv[5] & 8)) {
                o->y = (uint16_t)(o->y - 0x10);
                o->animator = A_YOUBEGINJUMP;
                return 0;
            }
        }
        const unsigned row = (unsigned)(uint16_t)(o->y + 0x19) >> 4;
        const unsigned col = (unsigned)(uint16_t)(o->x + 7) >> 4;
        if (fg_type_at(row, col) == T_VINE_TOP) {
            G.vine_x = (uint16_t)col;
            G.vine_y = (uint16_t)row;
            you->gv[1] = 4;
            o->animator = A_YOUBEGINSCRAMBLE;
            return 0;
        }
        if (!male() || !(G.game_flags & 2)) {
            if ((G.wisp_walk == -1 || key(K_LEFT)) && !key(K_RIGHT)) {
                o->xspeed = -5000;
                you->xdir = 0;
            }
            if ((G.wisp_walk == 1 || key(K_RIGHT)) && !key(K_LEFT)) {
                o->xspeed = 5000;
                you->xdir = 1;
            }
        }
        if (key(K_ATTACK) && (G.wisp_walk != -1 || you->xdir) &&
            (G.wisp_walk != 1 || !you->xdir)) {
            if (!male() && you->gv[0] == 0) {
                o->xspeed = 0;
                return you_begin_shoot(o);
            }
            if (!(G.game_flags & 2) && male() && you->gv[0] < 10) {
                o->xspeed = 0;
                return you_begin_whip(o);
            }
        }
        special_item(o);
    }
    if (G.game_flags & 2) {
        if (male())
            you_whip(o);
        else
            you_shoot(o);
    }
    if (carry(&o->xpart, iabs16(o->xspeed))) {
        can_move(o);
        if (you->down_offset == 1) {
            if (G.game_flags & 8)
                G.game_flags &= 0xFFF7;
            o->animator = A_YOUBEGINFALL;
            return 0;
        }
        if (you->up_offset > 10)
            player_up(1, o);
        if (you->down_offset > 10)
            player_down(1, o);
        if (o->xspeed < 0) {
            if (!can_go_left(you) ||
                !(G.level == 0x0F ? (uint16_t)(G.org_x + 0x12) < you->x : you->x >= 0x19))
                return 0;
            player_left(1, o);
        } else {
            if (!can_go_right(o) || !room_right(o))
                return 0;
            player_right(1, o);
        }
        if (G.game_flags & 2)
            return 0;
        next_frame(o, G.you_duck);
        return 0;
    }
    if (o->xspeed < 0) {
        o->xspeed = (int16_t)(o->xspeed + you->friction);
        if (o->xspeed > 0)
            o->xspeed = 0;
    } else {
        o->xspeed = (int16_t)(o->xspeed - you->friction);
        if (o->xspeed < 0)
            o->xspeed = 0;
    }
    if ((!male() || you->gv[0] == 0) && !(G.game_flags & 2))
        obj_image(o, img(G.you_duck, you->frame_no));
    player_right(0, o);
    return 0;
}

/* RXYOUF.C:988 FYOUBEGINCLIMB: onto the vine in column _Vine_x. */
int you_begin_climb(obj_t *o)
{
    obj_t *you = G.you;
    set_scroll_bounds(0x8C, G.window_xsize);
    G.window_ysize = 0x8C;
    G.center_off = 'V';
    you->gv[5] &= ~4 & ~1;
    you->xspeed = 0;
    you->frame_no = 0;
    you->yspeed = 0;
    you->friction = 199;
    o->animator = A_YOUCLIMB;
    you->anim_inc = 1;
    you->max_frames = 6;
    G.game_flags |= 1;
    you->frame_count_max = male() ? 5 : 6;
    you->frame_count = you->frame_count_max;
    const int target = you->xdir == 0 ? G.vine_x * 16 + 4 : G.vine_x * 16 - 1;
    const uint16_t x = you->x;
    obj_image(o, img(G.you_climb, 0));
    o->x = (uint16_t)(o->x + (target - x));
    return 0;
}

/* RXYOUF.C:1022 FYOUCLIMB: on a vine: turn, climb, or jump off sideways. */
int you_climb(obj_t *o)
{
    obj_t *you = G.you;
    if (key(K_JUMP) && (you->gv[5] & 8) && !key(K_UP) && !key(K_DOWN) && !G.looking) {
        if (key(K_RIGHT) || key(K_LEFT)) {
            o->xspeed = male() ? 8000 : 0x2340;
            if (key(K_LEFT))
                o->xspeed = (int16_t)-o->xspeed;
        }
        you->max_frames = male() ? 8 : 7;
        you->frame_count_max = 0x2D;
        G.game_flags &= 0xFFFE;
        o->animator = A_YOUBEGINJUMP;
        return 0;
    }
    if (!key(K_JUMP))
        you->gv[5] |= 8;
    if ((you->gv[5] & 0x10) && key(K_ITEM) && !(G.game_flags & 2) && !(G.game_flags & 8)) {
        you->frame_count = you->frame_count_max = male() ? 5 : 6;
        special_item(o);
    }
    if (key(K_LEFT) && you->xdir != 0) {
        you->xdir = 0;
        o->x = (uint16_t)(o->x + 5);
    } else if (key(K_RIGHT) && you->xdir == 0) {
        you->xdir = 1;
        o->x = (uint16_t)(o->x - 5);
    }
    if (!male() || !(G.game_flags & 2)) {
        if (key(K_UP) && !G.looking)
            o->yspeed = male() ? -0x13AA : -0xFAA;
        else if (key(K_DOWN) && !G.looking)
            o->yspeed = 8000;
    }
    return you_climb_move(o);
}

/* RXYOUF.C:1087 FYOUCLIMBMOVE: the speed decays by Friction / 2; at the top of the vine
 * scramble up, at the bottom let go. */
int you_climb_move(obj_t *o)
{
    obj_t *you = G.you;
    if (G.game_flags & 2) {
        if (male())
            you_whip(o);
        else
            you_shoot(o);
    }
    if (o->yspeed < 0) {
        o->yspeed = (int16_t)(o->yspeed + (you->friction >> 1));
        if (o->yspeed > 0)
            o->yspeed = 0;
    } else {
        o->yspeed = (int16_t)(o->yspeed - (you->friction >> 1));
        if (o->yspeed < 0)
            o->yspeed = 0;
    }
    if (o->yspeed != 0) {
        const uint16_t old = o->ypart;
        o->ypart = (uint16_t)(o->ypart + iabs16(o->yspeed));
        if (o->ypart <= old) {
            can_move(o);
            const unsigned row = (unsigned)(uint16_t)(o->y + 6) >> 4;
            const unsigned col = (unsigned)(uint16_t)(o->x + 7) >> 4;
            if (o->yspeed < 0 && can_go_up(o) && o->y >= 3) {
                player_up(1, o);
                if (!(G.game_flags & 2))
                    next_frame(o, G.you_climb);
                const unsigned r2 = (unsigned)(uint16_t)(o->y + 6) >> 4;
                if (fg_type_at(r2, col) == T_VINE_TOP) {
                    G.game_flags &= 0xFFFE;
                    G.vine_x = (uint16_t)col;
                    G.vine_y = (uint16_t)r2;
                    you->gv[1] = 1;
                    o->animator = A_YOUBEGINSCRAMBLE;
                }
                return 0;
            }
            if (o->yspeed < 1 || you->down_offset != 1)
                return 0;
            player_down(1, o);
            if (!(G.game_flags & 2))
                next_frame(o, G.you_climb);
            const unsigned r2 = (unsigned)(uint16_t)(o->y + 6) >> 4;
            (void)row;
            if (fg_type_at(r2, col) != T_VINE_BOTTOM && bg_type_at(r2, col) != T_VINE_BOTTOM)
                return 0;
            G.game_flags &= 0xFFFE;
            you->max_frames = male() ? 8 : 7;
            you->frame_count_max = 0x2D;
            o->animator = A_YOUBEGINFALL;
            return 0;
        }
    }
    if ((!male() || you->gv[0] == 0) && !(G.game_flags & 2))
        obj_image(you, img(G.you_climb, you->frame_no));
    if (key(K_ATTACK)) {
        if (!male()) {
            if (you->gv[0] == 0)
                return you_begin_shoot(o);
        } else if (!(G.game_flags & 2) && you->gv[0] < 10) {
            return you_begin_whip(o);
        }
    }
    return 0;
}

/* RXYOUF.C:1157 FYOUBEGINSCRAMBLE: climbing over the top of a vine onto the ledge. */
int you_begin_scramble(obj_t *o)
{
    obj_t *you = G.you;
    set_scroll_bounds(0x8C, G.window_xsize);
    G.window_ysize = 0x8C;
    if (G.center_off >= 0)
        G.center_off = 'V';
    you->gv[5] &= ~4 & ~1;
    if (G.game_flags & 2)
        G.game_flags &= 0xFFFD;
    o->yspeed = 0;
    o->xspeed = 0;
    o->animator = A_YOUSCRAMBLE;
    you->frame_count_max = male() ? 0x40 : 0x50;
    you->frame_count = you->frame_count_max;
    return 0;
}

/* RXYOUF.C:1180 FYOUSCRAMBLE: GenericVar[1] is the step (1-4, climbing frames 7-10); up goes
 * on to standing on the ledge (5), down back onto the vine (0). */
int you_scramble(obj_t *o)
{
    obj_t *you = G.you;
    if (key(K_JUMP) && (you->gv[5] & 8)) {
        you->max_frames = male() ? 8 : 7;
        o->animator = A_YOUBEGINJUMP;
        return 0;
    }
    if (!key(K_JUMP))
        you->gv[5] |= 8;
    if (--you->frame_count != 0)
        return 0;
    you->frame_count = you->frame_count_max;
    if (key(K_UP) && !G.looking) {
        if (++you->gv[1] == 5) {
            o->y = (uint16_t)(G.vine_y * 16 - 0x29);
            o->animator = A_YOUBEGINRUN;
            return 0;
        }
    } else if (key(K_DOWN) && !G.looking) {
        if (--you->gv[1] == 0) {
            o->animator = A_YOUBEGINCLIMB;
            return 0;
        }
    }
    static const int8_t female_y[] = {0, 3, -3, -0x0E, -0x14};
    static const int8_t male_y[] = {0, 3, -6, -9, -0x13};
    if (you->gv[1] >= 1 && you->gv[1] <= 4)
        o->y = (uint16_t)(G.vine_y * 16 + (male() ? male_y : female_y)[you->gv[1]]);
    obj_image(o, img(G.you_climb, you->gv[1] + 6));
    return 0;
}

/* ------------------------------------------------------------------------------------------ */
/* Attacks                                                                                     */

/* RXYOUF.C:752 FYOUBEGINSHOOT: the female's throwing pose for where she is and the keys held
 * (crouching, climbing, standing, in the air; aiming up or down), then FYOUSHOOT. */
int you_begin_shoot(obj_t *o)
{
    obj_t *you = G.you;
    int base = G.you_attack, n;
    G.whip_fc = 100;
    if (o->ysize == 0x19) {
        base = G.you_duck;
        n = 3;
    } else if (G.game_flags & 1) {
        n = key(K_UP) ? 3 : key(K_DOWN) ? 4 : 2;
    } else if (!(you->gv[5] & 1)) {
        n = key(K_UP) ? 6 : key(K_DOWN) ? 7 : 5;
    } else {
        n = key(K_UP) ? 1 : 0;
    }
    obj_image(o, img(base, n));
    G.game_flags |= 2;
    you->gv[0] = 0x0C;
    return you_shoot(o);
}

/* RXYOUF.C:786 FYOUSHOOT: the first call throws a fireball (if one of the two slots is free
 * and there's a gem to pay with): sideways, up (standing, holding up) or down (in the air,
 * holding down); a stronger fire adds one or two that follow it. Then the pose holds for 75
 * calls. */
int you_shoot(obj_t *o)
{
    obj_t *you = G.you;
    if (G.whip_fc < 100) {
        if (--G.whip_fc == 0)
            G.game_flags &= 0xFFFD;
        return 0;
    }
    G.whip_fc = 'K';
    int slot = 10, i = 0;
    do {
        if (!Inv.weapon_status[i])
            slot = i;
    } while (slot == 10 && ++i < 2);
    if (slot == 10 || Inv.gems == 0)
        return 0;
    obj_t *f = spawn_obj(0x06, LIST_MAIN, G.img[IMG_FIREBALL], 0, 0);
    f->gv[1] = 0;
    if (!key(K_UP) || you->ysize != 0x29) {
        if (!key(K_DOWN) || (you->gv[5] & 1)) {
            f->x = you->xdir == 0 ? (uint16_t)(you->x - 0x0E) : (uint16_t)(you->x + 0x0C);
            f->y = you->ysize == 0x19 ? (uint16_t)(you->y + 3) : (uint16_t)(o->y + 6);
        } else {
            f->x = (uint16_t)(you->x + 2);
            f->y = (uint16_t)(you->y + 0x10);
            f->gv[1] = 2; /* down */
        }
    } else {
        f->x = you->xdir == 0 ? (uint16_t)(you->x + 2) : (uint16_t)(you->x - 2);
        f->y = (uint16_t)(you->y - 9);
        f->gv[1] = 1; /* up */
    }
    if (f->gv[1] == 0)
        f->gv[1] = you->xdir == 0 ? 3 : 4; /* left, right */
    const int16_t right = (int16_t)(G.lv->cols * 16 - 1), bottom = (int16_t)(G.lv->rows * 16 - 1);
    if ((int16_t)f->x < 0x13)
        f->x = 0x13;
    else if ((int16_t)f->x > right)
        f->x = (uint16_t)right;
    if ((int16_t)f->y < 0x13)
        f->x = 0x13; /* (sic) */
    else if ((int16_t)f->y > bottom)
        f->y = (uint16_t)bottom;
    f->gv[0] = (int16_t)slot;
    f->gv[2] = 0;
    Inv.weapon_status[i] = 1;
    play_sample(0x61, NULL);
    Inv.gems--;
    for (int k = 1; k <= 2; k++) {
        if (fire_power() > (unsigned)k) {
            obj_t *g = spawn_obj(0x06, LIST_MAIN, G.img[IMG_FIREBALL], f->x, f->y);
            g->gv[0] = f->gv[0];
            g->gv[1] = f->gv[1];
            g->gv[2] = (int16_t)k;
        }
    }
    return 0;
}

/* RXYOU.C:82 DOFIREBALL: a fireball (GenericVar[2] 0-2 the player's: the first, and the ones
 * following it 30 and 60 ticks later; 3-5 an enemy's). It flies a pixel a run in direction
 * GenericVar[1] (1 up, 2 down, 3 left, 4 right; 13 / 14 left / right while falling) and
 * bursts on a wall, off screen, or on what it hits. */
int do_fireball(obj_t *o)
{
    obj_t *you = G.you;
    bool boom = false;
    if (o->gv[3] == 0) {
        o->gv[3]++;
        const uint16_t w = map_cell(o->y >> 4, o->x >> 4) |
                           map_cell((uint16_t)(o->y + o->ysize) >> 4,
                                    (uint16_t)(o->x + o->xsize) >> 4);
        boom = (w & 0x1E00) != 0; /* thrown into a wall */
    }
    if (!boom) {
        if (!obj_onscreen(o)) {
            if (o->gv[2] == 0) {
                Inv.weapon_status[o->gv[0] & 1] = 0;
                if (you)
                    you->gv[0] = 9;
            }
            obj_kill(o);
            return 0;
        }
        int x = 0;
        const int k = o->gv[2];
        if (k < 3) {
            if (fire_power() == 2)
                x = k == 0 ? 3 : 0;
            else if (fire_power() != 1)
                x = k == 0 ? 6 : k == 1 ? 3 : 0;
        } else {
            x = k == 3 ? 6 : k == 4 ? 3 : 0;
        }
        next_frame(o, IMG_FIREBALL + x);
        if ((k == 1 || k == 4) && o->counter < 0x1E)
            return 0;
        if ((k == 2 || k == 5) && o->counter < 0x3C)
            return 0;
        can_move(o);
        switch (o->gv[1]) {
        case 1:
            if (!can_go_up(o) || o->y < 3)
                boom = true;
            else
                o->y--;
            break;
        case 2:
            if (o->down_offset == 0 ||
                (G.level == 0x0D && (uint16_t)(G.org_y + 0xC0) < (uint16_t)(o->y + o->ysize)))
                boom = true;
            else
                o->y++;
            break;
        case 3: case 13: case 4: case 14:
            if (o->gv[1] > 5) {
                if (o->down_offset == 0)
                    o->gv[1] -= 10;
                else
                    o->y++;
            }
            if (o->gv[1] == 3 || o->gv[1] == 13) {
                if (can_go_left(o) && room_left(o))
                    o->x--;
                else
                    boom = true;
            } else {
                if (can_go_right(o) && room_right(o))
                    o->x++;
                else
                    boom = true;
            }
            break;
        default:
            break;
        }
    }
    if (boom)
        o->up_offset = 11;
    if (o->gv[2] < 3) {
        obj_t *nx;
        for (obj_t *p = G.list[LIST_MAIN]; p; p = nx) {
            nx = p->next;
            if (obj_overlap(o, p) && (p->flags & OF_HITTABLE)) {
                o->up_offset = 11;
                p->gv[3]++;
                p->flags |= OF_HIT;
                p->blink_color = p->priority == 2 ? 0x1D : 0x2E;
                if (p == G.boss)
                    obj_blink(p, 2, 3, 3);
                else
                    obj_blink(p, 2, 5, 5);
            }
        }
    } else if (you && obj_collide(you, o)) {
        o->up_offset = 11;
        hit_player(o);
    }
    if (o->up_offset <= 10)
        return 0;
    play_sample(0x62, NULL);
    spawn_obj(0x0D, LIST_MAIN, G.img[IMG_EXPLODE], o->x, o->y)->gv[0] = 0;
    if (o->gv[2] == 0) {
        Inv.weapon_status[o->gv[0] & 1] = 0;
        if (you)
            you->gv[0] = 9;
    }
    obj_kill(o);
    return 0;
}

/* _SwordIndex (DS:19df): the whip's three animation frames for each power, as _Img
 * indexes: Sword1ID[0/3/6], Sword2ID[0/5/10], Sword3ID[0/5/10]. Each frame has a picture
 * per segment (3 for the first whip, 5 for the others). */
static const int sword_index[3][3] = {
    {IMG_SWORD1, IMG_SWORD1 + 3, IMG_SWORD1 + 6},
    {IMG_SWORD2, IMG_SWORD2 + 5, IMG_SWORD2 + 10},
    {IMG_SWORD3, IMG_SWORD3 + 5, IMG_SWORD3 + 10},
};

static unsigned whip_level(void)
{
    const unsigned p = whip_power();
    return p == 0 ? 1 : p;
}

/* RXYOUF.C:971 WHIPPINGFRAME: the arm's three frames, image[1], image[2], image[1], each held
 * _WhippingFCM calls. */
static void whipping_frame(int base)
{
    if (--G.whip_fc >= 1)
        return;
    G.whip_fc = G.whip_fcm;
    G.whip_fn = (int8_t)(G.whip_fn + G.whip_ai);
    if ((G.whip_ai < 0 && G.whip_fn == 0) || (G.whip_ai > 0 && G.whip_fn == 2))
        G.whip_ai = (int8_t)-G.whip_ai;
    G.you->image = img(base, G.whip_fn);
}

/* RXYOUF.C:874 FYOUBEGINWHIP: a crack of the whip; the stronger whips are quicker but need
 * longer to recover (GenericVar[0]). */
int you_begin_whip(obj_t *o)
{
    G.game_flags |= 2;
    G.whip_fn = 0;
    G.whip_ai = 1;
    if (whip_power() == 1) {
        G.whip_fcm = G.whip_fc = 'N';
        G.you->gv[0] = 0x19;
    } else if (whip_power() == 2) {
        G.whip_fcm = G.whip_fc = '<';
        G.you->gv[0] = 0x14;
    } else {
        G.whip_fcm = G.whip_fc = '3';
        G.you->gv[0] = 0x0F;
    }
    return you_whip(o);
}

/* RXYOUF.C:897 FYOUWHIP: swing the arm; as it reaches out, lay the whip's segments (DOWHIP)
 * 16 pixels apart in front of the player, short of any wall (cells with 0x200 / 0x400). */
int you_whip(obj_t *o)
{
    obj_t *you = G.you;
    const int base = o->ysize == 0x19 ? G.you_duck + 2
                     : !(G.game_flags & 1) ? G.you_attack - 1 : G.you_attack + 1;
    whipping_frame(base);
    if (G.whip_fn == 2 && G.whip_fc == G.whip_fcm - 1) {
        play_sample(0x81, o);
        for (int g = 0; g < 5; g++) {
            bool blocked = false;
            uint16_t sx = you->xdir == 0 ? (uint16_t)(you->x - 0x17 - (4 - g) * 16)
                                         : (uint16_t)(you->x + (4 - g) * 16 + 0x15);
            const uint16_t y = you->ysize == 0x19 ? (uint16_t)(you->y + 6)
                                                  : (uint16_t)(you->y + 0x0E);
            if ((uint16_t)(G.lv->cols * 16 - 1) < sx) {
                if (g < 4)
                    blocked = true;
                else
                    sx = 0;
            }
            if (g < 4 && !blocked) {
                if (you->xdir == 0) {
                    for (uint16_t u = sx; u < you->x; u = (uint16_t)(u + 16))
                        if ((map_cell((uint16_t)(y + 6) >> 4, (uint16_t)(u - 8) >> 4) |
                             map_cell((uint16_t)(y + 10) >> 4, (uint16_t)(u - 8) >> 4)) & 0x600)
                            blocked = true;
                } else {
                    for (uint16_t u = sx; you->x < u; u = (uint16_t)(u - 16))
                        if ((map_cell((uint16_t)(y + 6) >> 4, (uint16_t)(u + 8) >> 4) |
                             map_cell((uint16_t)(y + 10) >> 4, (uint16_t)(u + 8) >> 4)) & 0x600)
                            blocked = true;
                }
            }
            int frame = sword_index[whip_level() - 1][0] + g;
            if (whip_level() == 1) {
                if (g > 1)
                    frame -= 2;
                else
                    blocked = true; /* the first whip has three segments */
            }
            if (!blocked) {
                obj_t *w = spawn_obj(0x05, LIST_MAIN, G.img[frame], sx, y);
                w->xdir = you->xdir;
                w->gv[0] = (int16_t)g;
                w->frame_count_max = w->frame_count = (uint16_t)(G.whip_fcm / 3);
            }
        }
    } else if (G.whip_fn == 1 && G.whip_ai == -1) {
        G.game_flags &= 0xFFFD;
    }
    return 0;
}

/* RXYOU.C:238 DOWHIP: a segment of the whip (GenericVar[0] 0 the tip .. 4 by the hand). It
 * stays in front of the player, goes through its three frames, and hits (ENEMYHIT counts 3,
 * or 4 with the strongest whip) whatever hittable thing it touches, up to three objects a
 * crack (DS:553a). Segments that aren't out yet (Sword2ID[0/1], Sword3ID[0/1]) don't hit. */
int do_whip(obj_t *o)
{
    obj_t *you = G.you;
    if (!you) {
        obj_kill(o);
        return 0;
    }
    bool seen = false;
    const int g = o->gv[0];
    o->x = you->xdir == 0 ? (uint16_t)(you->x - 0x17 - (4 - g) * 16)
                          : (uint16_t)(you->x + (4 - g) * 16 + 0x15);
    if ((uint16_t)(G.lv->cols * 16 - 1) < o->x) {
        if (g < 4) {
            o->x = 100;
            obj_kill(o);
            return 0;
        }
        o->x = 0;
    }
    o->xdir = you->xdir;
    o->y = you->ysize == 0x19 ? (uint16_t)(you->y + 6) : (uint16_t)(you->y + 0x0E);
    if (g < 4 && ((map_cell((uint16_t)(o->y + 6) >> 4, (uint16_t)(o->x + 8) >> 4) |
                   map_cell((uint16_t)(o->y + 10) >> 4, (uint16_t)(o->x + 8) >> 4)) & 0x600)) {
        obj_kill(o);
        return 0;
    }
    if (--o->frame_count == 0) {
        if (o->frame_no == 3) {
            Inv.weapon_status[0] = 0;
            obj_kill(o);
            memset(G.whip_hits, 0, sizeof G.whip_hits);
            G.whip_hit_count = 0;
            return 0;
        }
        const int frame = sword_index[whip_level() - 1][o->frame_no] +
                          (whip_level() == 1 ? g - 2 : g);
        obj_image(o, G.img[frame]);
        o->frame_count = o->frame_count_max;
        o->frame_no++;
    }
    if (G.whip_hit_count == 3 || o->image == G.img[IMG_SWORD2] ||
        o->image == G.img[IMG_SWORD2 + 1] || o->image == G.img[IMG_SWORD3] ||
        o->image == G.img[IMG_SWORD3 + 1])
        return 0;
    obj_t *nx;
    for (obj_t *p = G.list[LIST_MAIN]; p; p = nx) {
        nx = p->next;
        /* (once an object already hit turns up, `seen` stays set for the rest of the list) */
        for (int i = 0; i < G.whip_hit_count; i++)
            if (G.whip_hits[i] == p)
                seen = true;
        if (!obj_collide(o, p) || !(p->flags & OF_HITTABLE) || (p->flags & OF_HIT) || seen)
            continue;
        G.whip_hits[G.whip_hit_count++] = p;
        const uint16_t ex = (uint16_t)(o->x + frandom(o->xsize));
        const uint16_t ey = (uint16_t)(o->y + frandom(o->ysize));
        obj_t *e = spawn_obj(0x0D, LIST_MAIN, G.img[IMG_EXPLODE + 12], ex, ey);
        const int16_t pr = p->priority;
        if (pr == 0x34 || pr == 0x96 || pr == 2 || pr == 0x31 || pr == 0x47 || pr == 0x4E ||
            pr == 0x4A || pr == 0x29 || pr == 0x39 || !G.blood_on) {
            /* sparks, and the thing blinks */
            obj_image(e, G.img[IMG_EXPLODE + 6]);
            e->gv[0] = 1;
            p->blink_color = pr == 2 ? 0x1D : 0x2E;
            if (p == G.boss)
                obj_blink(p, 2, 3, 3);
            else
                obj_blink(p, 2, 5, 5);
        } else {
            e->gv[0] = 3; /* blood */
        }
        p->gv[3] = (int16_t)(p->gv[3] + (whip_power() == 3 ? 4 : 3));
        p->flags |= OF_HIT;
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------ */
/* Hurt and dying                                                                              */

/* RXYOUF.C:1235 FYOUHIT: knocked back (HITPLAYER's jump; GENERICUP / GENERICFALL call this
 * every tick): keep protected and drift away from the blow, except past the view's edge on
 * the levels whose view doesn't follow. */
int you_hit(obj_t *o)
{
    can_move(o);
    const uint16_t old = o->xpart;
    o->xpart = (uint16_t)(o->xpart + iabs16(o->xspeed));
    if (old < o->xpart)
        return 0;
    G.hitagain = 0x0B;
    if (!G.blood_on && o->blinks == 0) {
        G.you->blink_color = 0x2E;
        obj_blink(G.you, 0x19, 3, 3);
    }
    if (o->xspeed == 0)
        return 0;
    const uint8_t lv = G.level;
    const bool fixed = lv == 1 || lv == 3 || lv == 10 || lv == 0x10 || lv == 0x13 ||
                       lv == 0x14 || lv == 0x16 || lv == 0x19;
    if (o->xdir == 0 && can_go_right(o) && room_right(o) && o->down_offset < 10 &&
        o->up_offset < 10 && (!fixed || o->x <= (uint16_t)(G.org_x + 0x12D))) {
        player_right(1, o);
        return 0;
    }
    if (o->xdir != 0 && can_go_left(o) && room_left(o) && o->down_offset < 10 &&
        o->up_offset < 10 && (!fixed || (uint16_t)(G.org_x + 5) <= o->x))
        player_left(1, o);
    return 0;
}

/* RXYOUF.C:1274 FYOUBEGINIMPALE: on a spike (GRABSTUFF, tile type 0x6d): sink to _Vine_y,
 * then die. */
int you_begin_impale(obj_t *o)
{
    G.center_off = 'V';
    o->animator = A_YOUBEGINIMPALE;
    if (carry(&o->xpart, 10000)) {
        if (G.vine_y < o->y)
            o->y--;
        else if (o->y < G.vine_y)
            o->y++;
    }
    if (o->counter > 0x28 && o->y == G.vine_y)
        o->animator = A_YOUDIE;
    return 0;
}

/* RXYOUF.C:1295 FYOUDIE: the body; PROCESSLEVEL restarts the level a while later
 * (_quitflag 4). */
int you_die(obj_t *o)
{
    G.hitagain = 0;
    obj_image(o, G.img[IMG_BOTHDEAD + ((G.game_flags & 4) ? 1 : 0)]);
    obj_blink(G.you, 0, 0, 0);
    if (G.hit_pts)
        G.hit_pts->blinks = 0;
    o->animator = A_NULLANIMATOR;
    Inv.hit_points[0] = Inv.hit_points[1] = 0;
    G.center_off = 'f';
    G.quitflag = 4;
    return 0;
}

/* RXYOUF.C:1320 FYOUCRUSH: crushed (by a crusher or a falling block): gone in a burst of
 * blood. */
int you_crush(obj_t *o)
{
    if (G.you && G.you->animator != A_NULLANIMATOR && Inv.hit_points[0]) {
        play_sample((G.game_flags & 4) ? 0xAD : 0xAB, NULL);
        if (G.blood_on) {
            obj_t *b = spawn_obj(0x21, LIST_MAIN, G.img[IMG_SWORD3], (uint16_t)(o->x - 5),
                                 (uint16_t)(o->y - 5));
            b->xsize = o->xsize;
            b->ysize = o->ysize;
            b->gv[1] = 3;
            b->gv[0] = (int16_t)o->ysize;
            b = spawn_obj(0x24, LIST_FORE, G.img[IMG_SWORD3], o->x, o->y);
            b->gv[0] = 0;
            b->gv[1] = 0x19;
            b->gv[2] = 1;
            b->animator = A_SPLASHSPAWNER;
            b->xsize = o->xsize;
            b->ysize = o->ysize;
        }
        o->image = G.img[IMG_SWORD3];
        o->animator = A_NULLANIMATOR;
        Inv.hit_points[0] = Inv.hit_points[1] = 0;
        G.center_off = 'f';
        G.you->blinks = 0;
        if (G.hit_pts)
            G.hit_pts->blinks = 0;
    }
    G.quitflag = 4;
    return 0;
}

/* RXYOUF.C:1359 FYOUDROWN: into water or a bottomless pit (GRABSTUFF tile type 0x65, the
 * swamp's bottom): a splash, and gone. */
int you_drown(obj_t *o)
{
    play_sample(0x72, NULL);
    obj_t *s = spawn_obj(0x24, LIST_FORE, G.you ? G.you->image : NULL, (uint16_t)(o->x + 7),
                         (uint16_t)(o->y + 10));
    s->image = NULL;
    s->gv[0] = 0;
    s->gv[1] = 0x10;
    s->gv[2] = 0;
    s->animator = A_SPLASHSPAWNER;
    o->image = G.img[IMG_SWORD3];
    o->animator = A_NULLANIMATOR;
    Inv.hit_points[0] = Inv.hit_points[1] = 0;
    G.center_off = 'f';
    if (G.you)
        G.you->blinks = 0;
    if (G.hit_pts)
        G.hit_pts->blinks = 0;
    G.quitflag = 4;
    return 0;
}

/* RXYOUF.C:1385 FDROPGEMSPAWNER: after a hit, up to five of the player's gems spill out
 * (DOCOLLECTABLE) where there's room, one a frame. */
int drop_gem_spawner(obj_t *o)
{
    obj_t *you = G.you;
    if (Inv.gems == 0 || o->gv[0] == 5 || !you) {
        obj_kill(o);
        return 0;
    }
    const uint16_t x0 = (uint16_t)(you->x + frandom(0x1E));
    const uint16_t y0 = (uint16_t)(you->y + frandom(0x10));
    const uint16_t x = (uint16_t)(x0 - 8), y = (uint16_t)(y0 + 8);
    const uint16_t x2 = (uint16_t)(x0 + 8), y2 = (uint16_t)(y0 + 0x18);
    const uint16_t w = map_cell(y >> 4, x >> 4) | map_cell(y2 >> 4, x2 >> 4) |
                       map_cell(y2 >> 4, x >> 4) | map_cell(y >> 4, x2 >> 4);
    if (w & 0x1E00)
        return 0;
    Inv.gems--;
    o->gv[0]++;
    obj_t *c = spawn_obj(0x14, LIST_MAIN, G.img[IMG_COLLECT + 0x12], x, y);
    c->max_frames = 3;
    c->frame_count_max = 0x28;
    c->xspeed = (int16_t)frandom(10000);
    c->yspeed = (int16_t)(-1000 - frandom(9000));
    c->gv[1] = 0x12;
    c->gv[4] = 0x78;
    c->animator = A_GENERICBEGINJUMP;
    c->xdir = (uint8_t)frandom(2);
    return 0;
}

/* RXYOUF.C:1439 FDOHITPOINTS: the hit points display (HpID, by the maximum: 4, 6, 8 or 10)
 * in the view's top left corner. It blinks on the last hit point and while poisoned, and
 * poison takes a hit point at each of the player's blinks. */
int do_hit_points(obj_t *o)
{
    obj_t *you = G.you;
    uint8_t *hp = you_hp();
    const bool m = (G.game_flags & 4) != 0;
    const bool poisoned = (Inv.flags & (m ? 1 : 2)) != 0;
    o->x = (uint16_t)(G.org_x + 8);
    o->y = (uint16_t)(G.org_y + 7);
    if (*hp == 1 || poisoned) {
        if (o->blinks == 0) {
            o->blink_color = poisoned ? 0x53 : 0x2E;
            obj_blink(o, 0x32, 3, 6);
        }
        if (poisoned && you && you->blinks == 0) {
            you->blink_color = 0x53;
            obj_blink(you, 0x32, 3, 6);
            if (!G.hitagain && *hp) {
                play_sample(0xA4, NULL);
                if (--*hp == 0) {
                    G.free_hit = 1;
                    *hp = 2;
                    hit_player(o);
                }
            }
        }
    }
    if (*hp != o->gv[0]) {
        o->gv[0] = *hp;
        static const struct { uint8_t max; int base; } sets[] = {{4, 0}, {6, 5}, {8, 12}, {10, 21}};
        for (int i = 0; i < 4; i++)
            if (Inv.max_hit_points[m] == sets[i].max)
                o->image = img(IMG_HP + sets[i].base, o->gv[0]);
    }
    if (!G.status_on) {
        obj_kill(o);
        G.hit_pts = NULL;
    }
    return 0;
}

/* RXYOU.C:409 DOGEMNUMBER: the gem counter under the hit points: GenericVar[0] 0-3 a digit of
 * the count (GemNumID, left aligned), 4 the spinning gem. Nothing shows without gems. */
int do_gem_number(obj_t *o)
{
    if (o->gv[0] < 4) {
        o->x = (uint16_t)(G.org_x + o->gv[0] * 8 + 0x11);
        o->y = (uint16_t)(G.org_y + 0x10);
    } else {
        o->x = (uint16_t)(G.org_x + 4);
        o->y = (uint16_t)(G.org_y + 0x0C);
    }
    if (!G.status_on) {
        obj_kill(o);
    } else if (Inv.gems == 0) {
        o->image = G.img[IMG_SWORD3];
    } else if (o->gv[0] < 4) {
        if (o->counter > 4) {
            o->counter = 0;
            char digits[8];
            snprintf(digits, sizeof digits, "%u", (unsigned)Inv.gems);
            const char c = (size_t)o->gv[0] < strlen(digits) ? digits[o->gv[0]] : 0;
            o->image = c ? G.img[IMG_GEMNUM + (c - '0')] : G.img[IMG_SWORD3];
        }
    } else {
        next_frame(o, IMG_COLLECT + 0x12);
    }
    return 0;
}

/* RXYOU.C:463 DOBLOOD: a splat of blood drying up (BloodID 10 .. 6). */
int do_blood(obj_t *o)
{
    if (o->gv[2] < 6) {
        obj_kill(o);
    } else {
        const int k = o->gv[2]--;
        obj_image(o, G.img[IMG_BLOOD + k]);
    }
    return 0;
}

/* RXYOU.C:471 DOAMBIENTNOISE / RXYOUF.C:1589 FDOAMBIENTNOISE: the level's background sound
 * (type 0x5c, OF_RATE, xSpeed 0x2000): when the last one has finished (and the sound card has
 * more than 3 voices), play the level's again: birds in the forest, wind, water, the swamp...
 * The sound is a stub, so for now this only picks the sample. */
int do_ambient_noise(obj_t *o)
{
    /* (MAINTAINSOUND first, and nothing at all with _SoundVoices <= 3.) */
    if (o->gv[0] != 0 && !sound_link_active(o->gv[0]))
        o->gv[0] = 0;
    if (o->gv[0] != 0)
        return 0;
    const uint8_t lv = G.level;
    uint8_t sample = 0;
    if (lv == 1 || lv == 2 || lv == 11 || lv == 12)
        sample = 0x16;
    else if (lv == 3)
        sample = 0x0B;
    else if (lv >= 4 && lv <= 7)
        sample = 0x07;
    else if (lv >= 8 && lv <= 10)
        sample = 0x26;
    else if (lv == 13 || lv == 14 || lv == 21)
        sample = 0x30;
    else if (lv >= 17 && lv <= 20)
        sample = 0x13;
    else if (lv >= 22 && lv <= 24)
        sample = 0x10;
    else if (lv == 25 || lv == 26)
        sample = 0x09;
    if (sample)
        o->gv[0] = (int16_t)play_sample(sample, o);
    return 0;
}

/* RXGENERL.C SCREENCENTER: the camera object (INITLEVEL spawns it). It scrolls a pixel at a
 * time until the player is between 0x4b and _CenterOff pixels from the top of the view (and,
 * on a vine, near the middle), then restores the normal dead zone. */
int screen_center(obj_t *o)
{
    (void)o;
    const obj_t *you = G.you;
    if (!you || G.center_off <= 0)
        return 0;
    if (you->animator == A_YOUCLIMB && G.level != 0x16) {
        if (you->x < (uint16_t)(G.org_x + 0x8B) && G.org_x != 0)
            scroll_left(1);
        else if ((uint16_t)(G.org_x + 0x95) < you->x &&
                 G.org_x < (uint16_t)(G.lv->cols * 16 - 320))
            scroll_right(1);
    }
    if (you->y < (uint16_t)(G.org_y + 0x4B) && G.org_y != 0) {
        scroll_up(1);
    } else if ((uint16_t)(G.org_y + G.center_off) < you->y &&
               G.org_y < (uint16_t)(G.lv->rows * 16 - 200)) {
        scroll_down(1);
    } else if (G.window_ysize != 50) {
        set_scroll_bounds(0x32, G.window_xsize);
        G.window_ysize = 50;
    }
    return 0;
}

/* The hero's sprite sets (_YouWalkID...): the male's or the female's, by _GameFlags bit 2. */
static void set_hero_sets(void)
{
    if (G.game_flags & 4) {
        G.you_walk = IMG_MALWALK, G.you_jump = IMG_MALJUMP, G.you_duck = IMG_MALDUCK;
        G.you_climb = IMG_MALCLIMB, G.you_attack = IMG_MALATTAC;
    } else {
        G.you_walk = IMG_FEMWALK, G.you_jump = IMG_FEMJUMP, G.you_duck = IMG_FEMDUCK;
        G.you_climb = IMG_FEMCLIMB, G.you_attack = IMG_FEMATTAC;
    }
}

/* RXYOUF.C:1494 FDOTRANSFORMER (type 0x37; DOTRANSFORMER calls it): the special item. Lightning
 * strikes the player (DOLIGHTNING bolts every 0x20 pixels from the view's top, and 200 ticks of
 * sparks), and 100 ticks later the other hero stands there: _GameFlags bit 2 flips and with it
 * the sprite sets and which hit points count. The player blinks (colour 0x1d) meanwhile; after
 * 800 ticks the item can be used again. Starting a level with _LightningFlag set (a warp) the
 * same object brings the player in (> 0) or takes them away to _nextlevel (< 0, _quitflag 8). */
int do_transformer(obj_t *o)
{
    obj_t *you = G.you;
    if (!you)
        return 0;
    if (o->counter < 300) {
        if (o->gv[0] == 0) {
            play_sample(0xB2, NULL);
            if ((uint16_t)(G.org_y + 9) < o->y)
                for (uint16_t y = G.org_y; y < (uint16_t)(o->y - 8); y = (uint16_t)(y + 0x20))
                    spawn_obj(0x0C, LIST_MAIN, G.img[IMG_LIGHTNG + frandom(2)], o->x, y);
            obj_t *s = spawn_obj(0x21, LIST_FORE, you->image, (uint16_t)(you->x - 5),
                                 (uint16_t)(you->y - 5));
            s->image = G.img[IMG_SWORD3];
            s->xsize = 0x18;
            s->ysize = 0x23;
            s->gv[0] = 200;
            s->gv[1] = 4;
            o->gv[0]++;
        }
        if (o->counter > 100 && o->gv[0] == 1) {
            if (G.lightning_flag == 0) {
                G.game_flags ^= 4;
                set_hero_sets();
                if (you->animator == A_YOURUN)
                    you->max_frames = male() ? 8 : 7;
            } else if (G.lightning_flag < 0) {
                G.next_level = (uint8_t)-G.lightning_flag;
                you->image = G.img[IMG_SWORD3];
            } else {
                you->animator = A_YOUBEGINRUN;
                obj_image(you, G.img[G.you_walk + 8]);
            }
            if (G.hit_pts) {
                G.hit_pts->gv[0] = 0;
                G.hit_pts->blinks = 0;
            }
            o->gv[0]++;
        }
    }
    o->x = you->x;
    o->y = you->y;
    you->blink_color = 0x1D;
    if (you->blinks == 0 && you->image)
        obj_blink(you, 10, 2, 3);
    if (o->counter > 800) {
        you->gv[5] |= 0x10;
        if (G.lightning_flag < 0)
            G.quitflag = 8;
        G.lightning_flag = 0;
        obj_kill(o);
    }
    return 0;
}
