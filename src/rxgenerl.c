/* RXGENERL.C: behaviours shared by many objects: falling, jumping, projectiles, containers. */
#include "behave.h"

bool room_left(const obj_t *o)
{
    return G.level == 0x0F ? (uint16_t)(G.org_x + 0x12) < o->x : o->x >= 0x15;
}

bool room_right(const obj_t *o)
{
    const uint16_t edge = (uint16_t)(o->x + o->xsize);
    return G.level == 0x0F ? edge < (uint16_t)(G.org_x + 0x130)
                           : edge < (uint16_t)(G.lv->cols * 16 - 1);
}

int null_animator(obj_t *o)
{
    (void)o;
    return 0;
}

/* RXGENERL.C:146 DOARROWSIGN: the "this way" sign blinks: the arrow after 300 ticks, a blank
 * (Sword3ID[0]) after 800. */
int do_arrow_sign(obj_t *o)
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

/* RXGENERL.C:1203 DOCONTAINER: a statue. Hit, it breaks into pieces and (after three hits)
 * shows its other frame; the hit part waits for the player. A statue that lands after a
 * fast fall (GenericVar[1] reset to 0) bounces twice. */
int do_container(obj_t *o)
{
    if (G.you && obj_overlap(o, G.you) && G.message_on)
        G.game_flags |= 0x40;
    if (o->flags & OF_HIT) {
        /* smashing (4 pieces, ENEMYHIT, the broken image): not ported yet */
        o->flags &= 0xFD;
    }
    if (o->animator == A_DOCONTAINER) {
        if (o->gv[1] < 3) {
            if (++o->gv[1] == 1) {
                o->yspeed = -0x9C4;
                o->gv[4] = 0x1E;
                play_sample(0x5A, o);
                generic_begin_jump(o);
            }
        } else if (o->image != G.img[IMG_STATUE]) {
            o->animator = A_NULLANIMATOR;
        }
    } else if (o->animator == A_GENERICFALL && o->gv[1] == 10 && o->yspeed > 10000) {
        o->gv[1] = 0;
    }
    return 0;
}

/* The per-object part of GENERICFALL / GENERICUP, by priority. The two lists differ: only
 * falling has CATSKELETON (0x4e), DODROP (100) and ROCKMONSTERFALL (0x31), and 0xe7 does
 * nothing while falling; otherwise those go to DOPROJECTILE like any other priority. */
static void own_behaviour(obj_t *o, bool falling)
{
    switch (o->priority) {
    case 2:
        do_container(o);
        return;
    case 0x3A: case 0x3B: case 0x3C: case 0xE6: case 0xE5: case 0x35: case 0x33: case 0x39:
    case 0x66: case 0x49: case 0x47: case 0x97: case 0x3E: case 0x4A:
        return; /* DOBOMB, ROSSPIECE, ROSSJUMP, YOUHIT, ...: not ported yet */
    case 0x4E: case 100: case 0x31: case 0xE7:
        if (falling)
            return; /* DOCATSKELETON, DODROP, ROCKMONSTERFALL: not ported yet */
        break;
    default:
        break;
    }
    do_projectile(o);
}

/* RXGENERL.C:610 GENERICBEGINFALL: start falling (GENERICFALL). */
int generic_begin_fall(obj_t *o)
{
    o->animator = A_GENERICFALL;
    o->gv[5] &= ~(4 | 8 | 1);
    /* Priority 0x4a (the boss) and 0x35 (frogs in the foreground list) do more here; not
     * ported yet. */
    return 0;
}

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
            if (o->priority == 0xE6) {
                player_down(1, G.you);
            } else if (o->priority == 0x97) {
                o->gv[5] |= 2;
                maintain_platform(o); /* (and MAINTAINBLOCK, not ported) */
            } else {
                o->y++;
            }
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
int generic_fall(obj_t *o)
{
    own_behaviour(o, true);
    if (o->animator == A_GENERICFALL) {
        const int16_t v = (int16_t)(o->yspeed + 0xB4);
        if (v >= 0)
            o->yspeed = v;
        if (carry(&o->ypart, o->yspeed))
            generic_down(o);
    }
    return 0;
}

/* RXGENERL.C:491 GENERICBEGINJUMP: start a jump (GENERICJUMP for GenericVar[4] ticks, then
 * GENERICUP until the upward speed is spent). */
int generic_begin_jump(obj_t *o)
{
    o->gv[5] = (int16_t)((o->gv[5] & ~8 & ~1) | 4);
    if (o->anim_inc)
        o->anim_inc = 1;
    o->frame_no = 0;
    o->frame_count = o->frame_count_max;
    o->animator = A_GENERICJUMP;
    if (o->priority == 0x3C) {
        obj_image(o, G.img[IMG_ROSSJUMP + 1]);
    } else if (o->priority == 0xE6) {
        /* The player: SETSCROLLBOUNDS (not ported), and from a crouch stand up first unless
         * there's a ceiling. */
        if (G.center_off >= 0) {
            set_scroll_bounds(0x8C, G.window_xsize);
            G.center_off = 0;
        }
        if (o->ysize == 0x19) {
            const unsigned row = (unsigned)(G.you->y - 0x10) >> 4;
            if ((map_cell(row, G.you->x >> 4) & 0x1000) ||
                (map_cell(row, (unsigned)(G.you->x + 0x0D) >> 4) & 0x1000)) {
                o->image = G.img[G.you_jump + 3];
                return 0;
            }
            o->y = (uint16_t)(o->y - 0x10);
        }
        obj_image(o, G.img[G.you_jump + 3]);
    }
    return 0;
}

/* RXGENERL.C:534 GENERICJUMP */
int generic_jump(obj_t *o)
{
    if (--o->gv[4] < 1)
        o->animator = A_GENERICUP;
    else
        o->yspeed = (int16_t)(o->yspeed - 0xB4);
    generic_up(o);
    return 0;
}

/* RXGENERL.C:545 GENERICUP: the object's own behaviour, then rise while ySpeed (negative,
 * growing by 0xb4 a tick) lasts; each borrow of yPart is a pixel up. At a ceiling, or once
 * the speed is spent, it starts falling. */
int generic_up(obj_t *o)
{
    own_behaviour(o, false);
    if (o->animator != A_GENERICJUMP && o->animator != A_GENERICUP)
        return 0;
    const int32_t v = o->yspeed + 0xB4;
    o->yspeed = (int16_t)v;
    if (v > 0)
        return generic_begin_fall(o);
    if (carry(&o->ypart, o->yspeed)) {
        if ((o->flags & OF_HIT) && (o->priority == 0x35 || o->priority == 0x33)) {
            if (o->priority == 0x33)
                play_sample(0x8C, NULL);
            enemy_hit(o, o->priority == 0x35 ? 3 : 5);
        }
        return 0;
    }
    if ((!can_go_up(o) || o->y < 3) && o->priority != 0x35) {
        o->yspeed = 0;
        return generic_begin_fall(o);
    }
    if (o->priority == 0xE6) {
        player_up(1, G.you);
    } else if (o->priority == 0x97) {
        o->gv[5] |= 1;
        maintain_platform(o); /* (and MAINTAINBLOCK, not ported) */
    } else {
        o->y--;
    }
    return 0;
}

/* RXGENERL.C:812 DOPROJECTILE: things thrown or dropped. GenericVar[0] is the kind: 1 sparks,
 * 2 a thrown gem, 3/4 a knife (4 doesn't hurt), 5 a bird dropping, 6 a falling spike, 7 a
 * skeleton's head (0: debris). They fly sideways at xSpeed until a wall, and finish when
 * they land. */
int do_projectile(obj_t *o)
{
    switch (o->gv[0]) {
    case 1: next_frame(o, IMG_SPARKS); break;
    case 7: next_frame(o, IMG_SKELHEAD + o->gv[1]); break;
    case 3: case 5: hit_player(o); break; /* (on the hardest level a knife can be caught) */
    case 6: spike_check(o, 0); break;
    default: break;
    }
    if (carry(&o->xpart, o->xspeed)) {
        if (can_go_right(o) && room_right(o) && can_go_left(o) && room_left(o)) {
            if (!(o->flags & 1) && o->xdir == 0) {
                if (o->x > 4)
                    o->x--;
            } else {
                o->x++;
            }
        } else {
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
