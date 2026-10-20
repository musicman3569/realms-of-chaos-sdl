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
 * spiked wall (IsWall) only blocks a player walking or crawling into it (MAINTAINBLOCK). */
void spike_check(obj_t *o, uint8_t is_wall)
{
    obj_t *you = G.you;
    if (!you || !obj_collide(you, o))
        return;
    if ((you->animator == A_YOURUN || you->animator == A_YOUCROUCH) && is_wall) {
        maintain_block(o);
        return;
    }
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

/* RXFRSTF.C:526 FDOGARGOYLE (DOGARGOYLE calls it): a gargoyle statue on a 2050-tick cycle
 * (Borland's sparse switch: a table of five Counter values at CS:0df2, then their targets).
 * At ticks 1, 401 and 801, if it's in view, it spits a fireball at the player from its
 * mouth (3 pixels down; 21 for GargoyleID[1] shooting left or GargoyleID[0] shooting
 * right); at 2000 it blinks. On those ticks it doesn't hurt. 20 damage points destroy it. */
int do_gargoyle(obj_t *o)
{
    switch (o->counter) {
    case 1: case 0x191: case 0x321: {
        if (!obj_visible(o))
            return 0;
        play_sample(0x61, NULL);
        const bool right = !(G.you && G.you->x < o->x);
        const uint16_t x = right ? (uint16_t)(o->x + o->xsize) : (uint16_t)(o->x - 7);
        const bool carving = o->image == G.img[IMG_GARGOYLE + 1];
        const uint16_t y = (uint16_t)(o->y + (carving != right ? 0x15 : 3));
        obj_t *s = spawn_obj(0x24, LIST_MAIN, G.img[IMG_GARGFIRE], x, y);
        s->animator = A_DOGARGOYLEFIRE;
        s->flags |= OF_RATE | OF_HITTABLE;
        s->xspeed = 16000;
        if (right)
            s->flags |= 1;
        else
            s->flags &= 0xFFFE;
        return 0;
    }
    case 0x7D0:
        o->blink_color = 0x4A;
        obj_blink(o, 2, 5, 5);
        return 0;
    case 0x802:
        o->counter = 0;
        return 0;
    default:
        break;
    }
    hit_player(o);
    if (o->flags & OF_HIT) {
        play_sample(0x83, NULL);
        enemy_hit(o, 0x14);
    }
    return 0;
}

/* RXFRSTF.C:577 FDOGARGOYLEFIRE: the gargoyle's fireball flies a pixel a run (OF_RATE,
 * xSpeed 16000) left or right (Flags bit 0), blinking. A wall, the map's edge, the player's
 * weapon or the player ends it in an explosion (UpOffset 11 is the "done" mark). */
int do_gargoyle_fire(obj_t *o)
{
    can_move(o);
    if (o->blinks == 0) {
        o->blink_color = 0x1D;
        obj_blink(o, 4, 2, 2);
    }
    bool moved = false;
    if ((o->flags & 1) && can_go_right(o) && room_right(o)) {
        o->x++;
        moved = true;
    } else if (!(o->flags & 1) && can_go_left(o) && room_left(o)) {
        o->x--;
        moved = true;
    }
    if (!moved)
        o->up_offset = 0x0B;
    if (o->flags & OF_HIT)
        o->up_offset = 0x0B;
    if (G.you && obj_collide(o, G.you)) {
        o->up_offset = 0x0B;
        hit_player(o);
    }
    if (o->up_offset > 10) {
        play_sample(0x62, NULL);
        spawn_obj(0x0D, LIST_MAIN, G.img[IMG_EXPLODE], (uint16_t)(o->x - 3),
                  (uint16_t)(o->y - 3))->gv[0] = 0;
        obj_kill(o);
    }
    return 0;
}

/* RXFRSTF.C:607 FDOCAVEHOLE: a vent (OF_RATE, a run every 44 ticks) that puffs flames
 * (DOTORCHPUFF) from tick 1500 to 2000 of each cycle: info 'r' / 'l' blows sideways (the side
 * hole frame, CaveHoleID[1]), 'd' down (invisible), anything else up. On the demon's level
 * (25) a landed spike becomes a hole like this ('u'): it blinks from 1900, vanishes at 2200
 * in a puff and goes at 2350. */
int do_cave_hole(obj_t *o)
{
    const int16_t kind = o->gv[0];
    if (o->gv[1] == 0) {
        o->gv[1] = 1;
        if (kind == 'r' || kind == 'l') {
            o->xdir = kind == 'r';
            obj_image(o, G.img[IMG_CAVEHOLE + 1]);
        } else if (kind == 'd') {
            o->image = NULL;
        }
    }
    if (o->image && o->counter > 0x76C && G.level == 0x19 && o->blinks == 0) {
        o->blink_color = 0x4A;
        obj_blink(o, 10, 3, 7);
    }
    if (o->counter <= (G.level == 0x19 ? 0x898 : 0x5DC))
        return 0;
    if (o->gv[1] == 1) {
        o->gv[1] = 2;
        if (G.level == 0x19) {
            play_sample(0x62, NULL);
            o->x = (uint16_t)(o->x - 4);
            o->image = NULL;
        } else {
            play_sample(0x91, o);
        }
    }
    if (o->counter > (G.level == 0x19 ? 0x92E : 2000)) {
        o->gv[1] = 1;
        o->counter = 0;
        if (G.level == 0x19) {
            obj_kill(o);
            return 0;
        }
    }
    obj_t *p = spawn_obj(0x24, LIST_MAIN, G.img[IMG_EXPLODE], (uint16_t)(o->x + 1),
                         (uint16_t)(o->y - 5));
    p->animator = A_DOTORCHPUFF;
    if (kind == 'r' || kind == 'l') {
        p->xspeed = 16000;
        p->yspeed = 0;
        if (kind == 'r')
            p->flags |= 1;
        else
            p->flags &= 0xFFFE;
        p->y = (uint16_t)(p->y + 3);
        p->x = (uint16_t)(kind == 'r' ? p->x + 2 : p->x - 2);
    } else if (kind == 'd') {
        p->xspeed = 0;
        p->yspeed = 16000;
        p->gv[1] = 1;
        p->y = (uint16_t)(p->y + 4);
    } else {
        p->xspeed = 0;
        p->yspeed = 16000;
        p->gv[1] = 0;
    }
    p->max_frames = 6;
    p->anim_inc = 0;
    p->frame_count_max = 0x2D;
    p->frame_count = 0x2D;
    p->gv[0] = 1;
    return 0;
}

/* RXFRSTF.C:686 FDOCAVESPIDER: a spider on its thread (OF_RATE, a run every 16 ticks).
 * GenericVar[0]: 0 climbing (Flags bit 0 clear, SpiderID[0]) or letting itself down (bit 0
 * set, SpiderID[1]) a pixel a run, writing its thread into the foreground map (tile 0x4f) as
 * it goes down and clearing it climbing back; after 251 ticks (then 1 chance in 10 a run) or
 * when it can't go on, it turns round or (1 in 2) waits. 1 waiting: after 251 ticks it spits
 * (1 in 2, unless there's floor just below), else it moves again. 2 spitting: three drops of
 * poison (type 0x2f, PoisonID[0..2]) 11 ticks apart, all with the same random speed and
 * side. */
int do_cave_spider(obj_t *o)
{
    bool must_go = false;
    switch (o->gv[0]) {
    case 1:
        if (o->counter < 0xFB)
            break;
        if (frandom(2) != 0 &&
            !(map_cell((unsigned)(uint16_t)(o->y + o->ysize + 6) >> 4,
                       (unsigned)(uint16_t)(o->x + 2) >> 4) & 0x800)) {
            o->gv[1] = (int16_t)frandom(9000);
            o->gv[2] = (int16_t)frandom(2);
            o->gv[4] = 0;
            o->gv[0] = 2;
            o->counter = 0;
            break;
        }
        o->gv[0] = 0;
        break;
    case 2: {
        if (o->counter < 0x0B)
            break;
        o->counter = 0;
        const int16_t n = o->gv[4]++;
        obj_t *p = spawn_obj(0x2F, LIST_MAIN, G.img[IMG_POISON + n], (uint16_t)(o->x + 2),
                             (uint16_t)(o->y + o->ysize));
        p->xspeed = o->gv[1];
        if (o->gv[2] == 0)
            p->flags |= 1;
        else
            p->flags &= 0xFFFE;
        if (o->gv[4] == 3)
            o->gv[0] = 0;
        break;
    }
    default:
        if (!(o->flags & 1)) {
            if (!can_go_up(o)) {
                must_go = true;
            } else {
                if ((o->y & 0x0F) == 0)
                    map_set_fg((unsigned)(uint16_t)(o->y + 1) >> 4, o->x >> 4, 0);
                o->y--;
            }
        } else {
            if (!can_go_down(o) || (G.level == 0x0D && (uint16_t)(o->y + o->ysize) > 0x9F)) {
                must_go = true;
            } else {
                if ((o->y & 0x0F) == 0)
                    map_set_fg(o->y >> 4, o->x >> 4, 0x4F);
                o->y++;
            }
        }
        if (!must_go && (o->counter < 0xFB || frandom(10) != 0))
            break;
        o->counter = 0;
        if (frandom(2) != 0) {
            if (o->flags & 1) {
                o->image = G.img[IMG_SPIDER];
                o->flags &= 0xFFFE;
            } else {
                o->image = G.img[IMG_SPIDER + 1];
                o->flags |= 1;
            }
            break;
        }
        o->gv[0] = 1;
        break;
    }
    hit_player(o);
    if (o->flags & OF_HIT)
        enemy_hit(o, 2);
    return 0;
}

/* RXFRSTF.C:493 FDOCATSKELETON (DOCATSKELETON calls it; GENERICFALL too while it falls): a
 * skeleton cat walking a step at each carry of 0xdac, following slopes and turning at walls
 * (and short of x 40). Off a ledge it falls (GENERICBEGINFALL) and lands walking again.
 * Between steps it hurts; 12 damage points kill it, and so does falling to the bottom of the
 * map. */
int do_cat_skeleton(obj_t *o)
{
    if (carry(&o->xpart, 0x0DAC)) {
        next_frame(o, IMG_SKELCAT);
        if (o->xdir == 0) {
            if (!can_go_left(o) || o->x < 0x28)
                o->xdir = 1;
            else
                o->x--;
        } else if (!can_go_right(o)) {
            o->xdir = 0;
        } else {
            o->x++;
        }
        can_move(o);
        if (o->up_offset > 10)
            o->y = (uint16_t)(o->y - (o->up_offset - 10));
        else if (o->down_offset > 10)
            o->y = (uint16_t)(o->y + (o->down_offset - 10));
        else if (o->down_offset == 1)
            o->animator = A_GENERICBEGINFALL;
        return 0;
    }
    hit_player(o);
    if (o->flags & OF_HIT) {
        play_sample(0x82, NULL);
        enemy_hit(o, 12);
    }
    if ((uint16_t)(o->y + o->ysize) > (uint16_t)(G.lv->rows * 16 - 0x21))
        enemy_hit(o, 0);
    return 0;
}
