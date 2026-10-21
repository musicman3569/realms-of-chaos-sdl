/* RXPLAT.C: platforms. A platform moves by setting bits in GenericVar[5] (1 up, 2 down,
 * 4 left, 8 right) for MAINTAINPLATFORM, which moves it one pixel and carries the player if
 * the player rides it (bit 0x20). Bit 0x10 means it can't be ridden (it's breaking up). */
#include "behave.h"

enum { MOVE_UP = 1, MOVE_DOWN = 2, MOVE_LEFT = 4, MOVE_RIGHT = 8, NO_RIDE = 0x10, RIDER = 0x20 };

/* RXPLAT.C:983 MAINTAINPLATFORM */
void maintain_platform(obj_t *o)
{
    obj_t *you = G.you;
    int riding_another = 0;
    if (o->gv[5] & MOVE_UP) {
        o->y--;
        if ((o->gv[5] & RIDER) && you) {
            for (const obj_t *p = G.list[LIST_MAIN]; p; p = p->next)
                if ((p->surface_flags & 0x10) && (p->gv[5] & 0x40) &&
                    you->x < (uint16_t)(p->x + p->xsize) && p->x < (uint16_t)(you->x + you->xsize) &&
                    p->y < you->y && (uint16_t)(you->y - 0x11) < (uint16_t)(p->y + p->ysize))
                    riding_another++;
            if (!can_go_up(you) || riding_another) {
                /* squashed against the ceiling: crouch, or be crushed */
                if (you->animator != A_XLEVEL && you->animator != A_YOUDIE &&
                    you->animator != A_YOUBEGINIMPALE && you->image) {
                    if (you->ysize < 0x1A) {
                        if (!G.you_invincible)
                            you->animator = A_YOUCRUSH;
                    } else {
                        you->animator = A_YOUBEGINCROUCH;
                    }
                }
            } else {
                player_up(1, you);
            }
        }
    }
    if (o->gv[5] & MOVE_DOWN) {
        o->y++;
        if ((o->gv[5] & RIDER) && you && can_go_down(you))
            player_down(1, you);
    }
    if (o->gv[5] & MOVE_LEFT) {
        o->x--;
        if ((o->gv[5] & RIDER) && you && can_go_left(you))
            player_left(1, you);
    }
    if (o->gv[5] & MOVE_RIGHT) {
        o->x++;
        if ((o->gv[5] & RIDER) && you && can_go_right(you))
            player_right(1, you);
    }
    if (!you)
        return;
    const uint16_t feet = (uint16_t)(you->y + you->ysize);
    if (!(o->gv[5] & RIDER)) {
        /* Land on it: feet within the top 8 pixels, overlapping it sideways. */
        if (!(you->gv[5] & 4) && (uint16_t)(o->y - 1) < feet && feet < (uint16_t)(o->y + 8) &&
            (uint16_t)(o->x - 1) < (uint16_t)(you->x + you->xsize) &&
            you->x < (uint16_t)(o->x + o->xsize + 1) && !(o->gv[5] & NO_RIDE)) {
            you->surface_flags |= 2;
            you->down_offset = 0;
            you->y = (uint16_t)(o->y - you->ysize);
            o->gv[5] |= RIDER;
        }
    } else if (feet != o->y || (uint16_t)(you->x + you->xsize) < o->x ||
               (uint16_t)(o->x + o->xsize) < you->x || (o->gv[5] & NO_RIDE)) {
        /* Got off. */
        o->gv[5] &= 0xDF;
        for (const obj_t *p = G.list[LIST_MAIN]; p; p = p->next)
            if ((p->surface_flags & 0x10) && (p->gv[5] & RIDER))
                riding_another++;
        if (!riding_another) {
            you->surface_flags &= 0xFD;
            you->down_offset = 1;
            if (you->animator == A_YOUCROUCH) {
                G.game_flags &= 0xFFF7;
                you->animator = A_YOUBEGINFALL;
            }
        }
    }
}

/* RXPLAT.C:310 FLOATINGPLATFORM: moves back and forth GenericVar[4] pixels, vertically
 * (ySpeed 0x4270, "v" in the level) or horizontally (0x59e0, "h"); yDir is the direction.
 * From level 10 on the platforms instead go until a wall (priority 0x17 vertical, else
 * horizontal; flag 1 is the direction). When the player lands on one it dips (PULLYPLATDIP). */
int floating_platform(obj_t *o)
{
    o->gv[5] &= 0x60;
    if (carry(&o->ypart, o->yspeed)) {
        if (G.level < 10) {
            if (o->yspeed == 0x4270)
                o->gv[5] |= o->ydir == 0 ? MOVE_UP : MOVE_DOWN;
            else
                o->gv[5] |= o->ydir == 0 ? MOVE_LEFT : MOVE_RIGHT;
            if (++o->gv[3] == o->gv[4]) {
                if (obj_visible(o))
                    play_sample(0x4F, o);
                o->ydir = o->ydir ? 0 : 1;
                o->gv[3] = 0;
            }
            if ((o->gv[5] & RIDER) && o->gv[0] == 0) {
                o->gv[0]++;
                o->friction = 0;
                o->anim_inc = 0;
                o->old_animator = A_FLOATINGPLATFORM;
                o->animator = A_PULLYPLATDIP;
                play_sample(0x82, o);
            } else if (!(o->gv[5] & RIDER)) {
                o->gv[0] = 0;
            }
        } else {
            if (o->priority == 0x17) {
                if (!(o->flags & 1)) {
                    if (can_go_up(o))
                        o->gv[5] |= MOVE_UP;
                    else
                        o->gv[3] = 0x81;
                } else if (can_go_down(o)) {
                    o->gv[5] |= MOVE_DOWN;
                } else {
                    o->gv[3] = 0x81;
                }
            } else if (!(o->flags & 1)) {
                if (can_go_left(o))
                    o->gv[5] |= MOVE_LEFT;
                else
                    o->gv[3] = 0xA9;
            } else if (can_go_right(o)) {
                o->gv[5] |= MOVE_RIGHT;
            } else {
                o->gv[3] = 0xA9;
            }
            if (++o->gv[3] == (o->priority == 0x17 ? 0x82 : 0xAA)) {
                if (obj_visible(o))
                    play_sample(0x4F, o);
                o->flags ^= 1;
                o->gv[3] = 0;
            }
        }
    }
    maintain_platform(o);
    /* The goblin boss's platform (_Boss) is solid. */
    if (o == G.boss && !(o->gv[5] & RIDER) && G.you && obj_overlap(G.you, o))
        maintain_block(o);
    return 0;
}

/* RXPLAT.C:684 PULLYPLATDIP: a platform sinks 3 pixels and comes back up when the player
 * lands on it, then carries on with its old animator; a pulley platform starts its pulleys. */
int pully_plat_dip(obj_t *o)
{
    bool sound = false;
    o->gv[5] &= 0x60;
    if (++o->friction > 3) {
        o->friction = 0;
        o->gv[5] |= ++o->anim_inc < 4 ? MOVE_DOWN : MOVE_UP;
    }
    maintain_platform(o);
    if (o->anim_inc == 6) {
        o->animator = o->old_animator;
        if (o->animator == A_PULLYPLATMOVE) {
            for (obj_t *p = G.list[LIST_MAIN]; p; p = p->next) {
                if (p->priority != o->priority - 10)
                    continue;
                if (!sound) {
                    p->gv[0] = (int16_t)play_sample(0x48, o);
                    sound = true;
                }
                p->xspeed = 0x4B0;
                if (p->ypart == 0)
                    p->flags &= 0xFE;
                else
                    p->flags |= 1;
            }
        }
    }
    return 0;
}

/* RXPLAT.C:451 CRUMBLINGPLATFORM: waits for the player, then shakes. */
int crumbling_platform(obj_t *o)
{
    if (o->gv[5] & RIDER) {
        o->flags |= 1;
        o->yspeed = 0;
        o->frame_no = 0;
        o->frame_count = 0;
        o->frame_count_max = 0;
        o->animator = A_CRUMBLEPLATSHAKE;
        play_sample(0x6A, NULL);
    }
    maintain_platform(o);
    return 0;
}

/* RXPLAT.C:468 CRUMBLEPLATSHAKE: jiggles up and down (FrameNo, FrameCount and FrameCountMax
 * are counters here), dropping a bit of debris and showing the cracked image every other
 * jiggle, and falls after the fourth. */
int crumble_plat_shake(obj_t *o)
{
    o->gv[5] &= 0x60;
    if (++o->frame_no > 2) {
        o->frame_no = 0;
        if (++o->frame_count == 2) {
            const uint16_t x = (uint16_t)(o->x + frandom(o->xsize));
            obj_t *s = spawn_obj(0x0E, LIST_MAIN, G.img[IMG_PIECES + 1], x,
                                 (uint16_t)(o->y + o->ysize));
            s->gv[0] = 0;
            s->gv[3] = (int16_t)(frandom(0x59) + 1);
            if (frandom(2) == 0)
                s->flags &= 0xFE;
            else
                s->flags |= 1;
            s->xspeed = (int16_t)frandom(0xFA0);
            s->yspeed = (int16_t)frandom(0x64);
            o->image = G.img[IMG_CATWALK + 1];
            o->flags &= 0xFFFE;
            if (++o->frame_count_max == 4) {
                o->flags &= 0xBF;
                o->animator = A_CRUMBLEPLATFALL;
                play_sample(0x6B, NULL);
            }
        } else if (o->frame_count == 4) {
            o->flags |= 1;
            o->frame_count = 0;
        }
        o->gv[5] |= (o->flags & 1) ? MOVE_DOWN : MOVE_UP;
    }
    maintain_platform(o);
    return 0;
}

/* RXPLAT.C:504 CRUMBLEPLATFALL: falls (gravity 0x5a a tick) until it hits something, then
 * breaks into 8 pieces. */
int crumble_plat_fall(obj_t *o)
{
    o->gv[5] &= 0x60;
    if (o->yspeed < 32000)
        o->yspeed = (int16_t)(o->yspeed + 0x5A);
    if (carry(&o->ypart, o->yspeed)) {
        can_move(o);
        if (o->down_offset == 0 || o->down_offset > 10 || o->up_offset > 10) {
            play_sample(0x92, NULL);
            o->gv[5] |= NO_RIDE;
            for (int i = 0; i < 8; i++) {
                const uint16_t x = (uint16_t)(o->x + frandom(o->xsize));
                const uint16_t y = (uint16_t)(o->y + frandom(o->ysize));
                obj_t *s = spawn_obj(0x0E, LIST_MAIN, G.img[IMG_PIECES + 1], x, y);
                s->gv[0] = 0;
                s->gv[3] = 0x5A;
                if (frandom(2) == 0)
                    s->flags &= 0xFE;
                else
                    s->flags |= 1;
                s->xspeed = (int16_t)frandom(0x4000);
                s->yspeed = (int16_t)-frandom(0x2328);
            }
            maintain_platform(o);
            obj_kill(o);
            return 0;
        }
        o->gv[5] |= MOVE_DOWN;
    }
    maintain_platform(o);
    return 0;
}

/* RXPLAT.C:539 DOPULLEY: a pulley wheel turns (flag 1 the direction) while xSpeed lets it
 * run; a platform with its priority + 10 drives it. ySpeed picks the left or right wheel. */
int do_pulley(obj_t *o)
{
    if (!(o->flags & 1)) {
        if (--o->frame_no > 2)
            o->frame_no = 2;
    } else if (++o->frame_no > 2) {
        o->frame_no = 0;
    }
    int platforms = 0;
    for (const obj_t *p = G.list[LIST_MAIN]; p; p = p->next)
        if (p->priority == o->priority + 10)
            platforms++;
    if (!platforms)
        o->xspeed = 0;
    o->image = G.img[(o->yspeed == 0 ? IMG_LPULLEY : IMG_RPULLEY) + o->frame_no];
    return 0;
}

/* Start or stop the pulleys of a pulley platform: xSpeed, and the turning direction from the
 * pulley's "r" (reversed) flag in yPart. */
static void set_pulleys(const obj_t *o, int16_t xspeed, bool forward)
{
    for (obj_t *p = G.list[LIST_MAIN]; p; p = p->next) {
        if (p->priority != o->priority - 10)
            continue;
        p->xspeed = xspeed;
        if ((p->ypart == 0) == forward)
            p->flags |= 1;
        else
            p->flags &= 0xFE;
    }
}

/* RXPLAT.C:562 PULLYPLATWAIT: a pulley platform waits for the player. */
int pully_plat_wait(obj_t *o)
{
    maintain_platform(o);
    if (o->gv[5] & RIDER) {
        o->yspeed = 16000;
        o->friction = 0;
        o->anim_inc = 0;
        o->old_animator = A_PULLYPLATMOVE;
        o->animator = A_PULLYPLATDIP;
        play_sample(0x82, o);
    }
    return 0;
}

/* The direction bits for one step of a pulley platform: GenericVar[2] is the direction
 * (1 up, 5 down; 2 / 4 right and 8 / 6 left, rising or sinking a pixel every other step). */
static void pully_step(obj_t *o, bool back)
{
    const int d = o->gv[2];
    if (++o->gv[1] > 1) {
        o->gv[1] = 0;
        if (d == 8 || d == 2)
            o->gv[5] |= back ? MOVE_DOWN : MOVE_UP;
        else if (d == 6 || d == 4)
            o->gv[5] |= back ? MOVE_UP : MOVE_DOWN;
    }
    if (d == 2 || d == 4)
        o->gv[5] |= back ? MOVE_LEFT : MOVE_RIGHT;
    else if (d == 6 || d == 8)
        o->gv[5] |= back ? MOVE_RIGHT : MOVE_LEFT;
    if (d == 1)
        o->gv[5] |= back ? MOVE_DOWN : MOVE_UP;
    else if (d == 5)
        o->gv[5] |= back ? MOVE_UP : MOVE_DOWN;
}

/* RXPLAT.C:577 PULLYPLATMOVE: carries the player GenericVar[4] steps, then stops (and so do
 * its pulleys); when the player gets off it goes back. */
int pully_plat_move(obj_t *o)
{
    o->gv[5] &= 0x60;
    if (carry(&o->ypart, o->yspeed)) {
        if (o->gv[3] < o->gv[4]) {
            o->gv[3]++;
            pully_step(o, false);
        } else {
            for (obj_t *p = G.list[LIST_MAIN]; p; p = p->next) {
                if (p->priority == o->priority - 10) {
                    p->xspeed = 0;
                    if (p->gv[0] != 0) {
                        sound_link_kill(p->gv[0]);
                        p->gv[0] = 0;
                    }
                }
            }
        }
    }
    maintain_platform(o);
    if (!(o->gv[5] & RIDER)) {
        o->yspeed = 30000;
        o->animator = A_PULLYPLATRETURN;
        for (obj_t *p = G.list[LIST_MAIN]; p; p = p->next) {
            if (p->priority != o->priority - 10)
                continue;
            p->xspeed = 2000;
            if (p->ypart == 0)
                p->flags |= 1;
            else
                p->flags &= 0xFE;
            if (p->gv[0] != 0) {
                sound_link_kill(p->gv[0]);
                p->gv[0] = 0;
            }
        }
    }
    return 0;
}

/* RXPLAT.C:631 PULLYPLATRETURN: goes back to the start, and waits there; if the player lands
 * on it on the way, it dips and sets off again. */
int pully_plat_return(obj_t *o)
{
    o->gv[5] &= 0x60;
    if (carry(&o->ypart, o->yspeed)) {
        if (o->gv[3] < 1) {
            o->animator = A_PULLYPLATWAIT;
            set_pulleys(o, 0, true);
        } else {
            o->gv[3]--;
            pully_step(o, true);
        }
    }
    maintain_platform(o);
    if (o->gv[5] & RIDER) {
        o->yspeed = 16000;
        o->friction = 0;
        o->anim_inc = 0;
        o->old_animator = A_PULLYPLATMOVE;
        o->animator = A_PULLYPLATDIP;
        play_sample(0x82, o);
        set_pulleys(o, 0, false);
    }
    return 0;
}

/* Climb or drop with a slope after a push (CANMOVE's offsets, as the walkers use them). */
static void follow_slope(obj_t *you)
{
    can_move(you);
    if (you->up_offset > 10)
        player_up((uint8_t)(you->up_offset - 10), you);
    else if (you->down_offset > 10)
        player_down((uint8_t)(you->down_offset - 10), you);
}

/* RXPLAT.C:925 MAINTAINBLOCK: a solid object (a falling brick, a crusher, a boulder, the
 * goblin boss's platform, a spiked wall) overlapping the player. From below, it stops the
 * jump: the player drops a pixel if there's room, crouches if standing under a block that
 * isn't moving sideways, else is crushed. From the side, it pushes the player a pixel away
 * (following slopes); against a wall a block moving that way crushes. (The original checks
 * HitPoints[0], the female's, whichever character is playing.) */
void maintain_block(obj_t *o)
{
    obj_t *you = G.you;
    if (!you || you->animator == A_XLEVEL || !you->image || Inv.hit_points[0] == 0 ||
        you->animator == A_YOUBEGINIMPALE || you->animator == A_YOUDIE)
        return;
    const uint8_t lv = G.level;
    if ((uint16_t)(o->y + (o->ysize - 5)) < you->y && (uint16_t)(o->x + 4) < (uint16_t)(you->x + 0x0E) &&
        you->x < (uint16_t)(o->x + (o->xsize - 4))) {
        /* the player is under it */
        if (you->yspeed < 0)
            you->yspeed = 0;
        can_move(you);
        if (you->down_offset == 1) {
            player_down(1, you);
            if (!(you->gv[5] & 4))
                return;
            if (G.game_flags & 8)
                generic_begin_fall(you);
            else
                you_begin_fall(you);
            return;
        }
        if (you->ysize == 0x29 && ((you->gv[5] & 1) || (you->surface_flags & 2)) &&
            ((!(o->gv[5] & MOVE_RIGHT) && !(o->gv[5] & MOVE_LEFT)) || lv == 0x10)) {
            you->animator = A_YOUBEGINCROUCH;
            return;
        }
        if (you->animator == A_NULLANIMATOR)
            return;
    } else if ((uint16_t)(o->x + (o->xsize - 5)) < you->x) {
        /* the player is to its right */
        if (you->xspeed < 0)
            you->xspeed = 0;
        const bool room = lv == 0x0F ? (uint16_t)(you->x + 0x0E) < (uint16_t)(G.org_x + 0x130)
                                     : (uint16_t)(you->x + 0x0E) < (uint16_t)(G.lv->cols * 16 - 1);
        if (can_go_right(you) && room &&
            ((lv != 0x10 && lv != 0x13 && lv != 0x19) || you->x <= (uint16_t)(G.org_x + 300))) {
            player_right(1, you);
            follow_slope(you);
            return;
        }
        if (!(o->gv[5] & MOVE_RIGHT) && lv != 0x10 && lv != 0x19)
            return;
        if (you->animator == A_NULLANIMATOR)
            return;
    } else {
        /* the player is to its left */
        if ((uint16_t)(o->x + 5) <= (uint16_t)(you->x + 0x0E))
            return;
        if (you->xspeed > 0)
            you->xspeed = 0;
        const bool room = lv == 0x0F ? (uint16_t)(G.org_x + 0x12) < you->x : you->x > 0x18;
        if (can_go_left(you) && room &&
            ((lv != 0x10 && lv != 0x19) || (uint16_t)(G.org_x + 6) <= you->x)) {
            player_left(1, you);
            follow_slope(you);
            return;
        }
        if (!(o->gv[5] & MOVE_LEFT) && lv != 0x10 && lv != 0x19)
            return;
        if (you->animator == A_NULLANIMATOR)
            return;
    }
    you->animator = A_YOUCRUSH;
}

/* What the solid platforms do after moving: carry a rider, push the player otherwise. */
static void platform_and_block(obj_t *o)
{
    maintain_platform(o);
    if (G.you && obj_overlap(G.you, o) && !(o->gv[5] & RIDER))
        maintain_block(o);
}

/* RXPLAT.C:718 DOSQUAREPLAT: a cave block ("s<n>", type 0x2e) going round a square, 64
 * pixels a side, a pixel every 16 ticks: down, right, up, left (Flags bit 0, info "s<n>r":
 * right, down, left, up). GenericVar[1] is the side it starts on (1-4), GenericVar[3] the
 * pixel count. */
int do_square_plat(obj_t *o)
{
    o->gv[5] &= 0x60;
    if (carry(&o->ypart, 0x1000)) {
        if (++o->gv[3] == 0x40)
            o->gv[3] = 0;
        const bool corner = o->gv[3] == 0;
        if (!(o->flags & 1)) {
            switch (o->gv[1]) {
            case 1: o->gv[5] |= MOVE_DOWN; if (corner) o->gv[1] = 3; break;
            case 2: o->gv[5] |= MOVE_LEFT; if (corner) o->gv[1]--; break;
            case 3: o->gv[5] |= MOVE_RIGHT; if (corner) o->gv[1]++; break;
            case 4: o->gv[5] |= MOVE_UP; if (corner) o->gv[1] = 2; break;
            default: break;
            }
        } else {
            switch (o->gv[1]) {
            case 1: o->gv[5] |= MOVE_RIGHT; if (corner) o->gv[1]++; break;
            case 2: o->gv[5] |= MOVE_DOWN; if (corner) o->gv[1] = 4; break;
            case 3: o->gv[5] |= MOVE_UP; if (corner) o->gv[1] = 1; break;
            case 4: o->gv[5] |= MOVE_LEFT; if (corner) o->gv[1]--; break;
            default: break;
            }
        }
    }
    platform_and_block(o);
    return 0;
}

/* RXPLAT.C:766 DOFALLINGPLAT: a falling cave brick (type 0x46, or a cave block that isn't
 * "s..": a crusher). Flags bit 0: falling (gravity 0xb4 a tick, at most 32000), else rising
 * at ySpeed. A brick (under 18 pixels high) waits until the player comes under it (BRIKSHAKE
 * shakes it first), falls, bounces once (GenericVar[1] 1: rising at a quarter of the speed
 * until it's spent) and on the second landing becomes part of the floor: its two cells get
 * the floor and wall bits (GenericVar[1] 99). A crusher starts falling, and 500 ticks after
 * each bounce rises back (ySpeed 0x1000) to a pixel below where it started (GenericVar[0])
 * and falls again. */
int do_falling_plat(obj_t *o)
{
    obj_t *you = G.you;
    o->gv[5] &= 0x60;
    if (o->ysize < 0x12 && !(o->flags & 1) && o->gv[1] == 0) {
        if (you && o->y < you->y && you->y < (uint16_t)(o->y + 0x40) &&
            (uint16_t)(o->x - 0x28) < you->x && you->x < (uint16_t)(o->x + 0x56)) {
            o->animator = A_BRIKSHAKE;
            o->counter = 0;
            if (o->gv[2] != 0 && !sound_link_active(o->gv[2]))
                o->gv[2] = 0;
            if (o->gv[2] == 0)
                o->gv[2] = (int16_t)play_sample(0x53, o);
            return 0;
        }
    } else {
        if (!(o->flags & 1)) {
            if (o->gv[1] == 1) {
                const int16_t old = o->yspeed;
                o->yspeed = (int16_t)(o->yspeed - 0xB4);
                if (old < 0xB4) {
                    o->flags |= 1;
                    o->yspeed = 0;
                }
            }
        } else {
            o->yspeed = (int16_t)(o->yspeed + 0xB4);
            if (o->yspeed > 32000)
                o->yspeed = 32000;
        }
        if (carry(&o->ypart, iabs16(o->yspeed))) {
            if (!(o->flags & 1)) {
                o->gv[5] |= MOVE_UP;
                if ((uint16_t)(o->gv[0] + 1) == o->y) {
                    o->yspeed = 0;
                    o->flags |= 1;
                }
            } else if (can_go_down(o)) {
                o->gv[5] |= MOVE_DOWN;
            } else if (o->gv[1] == 0) {
                o->gv[1]++;
                o->counter = 0;
                o->yspeed = (int16_t)(o->yspeed >> 2);
                o->flags &= 0xFFFE;
                if (obj_visible(o))
                    play_sample(0x73, o);
            } else if (o->ysize < 0x12 && o->gv[1] != 99) {
                const unsigned r = o->y >> 4, c0 = o->x >> 4,
                               c1 = (unsigned)(uint16_t)(o->x + 0x10) >> 4;
                map_set_cell(r, c0, map_cell(r, c0) | 0x800 | 0x200);
                map_set_cell(r, c1, map_cell(r, c1) | 0x800 | 0x400);
                o->gv[1] = 99;
            } else if (o->counter > 500 && o->ysize > 0x11) {
                o->gv[1] = 0;
                o->yspeed = 0x1000;
                o->flags &= 0xFFFE;
            }
        }
    }
    platform_and_block(o);
    return 0;
}

/* RXPLAT.C:846 BRIKSHAKE: the brick shakes (down at tick 18, up at 36). Then, if the player
 * is right under it, it falls; if the player has gone, it waits again (DOFALLINGPLAT); in
 * between it goes on shaking. */
int brik_shake(obj_t *o)
{
    obj_t *you = G.you;
    o->gv[5] &= 0x60;
    if (o->counter == 0x12) {
        o->gv[5] |= MOVE_DOWN;
    } else if (o->counter == 0x24 && you) {
        o->counter = 0;
        o->gv[5] |= MOVE_UP;
        bool shake_on = false;
        if (o->y <= you->y && you->y <= (uint16_t)(o->y + 0x4B) &&
            (uint16_t)(o->x - 0x36) <= you->x && you->x <= (uint16_t)(o->x + 0x48)) {
            if (you->y <= o->y || (uint16_t)(o->y + 0x4B) <= you->y ||
                you->x <= (uint16_t)(o->x - 0x26) || (uint16_t)(o->x + 0x38) <= you->x)
                shake_on = true; /* near, but not right under it */
            else
                o->flags |= 1;   /* under it: fall */
        }
        if (!shake_on) {
            o->animator = A_DOFALLINGPLAT;
            o->counter = 0;
            sound_link_kill(o->gv[2]);
        }
    }
    platform_and_block(o);
    return 0;
}

/* RXPLAT.C:384 DOCONTAINERBOMB: a statue with info "b". Touched by the player (who gets
 * hurt), by a flame puff or a spore (which it sets off: Flags 2, 6 damage points), or hit by
 * the player's weapon, it bursts: the broken statue, a BOMBEXPLODER (9 puffs) and three
 * flaming blobs (type 0x48, spores in all but name) thrown up left, right and straight up. */
int do_container_bomb(obj_t *o)
{
    if (o->image != G.img[IMG_STATUE]) {
        o->animator = A_NULLANIMATOR;
        return 0;
    }
    uint8_t touched = 0;
    obj_t *nx;
    for (obj_t *p = G.list[LIST_MAIN]; p; p = nx) {
        nx = p->next;
        if ((p == G.you || p->animator == A_DOTORCHPUFF || p->animator == A_DOSPORE) &&
            obj_collide(o, p)) {
            touched++;
            if (p == G.you) {
                hit_player(o);
            } else {
                p->flags |= OF_HIT;
                p->gv[3] += 6;
            }
        }
    }
    if (!touched && !(o->flags & OF_HIT))
        return 0;
    o->xdir = (uint8_t)frandom(2);
    obj_image(o, G.img[IMG_STATUE + 1]);
    o->flags &= 0xEF;
    o->flags &= 0xFD;
    G.statue_found++;
    obj_t *e = spawn_obj(0x12, LIST_FORE, NULL, (uint16_t)(o->x + 10), o->y);
    e->flags |= OF_RATE;
    e->animator = A_BOMBEXPLODER;
    e->xspeed = 9000;
    e->counter = 0;
    e->gv[1] = 0;
    play_sample(0x62, NULL);
    static const struct { uint8_t dx; int16_t xspeed; uint8_t xdir; } blob[3] = {
        {0x10, 12000, 1}, {10, 12000, 0}, {0x0D, 0, 0}};
    for (int i = 0; i < 3; i++) {
        obj_t *b = spawn_obj(0x48, LIST_MAIN, G.img[IMG_GARGFIRE], (uint16_t)(o->x + blob[i].dx),
                             o->y);
        b->yspeed = -8000;
        b->xspeed = blob[i].xspeed;
        if (i < 2)
            b->xdir = blob[i].xdir;
        b->flags &= 0xFFEF;
    }
    return 0;
}

/* RXPLAT.C:876 SWAMPRAFT: the raft (type 0x45) and its front piece (type 0x5b, RaftID[1],
 * drawn over the player), which it drags along. Once the player boards it (GenericVar[1])
 * it drifts right a pixel every 16 ticks, splashing, and stops at the landings x = 0x344
 * and 0xb84. While the player is recovering from a hit off the raft, it drifts towards the
 * player instead (within x 0xef-0x95f or past 0x99b going left, short of 0x33f or between
 * 0x385 and 0xb7e going right). */
int swamp_raft(obj_t *o)
{
    obj_t *you = G.you;
    if (o->gv[2] == 0) {
        o->gv[2] = 1;
        o->child = spawn_obj(0x5B, LIST_MAIN, G.img[IMG_RAFT + 1], o->x, o->y);
    }
    o->gv[5] &= 0x60;
    if ((o->gv[5] & RIDER) && o->gv[1] == 0)
        o->gv[1] = 1;
    const bool landing = o->x == 0x344 || o->x == 0xB84;
    if (o->gv[1] != 0 && carry(&o->xpart, 0x1000)) {
        if (landing) {
            if (o->gv[3] != 0)
                sound_link_kill(o->gv[3]);
        } else {
            if (!sound_link_active(o->gv[3]))
                o->gv[3] = 0;
            if (o->gv[3] == 0)
                o->gv[3] = (int16_t)play_sample(0x42, o);
            o->gv[5] |= MOVE_RIGHT;
        }
    }
    if (you && (G.game_flags & 8) && !(o->gv[5] & RIDER) && !landing) {
        bool left = false;
        if (you->x < o->x)
            left = o->x < 0x960 ? o->x > 0xEE : o->x >= 0x99C;
        if (left) {
            o->gv[5] |= MOVE_LEFT;
        } else if ((uint16_t)(o->x + o->xsize) < (uint16_t)(you->x + 0x0E) && can_go_right(o) &&
                   room_right(o) && (o->x < 0x385 ? o->x < 0x33F : o->x < 0xB7F)) {
            o->gv[5] |= MOVE_RIGHT;
        }
    }
    maintain_platform(o);
    if (o->child) {
        o->child->x = o->x;
        o->child->y = o->y;
    }
    return 0;
}

/* RXPLAT.C:242 FROGPLAT: the giant frog of the caves, a platform. Hidden for 200 ticks, it
 * surfaces (frames 0-2, and can't be ridden until tick 220), croaks at 450 with a splash,
 * then leaps straight up at 20000 for 90 pixels, falls back (gravity, at most 32000) to where
 * it started (GenericVar[0]), splashes and starts over. */
int frog_plat(obj_t *o)
{
    o->gv[5] &= 0x60;
    if (o->counter < 0xDC)
        o->gv[5] |= NO_RIDE;
    if (o->counter < 200) {
        o->image = NULL;
    } else if (o->counter == 200) {
        o->xdir = (uint8_t)frandom(2);
        obj_image(o, G.img[IMG_CAVEFROG]);
    } else if (o->counter == 0xDC) {
        obj_image(o, G.img[IMG_CAVEFROG + 1]);
    } else if (o->counter == 0xF0) {
        obj_image(o, G.img[IMG_CAVEFROG + 2]);
    } else if (o->counter == 0x1C2) {
        obj_image(o, G.img[IMG_CAVEFROG + 3]);
        play_sample(frandom(2) == 0 ? 0x6E : 0x6F, NULL);
        obj_t *s = spawn_obj(0x24, LIST_FORE, G.img[IMG_CAVEFROG + 3], o->x, o->y);
        s->image = NULL;
        s->gv[0] = 0;
        s->gv[1] = 8;
        s->gv[2] = 0;
        s->animator = A_SPLASHSPAWNER;
    } else if (o->counter > 0x1C2) {
        if (!(o->flags & 1)) {
            if (o->gv[4] > 0x5A) {
                play_sample(0x76, o);
                obj_image(o, G.img[IMG_CAVEFROG + 4]);
                o->flags |= 1;
            }
        } else {
            o->yspeed = (int16_t)(o->yspeed + 0xB4);
            if (o->yspeed > 32000)
                o->yspeed = 32000;
        }
        if (carry(&o->ypart, iabs16(o->yspeed))) {
            if (o->yspeed < 0) {
                o->gv[4]++;
                o->gv[5] |= MOVE_UP;
            } else {
                o->gv[5] |= MOVE_DOWN;
                if (o->y == (uint16_t)o->gv[0]) {
                    play_sample(frandom(2) == 0 ? 0x70 : 0x71, NULL);
                    obj_t *s = spawn_obj(0x24, LIST_FORE, G.img[IMG_CAVEFROG + 4],
                                         (uint16_t)(o->x + 10), (uint16_t)(o->y + 5));
                    s->image = NULL;
                    s->gv[0] = 0;
                    s->gv[1] = 10;
                    s->gv[2] = 0;
                    s->animator = A_SPLASHSPAWNER;
                    o->counter = 0;
                    o->gv[4] = 0;
                    o->yspeed = -20000;
                    o->flags &= 0xFFFE;
                }
            }
        }
    }
    maintain_platform(o);
    return 0;
}

/* RXPLAT.C:1354 SPINNINGPLATFORM: the goblin boss's platform (type 0x4f, BossPlatID, priority
 * 0x46): solid and rideable, it shuttles sideways a pixel every 7 ticks (rate 9000) between the
 * room's wall and the view's edge (Flags bit 0: right). After passing its wall-side end three
 * times (x 0x40, or x + width 0x1cd) it stops (NOSPINPLAT). When the boss dies it falls
 * (SPINPLATDIE). */
int spinning_platform(obj_t *o)
{
    const obj_t *b = G.boss;
    o->gv[5] &= 0x60;
    if (b->animator == A_BOSSHEADDIE) {
        o->yspeed = 0;
        o->animator = A_SPINPLATDIE;
        return 0;
    }
    if (carry(&o->xpart, 9000)) {
        const bool right_side = b->gv[0] != 0;
        if (!(o->flags & 1)) {
            if ((!right_side || o->x <= G.org_x) && (right_side || !can_go_left(o))) {
                o->flags |= 1;
            } else {
                if (!right_side && o->x == 0x40 && ++o->gv[0] == 3) {
                    o->animator = A_NOSPINPLAT;
                    return 0;
                }
                o->gv[5] |= 4;
            }
        } else if ((!right_side || !can_go_right(o)) &&
                   (right_side || (uint16_t)(G.org_x + 0x136) <= (uint16_t)(o->x + o->xsize))) {
            o->flags &= 0xFFFE;
        } else {
            if (right_side && (uint16_t)(o->x + o->xsize) == 0x1CD && ++o->gv[0] == 3) {
                o->animator = A_NOSPINPLAT;
                return 0;
            }
            o->gv[5] |= 8;
        }
    }
    maintain_platform(o);
    if (!(o->gv[5] & 0x20) && obj_overlap(G.you, o))
        maintain_block(o);
    return 0;
}

/* RXPLAT.C:1405 NOSPINPLAT: a boss platform at rest. The top one (y 0xb4) sends the floating
 * head home (BOSSHEADRETURN) when it passes x 400 or 0x54. */
int no_spin_plat(obj_t *o)
{
    obj_t *b = G.boss;
    o->gv[5] &= 0x60;
    if (b->animator == A_BOSSHEADDIE) {
        o->yspeed = 0;
        o->animator = A_SPINPLATDIE;
        return 0;
    }
    if (o->y == 0xB4 && b->animator == A_BOSSHEADFLOAT && (b->x == 400 || b->x == 0x54)) {
        b->counter = 0;
        b->animator = A_BOSSHEADRETURN;
        b->xdir = b->gv[0] == 0;
    }
    maintain_platform(o);
    if (!(o->gv[5] & 0x20) && obj_overlap(G.you, o))
        maintain_block(o);
    return 0;
}

/* RXPLAT.C:1430 SPINPLATDIE: the platform falls (gravity 0x5a, to 30000) and shatters on the
 * floor into seven rocks (type 0, RockPiecID[0..1]), dropping a rider. */
int spin_plat_die(obj_t *o)
{
    o->gv[5] &= 0x60;
    o->yspeed = (int16_t)(o->yspeed + 0x5A);
    if (o->yspeed > 30000)
        o->yspeed = 30000;
    if (carry(&o->ypart, o->yspeed)) {
        if (!can_go_down(o)) {
            play_sample(0xA3, NULL);
            for (int i = 0; i < 7; i++) {
                const sprite_t *image = G.img[IMG_ROCKPIEC + frandom(2)];
                const uint16_t x = (uint16_t)(o->x + frandom((uint16_t)(o->xsize - 10)));
                const uint16_t y = (uint16_t)(o->y + frandom((uint16_t)(o->ysize - 10)));
                obj_t *r = spawn_obj(0x00, LIST_MAIN, image, x, y);
                r->yspeed = (int16_t)-(o->yspeed >> 1);
                r->xdir = (uint8_t)frandom(2);
                if (frandom(2) != 0)
                    r->flags &= 0xFFFE;
                else
                    r->flags |= 1;
            }
            if (o->gv[5] & 0x20)
                G.you->surface_flags &= 0xFD;
            obj_kill(o);
            return 0;
        }
        o->y++;
    }
    maintain_platform(o);
    if (!(o->gv[5] & 0x20) && obj_overlap(G.you, o))
        maintain_block(o);
    return 0;
}

/* A boulder landing hard: the view shakes, with a crash. */
static void boulder_crash(obj_t *o)
{
    spawn_shake();
    play_sample(0x73, o);
}

/* RXPLAT.C:1182 ROLLINGBOULDER: a boulder (type 0x50, priority 0x97, a solid platform; also run
 * by GENERICUP / GENERICFALL in the air). GenericVar[0]: 0 dropped, 1 bounced once, 2 rolling;
 * GenericVar[2]: 0 on the ground, 1 falling, 2 rising (GenericVar[4] ticks at its ySpeed, then
 * slowing). It moves through MAINTAINPLATFORM (GenericVar[5]'s bits), turning backwards through
 * DeadRck1ID[7..0] (every 0x2d ticks after the bounce, else 0x41). The first landing shakes the
 * view and bounces it (ySpeed -10000); from the second it rolls in its direction at 10000 (with a
 * rumble), following slopes and falling off ledges, until a wall or the map's edge stops it on
 * the ground (STILLBOULDER). A player riding it is carried along at 13000. On level 25 it goes
 * once off the screen (except while falling). */
int rolling_boulder(obj_t *o)
{
    obj_t *you = G.you;
    o->gv[5] &= 0x60;
    if (G.level == 0x19 && o->gv[2] != 1 && !obj_visible(o)) {
        obj_kill(o);
        return 0;
    }
    if (o->counter > (o->gv[0] == 1 ? 0x2D : 0x41)) {
        o->counter = 0;
        o->frame_no = o->frame_no == 0 ? 7 : (uint8_t)(o->frame_no - 1);
        o->image = G.img[IMG_DEADRCK1 + o->frame_no];
    }
    if (o->gv[2] == 1) {
        o->yspeed = (int16_t)(o->yspeed + 0xB4);
        if (o->yspeed > 32000)
            o->yspeed = 32000;
    } else if (o->gv[2] == 2) {
        if (o->gv[4] == 0) {
            o->yspeed = (int16_t)(o->yspeed + 0xB4);
            if (o->yspeed > 0)
                o->gv[2] = 1;
        } else {
            o->gv[4]--;
        }
    }
    if (o->gv[2] != 0 && carry(&o->ypart, iabs16(o->yspeed))) {
        if (o->yspeed < 0) {
            o->gv[5] |= MOVE_UP;
        } else if (o->yspeed > 0) {
            can_move(o);
            if (o->down_offset == 1) {
                o->gv[5] |= MOVE_DOWN;
            } else {
                boulder_crash(o);
                o->gv[2] = 0;
                o->yspeed = 0;
            }
        }
    }
    bool stepped = false; /* a step of the roll was due (blocked or not) */
    if (o->gv[0] == 0) {
        if (o->gv[2] == 0) {
            o->gv[0]++;
            o->gv[2] = 2;
            o->gv[4] = 0x5A;
            o->yspeed = -10000;
        }
    } else if (carry(&o->xpart, 10000)) {
        bool way;
        if (o->xdir == 0) {
            way = can_go_left(o) && (G.level == 0x0F ? (uint16_t)(G.org_x + 0x12) < o->x
                                                      : o->x >= 0x15);
            if (way)
                o->gv[5] |= MOVE_LEFT;
        } else {
            way = can_go_right(o) &&
                  (uint16_t)(o->x + o->xsize) < (G.level == 0x0F ? (uint16_t)(G.org_x + 0x130)
                                                                 : (uint16_t)(G.lv->cols * 16 - 1));
            if (way)
                o->gv[5] |= MOVE_RIGHT;
        }
        if (!way && o->down_offset != 1) {
            o->update = 0x32;
            o->animator = A_STILLBOULDER;
            sound_link_kill(o->gv[3]);
        }
        stepped = true;
    }
    if (stepped && o->gv[0] == 1) {
        if (o->gv[2] == 0)
            o->gv[0]++;
    } else if (stepped && o->gv[0] == 2) {
        can_move(o);
        if (!sound_link_active(o->gv[3]))
            o->gv[3] = 0;
        if (o->gv[3] == 0 && o->animator != A_STILLBOULDER && o->down_offset != 1)
            o->gv[3] = (int16_t)play_sample(0x36, o);
        if (o->down_offset > 10) {
            o->gv[5] |= MOVE_DOWN;
        } else if (o->down_offset == 1) {
            sound_link_kill(o->gv[3]);
            o->gv[2] = 1;
        }
    }
    maintain_platform(o);
    if (!(o->gv[5] & RIDER)) {
        if (you && obj_overlap(you, o))
            maintain_block(o);
    } else if (you && carry((uint16_t *)&o->gv[1], 13000)) {
        you->xspeed = o->xdir == 0 ? -13000 : 13000;
        if (*you_hp() == 0) {
            you->yspeed = -14000;
            you->gv[4] = 0x5A;
            generic_begin_jump(you);
        } else {
            you->animator = A_YOUBEGINJUMP;
            you->gv[4] = 0x3C;
            you->yspeed = -2000;
        }
    }
    return 0;
}

/* RXPLAT.C:1327 STILLBOULDER: a boulder at rest, still solid; one stopping at x 0x8e0 (the end
 * of its run on level 17 / 19) lands with a crash. */
int still_boulder(obj_t *o)
{
    o->gv[5] &= 0x60;
    if (o->x == 0x8E0 && o->gv[0] != 3) {
        boulder_crash(o);
        o->gv[0] = 3;
    }
    maintain_platform(o);
    if (!(o->gv[5] & RIDER) && G.you && obj_overlap(G.you, o))
        maintain_block(o);
    return 0;
}

/* The player follows the slopes under them (CANMOVE's offsets, as the walkers do). */
static void you_follow_slope(obj_t *you)
{
    can_move(you);
    if (you->up_offset > 10)
        player_up((uint8_t)(you->up_offset - 10), you);
    if (you->down_offset > 10)
        player_down((uint8_t)(you->down_offset - 10), you);
}

static bool you_room_right(const obj_t *you)
{
    return G.level == 0x0F ? (uint16_t)(you->x + 0x0E) < (uint16_t)(G.org_x + 0x130)
                           : (uint16_t)(you->x + 0x0E) < (uint16_t)(G.lv->cols * 16 - 1);
}

static bool you_room_left(const obj_t *you)
{
    return G.level == 0x0F ? (uint16_t)(G.org_x + 0x12) < you->x : you->x >= 0x19;
}

/* The original loops until the player is back in the view; a guard keeps a player who can't
 * move from hanging the port (the original would hang there). */
enum { PUSH_GUARD = 400 };

/* RXPLAT.C:11 AUTOSCROLLER (type 0x29; INITLEVEL makes one on levels 1, 3 and 22 above
 * difficulty 2): the view scrolls right by itself, a pixel at each carry of 0x1000 (about 36
 * a second), to the map's end. A player left behind is pushed along (off a vine, following the
 * slopes) and crushed against a wall (YOUCRUSH); one ahead of the view is pulled back. On
 * level 14 it starts 700 ticks in, once the player has landed. Nothing moves while the player
 * is dead or flying (_LightningFlag). */
int autoscroller(obj_t *o)
{
    obj_t *you = G.you;
    if (!you)
        return 0;
    if (G.level == 0x0E && o->gv[0] == 0 && o->counter > 700 && (you->gv[5] & 1))
        o->gv[0] = 1;
    if (G.lightning_flag || *you_hp() == 0 || (G.level == 0x0E && o->gv[0] == 0))
        return 0;
    for (int n = 0; n < PUSH_GUARD && you->x < (uint16_t)(G.org_x + 5) &&
                    you->animator != A_NULLANIMATOR && you->animator != A_YOUCRUSH; n++) {
        if (G.game_flags & 1) {
            G.game_flags &= 0xFFFE;
            you->max_frames = 5;
            you->frame_count_max = 0x32;
            you->animator = A_YOUBEGINFALL;
        }
        you_follow_slope(you);
        if (can_go_right(you) && you_room_right(you))
            player_right(1, you);
        else if (you->animator != A_NULLANIMATOR && you->animator != A_YOUCRUSH)
            you->animator = A_YOUCRUSH;
    }
    for (int n = 0; n < PUSH_GUARD && (uint16_t)(G.org_x + 0x12F) < you->x; n++) {
        you_follow_slope(you);
        if (can_go_left(you) && you_room_left(you))
            player_left(1, you);
    }
    if (carry(&o->xpart, 0x1000) && G.org_x < (uint16_t)(G.lv->cols * 16 - 0x141))
        scroll_right(1);
    return 0;
}

/* Objects that CAVESCROLLER carries along with the view. */
static bool cave_carries(const obj_t *p, const obj_t *scroller)
{
    const uint16_t a = p->animator;
    if (a == A_DOTORCH || a == A_DOFIREBALL || a == A_DOWHIP || a == A_DOEXPLOSION ||
        a == A_DOGARGOYLEFIRE)
        return true;
    if (p == G.you && scroller->gv[3] == 0)
        return true;
    if (p->priority == 0x65 && a != A_DOCAVEFALLSPIKES)
        return true;
    return (p->gv[5] & 1) &&
           !(map_cell((unsigned)(p->y + 0x12) >> 4, (unsigned)(p->x + 8) >> 4) & 0x800) &&
           (a == A_DOBLOOD || a == A_DOCOLLECTABLE);
}

/* RXPLAT.C:64 CAVESCROLLER (type 0x2b, level 13): the view rises and sinks over the cave by
 * itself, and its bottom edge (view y 0xc0) is a floor: the player and the loose things on it
 * (prizes, spikes, debris...) ride it (GenericVar[3] 0 while the player isn't on the cave's
 * own ground). Flags bit 0 set means rising: it speeds up by 0x50 a tick to 15000 until the
 * view's top reaches 0; then it sinks (GenericVar[2] counts the trips: the first slows down by
 * 0x5a and turns back up, the next sink at 0x1000 after a 200-tick pause) to view y 100, and
 * after a 0x96-tick pause rises again. While rising, a player under a ceiling is slid out from
 * under it, made to crouch, or crushed. Past x 0x8fc at the top it stops (GenericVar[1]). */
int cave_scroller(obj_t *o)
{
    obj_t *you = G.you;
    G.center_off = -2;
    G.window_xsize = 0x2A;
    G.window_ysize = 200;
    set_scroll_bounds(200, 0x2A);
    if (!you)
        return 0;
    const uint16_t floor = (uint16_t)(G.org_y + 0xC0);
    for (obj_t *p = G.list[LIST_MAIN]; p; p = p->next) {
        const int16_t pr = p->priority;
        if ((p == you || pr == 0xE5 || pr == 0x29 || pr == 0xDC || pr == 0xF0 || pr == 0xB9 ||
             pr == 0x65) &&
            floor < (uint16_t)(p->y + p->ysize)) {
            if (p->image)
                p->y = (uint16_t)(floor - p->ysize);
            p->down_offset = 0;
            p->gv[5] |= 1;
            if (p == you) {
                you->surface_flags |= 2;
                o->gv[3] = 0;
            }
        }
    }
    if (you->y < (uint16_t)(G.org_y + 3))
        player_down(1, you);
    if (you->animator == A_YOUJUMP || you->animator == A_GENERICJUMP) {
        o->gv[3] = 0;
        you->surface_flags &= 0xFD;
    }
    if (o->gv[1] != 0)
        return 0;
    if (o->gv[3] == 0 && !can_go_down(you))
        o->gv[3] = 1;
    if (o->gv[0] != 0) {
        if (--o->gv[0] == 0 && !(o->flags & 1))
            play_sample(0x97, o);
        return 0;
    }
    if ((o->flags & 1) && o->yspeed < 15000) {
        o->yspeed = (int16_t)(o->yspeed + 0x50);
    } else if (!(o->flags & 1) && o->gv[2] == 1) {
        const int16_t old = o->yspeed;
        o->yspeed = (int16_t)(o->yspeed - 0x5A);
        if (old < 0x5A) {
            o->flags |= 1;
            o->yspeed = 0;
        }
    }
    const uint16_t old_part = o->ypart;
    o->ypart = (uint16_t)(o->ypart + o->yspeed);
    if (o->ypart > old_part)
        return 0; /* no step this tick (a step is a carry, or a speed of 0) */
    if (!(o->flags & 1)) {
        scroll_down(1);
        for (obj_t *p = G.list[LIST_MAIN]; p; p = p->next)
            if (cave_carries(p, o))
                p->y++;
        if (G.org_y == 100) {
            o->flags |= 1;
            o->gv[2] = 0;
            o->yspeed = 0;
            o->gv[0] = 0x96;
        }
        return 0;
    }
    if (!can_go_up(you)) {
        if (!can_go_down(you) || (you->surface_flags & 2)) {
            const unsigned row = (unsigned)(you->y - 4) >> 4;
            if (!(map_cell(row, (unsigned)(you->x + 6) >> 4) & 0x1000) &&
                (map_cell(row, (unsigned)(you->x + 0x0E) >> 4) & 0x1000)) {
                for (int n = 0; n < PUSH_GUARD && !can_go_up(you); n++)
                    player_left(1, you);
            } else if (!(map_cell(row, (unsigned)(you->x + 8) >> 4) & 0x1000) &&
                       (map_cell(row, (unsigned)you->x >> 4) & 0x1000)) {
                for (int n = 0; n < PUSH_GUARD && !can_go_up(you); n++)
                    player_right(1, you);
            } else if (you->ysize < 0x1A || !(you->surface_flags & 2) || Inv.hit_points[0] == 0) {
                you->animator = A_YOUCRUSH;
            } else {
                you_begin_crouch(you);
            }
        } else {
            player_down(1, you);
        }
    }
    scroll_up(1);
    if (o->gv[4] != 0 && !sound_link_active(o->gv[4]))
        o->gv[4] = 0;
    if (o->gv[4] == 0)
        o->gv[4] = (int16_t)play_sample(0x95, o);
    for (obj_t *p = G.list[LIST_MAIN]; p; p = p->next)
        if (cave_carries(p, o))
            p->y--;
    if (G.org_y == 0) {
        o->flags &= 0xFFFE;
        sound_link_kill(o->gv[4]);
        play_sample(0xA2, o);
        if (++o->gv[2] == 1) {
            o->yspeed = 7000;
        } else {
            o->gv[0] = 200;
            o->yspeed = 0x1000;
            if (0x8FC < you->x)
                o->gv[1]++;
        }
    }
    return 0;
}

/* Where BOULDERSPAWNER drops its four boulders (DS:048a for level 17, DS:04a2 for level 19):
 * the view's x that sets each off, the boulder's x, and its direction (xDir). */
static const uint16_t boulders17[4][3] = {
    {0x5C2, 0x5C8, 1}, {0x941, 0xA4A, 0}, {0xB0C, 0xB20, 1}, {0xC53, 0xDB6, 0}};
static const uint16_t boulders19[4][3] = {
    {0xD8F, 0xEA1, 0}, {0xCE4, 0xCB1, 1}, {0x95D, 0xA4A, 0}, {0x5E2, 0x5E2, 1}};

/* RXPLAT.C:1114 BOULDERSPAWNER (type 0x11, levels 17 and 19): drops each boulder (type 0x50 at
 * y 0x14, ROLLINGBOULDER) once the view passes its place (rightwards on level 17, leftwards on
 * 19). Level 19 is played right to left: the view scrolls left by itself (a pixel at each carry
 * of 0x1000), pushing the player along like AUTOSCROLLER does. */
int boulder_spawner(obj_t *o)
{
    obj_t *you = G.you;
    if (o->gv[0] < 4) {
        const uint16_t *b = NULL;
        if (G.level == 0x11 && boulders17[o->gv[0]][0] < G.org_x)
            b = boulders17[o->gv[0]];
        else if (G.level == 0x13 && boulders19[o->gv[0]][0] > G.org_x)
            b = boulders19[o->gv[0]];
        if (b) {
            obj_t *r = spawn_obj(0x50, LIST_MAIN, G.img[IMG_DEADRCK1], b[1], 0x14);
            r->xdir = (uint8_t)b[2];
            o->gv[0]++;
        }
    }
    if (G.level != 0x13 || !you || *you_hp() == 0 || G.lightning_flag)
        return 0;
    if (you->x < (uint16_t)(G.org_x + 3)) {
        if (you->xdir == 0 || !(you->gv[5] & 1))
            you->xspeed = 0;
        player_right(1, you);
    } else if ((uint16_t)(G.org_x + 0x12F) < you->x) {
        if (you->xdir != 0 || !(you->gv[5] & 1))
            you->xspeed = 0;
        you_follow_slope(you);
        if (can_go_left(you) && you_room_left(you))
            player_left(1, you);
        else if (you->animator != A_NULLANIMATOR)
            you->animator = A_YOUCRUSH;
    }
    if (carry(&o->xpart, 0x1000) && G.org_x != 0)
        scroll_left(1);
    return 0;
}
