/* RXYOU.C / RXYOUF.C: the player. Each YOU* animator calls its FYOU* worker; the port names
 * them after the YOU* animator and ports the worker.
 *
 * GenericVar[5] bits: 1 on the ground, 2 falling, 4 jumping, 8 the jump key has been released
 * (so holding it doesn't jump again), 0x10 holds the special item. _GameFlags: 1 climbing,
 * 2 attacking (whip / fireball in progress), 4 the male character, 8 recovering from a hit.
 * The female character shoots fireballs, the male uses the whip.
 *
 * Not ported yet: attacking (YOUBEGINSHOOT / YOUBEGINWHIP), the special item (key 6),
 * damage and death (YOUHIT, YOUDIE, YOUCRUSH, YOUDROWN...), blinking. */
#include "behave.h"

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
        G.hit_points[0])
        return 0; /* FYOUDROWN: not ported */
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
    /* (Key 6 with the special item spawns it, type 0x37: not ported yet.) */

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
    /* (_GameFlags 2: FYOUSHOOT / FYOUWHIP, not ported yet.) */
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
    /* (The attack key starts FYOUBEGINSHOOT / FYOUBEGINWHIP: not ported yet.) */
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
    if (!G.hit_points[1] || !G.hit_points[0]) {
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
    /* (blinking while recovering from a hit: not ported) */
    if (you->gv[1] == 0) {
        if (G.game_flags & 8)
            G.game_flags &= 0xFFF7;
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
        /* (attack and special item keys: not ported yet) */
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
    /* (special item key: not ported yet) */
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
    /* (attack key: not ported yet) */
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
