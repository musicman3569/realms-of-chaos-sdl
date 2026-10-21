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

/* RXGENERL.C:1203 DOCONTAINER: a statue. Each hit knocks four pieces off (DS:01af); after
 * three (ENEMYHIT with 3) it shows its broken frame, drops its prize and can't be hit any
 * more. A statue that lands after a fast fall (GenericVar[1] reset to 0) bounces twice. */
int do_container(obj_t *o)
{
    static const int16_t smash[4][4] = {
        {24, 8, 6000, -5000}, {24, 16, 4000, -500}, {8, 8, -4000, -5000}, {8, 16, -6000, -500}};
    if (G.you && obj_overlap(o, G.you) && G.message_on)
        G.game_flags |= 0x40;
    if (o->flags & OF_HIT) {
        play_sample(0x83, NULL);
        for (int i = 0; i < 4; i++) {
            obj_t *p = spawn_obj(0x0E, LIST_MAIN, G.img[IMG_PIECES], (uint16_t)(o->x + smash[i][0]),
                                 (uint16_t)(o->y + smash[i][1]));
            p->gv[0] = 0;
            p->gv[3] = 0x5A;
            p->xdir = smash[i][2] > 0;
            p->xspeed = iabs16(smash[i][2]);
            p->yspeed = smash[i][3];
        }
        enemy_hit(o, 3);
        if (o->gv[3] > 2) {
            play_sample(0x84, NULL);
            if (G.you && G.you->x < o->x)
                o->xdir = 1;
            obj_image(o, G.img[IMG_STATUE + 1]);
            o->flags &= ~OF_HITTABLE;
            if (G.message_on)
                G.game_flags |= 0x40;
        }
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
 * falling has DOCATSKELETON (0x4e), DODROP (100) and ROCKMONSTERFALL (0x31), and 0xe7 does
 * nothing while falling; rising, those go to DOPROJECTILE like any other priority. */
static void own_behaviour(obj_t *o, bool falling)
{
    switch (o->priority) {
    case 2:
        do_container(o);
        return;
    case 0xE6:
        you_hit(o);
        return;
    case 0xE5:
        do_collectable(o);
        return;
    case 0x33:
        goblin_jump(o);
        return;
    case 0x3E:
        do_boar_spear(o);
        return;
    case 0x35:
        frog_jump(o);
        return;
    case 0x39:
        do_spore(o);
        return;
    case 0x3A:
        do_bomb(o);
        return;
    case 0x3B:
        ross_piece(o);
        return;
    case 0x3C:
        ross_jump(o);
        return;
    case 0x66:
        dragon_head_die(o);
        return;
    case 0x49:
        do_spittle2(o);
        return;
    case 0x47:
        boss_head_bounce(o);
        return;
    case 0x4A:
        /* the skeleton boss (rising it throws up rocks), or its head */
        if (o != G.boss)
            do_skel_head(o);
        else if (falling)
            skel_fall(o);
        else
            skel_rise(o);
        return;
    case 0x97:
        rolling_boulder(o);
        return;
    case 0x4E:
        if (!falling)
            break;
        do_cat_skeleton(o);
        return;
    case 100:
        if (!falling)
            break;
        do_drop(o);
        return;
    case 0x31:
        if (!falling)
            break;
        rock_monster_fall(o);
        return;
    case 0xE7:
        if (falling)
            return;
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
    if (o->priority == 0x4A && o == G.boss)
        obj_image(o, G.img[IMG_SKELHACK + 2]); /* the skeleton boss */
    if (o->priority == 0x35 && o->list == LIST_FORE) {
        /* A frog leaping out of the water is drawn in front of it; coming down, it moves to
         * the main list as a copy (type 0x3e), and the original goes. */
        obj_t *c = spawn_obj(0x3E, LIST_MAIN, o->image, o->x, o->y);
        c->animator = o->animator;
        c->blinks = o->blinks;
        c->draw_on = o->draw_on;
        c->draw_off = o->draw_off;
        c->blink_color = o->blink_color;
        c->blink_ct = o->blink_ct;
        c->flags = o->flags;
        c->xdir = o->xdir;
        c->xpart = o->xpart;
        c->ypart = o->ypart;
        c->xspeed = o->xspeed;
        c->yspeed = o->yspeed;
        c->counter = o->counter;
        c->old_animator = o->old_animator;
        c->down_offset = o->down_offset;
        c->up_offset = o->up_offset;
        for (int i = 0; i < 6; i++)
            c->gv[i] = o->gv[i];
        c->surface_flags = o->surface_flags;
        G.monster_number++;
        o->animator = A_OFFKILLER;
    }
    return 0;
}

/* RXGENERL.C:709 GENERICDOWN: one pixel down, or land. */
static void generic_down(obj_t *o)
{
    if ((G.level == 8 || G.level == 9) &&
        (uint16_t)(G.lv->rows * 16 - 0x21) < (uint16_t)(o->y + o->ysize)) {
        /* fell into the swamp */
        if (o == G.you) {
            you_drown(o);
            return;
        }
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
                maintain_platform(o);
                if (!(o->gv[5] & 0x20) && G.you && obj_overlap(G.you, o))
                    maintain_block(o);
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
    case 0xE6: you_begin_stun(o); break;
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
        maintain_platform(o);
        if (!(o->gv[5] & 0x20) && G.you && obj_overlap(G.you, o))
            maintain_block(o);
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
        if (o->gv[0] == 3) {
            /* a knife (or poison) splashes */
            if (o->image == G.img[IMG_POISON])
                play_sample(0x86, NULL);
            obj_t *p = spawn_obj(0x24, LIST_FORE, o->image, o->x, (uint16_t)(o->y - 4));
            p->gv[1] = 2;
            p->gv[2] = 2;
            p->animator = A_SPLASHSPAWNER;
        } else if (o->gv[0] == 2) {
            /* a drop of blood leaves a splat (DOBLOOD) */
            o->gv[2] = o->gv[2] == 0 ? 10 : o->gv[2] < 3 ? 9 : o->gv[2] < 4 ? 8 : 7;
            obj_image(o, G.img[IMG_BLOOD + o->gv[2]]);
            spawn_obj(0x58, LIST_MAIN, o->image, o->x, o->y)->gv[2] = o->gv[2];
        } else {
            if (o->gv[0] == 5 && G.level < 3) {
                o->gv[0] = 3;
                o->animator = A_DOBIRD; /* the splat */
                o->counter = 0;
                play_sample(0x5C, NULL);
                return 0;
            }
            if (o->gv[0] == 5) {
                play_sample(0x83, NULL);
            } else if (o->gv[0] == 6) {
                /* a falling spike sticks in the ground */
                play_sample(0x83, NULL);
                o->flags |= OF_RATE;
                if (G.level == 0x19) {
                    o->xspeed = 0x5DC;
                    o->gv[0] = 0x75;
                    o->gv[1] = 0;
                    o->counter = 0;
                    o->animator = A_DOCAVEHOLE;
                    obj_image(o, G.img[IMG_DEMSPIK + 1]);
                } else {
                    obj_image(o, G.img[IMG_FALLSPIK + 1]);
                    o->animator = A_NULLANIMATOR;
                    o->xspeed = 0x100;
                }
                o->flags &= 0xFB;
                return 0;
            }
        }
        obj_kill(o);
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------ */
/* Damage                                                                                      */

/* RXGENERL.C:416 HITPLAYER: an enemy (or a hazard, with _FreeHit) touches the player. Unless
 * the player is protected (_hitagain counting down, recovering, invincible), take a hit point
 * (two on the hardest difficulties), splash blood, and knock the player back with a jump
 * (GENERICBEGINJUMP; YOUHIT steers it and the landing stuns) while dropping gems. On a vine,
 * or on the easiest difficulty, the player only blinks. */
int hit_player(obj_t *o)
{
    obj_t *you = G.you;
    if ((G.game_flags & 8) || G.hitagain) {
        G.free_hit = 0;
        return 0;
    }
    if (!you || G.quitflag || you->animator == A_YOUDIE || G.you_invincible ||
        (!G.free_hit && !obj_collide(o, you)))
        return 0;
    G.hitagain = 0x0B;
    G.free_hit = 0;
    uint8_t *hp = you_hp();
    const bool m = (G.game_flags & 4) != 0;
    if (*hp)
        (*hp)--;
    if (G.diff_mode > 2 && *hp)
        (*hp)--;
    if (*hp == 0)
        play_sample(m ? 0xB1 : 0xB0, NULL);
    else
        play_sample(m ? 0xA8 : 0xA7, you);
    if (G.blood_on) {
        /* where the blow lands: the enemy's near edge, at its middle, inside the player */
        uint16_t x = you->x < o->x ? (uint16_t)(o->x + o->xsize) : o->x;
        uint16_t y = (uint16_t)(o->y + (o->ysize >> 1));
        if (x < you->x)
            x = you->x;
        if ((uint16_t)(you->x + 0x0E) < x)
            x = (uint16_t)(you->x + 0x0E);
        if (y < you->y)
            y = you->y;
        if ((uint16_t)(you->y + you->ysize) < y)
            y = (uint16_t)(you->y + you->ysize);
        spawn_obj(0x0D, LIST_MAIN, G.img[IMG_EXPLODE + 12], (uint16_t)(x - 6),
                  (uint16_t)(y - 6))->gv[0] = 3;
    }
    if ((you->animator == A_YOUCLIMB || G.diff_mode == 0) && *hp) {
        if (G.diff_mode == 0)
            G.game_flags &= 0xFFF7;
        if (!G.blood_on) {
            you->blink_color = 0x2E;
            obj_blink(you, 0x19, 3, 3);
        }
    } else {
        G.game_flags |= 8;
        spawn_obj(0x12, LIST_SLOW, G.img[IMG_SWORD3], (uint16_t)(you->x + 8),
                  (uint16_t)(you->y + 8));
        you->xspeed = you->xdir == 0 ? 10000 : -10000;
        you->yspeed = -14000;
        you->gv[4] = 0x5A;
        generic_begin_jump(you);
    }
    return 0;
}

/* RXGENERL.C:904 ENEMYHIT: the player's weapon hit an object (its GenericVar[3] counts the
 * damage: 3 or 4 a crack of the whip, 1 a fireball). Below MaxHits it just flinches
 * (ENEMYHITPAUSE); then it dies in a splash (EXPLODERSPAWNER) and drops a prize: a statue
 * its own (GenericVar[0]), others a random one. On the easiest difficulty one hit kills. */
void enemy_hit(obj_t *o, uint8_t max_hits)
{
    const int16_t pr = o->priority;
    uint8_t dropped = 0;
    if (pr != 0x3C && pr != 2 && pr != 0x66 && pr != 0x47 && pr != 0x4A && pr != 0x4C &&
        G.diff_mode == 0)
        max_hits = 1;
    if (o->gv[3] < max_hits) {
        o->flags &= 0xFD;
        o->old_animator = o->animator;
        o->animator = A_ENEMYHITPAUSE;
        G.hit_release = 0;
        return;
    }
    if (pr != 2)
        G.enemy_found++;
    if (pr == 0x3C) {
        /* Ross */
        play_sample(0xA0, NULL);
        o->animator = A_ROSSDIE;
        o->counter = 0;
        o->gv[0] = 0;
        o->gv[1] = 0;
        o->gv[2] = (int16_t)o->x;
        return;
    }
    if (pr == 0x66) {
        /* the dragon: its head is struck off and flies away */
        play_sample(0x85, NULL);
        G.boss->animator = A_GENERICBEGINJUMP;
        G.boss->yspeed = -10000;
        G.boss->xspeed = 0x4000;
        o->gv[4] = 0x78;
        o->gv[0] = 0;
        o->gv[1] = 0;
        o->gv[2] = 0;
        o->counter = 0;
        return;
    }
    if (pr == 0x47) {
        /* the goblin boss */
        sound_link_kill(o->gv[4]);
        o->gv[4] = (int16_t)play_sample(0x4C, NULL);
        G.boss->animator = A_BOSSHEADDIE;
        G.boss->counter = 0;
        o->gv[1] = 0;
        return;
    }
    if (pr == 0x4A && o == G.boss) {
        /* the skeleton boss */
        G.boss->animator = A_SKELDIE;
        G.boss->counter = 0;
        G.boss->gv[1] = 0;
        G.boss->gv[2] = 0;
        return;
    }
    if (pr == 0x4C) {
        /* the demon: its boars die and its spikes go */
        obj_t *b = G.boss;
        b->animator = A_DEMONDIE;
        b->counter = 0;
        b->flags &= 0xFFEF;
        b->gv[0] = 0;
        b->gv[1] = 0;
        b->gv[3] = (int16_t)o->x;
        b->gv[4] = (int16_t)o->y;
        b->gv[2] = (int16_t)play_sample(0xA1, NULL);
        for (obj_t *p = G.list[LIST_MAIN], *next; p; p = next) {
            next = p->next;
            if (p->priority == 0x3B) {
                p->gv[3] = (int16_t)(p->gv[3] + 3);
                enemy_hit(p, 2);
            } else if (p->priority == 0x65) {
                p->image = G.img[IMG_SWORD3];
                p->animator = A_OFFKILLER;
            }
        }
        return;
    }
    if (pr == 2) {
        o->old_animator = o->animator;
        o->animator = A_ENEMYHITPAUSE;
        dropped = (uint8_t)o->gv[0];
        G.statue_found++;
    } else {
        play_sample(pr == 0x36 || pr == 0x29 ? 0x86 : pr == 0x4E ? 0x92 : pr == 0x32 ? 0x8B
                    : pr == 0x34 ? 0x7D : pr == 0x96 ? 0x73 : pr == 0x35 ? 0x88 : 0x87, NULL);
        obj_t *b = spawn_obj(0x21, LIST_FORE, o->image, (uint16_t)(o->x - 5),
                             (uint16_t)(o->y - 5));
        b->image = G.img[IMG_SWORD3];
        b->gv[0] = (int16_t)(o->ysize << 1);
        if (pr == 0x29) {
            b->gv[1] = 1;
            obj_t *p = spawn_obj(0x24, LIST_FORE, G.img[IMG_SWORD3], o->x, o->y);
            p->xsize = o->xsize;
            p->ysize = o->ysize;
            p->gv[0] = 0;
            p->gv[1] = (int16_t)(o->ysize >> 1);
            p->gv[2] = 2;
            p->animator = A_SPLASHSPAWNER;
        } else if (pr == 0x4E) {
            /* the cat skeleton falls apart into its eleven bones (thrown like debris) */
            b->gv[1] = 1;
            for (int i = 0; i < 11; i++) {
                const uint16_t x = (uint16_t)(o->x + frandom(o->xsize));
                const uint16_t y = (uint16_t)(o->y + frandom(o->ysize >> 1));
                spawn_obj(0x38, LIST_MAIN, G.img[IMG_BONES + i], x, y)->xdir = (uint8_t)frandom(2);
            }
        } else if (!(G.game_flags & 4) || pr == 0x34 || pr == 0x96 || pr == 0x4A ||
                   !G.blood_on) {
            b->gv[1] = 1; /* sparks */
        } else {
            b->gv[1] = 3; /* blood */
        }
        o->animator = A_OFFKILLER;
    }
    if (pr == 0x36 || pr == 0x4A)
        return;
    obj_t *c = spawn_obj(0x14, LIST_MAIN, G.img[IMG_COLLECT],
                         (uint16_t)(o->x + (o->xsize >> 1) - 8),
                         (uint16_t)(o->y + (o->ysize >> 1) - 8));
    if (dropped == 0) {
        dropped = (uint8_t)frandom(0x1B);
        if ((dropped == 0 && G.diff_mode < 3) || dropped == 1)
            dropped = 2;
        if (G.diff_mode == 4 && (dropped == 2 || dropped == 3))
            dropped = 5;
    }
    if (dropped == 0x20 && fire_power() > 2)
        dropped = 1;
    /* The prize: CollectID[GenericVar[1]] and its animation. */
    if (dropped == 0) {
        c->gv[1] = 0x0D; /* poison */
        c->max_frames = 2;
        c->frame_count_max = 0x3C;
    } else if (dropped == 1) {
        c->gv[1] = 0x19;
        c->anim_inc = 1;
        c->max_frames = 2;
        c->frame_count_max = 0x1E;
    } else if (dropped <= 4) {
        c->gv[1] = 10; /* a potion */
        c->max_frames = 2;
        c->frame_count_max = 0x3C;
    } else if (dropped < 0x10) {
        c->gv[1] = 0; /* a coin */
        c->max_frames = 9;
    } else if (dropped <= 0x15) {
        c->gv[1] = 0x11;
        c->max_frames = 0;
    } else if (dropped <= 0x18) {
        c->gv[1] = 0x1C;
        c->max_frames = 0;
    } else if (dropped <= 0x1A) {
        c->gv[1] = 0x10;
        c->max_frames = 0;
    } else if (dropped == 0x1B) {
        c->gv[1] = 0x16;
        c->max_frames = 2;
        c->frame_count_max = 0x3C;
    } else if (dropped <= 0x20) {
        /* the magic items a statue may hold */
        static const int8_t magic[] = {0x21, 0x23, 0x25, 0x27, 0x29};
        c->gv[1] = magic[dropped - 0x1C];
        c->max_frames = 1;
        c->frame_count_max = 0x32;
        c->flags |= OF_PLACED;
    }
    c->image = G.img[IMG_COLLECT + c->gv[1]];
    c->xspeed = 0;
}

/* RXGENERL.C:1154 ENEMYHITPAUSE: one tick of flinching: shoved up to 5 (female) / 7 (male)
 * pixels away from the player, then back to the old animator. */
int enemy_hit_pause(obj_t *o)
{
    obj_t *you = G.you;
    if (o == G.boss && you) {
        if (you->x < (uint16_t)(G.org_x + 5)) {
            you->x = (uint16_t)(G.org_x + 5);
            you->xspeed = 0;
        } else if ((uint16_t)(G.org_x + 0x13B) < you->x) {
            you->x = (uint16_t)(G.org_x + 0x13B);
            you->xspeed = 0;
        }
        if (G.level == 10 && you->x > 0x164) {
            you->x = 0x164;
            you->xspeed = 0;
        }
    }
    bool neck = false;
    for (int i = 0; i < 4; i++)
        neck = neck || o == G.neck[i];
    if (you && (o != G.boss || G.boss->priority == 0x4A) && !neck && o->priority != 0x96 &&
        o->priority != 0x29) {
        bool grounded = false;
        const int n = (G.game_flags & 4) ? 7 : 5;
        for (int i = 0; i < n; i++) {
            can_move(o);
            if (!can_go_down(o))
                grounded = true;
            if (o->up_offset > 10 && can_go_up(o))
                o->y = (uint16_t)(o->y - (o->up_offset - 10));
            if (o->down_offset > 10)
                o->y = (uint16_t)(o->y + (o->down_offset - 10));
            if (you->x < o->x && can_go_right(o) && room_right(o)) {
                o->x++;
                if (grounded && can_go_down(o))
                    o->x--; /* not off a ledge */
            } else if (o->x < you->x && can_go_left(o) && room_left(o)) {
                o->x--;
                if (grounded && can_go_down(o))
                    o->x++;
            }
        }
    }
    o->counter--;
    o->animator = o->old_animator;
    return 0;
}

/* RXGENERL.C:76 DOEXPLOSION: six frames of ExplodeID: from 0 (GenericVar[0] 0 or 2: an
 * explosion; 2 also hurts the player), 6 (1, 4: sparks) or 12 (3: blood). On the fifth some
 * kinds throw out debris, sparks or blood (SPLASHSPAWNER). */
int do_explosion(obj_t *o)
{
    if (++o->frame_no == 6) {
        obj_kill(o);
        return 0;
    }
    obj_t *p;
    const int kind = o->gv[0];
    if (kind == 1 || kind == 4) {
        obj_image(o, G.img[IMG_EXPLODE + 6 + o->frame_no]);
        if (kind != 4 || o->frame_no != 4)
            return 0;
        p = spawn_obj(0x24, LIST_FORE, G.img[IMG_EXPLODE + 4], o->x, o->y);
        p->image = G.img[IMG_SWORD3];
        p->gv[1] = 2;
        p->gv[2] = 3;
    } else if (kind != 3) {
        obj_image(o, G.img[IMG_EXPLODE + o->frame_no]);
        if (o->frame_no == 4) {
            p = spawn_obj(0x24, LIST_FORE, G.img[IMG_EXPLODE + 4], o->x, o->y);
            p->image = G.img[IMG_SWORD3];
            p->gv[1] = 3;
            p->gv[2] = 3;
            p->animator = A_SPLASHSPAWNER;
        }
        if (kind == 2)
            hit_player(o);
        return 0;
    } else {
        obj_image(o, G.img[IMG_EXPLODE + 12 + o->frame_no]);
        if (o->frame_no != 4)
            return 0;
        p = spawn_obj(0x24, LIST_FORE, G.img[IMG_EXPLODE + 17], o->x, o->y);
        p->image = NULL;
        p->gv[1] = 5;
        p->gv[2] = 1;
    }
    p->animator = A_SPLASHSPAWNER;
    return 0;
}

/* RXGENERL.C:50 EXPLODERSPAWNER: for GenericVar[0] ticks, sparks (GenericVar[1] 1) or blood
 * (3) burst at random points of its box; 4 follows the player. */
int exploder_spawner(obj_t *o)
{
    if (o->gv[1] == 4 && G.you) {
        o->x = (uint16_t)(G.you->x - 5);
        o->y = (uint16_t)(G.you->y - 5);
    }
    if (o->counter < (uint16_t)o->gv[0]) {
        const sprite_t *f = G.img[IMG_EXPLODE + (o->gv[1] == 3 ? 12 : 6)];
        const uint16_t x = (uint16_t)(o->x + frandom(o->xsize));
        const uint16_t y = (uint16_t)(o->y + frandom(o->ysize));
        spawn_obj(0x0D, LIST_MAIN, f, x, y)->gv[0] = o->gv[1];
    } else {
        obj_kill(o);
    }
    return 0;
}

/* RXGENERL.C:1362 SPLASHSPAWNER: throws out GenericVar[1] + 1 bits by kind (GenericVar[2]):
 * 0 debris, 1 blood, 2 poison, 3 sparks; from the top, the left or the right of its box in
 * turn (DS:38c2). */
int splash_spawner(obj_t *o)
{
    obj_t *p;
    switch (o->gv[2]) {
    case 0:
        p = spawn_obj(0x0E, LIST_MAIN, G.img[IMG_CAVEDROP + frandom(4)], o->x, o->y);
        p->gv[0] = 0;
        break;
    case 1: {
        uint16_t k = frandom(5);
        if (k == 5)
            k = 0;
        p = spawn_obj(0x0A, LIST_MAIN, G.img[IMG_BLOOD + k], o->x, (uint16_t)(o->y - 9));
        p->gv[0] = 2;
        p->gv[1] = 0;
        p->gv[2] = (int16_t)k;
        break;
    }
    case 2:
        p = spawn_obj(0x0E, LIST_MAIN, G.img[IMG_POISON + 3], o->x, o->y);
        p->gv[0] = 4;
        break;
    case 3:
        p = spawn_obj(0x0E, LIST_MAIN, G.img[IMG_SPARKS], o->x, (uint16_t)(o->y - 9));
        if (G.splash_side == 2) {
            p->xspeed = 0;
            p->yspeed = -15000;
        } else {
            p->xspeed = 7000;
            p->yspeed = 0;
        }
        p->gv[4] = 0x5A;
        G.splash_side = frandom(3);
        goto side;
    default:
        return 0;
    }
    p->xspeed = 0;
    p->yspeed = 0;
    p->gv[4] = 0x3C;
side:
    if (++G.splash_side == 3)
        G.splash_side = 0;
    if (G.splash_side == 0) {
        p->x = (uint16_t)(p->x + frandom(o->xsize));
        p->yspeed = (int16_t)(p->yspeed - 5000);
        p->gv[4] += 0x1E;
    } else {
        if (G.splash_side == 2)
            p->x = (uint16_t)(p->x + o->xsize);
        p->y = (uint16_t)(p->y + frandom(o->ysize));
        p->xspeed = (int16_t)(p->xspeed + 6000);
    }
    p->xspeed = (int16_t)(p->xspeed + frandom(10000));
    p->yspeed = (int16_t)(p->yspeed - frandom(4000));
    p->xdir = (uint16_t)(p->x - o->x) < (o->xsize >> 1) ? 0 : 1;
    if (o->gv[1] < ++o->gv[0])
        obj_kill(o);
    return 0;
}

/* The bosses' hit points (DS:00c6): by _DiffMode, five bytes for Ross (level 7), the dragon
 * (10), the goblin boss (16), level 20's and the demon (25). */
uint8_t boss_max_hits(int boss)
{
    static const uint8_t hits[5][5] = {
        {0x46, 0x14, 0x32, 0x0F, 0x3C}, {0x82, 0x28, 0x64, 0x14, 0x64},
        {0xAA, 0x1E, 0x78, 0x14, 0x6E}, {0xE6, 0x28, 0x96, 0x19, 0x7D},
        {0xFF, 0x2D, 0xA0, 0x1E, 0x96},
    };
    return hits[G.diff_mode < 5 ? G.diff_mode : 4][boss];
}

/* RXGENERL.C:1281 BOSSHITPOINTS: the boss's bar at the top of the view (type 0x34, BossHpID):
 * BossHpID[10] full down to [1], one step for each tenth of the boss's hits taken (its
 * GenericVar[3]), [0] once beaten; the last step blinks. */
int boss_hit_points(obj_t *o)
{
    o->x = (uint16_t)(G.org_x + 0x89);
    o->y = (uint16_t)(G.org_y + 7);
    if (o->image == G.img[IMG_BOSSHP + 1] && o->blinks == 0) {
        o->blink_color = 0x2E;
        obj_blink(o, 10, 3, 6);
    }
    const int16_t taken = G.boss->gv[3];
    if (o->gv[0] == taken)
        return 0;
    o->gv[0] = taken;
    int boss = -1;
    switch (G.level) {
    case 7: boss = 0; break;
    case 10: boss = 1; break;
    case 0x10: boss = 2; break;
    case 0x14: boss = 3; break;
    case 0x19: boss = 4; break;
    default: break;
    }
    if (boss >= 0 && taken >= boss_max_hits(boss)) {
        o->image = G.img[IMG_BOSSHP];
        return 0;
    }
    /* (On another level the original uses whatever is in its uninitialised x.) */
    const uint8_t max = boss >= 0 ? boss_max_hits(boss) : 10;
    int x = max / 10 ? (max - taken) / (max / 10) : 0;
    if (x == 0)
        x = 1;
    o->image = G.img[IMG_BOSSHP + (x < 11 ? x : 10)];
    return 0;
}

/* RXGENERL.C:171 DOCOLLECTABLE: a dropped prize (CollectID[GenericVar[1]]). It bounces once
 * when it lands; touching it collects it (DOFLYINGSHIT, priced magic items only with enough
 * gems and on the ground); after about 1000 ticks it blinks 13 times and goes. */
int do_collectable(obj_t *o)
{
    obj_t *you = G.you;
    if (o->image)
        next_frame(o, IMG_COLLECT + o->gv[1]);
    if (Inv.hit_points[0] == 0 || (uint16_t)(G.lv->rows * 16 - 0x11) < o->y || !you) {
        obj_kill(o);
        return 0;
    }
    if (obj_overlap(you, o) && !(G.game_flags & 8)) {
        const int k = o->gv[1];
        bool take = true;
        if (k == 0x21 || k == 0x23 || k == 0x25 || k == 0x29) {
            static const uint16_t msg[] = {0x200, 0x4000, 0x2000, 0x400};
            const int i = k == 0x21 ? 0 : k == 0x23 ? 1 : k == 0x25 ? 2 : 3;
            if (G.message_on)
                G.game_flags |= msg[i];
            take = (k == 0x21 ? Inv.gems > 0x18 : k == 0x23 ? Inv.gems > 0x0E
                    : k == 0x25 ? Inv.gems > 0x18 : Inv.gems > 0x31 && fire_power() < 3) &&
                   (you->gv[5] & 1);
        }
        if (take) {
            spawn_obj(0x13, LIST_SLOW, G.img[IMG_COLLECT + k], o->x, o->y)->gv[0] = (int16_t)k;
            int hint = 0;
            if (k == 0 || k == 0x10 || k == 0x11 || k == 0x19 || k == 0x1C) {
                hint = 1;
                play_sample(0x69, NULL);
            } else if (k == 10 || k == 0x16) {
                hint = 2;
                play_sample(0xA5, NULL);
            } else if (k == 0x21 || k == 0x23 || k == 0x25) {
                play_sample(0xA5, NULL);
            } else if (k == 0x29) {
                play_sample(0xB4, NULL);
            } else if (k == 0x12) {
                play_sample(0x67, NULL);
            } else if (k == 0x0D) {
                play_sample(0xA4, NULL);
                if (G.message_on)
                    G.game_flags |= 0x100;
            }
            if (hint == 1 && G.message_on)
                G.game_flags |= 0x80;
            else if (hint == 2 && G.message_on)
                G.game_flags |= 0x20;
            obj_kill(o);
            return 0;
        }
    }
    if (carry(&o->xpart, o->xspeed)) {
        if (!can_go_right(o) || !room_right(o) || !can_go_left(o) || !room_left(o)) {
            o->xspeed = 0;
            return 0;
        }
        if (o->flags & 1)
            o->x++;
        else
            o->x--;
    }
    if (!(o->gv[5] & 1))
        return 0;
    if (o->gv[2] == 0) {
        o->gv[2]++;
        o->yspeed = -5000;
        o->xspeed >>= 1;
        o->gv[4] = 100;
        generic_begin_jump(o);
        return 0;
    }
    o->xspeed = 0;
    if (o->gv[1] > 0x20)
        return 0;
    if (++o->gv[2] < 0x3E9) {
        if (o->gv[2] == 0x3CF)
            o->image = G.img[IMG_SWORD3];
        return 0;
    }
    o->gv[2] = 0x3B6;
    o->image = G.img[IMG_COLLECT + o->frame_no + o->gv[1]];
    if (++o->gv[3] >= 0x0D)
        obj_kill(o);
    return 0;
}

/* RXGENERL.C:129 DOLIGHTNING (type 0x0c): a bolt of the special item's lightning, kept where it
 * struck relative to the player (GenericVar[3] / [4]) for 30 ticks. */
int do_lightning(obj_t *o)
{
    o->x = (uint16_t)(G.you->x + o->gv[3]);
    o->y = (uint16_t)(G.you->y + o->gv[4]);
    if (o->y == 0)
        o->y = 1;
    if (o->counter > 0x1E)
        obj_kill(o);
    return 0;
}

/* RXGENERL.C:401 XLEVEL: the cheat fly mode (D + CapsLock + X; PROCESSLEVEL gives the player
 * this animator): the player can't be hurt and moves a pixel a tick with the arrows, through
 * anything, within the map; the jump key lands them (YOUBEGINRUN). */
int xlevel(obj_t *o)
{
    G.you_invincible = 1;
    if (key(K_UP) && o->y > 0x10)
        player_up(1, o);
    if (key(K_DOWN) && o->y < (uint16_t)(G.lv->rows * 16 - o->ysize - 2))
        player_down(1, o);
    if (key(K_LEFT) && o->x > 0x10)
        player_left(1, o);
    if (key(K_RIGHT) && o->x < (uint16_t)(G.lv->cols * 16 - o->xsize - 2))
        player_right(1, o);
    if (key(K_JUMP)) {
        o->animator = A_YOUBEGINRUN;
        G.you_invincible = 0;
    }
    return 0;
}

/* RXGENERL.C:1325 DOHINTS (type 0x36): a hint picture (HintLernID / HintsID) at the view's left,
 * which slides up a pixel at each carry of 0x4000 from view y 0xc6 to 0xa1, stays 0x579 ticks
 * (then another may come: _OldGameFlags 0x4000 cleared), and slides back down and goes. Turning
 * the hints off (_MessageOn) takes it away. */
int do_hints(obj_t *o)
{
    o->x = (uint16_t)(G.org_x + 0x10);
    o->y = (uint16_t)(G.org_y + o->gv[0]);
    if (!carry(&o->ypart, 0x4000))
        return 0;
    if (G.message_on) {
        if (o->gv[1] == 0) {
            if (o->gv[0] > 0xA1)
                o->gv[0]--;
            else {
                o->gv[1]++;
                o->counter = 0;
            }
            return 0;
        }
        if (o->gv[1] == 1) {
            if (o->counter >= 0x579) {
                G.old_game_flags &= 0xBFFF;
                o->gv[1]++;
            }
            return 0;
        }
        if (o->gv[0] < 0xC6) {
            o->gv[0]++;
            return 0;
        }
    }
    obj_kill(o);
    return 0;
}

/* RXNEWLVF.C:586 SPAWNHINTS (PROCESSLEVEL, while no hint is up and the hints are on): the next
 * hint the play so far calls for. On the first two levels the lessons (HintLernID) come first:
 * the controls ([4], then [6]), low health ([5]), and the first gem, statue, prize or secret
 * (_GameFlags 0x10 / 0x40 / 0x80 / 0x20, each once); then the general hints (HintsID): the exit
 * found ([3]), an extra hit point ([1]), the last ([2]), a statue too dear ([6], once), magic
 * items ([4], [5], once each), the special item ([7], once) and poison ([0], once). */
void spawn_hints(void)
{
    const sprite_t *hint = NULL;
    const uint16_t gf = G.game_flags, of = G.old_game_flags;
    if (G.level < 2) {
        const uint8_t hp = *you_hp();
        if (!(of & 0x400)) {
            G.old_game_flags |= 0x400;
            hint = G.img[IMG_HINTLERN + 4];
        } else if (!(of & 4)) {
            G.old_game_flags |= 4;
            hint = G.img[IMG_HINTLERN + 6];
        } else if (!(gf & 0x1000) && (hp == 1 || (G.diff_mode > 2 && hp == 2))) {
            G.game_flags |= 0x1000;
            hint = G.img[IMG_HINTLERN + 5];
        } else if ((gf & 0x10) && !(of & 0x10)) {
            G.old_game_flags |= 0x10;
            hint = G.img[IMG_HINTLERN];
        } else if ((gf & 0x40) && !(of & 0x40)) {
            G.old_game_flags |= 0x40;
            hint = G.img[IMG_HINTLERN + 1];
        } else if ((gf & 0x80) && !(of & 0x80)) {
            G.old_game_flags |= 0x80;
            hint = G.img[IMG_HINTLERN + 2];
        } else if ((gf & 0x20) && !(of & 0x20)) {
            G.old_game_flags |= 0x20;
            hint = G.img[IMG_HINTLERN + 3];
        }
        if (hint) {
            spawn_obj(0x36, LIST_FORE, hint, (uint16_t)(G.org_x + 0x10), (uint16_t)(G.org_y + 0xC6));
            hint = NULL;
        }
    }
    if (G.old_game_flags & 0x4000)
        return;
    const uint16_t g = G.game_flags, o = G.old_game_flags;
    if (o & 1) {
        G.old_game_flags &= 0xFFFE;
        hint = G.img[IMG_HINTS + 3];
    } else if (g & 0x800) {
        G.game_flags &= 0xF7FF;
        hint = G.img[IMG_HINTS + 1];
    } else if ((g & 0x400) && !(o & 2)) {
        G.old_game_flags |= 2;
        hint = G.img[IMG_HINTS + 6];
    } else if (g & 0x8000) {
        G.game_flags &= 0x7FFF;
        hint = G.img[IMG_HINTS + 2];
    } else if ((g & 0x200) && !(o & 0x200)) {
        G.old_game_flags |= 0x200;
        hint = G.img[IMG_HINTS + 4];
    } else if ((g & 0x2000) && !(o & 0x2000)) {
        G.old_game_flags |= 0x2000;
        hint = G.img[IMG_HINTS + 5];
    } else if ((g & 0x4000) && !(o & 0x100)) {
        G.old_game_flags |= 0x100;
        hint = G.img[IMG_HINTS + 7];
    } else if ((Inv.flags & 3) && !(g & 0x100)) {
        G.game_flags |= 0x100;
        hint = G.img[IMG_HINTS];
    }
    if (hint)
        spawn_obj(0x36, LIST_FORE, hint, (uint16_t)(G.org_x + 0x10), (uint16_t)(G.org_y + 0xC6));
}
