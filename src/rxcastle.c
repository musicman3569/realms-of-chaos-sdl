/* RXCASTLE.C: the castle's creatures: the knife and bomb throwers, their knives and bombs, the
 * bomb's explosion and the stars behind the windows. (Ross, the castle's boss, is RXROSSF.C.) */
#include "behave.h"

/* The cell under one bottom corner of an object: the floor bit (0x800) says there's ground
 * to walk onto. */
static bool floor_under(const obj_t *o, uint16_t x)
{
    return (map_cell((unsigned)(uint16_t)(o->y + o->ysize + 1) >> 4, (unsigned)x >> 4) & 0x800) != 0;
}

/* RXCASTLE.C:11 DOKNIFEGUARD: the knife-throwing cat (OF_RATE, xSpeed 7000: about 62 runs a
 * second). Every 0x79 ticks (its Counter) GenericVar[0] counts a step of its routine: at 2 it
 * may start again (1 in 4) or skip ahead (1 in 4 of the rest); at 6 it raises its arm
 * (CatKnifeID[6]), at 7 throws (CatKnifeID[7]; a DOGUARDKNIFE aimed at the player's height) and
 * at 8 lowers it. Until it throws it faces the player; while it isn't throwing, it backs away
 * from a player nearer than 100 pixels and walks towards one more than 0x74 pixels away, but
 * only onto floor. The images are set without OBJIMAGE, so its size stays frame 0's. */
int do_knife_guard(obj_t *o)
{
    can_move(o);
    if (o->down_offset > 10)
        o->y = (uint16_t)(o->y + (o->down_offset - 10));
    if (o->up_offset > 10)
        o->y = (uint16_t)(o->y - (o->up_offset - 10));
    obj_t *you = G.you;
    if (o->counter > 0x78) {
        o->gv[0]++;
        o->counter = 0;
        switch (o->gv[0]) {
        case 2:
            if (frandom(4) == 0)
                o->gv[0] = 0;
            else if (frandom(4) == 0)
                o->gv[0] = 5;
            break;
        case 6:
            o->image = G.img[IMG_CATKNIFE + 6];
            break;
        case 7: {
            play_sample(0x80, o);
            const uint16_t x = o->xdir == 0 ? (uint16_t)(o->x - 0x23) : (uint16_t)(o->x + 0x23);
            o->image = G.img[IMG_CATKNIFE + 7];
            obj_t *k = spawn_obj(0x24, LIST_MAIN, G.img[IMG_KNIFE], x, (uint16_t)(o->y + 8));
            k->frame_count_max = 5;
            k->frame_count = 5;
            k->max_frames = 7;
            k->anim_inc = 0;
            k->flags |= OF_RATE;
            k->xspeed = 0x4000;
            k->animator = A_DOGUARDKNIFE;
            /* GenericVar[0]: pixels across for each pixel up, an unsigned division of the
             * distance by the height above the player's middle (a divide error in the
             * original when it's 0, the limit of a flatter and flatter climb: the port throws
             * straight then). Above the player the divisor wraps round to a huge one, so the knife flies
             * straight too: it only ever climbs. */
            const uint16_t dx = (uint16_t)iabs16((int16_t)(o->x - you->x));
            const uint16_t dy = (uint16_t)(o->y - (uint16_t)(you->y + 8));
            k->gv[0] = (int16_t)(dy ? dx / dy : 0);
            if (k->gv[0] > 0) {
                k->gv[1] = 1;
                if (k->gv[0] < 3)
                    k->gv[0] = 3;
            } else if (k->gv[0] < 0) {
                k->gv[1] = -1;
                if (k->gv[0] > -3)
                    k->gv[0] = -3;
            } else {
                k->gv[1] = 0;
            }
            k->xdir = o->xdir;
            obj_image(k, G.img[IMG_KNIFE]);
            break;
        }
        case 8:
            o->image = G.img[IMG_CATKNIFE];
            o->gv[0] = 0;
            o->frame_no = 0;
            break;
        default:
            break;
        }
    }
    if (o->gv[0] < 7)
        o->xdir = you->x >= o->x;
    if (o->gv[0] < 6) {
        const uint16_t dist = (uint16_t)iabs16((int16_t)(o->x - you->x));
        if (dist < 100) {
            /* Back away, the walk played backwards. */
            if ((o->xdir == 0 && floor_under(o, (uint16_t)(o->x + o->xsize))) ||
                (o->xdir != 0 && floor_under(o, o->x))) {
                o->anim_inc = -1;
                if (o->frame_no == 0)
                    o->frame_no = 6;
                next_frame(o, IMG_CATKNIFE);
                if (you->x < o->x)
                    o->x++;
                else
                    o->x--;
            }
        } else if (dist > 0x74 && ((o->xdir != 0 && floor_under(o, (uint16_t)(o->x + o->xsize))) ||
                                   (o->xdir == 0 && floor_under(o, o->x)))) {
            o->anim_inc = 0;
            next_frame(o, IMG_CATKNIFE);
            if (o->x <= you->x)
                o->x++;
            else
                o->x--;
        }
    }
    hit_player(o);
    if (o->flags & OF_HIT) {
        play_sample((uint8_t)(frandom(2) + 0x8D), NULL);
        enemy_hit(o, 7);
    }
    return 0;
}

/* RXCASTLE.C:111 DOGUARDKNIFE: the thrown knife (OF_RATE, xSpeed 0x4000: 145 px/s) spins
 * through KnifeID[0..7] and flies straight, or rises a pixel every GenericVar[0] pixels across
 * (GenericVar[1] counts them: 1 up; -1 down, which the unsigned aim never gives). It goes at a
 * wall or when it hits the player. */
int do_guard_knife(obj_t *o)
{
    next_frame(o, IMG_KNIFE);
    if (obj_collide(G.you, o)) {
        hit_player(o);
        obj_kill(o);
        return 0;
    }
    if (o->gv[1] > 0) {
        if (o->gv[0] <= ++o->gv[1]) {
            o->gv[1] = 1;
            if (!can_go_up(o)) {
                obj_kill(o);
                return 0;
            }
            o->y--;
        }
    } else if (o->gv[1] < 0) {
        if (--o->gv[1] <= o->gv[0]) {
            o->gv[1] = -1;
            if (!can_go_down(o)) {
                obj_kill(o);
                return 0;
            }
            o->y++;
        }
    }
    if (o->xdir == 0) {
        if (can_go_left(o)) {
            o->x--;
            return 0;
        }
    } else if (can_go_right(o)) {
        o->x++;
        return 0;
    }
    obj_kill(o);
    return 0;
}

/* RXCASTLE.C:156 DOBOMBGUARD: the bomb-throwing cat (OF_RATE, xSpeed 0x2000). It faces the
 * player; at tick 0x4b1 of its Counter it throws a bomb (type 0x31: a GENERICBEGINJUMP at
 * xSpeed 16000, which lands on DOBOMB) with its arm up (CatBombID[1]), and at 0x579 lowers it
 * and starts again. */
int do_bomb_guard(obj_t *o)
{
    hit_player(o);
    if (o->gv[0] == 0)
        o->xdir = G.you->x >= o->x;
    if (o->counter > 0x4B0 && o->gv[0] == 0) {
        o->gv[0] = 1;
        o->image = G.img[IMG_CATBOMB + 1];
        const uint16_t x = o->xdir == 0 ? (uint16_t)(o->x - 0x23) : (uint16_t)(o->x + 0x23);
        play_sample(0x7F, o);
        play_sample(0x80, o);
        obj_t *b = spawn_obj(0x31, LIST_MAIN, G.img[IMG_PROJECTI + 2], x, (uint16_t)(o->y + 8));
        b->xdir = o->xdir;
        b->xspeed = 16000;
    }
    if (o->counter > 0x578 && o->gv[0] != 0) {
        o->counter = 0;
        o->gv[0] = 0;
        o->image = G.img[IMG_CATBOMB];
    }
    if (o->flags & OF_HIT) {
        play_sample((uint8_t)(frandom(2) + 0x8D), NULL);
        enemy_hit(o, 6);
    }
    return 0;
}

/* RXCASTLE.C:202 DOBOMB: a bomb in the air (GENERICUP / GENERICFALL call it for priority
 * 0x3a) or rolling: a pixel sideways at each carry of xSpeed, turning at walls. Touching the
 * player (or GenericVar[5] bit 0) sets it off: it turns invisible into a BOMBEXPLODER. */
int do_bomb(obj_t *o)
{
    if ((o->gv[5] & 1) || obj_collide(o, G.you)) {
        o->flags |= OF_RATE;
        o->gv[1] = 0;
        o->counter = 0;
        o->xspeed = 6000;
        o->image = NULL;
        o->animator = A_BOMBEXPLODER;
        play_sample(0xA3, NULL);
        play_sample(0x62, NULL);
        return 0;
    }
    if (!carry(&o->xpart, o->xspeed))
        return 0;
    if (o->xdir != 0) {
        if (can_go_right(o))
            o->x++;
        else
            o->xdir = 0;
    } else if (can_go_left(o)) {
        o->x--;
    } else {
        o->xdir = 1;
    }
    return 0;
}

/* RXCASTLE.C:238 BOMBEXPLODER: an invisible spawner (OF_RATE, xSpeed 9000: a run every 7
 * ticks) that throws 9 burning puffs (DOTORCHPUFF), the first standing still, the others at
 * random speeds and directions, then goes. */
int bomb_exploder(obj_t *o)
{
    if (++o->gv[1] >= 10) {
        obj_kill(o);
        return 0;
    }
    obj_t *p = spawn_obj(0x24, LIST_MAIN, G.img[IMG_EXPLODE], o->x, o->y);
    if (o->gv[1] == 1) {
        p->xspeed = 0;
        p->yspeed = 0;
    } else {
        p->xspeed = (int16_t)frandom(10000);
        p->yspeed = (int16_t)frandom(10000);
    }
    p->animator = A_DOTORCHPUFF;
    p->xdir = (uint8_t)frandom(2);
    p->max_frames = 6;
    p->anim_inc = 0;
    p->frame_count_max = 0x19;
    p->frame_count = 0x19;
    p->gv[0] = 1;
    p->gv[1] = (int16_t)frandom(2);
    return 0;
}

/* RXCASTLE.C:268 DOBACKSTARS: a twinkling star (BackStarID[0..2]) seen through the castle's
 * windows. It keeps its place in the view (GenericVar[1] / [2] from the view's corner, so it
 * hangs far away while the walls scroll past; castle1 and castle2 clamp it to the part of the
 * map that has windows) and shows only where the background tile's pixel under it is colour
 * 1, the night sky. Every GenericVar[4] ticks (20-119) it steps its frame up and down. */
int do_back_stars(obj_t *o)
{
    o->x = (uint16_t)(G.org_x + o->gv[1]);
    if (G.level < 7) {
        if (o->gv[0] == 0) {
            if (o->x >= 0x680)
                o->x = 0x684;
            else if (o->x < 0x390)
                o->x = 0x390;
            o->y = (uint16_t)(G.org_y + o->gv[2]);
            if (o->y > 200)
                o->y = 200;
        } else if (o->x > 0x2E0) {
            o->x = 0x2E0;
        }
    } else {
        o->y = (uint16_t)(G.org_y + o->gv[2]);
    }
    /* The tile's pixel, in Mode X plane order (as _BgBuffer / _Bg2Buffer / _Bg3Buffer hold
     * the three layers of 240 tiles; the port keeps them in one array). */
    const unsigned t = map_cell((unsigned)o->y >> 4, (unsigned)o->x >> 4) & 0x1FF;
    const size_t at = (size_t)t * 256 + (o->x & 3) * 0x40 + (o->y & 15) * 4 + ((o->x & 15) >> 2);
    if (at >= G.lv->bg_tiles_len || G.lv->bg_tiles[at] != 1) {
        o->image = NULL;
        return 0;
    }
    if ((uint16_t)o->gv[4] < o->counter) {
        o->counter = 0;
        if (o->xdir == 0) {
            if (--o->gv[3] == -1) {
                o->gv[3] = 0;
                o->xdir = 1;
            }
        } else if (++o->gv[3] == 3) {
            o->gv[3] = 2;
            o->xdir = 0;
        }
    }
    o->image = G.img[IMG_BACKSTAR + o->gv[3]];
    return 0;
}
