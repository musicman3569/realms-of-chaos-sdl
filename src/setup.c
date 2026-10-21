/* Object creation: INITOBJ (RXOBJF.C:109) and SETUPOBJ (RXSETUPF.C).
 *
 * SETUPOBJ is a switch on an object type index (0-0x5f) that sets an object's Animator,
 * priority, speeds and animation counters. The cases here are the types the levels place
 * (INITOBJ) plus the ones the ported behaviours spawn; the others still need porting. */
#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_img(const sprite_t *image, int base)
{
    return image && image == G.img[base];
}

static int name_char(const obj_t *o, int i)
{
    return o->name && (int)strlen(o->name) > i ? (unsigned char)o->name[i] : 0;
}

void setup_obj(obj_t *o, uint8_t type)
{
    const uint8_t lv = G.level;
    switch (type) {
    case 0x04: /* the player */
        o->animator = G.lightning_flag ? A_NULLANIMATOR : A_YOUBEGINRUN;
        o->priority = 0xE6;
        o->max_frames = 5;
        o->frame_count_max = (G.game_flags & 4) ? 9 : 6;
        o->anim_inc = 0;
        o->xdir = (lv == 0x13 || lv == 0x19) ? 0 : 1;
        o->friction = 199;
        o->update = 0x1000;
        memset(o->gv, 0, sizeof o->gv);
        o->surface_flags |= 1;
        if (G.you)
            G.you->gv[5] |= 0x10;
        if (G.game_flags & 4) {
            G.you_walk = IMG_MALWALK, G.you_jump = IMG_MALJUMP, G.you_duck = IMG_MALDUCK;
            G.you_climb = IMG_MALCLIMB, G.you_attack = IMG_MALATTAC;
        } else {
            G.you_walk = IMG_FEMWALK, G.you_jump = IMG_FEMJUMP, G.you_duck = IMG_FEMDUCK;
            G.you_climb = IMG_FEMCLIMB, G.you_attack = IMG_FEMATTAC;
        }
        break;
    case 0x00:
    case 0x38: /* a blob of mud (case 0x38 jumps to case 0) */
        o->animator = A_GENERICBEGINJUMP;
        o->priority = 0x46;
        o->xspeed = (int16_t)frandom(16000);
        o->yspeed = (int16_t)-frandom(3000);
        o->gv[4] = 0x78;
        o->gv[0] = 0;
        o->gv[5] = 0;
        break;
    case 0x0E: /* debris (statue, crumbling platform) */
        o->animator = A_GENERICBEGINJUMP;
        o->priority = 0xB9;
        o->frame_count_max = 1;
        o->max_frames = 1;
        o->anim_inc = 0;
        o->surface_flags &= 0xFE;
        o->gv[0] = 1;
        o->gv[4] = 0xBE;
        o->gv[5] = 0x0F;
        break;
    case 0x05: /* a segment of the whip */
        o->animator = A_DOWHIP;
        o->priority = 0xDD;
        o->frame_no = 0;
        break;
    case 0x06: /* a fireball */
        o->flags |= OF_RATE;
        o->surface_flags &= 0xFE;
        o->xspeed = 32000;
        o->counter = 0;
        o->animator = A_DOFIREBALL;
        o->priority = 0xDC;
        o->max_frames = 2;
        o->frame_count_max = 0x0E;
        o->anim_inc = 0;
        o->update = 0x1000;
        o->gv[3] = 0;
        break;
    case 0x09: /* the gem counter: a gem, four digits */
        o->animator = A_DOGEMNUMBER;
        o->max_frames = 3;
        o->frame_count_max = 5;
        o->priority = 0xFC;
        o->update = 0x1000;
        break;
    case 0x0A: /* a drop of blood */
        o->animator = A_GENERICBEGINJUMP;
        o->priority = 0xF0;
        o->surface_flags &= 0xFE;
        o->gv[5] = 0x0F;
        break;
    case 0x0D: /* explosion, sparks, a splash of blood */
        o->flags |= OF_RATE;
        o->animator = A_DOEXPLOSION;
        o->xspeed = 2000;
        o->priority = (int16_t)(frandom(2) + 0xE6);
        o->gv[0] = 1;
        break;
    case 0x0F: /* the hit points display. (The original indexes HitPoints with _GameFlags & 4,
                * so for the male it reads the inventory's Flags byte; INITLEVEL then sets 0.) */
        o->animator = A_DOHITPOINTS;
        o->priority = 0xFC;
        o->update = 0x1000;
        o->gv[0] = (int16_t)(((G.game_flags & 4) ? Inv.flags : Inv.hit_points[0]) + 0x14);
        break;
    case 0x12: /* drops the player's gems after a hit */
        o->animator = A_DROPGEMSPAWNER;
        o->priority = 100;
        o->gv[0] = 0;
        break;
    case 0x13: /* a gem (or other prize) flying to the counter */
        o->animator = A_DOFLYINGSHIT;
        o->priority = 6;
        o->gv[1] = 0;
        break;
    case 0x14: /* a dropped prize: falls, bounces, waits to be picked up */
        o->animator = A_GENERICBEGINFALL;
        o->priority = 0xE5;
        o->frame_count_max = 0x12;
        o->gv[0] = o->gv[1] = o->gv[2] = o->gv[3] = o->gv[5] = 0;
        o->anim_inc = 0;
        o->yspeed = 0;
        break;
    case 0x21: /* spawns sparks or blood over a while */
        o->flags |= OF_RATE;
        o->xspeed = 0x960;
        o->priority = 0x67;
        o->animator = A_EXPLODERSPAWNER;
        break;
    case 0x24: /* spawns debris, blood, poison or sparks (the caller sets SPLASHSPAWNER) */
        o->priority = 0x96;
        o->gv[0] = o->gv[1] = o->gv[2] = o->gv[3] = o->gv[4] = 0;
        break;
    case 0x58: /* a splat of blood on the ground */
        o->flags |= OF_RATE;
        o->animator = A_DOBLOOD;
        o->priority = 0x1C;
        o->xspeed = 0x1E0;
        o->gv[5] |= 1;
        break;
    case 0x08: /* the camera (SCREENCENTER; some levels have their own) */
        o->flags |= OF_RATE;
        o->animator = A_SCREENCENTER;
        o->priority = 1;
        o->update = 0x1000;
        o->xspeed = 0x4000;
        o->counter = 0;
        if (lv == 0x0F) {
            o->xdir = 0;
            o->animator = A_CRUSHERCENTER;
            o->gv[0] = 1;
            G.window_xsize = 0x140;
            o->flags &= 0xBF;
        } else if (lv == 0x0A) {
            o->animator = A_DRAGONCENTER;
            o->gv[0] = 0;
        } else if (lv == 0x10) {
            o->animator = A_BOSSCENTER;
            o->gv[0] = 0;
            o->flags &= 0xBF;
            o->gv[5] &= 0xFFFE;
        } else if (lv == 0x19) {
            o->flags &= 0xBF;
            o->xspeed = 22000;
            o->animator = 0x15C7; /* DEMONCENTER */
        }
        break;
    case 0x0B: /* arrow sign */
        o->animator = A_DOARROWSIGN;
        o->priority = 0x0B;
        o->counter = 0;
        break;
    case 0x10: /* leaf (forest) / drip (caves) */
        o->animator = lv < 3 ? A_DOLEAF : A_DODROP;
        o->priority = lv < 3 ? 0xF0 : 100;
        if (lv > 2)
            o->update = 0x80;
        o->gv[0] = o->gv[1] = 0;
        o->xspeed = 0;
        break;
    case 0x15: /* statue: a hint or gem container, or a bomb ("b") */
        o->flags |= 0x10;
        if (name_char(o, 0) == 'b') {
            o->animator = A_DOCONTAINERBOMB;
        } else {
            o->animator = A_GENERICBEGINFALL;
            const int n = o->name && strlen(o->name) == 1
                              ? name_char(o, 0)
                              : name_char(o, 1) + (name_char(o, 0) - '0') * 10;
            o->gv[0] = (int16_t)(n - '0');
        }
        o->priority = 2;
        o->gv[3] = o->gv[4] = o->gv[5] = 0;
        o->gv[1] = 10;
        o->yspeed = 0;
        break;
    case 0x16: /* background stars */
        o->image = NULL;
        o->animator = A_DOBACKSTARS;
        o->priority = 0;
        o->gv[1] = (int16_t)o->x;
        o->gv[2] = (int16_t)o->y;
        o->gv[3] = 0;
        o->gv[4] = 0x32;
        o->gv[4] = (int16_t)(frandom(100) + 0x14);
        o->xdir = (uint8_t)frandom(2);
        if (lv > 10)
            break;
        if (o->y < 0x140) {
            o->x = 0x3A0;
            o->y = 200;
            o->gv[0] = 0;
            o->gv[1] -= 0x3A0;
            break;
        }
        o->gv[0] = 1;
        break;
    case 0x17: /* Ross (castle boss) */
        o->image = NULL;
        o->animator = A_ROSSWAIT;
        o->update = 0x1000;
        o->priority = 0x3C;
        o->gv[3] = 0;
        o->flags |= 0x10;
        break;
    case 0x18: /* Ross's sword (and the view shakers, which get ROSSSHAKE) */
        o->priority = 0x3D;
        o->animator = A_ROSSSWORD;
        o->update = 0x1000;
        break;
    case 0x19: /* Ross's knife */
        o->flags |= OF_RATE;
        o->xspeed = 26000;
        o->priority = 0x3E;
        o->animator = A_ROSSKNIFE;
        o->anim_inc = 0;
        o->max_frames = 7;
        o->frame_count_max = 10;
        break;
    case 0x1A: /* a stone from the ceiling Ross crashes through */
        o->priority = 0x3B;
        o->animator = A_GENERICBEGINFALL;
        o->yspeed = 4000;
        o->gv[0] = 0;
        break;
    case 0x1B: /* cave spikes, stakes */
        o->priority = 0x51;
        o->animator = A_DOCAVESPIKES;
        o->counter = 0;
        break;
    case 0x1C: /* falling spikes */
        o->animator = A_DOCAVEFALLSPIKES;
        o->priority = 0x65;
        o->flags |= OF_RATE;
        o->xspeed = 0x1000;
        o->gv[0] = 0;
        o->gv[3] = 0;
        break;
    case 0x1D: /* gargoyle */
        o->flags |= 0x10;
        o->priority = 0x96;
        o->counter = 0;
        o->animator = A_DOGARGOYLE;
        o->gv[4] = o->gv[3] = o->gv[2] = o->gv[1] = o->gv[0] = 0;
        o->gv[5] |= 1;
        break;
    case 0x1E: /* spider */
        o->flags |= OF_RATE | 0x10 | 1;
        o->priority = 0x29;
        o->animator = A_DOCAVESPIDER;
        o->xspeed = 0x1000;
        o->x = (uint16_t)((o->x & 0xFFF0) + 1);
        o->y = (uint16_t)((o->y + 2) & 0xFFF0);
        o->gv[0] = 0;
        o->gv[3] = 0;
        break;
    case 0x1F: /* bat: info "l" / "r" flies across the screen; otherwise a cave bat */
        o->flags |= 0x10;
        o->gv[4] = 0;
        if (name_char(o, 0) == 'l')
            o->xdir = 0;
        else if (name_char(o, 0) == 'r')
            o->xdir = 1;
        else
            o->gv[4] = 1;
        o->animator = A_DOBAT;
        o->priority = 0x36;
        o->xspeed = 9000;
        o->yspeed = 0;
        o->friction = 199;
        o->max_frames = 3;
        o->frame_count_max = 12;
        o->gv[1] = o->gv[2] = o->gv[0] = o->gv[3] = 0;
        o->anim_inc = 0;
        if (lv == 0x13)
            o->xdir = 1;
        break;
    case 0x20: /* wolf, dog */
        o->flags |= 0x10;
        o->surface_flags |= 1;
        o->xspeed = 16000;
        o->animator = A_WOLFRUN;
        o->priority = 0x32;
        o->max_frames = 5;
        o->anim_inc = 0;
        o->frame_count_max = 10;
        o->gv[3] = o->gv[0] = o->gv[1] = 0;
        o->xdir = lv == 0x13 ? 1 : 0;
        break;
    case 0x22: /* platforms: "v"/"h" floating, "p" pulley, else crumbling */
        o->flags |= OF_RATE;
        o->xspeed = 0x3333;
        o->update = 200;
        o->gv[3] = o->gv[5] = 0;
        o->surface_flags |= 0x10;
        if (name_char(o, 0) == 'v' || name_char(o, 0) == 'h') {
            o->animator = A_FLOATINGPLATFORM;
            o->priority = 0x16;
            o->x = (uint16_t)((o->x & 0xFFF8) + 4);
            o->gv[2] = 0;
            o->yspeed = name_char(o, 0) == 'h' ? 0x59E0 : 0x4270;
            o->ydir = (name_char(o, 1) == 'b' || name_char(o, 1) == 'r') ? 0 : 1;
        } else if (name_char(o, 0) != 'p') {
            o->priority = 0x15;
            o->animator = A_CRUMBLINGPLATFORM;
            o->frame_count_max = 0x4B;
            break;
        } else {
            o->animator = A_PULLYPLATWAIT;
            o->priority = (int16_t)(name_char(o, 4) - 0x26);
            o->gv[1] = 0;
            o->gv[2] = (int16_t)(name_char(o, 1) - '0');
        }
        /* distance in tiles, two digits */
        o->gv[4] = (int16_t)(((name_char(o, 2) - '0') * 10 + name_char(o, 3) - '0') * 16);
        break;
    case 0x23: /* bandit (cat with a club or mace) */
        o->animator = A_BANDITWALK;
        o->flags |= OF_RATE | 0x10;
        o->xspeed = lv < 4 ? 0xE74 : 7000;
        o->frame_count_max = lv < 4 ? 5 : 8;
        o->priority = 0x33;
        o->max_frames = 5;
        o->anim_inc = 0;
        o->gv[3] = 0;
        o->flags &= 0xFE;
        break;
    case 0x26: /* cat skeleton */
        o->animator = A_DOCATSKELETON;
        o->flags |= 0x10;
        o->priority = 0x4E;
        o->max_frames = 5;
        o->anim_inc = 0;
        o->frame_count_max = 5;
        o->gv[3] = 0;
        break;
    case 0x2A: /* pulley */
        o->flags |= OF_RATE;
        o->surface_flags |= 0x10;
        o->xspeed = 0;
        o->update = 200;
        o->priority = (int16_t)(name_char(o, 0) - '0');
        o->animator = A_DOPULLEY;
        o->frame_no = 0;
        o->gv[5] = 0;
        o->ypart = name_char(o, 1) == 'r' ? 1 : 0;
        o->yspeed = is_img(o->image, IMG_RPULLEY) ? 1 : 0;
        o->gv[0] = 0;
        break;
    case 0x2D: /* frog platform */
        o->flags |= 4;
        o->surface_flags |= 0x10;
        o->flags &= 0xFFFE;
        o->gv[3] = o->gv[5] = 0;
        o->priority = 9;
        o->gv[4] = 0;
        o->animator = A_FROGPLAT;
        o->gv[0] = (int16_t)o->y;
        o->yspeed = -20000;
        break;
    case 0x2E: /* cave block: "s.." square platform, else a falling one */
        o->gv[3] = o->gv[5] = 0;
        o->surface_flags |= 0x10;
        o->gv[5] |= 0x40;
        if (name_char(o, 0) == 's') {
            o->priority = 8;
            o->update = 0x1000;
            o->animator = A_DOSQUAREPLAT;
            o->gv[1] = (int16_t)(name_char(o, 1) - '0');
            if (name_char(o, 2) != 'r') {
                o->flags &= 0xFFFE;
                break;
            }
        } else {
            o->priority = 7;
            o->animator = A_DOFALLINGPLAT;
            o->gv[0] = (int16_t)o->y;
            o->gv[1] = o->gv[2] = 0;
            o->update = 200;
        }
        o->flags |= 1;
        break;
    case 0x2F: /* a spider's drop of poison (DOPROJECTILE kind 3) */
        o->priority = 0x29;
        o->animator = A_GENERICBEGINFALL;
        o->yspeed = 4000;
        o->gv[0] = 3;
        break;
    case 0x30: /* bomb-throwing cat */
        o->flags |= OF_RATE | 0x10;
        o->xspeed = 0x2000;
        o->priority = 0x3A;
        o->animator = A_DOBOMBGUARD;
        o->gv[0] = o->gv[1] = 0;
        o->gv[3] = 0;
        break;
    case 0x31: /* a cat's bomb: thrown up (and at the cat's xSpeed), then DOBOMB */
        o->gv[4] = 0x5F;
        o->yspeed = -4000;
        o->priority = 0x3A;
        o->animator = A_GENERICBEGINJUMP;
        break;
    case 0x32: /* bird */
        o->animator = A_DOBIRD;
        o->priority = 0xF0;
        o->gv[0] = o->gv[1] = 0;
        o->counter = 0;
        break;
    case 0x33: /* cave hole */
        o->flags |= OF_RATE;
        o->priority = 0x28;
        o->animator = A_DOCAVEHOLE;
        o->counter = 0;
        o->xspeed = 0x5DC;
        o->gv[0] = (int16_t)name_char(o, 0);
        o->gv[1] = 0;
        o->gv[5] |= 1;
        break;
    case 0x34: /* the boss's hit points */
        o->update = 0x1000;
        o->priority = 0xF0;
        o->animator = A_BOSSHITPOINTS;
        o->gv[0] = 0x190;
        break;
    case 0x35: /* knife-throwing cat */
        o->flags |= OF_RATE | 0x10;
        o->priority = 0x33;
        o->max_frames = 5;
        o->anim_inc = 0;
        o->gv[3] = 0;
        o->animator = A_DOKNIFEGUARD;
        o->flags &= 0xFE;
        o->xspeed = 7000;
        o->frame_count_max = 8;
        o->gv[0] = 0;
        break;
    case 0x39: /* rock monster */
        o->animator = A_DOROCKMONSTER;
        o->priority = 0x31;
        o->gv[0] = o->gv[3] = 0;
        o->frame_count_max = 6;
        o->max_frames = 5;
        o->frame_no = 0;
        o->anim_inc = 0;
        o->counter = 0;
        o->xspeed = 0xDAC;
        o->flags |= 0x10 | OF_RATE;
        o->surface_flags |= 1;
        break;
    case 0x3A: /* a rock monster split off a bigger one */
        o->animator = A_GENERICBEGINFALL;
        o->priority = 0x31;
        o->gv[0] = 1;
        o->gv[3] = 0;
        o->frame_count_max = 5;
        o->max_frames = 5;
        o->frame_no = 0;
        o->anim_inc = 0;
        o->counter = 0;
        o->xspeed = 4000;
        o->yspeed = 0;
        o->gv[5] = 0;
        o->flags |= 0x10 | OF_PLACED;
        o->surface_flags |= 1;
        break;
    case 0x3B: /* goblin */
        o->animator = A_GOBLINSTAND;
        o->priority = 0x33;
        o->gv[0] = 0;
        o->gv[1] = lv < 0x0E ? 0 : 1;
        o->gv[2] = o->gv[3] = 0;
        o->frame_count_max = 0x0C;
        o->max_frames = 5;
        o->anim_inc = 0;
        o->gv[5] = 0;
        o->flags |= 0x10;
        o->surface_flags |= 1;
        o->gv[5] |= 1;
        break;
    case 0x3C: /* mud man */
        o->flags |= OF_RATE | 0x10;
        o->animator = A_MUDMANWALK;
        o->xspeed = 0xA8C;
        o->frame_count_max = 5;
        o->counter = 0;
        o->priority = 0x34;
        o->max_frames = 3;
        o->anim_inc = 0;
        o->gv[3] = 0;
        o->gv[0] = 0;
        break;
    case 0x3D: /* mosquito */
        o->flags |= OF_RATE | 0x10;
        o->priority = 0x36;
        o->xspeed = 0x2000;
        o->animator = A_DOMOSQUITO;
        o->gv[0] = o->gv[1] = o->gv[2] = o->gv[4] = 0;
        o->gv[3] = 0;
        break;
    case 0x3E: /* a frog */
        o->flags |= 0x10;
        o->gv[3] = 0;
        o->gv[0] = 0;
        o->xspeed = 6000;
        o->priority = 0x35;
        o->animator = A_DOFROG;
        o->frame_count_max = frandom(3); /* its voice (and then the croak's sound handle) */
        o->gv[5] = 0x0F;
        break;
    case 0x3F: /* a goblin's fist or club */
        o->animator = A_GOBLINFIST;
        o->priority = 0x33;
        o->gv[2] = 0;
        o->counter = 0;
        o->gv[5] |= 1;
        break;
    case 0x40: /* goblin walls */
        o->flags |= OF_RATE;
        o->y--;
        o->xspeed = 0;
        o->update = 0x78;
        o->gv[5] = 0;
        o->animator = A_NULLANIMATOR;
        o->priority = 10;
        if (!o->name) {
            o->gv[0] = 2;
            o->x = 0x138;
        } else {
            o->gv[0] = (int16_t)(name_char(o, 0) - '0');
            o->x = o->gv[0] == 0 ? 0 : 0x130;
        }
        break;
    case 0x41: /* goblin platform 2 */
        o->flags |= OF_RATE;
        o->xspeed = 0;
        o->x = 0x108;
        o->update = 0x78;
        o->animator = A_NULLANIMATOR;
        o->priority = 0x14;
        o->gv[5] = 0;
        o->gv[5] |= 0x40;
        break;
    case 0x42: /* chain */
    case 0x43: /* chain end */
        o->surface_flags |= 0x10;
        o->priority = type == 0x42 ? 0x15 : 0x16;
        o->update = 0x78;
        o->gv[5] = 0;
        if (lv != 0x0F) {
            o->animator = type == 0x42 ? A_DOCHAIN : A_DOCHAINEND;
            break;
        }
        o->flags |= OF_RATE;
        o->xspeed = 0;
        o->x = 0x120;
        o->animator = A_NULLANIMATOR;
        break;
    case 0x45: /* raft */
        o->surface_flags |= 0x10;
        o->animator = A_SWAMPRAFT;
        o->priority = 0xF0;
        o->gv[5] = o->gv[1] = o->gv[2] = 0;
        o->y = (uint16_t)(o->y + 7);
        break;
    case 0x46: /* cave brick (falling platform) */
        o->surface_flags |= 0x10;
        o->gv[5] |= 0x40;
        o->flags &= 0xFFFE;
        o->gv[3] = 0;
        o->gv[5] = 0;
        o->gv[0] = (int16_t)o->y;
        o->gv[1] = 0;
        o->priority = 8;
        o->animator = A_DOFALLINGPLAT;
        break;
    case 0x47: /* spore hole */
        o->image = NULL;
        o->animator = A_DOSPOREHOLE;
        o->priority = 0x28;
        o->xspeed = G.diff_mode == 2 ? 0x4B0 : 900;
        o->flags |= OF_RATE;
        o->gv[0] = o->gv[1] = 0;
        break;
    case 0x48: /* a spore, or a statue bomb's flaming blob */
        o->flags |= 0x10;
        o->surface_flags &= 0xFE;
        o->animator = A_GENERICBEGINJUMP;
        o->priority = 0x39;
        o->gv[3] = 0;
        o->gv[4] = 0x78;
        o->yspeed = -32000;
        o->gv[5] = 0;
        break;
    case 0x49: /* goblin platform: "h" horizontal */
        o->surface_flags |= 0x10;
        o->update = 0x1000;
        o->animator = A_FLOATINGPLATFORM;
        o->gv[3] = o->gv[5] = o->gv[2] = 0;
        o->yspeed = 0x1000;
        if (name_char(o, 0) == 'h') {
            o->priority = 0x16;
            o->flags |= 1;
        } else {
            o->priority = 0x17;
            o->flags &= 0xFFFE;
        }
        if (!o->name || strlen(o->name) < 2)
            break;
        o->gv[5] |= 0x40;
        G.boss = o;
        break;
    case 0x4A: /* dragon head */
        o->flags |= 0x10 | 1;
        o->animator = A_DODRAGONBOB;
        o->priority = 0x66;
        o->gv[0] = o->gv[1] = o->gv[2] = 0;
        o->frame_no = 0;
        o->gv[4] = 0;
        o->frame_count_max = o->y;
        o->xspeed = o->yspeed = 0;
        o->counter = 0;
        G.boss = o;
        G.neck[0] = o;
        o->gv[3] = 0;
        break;
    case 0x4B: /* a dragon's neck (INITLEVEL makes four): _Neck[1..4], priorities 3 .. 0 */
        o->flags |= OF_PLACED;
        o->animator = A_DODRAGONNECK;
        o->priority = (int16_t)(3 - G.neck_count);
        o->gv[0] = 0;
        o->frame_no = 0;
        o->frame_count = 0;
        o->max_frames = 0;
        o->gv[1] = (int16_t)(G.neck_count + 1);
        G.neck[G.neck_count + 1] = o;
        if (++G.neck_count == 4)
            G.neck_count = 0;
        break;
    case 0x4C: /* the dragon's fireball */
        o->priority = 0xEA;
        o->animator = A_DODRAGONFIRE;
        o->frame_count_max = 3;
        o->max_frames = 3;
        o->frame_no = 0;
        o->anim_inc = 1;
        o->yspeed = 17000;
        o->ydir = 1;
        o->gv[0] = 0;
        break;
    case 0x4D: /* boss head */
        o->update = 0x1000;
        o->priority = 0x47;
        o->animator = A_NULLANIMATOR;
        o->gv[3] = 0;
        o->gv[0] = 1;
        o->gv[1] = 0;
        o->counter = 0;
        G.boss = o;
        break;
    case 0x4E: /* the goblin boss's spit (the caller sets DOSPITTLE) */
    case 0x57: /* a bit of it (GENERICBEGINJUMP, then DOSPITTLE2) */
        o->update = 0x1000;
        o->priority = type == 0x4E ? 0x48 : 0x49;
        o->gv[4] = (int16_t)(frandom(0x1E) + 0x5A);
        o->yspeed = 0;
        o->gv[5] = 0;
        break;
    case 0x4F: /* the goblin boss's spinning platform */
        o->surface_flags |= 0x10;
        o->priority = 0x46;
        o->animator = A_SPINNINGPLATFORM;
        o->gv[5] = 0;
        o->gv[0] = 0;
        o->update = 0x1000;
        o->gv[5] |= 0x40;
        break;
    case 0x51: /* boar */
        o->flags |= OF_RATE | 0x10;
        o->animator = A_DOBOAR;
        o->priority = 0x3B;
        o->max_frames = 5;
        o->frame_count_max = 5;
        o->anim_inc = 0;
        o->gv[3] = 0;
        o->xspeed = is_img(o->image, IMG_BOAR3) ? 8000 : 0x1060;
        o->counter = 0;
        o->gv[0] = o->gv[1] = o->gv[2] = 0;
        o->xdir = lv == 0x13 ? 1 : 0;
        break;
    case 0x52: /* a boar's spear */
        o->surface_flags &= 0xFE;
        o->animator = A_GENERICBEGINJUMP;
        o->priority = 0x3E;
        o->gv[4] = 0x82;
        o->yspeed = -6000;
        o->counter = 0;
        o->gv[5] = 0;
        o->gv[1] = 0;
        o->gv[2] = 0;
        o->gv[0] = 0;
        break;
    case 0x53: /* the skeleton boss and its head (INITLEVEL, level 20) */
        o->xdir = 1;
        o->update = 0x1000;
        o->animator = A_SKELWAIT;
        o->priority = 0x4A;
        o->gv[3] = 0;
        o->gv[0] = 0;
        o->gv[1] = 0;
        break;
    case 0x54: /* a rock the skeleton boss throws up */
        o->surface_flags &= 0xFE;
        o->animator = A_GENERICBEGINJUMP;
        o->priority = 0x4B;
        o->gv[5] = 0x0F;
        break;
    case 0x5B: /* the raft's front piece */
        o->animator = A_NULLANIMATOR;
        o->priority = 0x18;
        o->flags |= OF_PLACED;
        break;
    case 0x5C: /* ambient sounds */
        o->animator = A_DOAMBIENTNOISE;
        o->priority = 0x4D;
        o->gv[2] = o->gv[1] = o->gv[0] = 0;
        o->flags |= OF_RATE;
        o->xspeed = 0x2000;
        break;
    case 0x5D: /* bird dropping */
        o->gv[0] = 5;
        o->animator = A_GENERICBEGINFALL;
        o->yspeed = 0;
        o->update = 0x1000;
        o->priority = 0x19;
        break;
    case 0x5E: /* will-o'-the-wisp */
        o->flags |= 4;
        o->animator = A_DOWISP;
        o->priority = 0x98;
        o->frame_count_max = 5;
        o->gv[1] = o->gv[0] = o->gv[2] = 0;
        o->xspeed = 10000;
        break;
    case 0x5F: /* demon */
        G.boss = o;
        o->animator = A_DEMONWAIT;
        o->priority = 0x4C;
        o->gv[0] = 0;
        o->gv[1] = 0x10;
        break;
    default:
        fprintf(stderr, "roc: object type 0x%02x isn't set up yet\n", type);
        o->animator = A_NULLANIMATOR;
        break;
    }
}

/* INITOBJ: create an object from the level's list. The behaviour is picked by comparing
 * the sprite set with the _Img slots. */
void init_obj(const sprite_t *image, uint16_t x, uint16_t y, const char *info)
{
    obj_t *o = calloc(1, sizeof *o);
    if (!o) {
        fprintf(stderr, "roc: out of memory\n");
        exit(1);
    }
    o->flags = (o->flags & 0xBF) | OF_PLACED;
    o->x = x;
    o->y = y;
    o->update = 0x32;
    o->old_animator = A_NULLANIMATOR;
    int list = LIST_MAIN, type = -1;
    bool setup = true;

#define IS(name) is_img(image, IMG_##name)
    if (IS(MALWALK)) {
        G.you = o;
        setup_obj(o, 4);
        setup = false;
        const uint8_t lv = G.level;
        if (lv == 13 || lv == 16 || lv == 22 || lv == 25) {
            /* starts in mid-air */
            G.lightning_flag = 0;
            image = G.img[G.you_jump + 1];
            o->animator = 0xB628;
            o->down_offset = 1;
        } else {
            image = G.img[G.you_walk + 9];
        }
    } else if (IS(BAT)) {
        type = 0x1F, o->name = info;
    } else if (IS(CATBAND) || IS(CATMACE)) {
        type = 0x23;
    } else if (IS(CATKNIFE)) {
        type = 0x35;
    } else if (IS(CATBOMB)) {
        type = 0x30;
    } else if (IS(CATWALK)) {
        type = 0x22, o->name = info;
    } else if (IS(RPULLEY) || IS(LPULLEY)) {
        type = 0x2A, o->name = info, o->image = image;
    } else if (IS(ARROW)) {
        type = 0x0B, list = LIST_FORE;
    } else if (IS(BACKSTAR)) {
        type = 0x16;
    } else if (IS(STATUE)) {
        type = 0x15, o->name = info;
    } else if (IS(SKELCAT)) {
        type = 0x26;
    } else if (IS(CAVESPIK)) {
        type = 0x1B;
    } else if (IS(SPIDER)) {
        type = 0x1E;
    } else if (IS(FALLSPIK)) {
        type = 0x1C;
    } else if (IS(CAVEFROG)) {
        type = 0x2D;
    } else if (IS(CAVEBLOK)) {
        type = 0x2E, o->name = info;
    } else if (IS(CAVEBRIK)) {
        type = 0x46;
    } else if (IS(GARGOYLE) || IS(GOBGARG)) {
        type = 0x1D;
        if (G.level == 4)
            image = G.img[IMG_GARGOYLE + 1];
    } else if (IS(WOLF) || IS(DOG)) {
        setup_obj(o, 0x20);
        setup = false;
        o->gv[2] = !IS(WOLF);
    } else if (IS(CAVEHOLE)) {
        type = 0x33, o->name = info;
    } else if (IS(ROSSJUMP)) {
        type = 0x17, G.boss = o;
    } else if (IS(MOSQUITO)) {
        type = 0x3D;
    } else if (IS(MUDMAN)) {
        type = 0x3C;
    } else if (IS(DRAGHEAD)) {
        type = 0x4A;
    } else if (IS(GOBFIST) || IS(GOBCLUB)) {
        type = 0x3B;
    } else if (IS(MUSHSPOR)) {
        type = 0x47;
    } else if (IS(ROCKMON1)) {
        type = 0x39;
    } else if (IS(RAFT)) {
        type = 0x45;
    } else if (IS(GOBWALLS)) {
        o->name = info;
        list = name_char(o, 0) == '1' ? LIST_FORE : LIST_MAIN;
        setup_obj(o, 0x40);
        setup = false;
        if (o->gv[0] >= 0 && o->gv[0] < 2)
            image = G.img[IMG_GOBWALLS + o->gv[0]];
    } else if (IS(GOBWALL)) {
        type = 0x40, list = LIST_FORE;
    } else if (IS(CHAIN)) {
        type = 0x42;
    } else if (IS(CHAINEND)) {
        type = 0x43;
    } else if (IS(GOBPLAT2)) {
        type = 0x41;
    } else if (IS(BOAR1) || IS(BOAR3)) {
        type = 0x51;
        obj_image(o, image);
    } else if (IS(BOSSHEAD)) {
        type = 0x4D;
    } else if (IS(GOBPLAT)) {
        type = 0x49, o->name = info;
    } else if (IS(STAKE1)) {
        type = 0x1B;
    } else if (IS(LEAF) || IS(CAVEDROP)) {
        type = 0x10;
    } else if (IS(BIRD)) {
        type = 0x32;
    } else if (IS(WISP)) {
        if (G.diff_mode > 1) {
            setup_obj(o, 0x5E);
            o->frame_count_max = 0x10;
        } else {
            o->animator = A_NULLANIMATOR; /* the original leaves it unset */
        }
        setup = false;
    } else if (IS(DEMSTR)) {
        type = 0x5F;
    } else {
        o->animator = A_NULLANIMATOR;
        setup = false;
    }
#undef IS
    if (setup && type >= 0)
        setup_obj(o, (uint8_t)type);

    o->frame_no = 0;
    o->frame_count = o->frame_count_max;
    obj_image(o, image);
    if (o == G.you && G.lightning_flag)
        o->image = NULL;
    o->list = list;
    obj_link(o, list);
    if (list == LIST_FORE || list == LIST_SLOW)
        o->update = 0x1000;
}
