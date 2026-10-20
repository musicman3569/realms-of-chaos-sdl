/* RXFOREST.C / RXFRSTF.C: the forest creatures. */
#include "behave.h"

/* RXFRSTF.C:11 FDOLEAF (DOLEAF calls it): an invisible spawner that, once on screen, drops
 * one leaf (1 chance in 20000 per tick) and goes. The leaf sways: ySpeed and xSpeed swing
 * between limits, flipping yDir / xDir at each end. */
int do_leaf(obj_t *o)
{
    if (o->gv[0] == 0) {
        o->image = NULL;
        if (frandom(20000) != 0 || !obj_visible(o))
            return 0;
        obj_t *s = spawn_obj(0x10, LIST_FORE, G.img[IMG_LEAF], o->x, o->y);
        s->gv[0] = 1;
        s->gv[2] = 1;
        s->flags &= ~OF_PLACED;
        obj_kill(o);
        return 0;
    }
    if (o->gv[0] == 1) {
        if (o->yspeed < 0x1FA4) {
            o->yspeed += 0x34;
        } else {
            o->gv[0] = 2;
            o->ydir = o->ydir ? 0 : 1;
        }
    } else if (o->yspeed < 0x65) {
        o->gv[0] = 1;
        o->ydir = o->ydir ? 0 : 1;
    } else {
        o->yspeed -= 0x34;
    }
    if (o->gv[2] == 1) {
        if (o->xspeed < 0x4000) {
            o->xspeed += 100;
        } else {
            o->gv[2] = 2;
            o->xdir = o->xdir ? 0 : 1;
        }
    } else if (o->xspeed < -0x3FFF) {
        o->gv[2] = 1;
        o->xdir = o->ydir ? 0 : 1; /* (sic: tests yDir) */
    } else {
        o->xspeed -= 100;
    }
    if (carry(&o->ypart, o->yspeed)) {
        o->y++;
        o->gv[1]++;
        if ((o->gv[1] > 100 && frandom(500) == 0) || !obj_visible(o) ||
            (uint16_t)(G.lv->rows * 16 - 0x11) < o->y) {
            obj_kill(o);
            return 0;
        }
    }
    if (carry(&o->xpart, iabs16(o->xspeed))) {
        if (o->xspeed < 1)
            o->x--;
        else
            o->x++;
    }
    return 0;
}

/* RXFOREST.C:16 DOBIRD. GenericVar[0]: 0 an invisible spawner waiting for the player to come
 * within 49 pixels, 1 spawning 15 birds, 2 a bird flying up and away (flapping every 41
 * ticks, turning at random, maybe dropping something as it leaves), 3-5 a dropping's splat
 * (DOPROJECTILE hands landed droppings back here: BirdID[4], [5] are BirdpoopID[2], [3]). */
int do_bird(obj_t *o)
{
    if (o->gv[0] == 0) {
        o->image = NULL;
        if (!G.you || iabs16((int16_t)(o->x - G.you->x)) > 0x31 || !obj_visible(o))
            return 0;
        o->flags |= OF_RATE;
        o->xspeed = 0x800;
        o->gv[0] = 1;
        return 0;
    }
    if (o->gv[0] == 1) {
        play_sample((uint8_t)(frandom(2) + 0x93), NULL);
        obj_t *s = spawn_obj(0x32, LIST_FORE, G.img[IMG_BIRD], o->x, o->y);
        s->gv[0] = 2;
        s->xspeed = (int16_t)(frandom(8000) + 8000);
        s->yspeed = (int16_t)(frandom(8000) + 8000);
        s->xdir = (uint8_t)frandom(2);
        s->flags &= ~OF_PLACED;
        if (++o->gv[2] >= 15)
            obj_kill(o);
        return 0;
    }
    if (o->gv[0] > 2) {
        if (o->counter < 0x29)
            return 0;
        o->counter = 0;
        o->gv[0]++;
        o->image = G.img[IMG_BIRD + o->gv[0]];
        if (o->gv[0] == 5) {
            o->flags &= ~OF_PLACED;
            o->update = 0x32;
            o->animator = A_NULLANIMATOR;
        }
        return 0;
    }
    if (o->counter > 0x28) {
        o->counter = 0;
        o->image = o->image == G.img[IMG_BIRD] ? G.img[IMG_BIRD + 1] : G.img[IMG_BIRD];
    }
    const uint16_t you_x = G.you ? G.you->x : 0;
    if (o->xdir) {
        const bool turn = you_x < o->x ? frandom(500) == 0 : frandom(300) == 0;
        if (turn)
            o->xdir = 0;
    }
    if (!o->xdir) {
        const bool turn = o->x < you_x ? frandom(500) == 0 : frandom(300) == 0;
        if (turn)
            o->xdir = 1;
    }
    if (carry(&o->ypart, o->yspeed)) {
        o->y--;
        if (!obj_visible(o) || o->y < (uint16_t)(G.org_y - 5)) {
            if (frandom(3) == 0)
                spawn_obj(0x5D, LIST_MAIN, G.img[IMG_BIRDPOOP], (uint16_t)(o->x + 3),
                          (uint16_t)(o->y + 3));
            obj_kill(o);
            return 0;
        }
    }
    if (carry(&o->xpart, o->xspeed)) {
        if (o->xdir == 0)
            o->x--;
        else
            o->x++;
    }
    return 0;
}

/* RXFOREST.C:120 DOBAT. Bats with info "l"/"r" (GenericVar[4] 0) wait until they're on
 * screen, then fly in from the far edge and bounce between the edges of the view; cave bats
 * (GenericVar[4] 1) turn towards the player and stop at walls. Each xSpeed carry is a step
 * sideways and an animation tick; otherwise they bob up and down (ySpeed swings by
 * Friction / 2 between +-15000, or +-17000 for cave bats). */
int do_bat(obj_t *o)
{
    if (o->gv[4] != 0)
        can_move(o);
    if (carry(&o->xpart, o->xspeed)) {
        if (o->gv[2] != 0 && !sound_link_active(o->gv[2]))
            o->gv[2] = 0;
        if (o->gv[2] == 0 && obj_visible(o))
            o->gv[2] = (int16_t)play_sample(4, o);
        if (o->gv[0] == 0) {
            if (o->gv[4] != 0) {
                o->gv[0]++;
                o->xdir = (G.you && G.you->x < o->x) ? 0 : 1;
                return 0;
            }
            if (!obj_visible(o))
                return 0;
            o->gv[0]++;
            o->x = o->xdir == 0 ? (uint16_t)(G.org_x + 0x140) : (uint16_t)(G.org_x - 8);
            return 0;
        }
        if (o->xdir == 0) {
            if ((uint16_t)(G.org_x + 0x12) <= o->x) {
                if (o->gv[4] == 0) {
                    o->x--;
                    next_frame(o, IMG_BAT);
                    return 0;
                }
                if (can_go_left(o)) {
                    if (room_left(o) && o->up_offset < 0x0B) {
                        o->x--;
                        next_frame(o, IMG_BAT);
                        return 0;
                    }
                }
            }
            o->xdir = 1;
        } else {
            if ((uint16_t)(o->x + o->xsize) <= (uint16_t)(G.org_x + 0x138)) {
                if (o->gv[4] == 0) {
                    o->x++;
                    next_frame(o, IMG_BAT);
                    return 0;
                }
                if (can_go_right(o)) {
                    if (room_right(o) && o->up_offset < 0x0B) {
                        o->x++;
                        next_frame(o, IMG_BAT);
                        return 0;
                    }
                }
            }
            o->xdir = 0;
        }
        if (o->gv[4] == 0) {
            o->gv[0]++;
            o->flags &= ~OF_PLACED;
        }
        next_frame(o, IMG_BAT);
        return 0;
    }

    if (o->gv[1] == 0)
        o->yspeed = (int16_t)(o->yspeed - (o->friction >> 1));
    else
        o->yspeed = (int16_t)(o->yspeed + (o->friction >> 1));
    if (!carry(&o->ypart, iabs16(o->yspeed))) {
        hit_player(o);
    } else {
        if (o->yspeed < 0) {
            if ((can_go_up(o) && o->y >= 3) || o->gv[4] == 0) {
                o->y--;
            } else {
                o->gv[1] = 1;
                o->yspeed = (int16_t)-o->yspeed;
            }
        } else if (o->down_offset != 1 && o->gv[4] != 0) {
            o->gv[1] = 0;
            o->yspeed = (int16_t)-o->yspeed;
        } else {
            o->y++;
        }
        if (o->gv[1] == 0) {
            if (o->yspeed < (o->gv[4] == 0 ? -15000 : -17000)) {
                o->frame_count_max = 12;
                o->gv[1]++;
            }
        } else if ((o->gv[4] == 0 ? 15000 : 17000) < o->yspeed) {
            o->frame_count_max = 5;
            o->gv[1]--;
        }
    }
    if (o->flags & OF_HIT) {
        sound_link_kill(o->gv[2]);
        o->gv[2] = 0;
        enemy_hit(o, (uint8_t)(o->gv[4] + 1));
    }
    return 0;
}


/* RXFRSTF.C:86 FBANDITWALK (BANDITWALK calls it): the cat bandit walks a step at each
 * xSpeed carry, following slopes, and turns round at walls, the map's edges and ledges (where
 * CANMOVE says it could fall), and anyway every 0x835 ticks. */
int bandit_walk(obj_t *o)
{
    can_move(o);
    if (o->down_offset > 10)
        o->y = (uint16_t)(o->y + (o->down_offset - 10));
    if (o->up_offset > 10)
        o->y = (uint16_t)(o->y - (o->up_offset - 10));
    if (o->xdir == 0)
        o->x--;
    else
        o->x++;
    next_frame(o, G.level < 4 ? IMG_CATBAND : IMG_CATMACE);
    bool turn = true;
    if (o->counter < 0x835) {
        const bool way = o->xdir == 0 ? can_go_left(o) && room_left(o)
                                      : can_go_right(o) && room_right(o);
        turn = !way || o->down_offset == 1;
    }
    if (turn) {
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
    hit_player(o);
    if (o->flags & OF_HIT) {
        play_sample((uint8_t)(frandom(2) + 0x8D), NULL);
        enemy_hit(o, G.level < 4 ? 4 : 15);
    }
    return 0;
}

/* RXFRSTF.C:411 FWOLFRUN (WOLFRUN calls it): wolves and dogs. GenericVar[0]: 0 falling to the
 * ground, 1 waiting to be seen, 2+ seen (growl). The step rate is xSpeed 14000 within 100
 * pixels of the player and 20000 further away (so they slow down near the player). They turn
 * at walls, edges, ledges, warp tiles (types 10-20), and when more than 100 pixels past the
 * player after 80 steps. */
int wolf_run(obj_t *o)
{
    can_move(o);
    if (o->down_offset > 10)
        o->y = (uint16_t)(o->y + (o->down_offset - 10));
    if (o->up_offset > 10)
        o->y = (uint16_t)(o->y - (o->up_offset - 10));
    if (o->down_offset == 1 && o->gv[0] == 0) {
        o->y++;
        return 0;
    }
    if (o->gv[0] == 0)
        o->gv[0]++;
    const uint16_t you_x = G.you ? G.you->x : o->x;
    o->xspeed = (uint16_t)iabs16((int16_t)(o->x - you_x)) < 0x65 ? 14000 : 20000;
    if (!carry(&o->xpart, o->xspeed)) {
        hit_player(o);
        if (o->flags & OF_HIT) {
            sound_link_kill(o->gv[4]);
            enemy_hit(o, 3);
        }
        return 0;
    }
    if (o->gv[4] != 0 && !sound_link_active(o->gv[4]))
        o->gv[4] = 0;
    if (o->gv[0] == 1 && obj_onscreen(o)) {
        if (o->gv[4] == 0)
            o->gv[4] = (int16_t)play_sample(0x79, o);
        o->gv[0]++;
    } else if (o->gv[0] > 1 && !obj_visible(o) && ++o->gv[0] > 100 &&
               !sound_link_active(o->gv[4])) {
        o->gv[0] = 1;
    }
    bool step;
    if (o->xdir == 0) {
        const uint16_t t =
            G.lv->bg_type[map_cell(o->y >> 4, (unsigned)(uint16_t)(o->x - 0x10) >> 4) & 0x1FF];
        step = can_go_left(o) && room_left(o) &&
               ((int16_t)(you_x - o->x) < 0x65 || o->gv[1] < 0x51) && o->down_offset != 1 &&
               (t < 10 || t > 0x14);
        if (step)
            o->x--;
        else {
            o->x++;
            o->xdir = 1;
        }
    } else {
        const uint16_t t = G.lv->bg_type[map_cell(o->y >> 4,
                                                  (unsigned)(uint16_t)(o->x + o->xsize + 0x10) >>
                                                      4) & 0x1FF];
        step = can_go_right(o) && room_right(o) &&
               ((int16_t)(o->x - you_x) < 0x65 || o->gv[1] < 0x51) && o->down_offset != 1 &&
               (t < 10 || t > 0x14);
        if (step)
            o->x++;
        else {
            o->x--;
            o->xdir = 0;
        }
    }
    if (step)
        o->gv[1]++;
    else
        o->gv[1] = 0;
    next_frame(o, o->gv[2] == 0 ? IMG_WOLF : IMG_DOG);
    return 0;
}

/* RXFRSTF.C:759 SPIKECHECK: spikes (and stakes) hurt the player they touch, with blood. A
 * spiked wall (IsWall) only blocks a player walking or crawling into it (MAINTAINBLOCK, not
 * ported yet). */
void spike_check(obj_t *o, uint8_t is_wall)
{
    obj_t *you = G.you;
    if (!you || !obj_collide(you, o))
        return;
    if ((you->animator == A_YOURUN || you->animator == A_YOUCROUCH) && is_wall)
        return;
    if (G.hitagain || (G.game_flags & 8) || G.you_invincible || you->animator == A_YOUDIE ||
        G.quitflag)
        return;
    play_sample(0xA9, o);
    if (G.blood_on) {
        obj_t *b = spawn_obj(0x21, LIST_FORE, you->image, (uint16_t)(you->x - 5),
                             (uint16_t)(you->y - 5));
        b->image = NULL;
        b->gv[0] = (int16_t)you->ysize;
        b->gv[1] = 3;
    }
    hit_player(o);
}
