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
                player_up(1);
            }
        }
    }
    if (o->gv[5] & MOVE_DOWN) {
        o->y++;
        if ((o->gv[5] & RIDER) && you && can_go_down(you))
            player_down(1);
    }
    if (o->gv[5] & MOVE_LEFT) {
        o->x--;
        if ((o->gv[5] & RIDER) && you && can_go_left(you))
            player_left(1);
    }
    if (o->gv[5] & MOVE_RIGHT) {
        o->x++;
        if ((o->gv[5] & RIDER) && you && can_go_right(you))
            player_right(1);
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
    /* The goblin levels' boss platform also pushes the player (MAINTAINBLOCK): not ported. */
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
