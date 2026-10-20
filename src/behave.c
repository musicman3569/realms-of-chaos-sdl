/* Object behaviours (the Animator functions). Each is a port of the function named, with
 * its source file and line from the debug info. Calls into code that isn't ported yet (the
 * player, sound, damage) go to the stubs in engine.c. */
#include "engine.h"

static int16_t iabs16(int16_t v)
{
    return v < 0 ? (int16_t)-v : v;
}

/* Add a speed to a 16-bit fraction; true when it carries (one pixel / one step). */
static bool carry(uint16_t *part, int16_t speed)
{
    const uint16_t old = *part;
    *part = (uint16_t)(*part + speed);
    return *part < old;
}

static int null_animator(obj_t *o)
{
    (void)o;
    return 0;
}

/* RXGENERL.C:146 DOARROWSIGN: the "this way" sign blinks: the arrow after 300 ticks, a blank
 * (Sword3ID[0]) after 800. */
static int do_arrow_sign(obj_t *o)
{
    o->update = 0x32;
    if (o->counter == 300) {
        const uint8_t lv = G.level;
        const sprite_t *f = G.img[IMG_ARROW + 1];
        if (lv != 2 && lv != 12 && lv != 14 && lv != 15 && lv != 26)
            f = G.img[IMG_ARROW + (lv == 0x13 ? 2 : 0)];
        obj_image(o, f);
    } else if (o->counter > 800) {
        o->image = G.img[IMG_SWORD3];
        o->counter = 0;
    }
    return 0;
}

/* RXFRSTF.C:11 FDOLEAF (DOLEAF calls it): an invisible spawner that, once on screen, drops
 * one leaf (1 chance in 20000 per tick) and goes. The leaf sways: ySpeed and xSpeed swing
 * between limits, flipping yDir / xDir at each end. */
static int do_leaf(obj_t *o)
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
static int do_bird(obj_t *o)
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
static int do_bat(obj_t *o)
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
                    const bool room = G.level == 0x0F ? (uint16_t)(G.org_x + 0x12) < o->x
                                                      : o->x >= 0x15;
                    if (room && o->up_offset < 0x0B) {
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
                    const bool room =
                        G.level == 0x0F
                            ? (uint16_t)(o->x + o->xsize) < (uint16_t)(G.org_x + 0x130)
                            : (uint16_t)(o->x + o->xsize) < (uint16_t)(G.lv->cols * 16 - 1);
                    if (room && o->up_offset < 0x0B) {
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

/* RXGENERL.C:610 GENERICBEGINFALL: start falling (GENERICFALL). */
static int generic_begin_fall(obj_t *o)
{
    o->animator = A_GENERICFALL;
    o->gv[5] &= ~(4 | 8 | 1);
    /* Priority 0x4a (the boss) and 0x35 (frogs in the foreground list) do more here; not
     * ported yet. */
    return 0;
}

static int do_projectile(obj_t *o);

/* RXGENERL.C:709 GENERICDOWN: one pixel down, or land. */
static void generic_down(obj_t *o)
{
    if ((G.level == 8 || G.level == 9) &&
        (uint16_t)(G.lv->rows * 16 - 0x21) < (uint16_t)(o->y + o->ysize)) {
        /* fell into the swamp */
        if (o == G.you)
            return; /* FYOUDROWN: not ported */
        if (o->priority != 0x35) {
            obj_kill(o);
            return;
        }
    } else {
        can_move(o);
        if (o->down_offset == 1 && o->up_offset < 10) {
            if (o->priority == 0xE6 || o->priority == 0x97)
                return; /* the player (PLAYERDOWN), boulders (MAINTAINPLATFORM): not ported */
            o->y++;
            if (o->animator != A_YOUBEGINIMPALE && o->animator != A_NULLANIMATOR)
                o->animator = A_GENERICFALL;
            if ((o->flags & OF_HIT) && (o->priority == 0x35 || o->priority == 0x33))
                enemy_hit(o, o->priority == 0x35 ? 3 : 5);
            return;
        }
    }
    /* Landed: GenericVar[5] bits 3 and 0, and the animator for what it is. */
    o->gv[5] |= 8 | 1;
    switch (o->priority) {
    case 0x3A: o->animator = A_DOBOMB; break;
    case 0x3B: o->animator = A_ROSSPIECE; break;
    case 0x3C: play_sample(0x75, o); o->animator = A_ROSSATTACK; break;
    case 0xE6: break; /* YOUBEGINSTUN: not ported */
    case 0xE5: o->animator = A_DOCOLLECTABLE; break;
    case 2: o->animator = A_DOCONTAINER; break;
    case 0x4E: o->animator = A_DOCATSKELETON; break;
    case 100:
        if (o->gv[0] == 1) {
            o->gv[5] &= ~1;
            o->y++;
        } else {
            o->animator = A_DODROP;
        }
        break;
    case 0x35: o->animator = A_FROGLAND; break;
    case 0x31:
        o->flags |= OF_RATE;
        o->animator = A_DOROCKMONSTER;
        if (G.level == 0x10)
            o->xspeed = 6000;
        play_sample(o->gv[0] == 1 ? 0x73 : 0x83, o);
        break;
    case 0x33: o->animator = A_GOBLINRUN; o->update = 0x32; break;
    case 0x39: o->animator = A_DOSPORE; break;
    case 0x66: o->animator = A_DRAGONHEADDIE; break;
    case 0x49: o->animator = A_DOSPITTLE2; break;
    case 0x47: o->animator = A_BOSSHEADBOUNCE; break;
    case 0x97: o->animator = A_ROLLINGBOULDER; break;
    case 0x3E: o->animator = A_DOBOARSPEAR; break;
    case 0x4A:
        if (o == G.boss) {
            sound_link_kill(o->gv[2]);
            o->gv[2] = (int16_t)play_sample(o->xdir == 0 ? 0x97 : 0x52, o);
            o->animator = o->xdir == 0 ? A_SKELLUNGE : A_SKELWALK;
        } else {
            o->animator = A_DOSKELHEAD;
        }
        break;
    default: o->animator = A_DOPROJECTILE; break;
    }
}

/* RXGENERL.C:658 GENERICFALL: the object's own behaviour (picked by priority), then gravity:
 * ySpeed grows by 0xb4 a tick and each yPart carry is a GENERICDOWN. */
static int generic_fall(obj_t *o)
{
    switch (o->priority) {
    case 0x3A: case 2: case 0x3B: case 0x3C: case 0xE6: case 0xE5: case 0x4E: case 100:
    case 0x35: case 0x31: case 0x33: case 0x39: case 0x66: case 0x49: case 0x47: case 0x97:
    case 0x3E: case 0x4A: case 0xE7:
        break; /* DOBOMB, DOCONTAINER, ...: not ported yet */
    default:
        do_projectile(o);
        break;
    }
    if (o->animator == A_GENERICFALL) {
        const int16_t v = (int16_t)(o->yspeed + 0xB4);
        if (v >= 0)
            o->yspeed = v;
        if (carry(&o->ypart, o->yspeed))
            generic_down(o);
    }
    return 0;
}

/* RXGENERL.C:812 DOPROJECTILE: things thrown or dropped. GenericVar[0] is the kind: 1 sparks,
 * 2 a thrown gem, 3/4 a knife (4 doesn't hurt), 5 a bird dropping, 6 a falling spike, 7 a
 * skeleton's head. They fly sideways at xSpeed until a wall, and finish when they land. */
static int do_projectile(obj_t *o)
{
    switch (o->gv[0]) {
    case 1: next_frame(o, IMG_SPARKS); break;
    case 7: next_frame(o, IMG_SKELHEAD + o->gv[1]); break;
    case 3: case 5: hit_player(o); break; /* (on the hardest level a knife can be caught) */
    default: break; /* 6: SPIKECHECK, not ported */
    }
    if (carry(&o->xpart, o->xspeed)) {
        bool moved = false;
        if (can_go_right(o)) {
            const bool in = G.level == 0x0F
                                ? (uint16_t)(o->x + o->xsize) < (uint16_t)(G.org_x + 0x130)
                                : (uint16_t)(o->x + o->xsize) < (uint16_t)(G.lv->cols * 16 - 1);
            if (in && can_go_left(o)) {
                const bool in2 = G.level == 0x0F ? (uint16_t)(G.org_x + 0x12) < o->x
                                                 : o->x >= 0x15;
                if (in2) {
                    if (!(o->flags & 1) && o->xdir == 0) {
                        if (o->x > 4)
                            o->x--;
                    } else {
                        o->x++;
                    }
                    moved = true;
                }
            }
        }
        if (!moved) {
            if (o->gv[0] < 2) {
                obj_kill(o);
            } else {
                o->yspeed = 0;
                o->xspeed = 0;
            }
            return 0;
        }
    }
    if (o->gv[5] & 1) {
        /* landed */
        if (o->gv[0] == 5 && G.level < 3) {
            o->gv[0] = 3;
            o->animator = A_DOBIRD; /* the splat */
            o->counter = 0;
            play_sample(0x5C, NULL);
            return 0;
        }
        if (o->gv[0] == 5)
            play_sample(0x83, NULL);
        /* 2 (gems), 3 (knives) and 6 (spikes) leave something behind; not ported yet. */
        obj_kill(o);
    }
    return 0;
}

/* Sorted by offset. */
typedef int (*animator_fn)(obj_t *);
typedef struct {
    uint16_t offset;
    animator_fn fn;
} animator_t;

static const animator_t table[] = {
    {A_DOLEAF, do_leaf},
    {A_DOBIRD, do_bird},
    {A_DOBAT, do_bat},
    {A_DOARROWSIGN, do_arrow_sign},
    {A_GENERICBEGINFALL, generic_begin_fall},
    {A_GENERICFALL, generic_fall},
    {A_DOPROJECTILE, do_projectile},
    {A_NULLANIMATOR, null_animator},
};

const animator_t *behaviours(int *count)
{
    *count = (int)(sizeof table / sizeof table[0]);
    return table;
}
