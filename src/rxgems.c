/* RXGEMS.C: collected prizes and the magic gems. */
#include "behave.h"

#include <string.h>

/* RXGEMS.C:325 DOFLYINGSHIT: a collected prize (CollectID[GenericVar[0]]) flies to the top
 * left corner of the view (an eighth of the way a frame, the slow list running once a frame),
 * then counts: coins 50 / 100 / 200 / 500 / 1000 points, gems 1 or 5, a potion 2 hit points
 * (and the poison cured), a big potion all of them, poison takes one. The priced magic items
 * start their gems: 25 the shields (SHIELDGEM), 15 the lightning (LIGHTNINGGEM), 25 the death
 * (DEATHGEM), 50 a stronger weapon (SWORDGEM's show). */
int do_flying_shit(obj_t *o)
{
    obj_t *you = G.you;
    if (o->gv[1] == 0) {
        o->gv[1] = (int16_t)((uint16_t)(o->x - G.org_x) >> 3);
        o->gv[2] = (int16_t)((uint16_t)(o->y - G.org_y) >> 3);
        if (o->gv[1] < 10)
            o->gv[1] = 10;
        if (o->gv[2] < 3)
            o->gv[2] = 3;
    }
    if ((uint16_t)(G.org_y + 0x0F) <= (uint16_t)(o->y - o->gv[2]) &&
        (uint16_t)(G.org_x + 0x0F) <= (uint16_t)(o->x - o->gv[1]) && G.quitflag != 6) {
        o->x = (uint16_t)(o->x - o->gv[1]);
        if (o->x < (uint16_t)(G.org_x + 5))
            o->x = (uint16_t)(G.org_x + 5);
        o->y = (uint16_t)(o->y - o->gv[2]);
        if (o->y < (uint16_t)(G.org_y + 5))
            o->y = (uint16_t)(G.org_y + 5);
        return 0;
    }
    const bool m = (G.game_flags & 4) != 0;
    uint8_t *hp = you_hp();
    switch (o->gv[0]) {
    case 0x00: Inv.score += 50; break;
    case 0x11: Inv.score += 100; break;
    case 0x1C: Inv.score += 200; break;
    case 0x10: Inv.score += 500; break;
    case 0x19: Inv.score += 1000; break;
    case 0x1D: Inv.gems += 5; break;
    case 0x12: Inv.gems += 1; break;
    case 0x0A: case 0x16:
        /* a potion (2 hit points) or a big one (all), and the poison cured */
        if (G.hit_pts)
            obj_blink(G.hit_pts, 0, 0, 0);
        if (Inv.flags & (m ? 1 : 2)) {
            if (you)
                obj_blink(you, 0, 0, 0);
            Inv.flags &= m ? 0xFE : 0xFD;
        }
        if (o->gv[0] == 0x0A)
            *hp = (uint8_t)(*hp + 2);
        if (o->gv[0] == 0x16 || *hp > Inv.max_hit_points[m])
            *hp = Inv.max_hit_points[m];
        break;
    case 0x21: {
        /* five shields, spread round _Circle1 from the starting places at DS:00c0 */
        static const uint8_t start[5] = {0, 54, 106, 160, 212};
        Inv.gems = Inv.gems <= 0x19 ? 0 : (uint16_t)(Inv.gems - 0x19);
        for (int i = 0; i < 5 && you; i++) {
            obj_t *s = spawn_obj(0x55, LIST_MAIN, G.img[IMG_PROJECTI], you->x,
                                 (uint16_t)(you->y + 10));
            s->gv[0] = start[i];
            s->gv[1] = 3;
            s->gv[2] = -10;
            for (int k = 0; k < start[i]; k += 2) {
                s->gv[1] = (int16_t)(s->gv[1] + circle1[k]);
                s->gv[2] = (int16_t)(s->gv[2] + circle1[k + 1]);
            }
        }
        break;
    }
    case 0x23: case 0x25:
        /* the lightning (15 gems) or the death (25) */
        if (o->gv[0] == 0x23)
            Inv.gems = Inv.gems < 0x10 ? 0 : (uint16_t)(Inv.gems - 0x0F);
        else
            Inv.gems = Inv.gems < 0x1A ? 0 : (uint16_t)(Inv.gems - 0x19);
        spawn_obj(o->gv[0] == 0x23 ? 0x56 : 0x07, LIST_FORE, o->image, G.org_x, G.org_y)->image =
            NULL;
        break;
    case 0x29:
        /* a stronger weapon, shown by SWORDGEM while the player stands still */
        Inv.gems = Inv.gems < 0x33 ? 0 : (uint16_t)(Inv.gems - 0x32);
        {
            const unsigned p = (whip_power() + 1) & 3;
            Inv.flags = (uint8_t)((Inv.flags & ~0x3C) | p << 2 | p << 4);
        }
        if (you) {
            spawn_obj(0x59, LIST_MAIN, o->image, you->x, (uint16_t)(you->y + 10))->image = NULL;
            you->old_animator = you->animator;
            you->animator = A_NULLANIMATOR;
        }
        break;
    case 0x0D:
        /* poison: a hit point now, then one at each blink of the display (DOHITPOINTS) */
        if (*hp && !G.you_invincible && you) {
            Inv.flags |= m ? 1 : 2;
            if (--*hp == 0) {
                G.game_flags |= 8;
                you->xspeed = you->xdir == 0 ? 10000 : -10000;
                you->yspeed = -14000;
                you->gv[4] = 0x5A;
                you->animator = A_GENERICBEGINJUMP;
            } else {
                you->blink_color = 0x53;
                obj_blink(you, 0x32, 3, 6);
            }
        }
        break;
    default:
        break;
    }
    obj_kill(o);
    return 0;
}

/* RXGEMS.C:294 POWERUPSPARKS: a spark flying at its xSpeed / ySpeed (a pixel at each carry)
 * until it reaches the player, or on level 25 the demon's light, where it goes. */
int power_up_sparks(obj_t *o)
{
    if (carry(&o->xpart, iabs16(o->xspeed))) {
        if (o->xspeed < 0)
            o->x--;
        else
            o->x++;
    }
    if (carry(&o->ypart, iabs16(o->yspeed))) {
        if (o->yspeed < 0)
            o->y--;
        else
            o->y++;
    }
    const obj_t *t = G.level == 0x19 ? G.boss : G.you;
    if (!t || (uint16_t)(t->x + t->xsize) < o->x || (uint16_t)(o->x + o->xsize) < t->x ||
        (uint16_t)(t->y + t->ysize) < o->y || (uint16_t)(o->y + o->ysize) < t->y)
        return 0;
    obj_kill(o);
    return 0;
}

/* A step of the palette towards a target (LIGHTNINGGEM): 5 a step, landing on it exactly. False
 * while it hasn't arrived. */
static bool towards(uint8_t *p, int target)
{
    if (*p > target) {
        if (*p < target + 5) {
            *p = (uint8_t)target;
            return true;
        }
        *p = (uint8_t)(*p - 5);
        return false;
    }
    if (*p < target) {
        if (*p <= target - 5) {
            *p = (uint8_t)(*p + 5);
            return false;
        }
        *p = (uint8_t)target;
    }
    return true;
}

/* RXGEMS.C:14 SHIELDGEM (type 0x55, the 25-gem shield: five of them): a ball (ProjectiID)
 * going round the player on _Circle1 (GenericVar[0] the place in it, GenericVar[1] / [2] the
 * offset from the player, a step at each carry of 0x4000, clockwise or not by the player's
 * facing). It flies to its place a pixel at a time (through walls only by jumping there when
 * more than 0x37 pixels off), then sticks to it (GenericVar[3]); from then on it strikes what it
 * touches, a hit point each (with sparks or blood), twice, and goes. All go when the female's
 * hit points are gone. */
int shield_gem(obj_t *o)
{
    obj_t *you = G.you;
    next_frame(o, IMG_PROJECTI);
    const uint16_t ty = (uint16_t)(you->y + o->gv[2] - (you->ysize == 0x29 ? 0 : 0x10));
    const uint16_t tx = (uint16_t)(you->x + o->gv[1]);
    if ((uint16_t)iabs16((int16_t)(ty - o->y)) > 0x37 && ty < 30000)
        o->y = ty;
    if ((uint16_t)iabs16((int16_t)(tx - o->x)) > 0x37 && tx < 30000)
        o->x = tx;
    if (o->gv[3] != 0 || carry(&o->ypart, 0x4000)) {
        can_move(o);
        if (ty < o->y) {
            if (can_go_up(o) && o->y >= 3)
                o->y--;
        } else if (o->y < ty && ty < 30000 && o->up_offset < 2 && o->down_offset == 1) {
            o->y++;
        }
        bool moved = true;
        if (tx < o->x) {
            if (can_go_left(o) && room_left(o))
                o->x--;
        } else if (o->x < tx && tx < 30000) {
            if (can_go_right(o) && room_right(o))
                o->x++;
        } else {
            moved = false;
        }
        if (moved) {
            if (o->up_offset > 10)
                o->y--;
            if (o->down_offset > 10)
                o->y++;
        }
    }
    if (o->gv[3] == 0 && tx == o->x && (uint16_t)(you->y + o->gv[2]) == o->y)
        o->gv[3]++;
    if (Inv.hit_points[0] == 0) {
        obj_kill(o);
        return 0;
    }
    if (!carry(&o->xpart, 0x4000))
        return 0;
    if (you->xdir == 0) {
        if (o->gv[0] == 0) {
            o->gv[0] = CIRCLE_LEN;
            o->gv[1]--;
        }
        o->gv[2] = (int16_t)(o->gv[2] - circle1[--o->gv[0]]);
        o->gv[1] = (int16_t)(o->gv[1] - circle1[--o->gv[0]]);
    } else {
        o->gv[1] = (int16_t)(o->gv[1] + circle1[o->gv[0]++]);
        o->gv[2] = (int16_t)(o->gv[2] + circle1[o->gv[0]++]);
        if (o->gv[0] >= CIRCLE_LEN) {
            o->gv[0] = 0;
            o->gv[1]++;
        }
    }
    if (o->gv[3] == 0 || G.hitagain < 0)
        return 0;
    for (obj_t *p = G.list[LIST_MAIN], *next; p; p = next) {
        next = p->next;
        if (!obj_collide(o, p) || !(p->flags & OF_HITTABLE) || (p->flags & OF_HIT) ||
            p->priority == 2)
            continue;
        obj_t *s = spawn_obj(0x0D, LIST_MAIN, G.img[IMG_EXPLODE + 12],
                             (uint16_t)(o->x + frandom(o->xsize)),
                             (uint16_t)(o->y + frandom(o->ysize)));
        const int16_t pr = p->priority;
        if (pr == 0x34 || pr == 0x96 || pr == 2 || pr == 0x31 || pr == 0x47 || pr == 0x4E ||
            pr == 0x29 || !G.blood_on) {
            obj_image(s, G.img[IMG_EXPLODE + 6]); /* sparks */
            s->gv[0] = 1;
            p->blink_color = 0x2E;
        } else {
            s->gv[0] = 3; /* blood */
        }
        p->gv[3]++;
        p->flags |= OF_HIT;
        play_sample(0xA9, o);
        if (++o->gv[4] == 2)
            obj_kill(o);
    }
    return 0;
}

/* RXGEMS.C:130 LIGHTNINGGEM (type 0x56, the 15-gem lightning; its own rate, 0x400): for 0xddd
 * ticks the player can't be hurt (_hitagain held at -11) while the palette flashes to its
 * negative (63 - _LevelPal, 5 a step), then it fades back and goes. A warp (_quitflag 6) ends it
 * at once. (The original then sets _SavePal[0] to 100, read elsewhere: not ported.) */
int lightning_gem(obj_t *o)
{
    if (o->counter < 0xDDD)
        G.hitagain = -0x0B;
    if (G.quitflag == 6) {
        obj_kill(o);
        if (G.hitagain < 0)
            G.hitagain = 0;
        return 0;
    }
    if (o->gv[0] == 0) {
        o->gv[0]++;
        play_sample(0xB4, NULL);
        return 0;
    }
    if (o->gv[0] != 1 && o->counter <= 0xDDD && G.quitflag == 0)
        return 0;
    bool done = true;
    uint8_t *pal = G.lv->palette;
    for (int i = 0; i < 768; i++) {
        const int target = o->gv[0] == 1 ? 0x3F - (int8_t)G.level_pal[i] : (int8_t)G.level_pal[i];
        if (!towards(&pal[i], target))
            done = false;
    }
    if (done) {
        if (o->gv[0] == 1)
            o->gv[0]++;
        else
            obj_kill(o);
    }
    return 0;
}

/* RXGEMS.C:202 DEATHGEM (type 7, the 25-gem death; its own rate, 0x400): a white flash (every
 * component 60) that fades back to _LevelPal 3 a step; at each run until then every hittable
 * creature in view (but statues) dies (GenericVar[3] 5 against 4 hits), without blood. */
int death_gem(obj_t *o)
{
    if (o->gv[0] == 0) {
        o->gv[0]++;
        play_sample(0xB4, NULL);
        memset(G.lv->palette, 0x3C, 768);
        return 0;
    }
    const uint8_t blood = G.blood_on;
    for (obj_t *p = G.list[LIST_MAIN], *next; p; p = next) {
        next = p->next;
        if ((p->flags & OF_HITTABLE) && p->priority != 2 && !p->dead && obj_visible(p)) {
            G.blood_on = 0;
            p->gv[3] = 5;
            enemy_hit(p, 4);
            G.blood_on = blood;
        }
    }
    bool done = true;
    uint8_t *pal = G.lv->palette;
    for (int i = 0; i < 768; i++) {
        if (G.level_pal[i] < pal[i]) {
            if (pal[i] < G.level_pal[i] + 3) {
                pal[i] = G.level_pal[i];
            } else {
                pal[i] = (uint8_t)(pal[i] - 3);
                done = false;
            }
        }
    }
    if (done)
        obj_kill(o);
    return 0;
}

/* RXGEMS.C:255 SWORDGEM (type 0x59; its own rate, 0x1770): the 50-gem weapon upgrade's show.
 * The player, frozen meanwhile (DOFLYINGSHIT gave them NULLANIMATOR), glows (colour 0x4a: the
 * blink restarts at every run) and can't be hurt while sparks (POWERUPSPARKS) fly into them from
 * the view's edges for 0xc00 ticks; at 0xf00 they move again. */
int sword_gem(obj_t *o)
{
    obj_t *you = G.you;
    if (o->blinks == 0) {
        you->blink_color = 0x4A;
        obj_blink(you, 0x19, 3, 3);
    }
    if (o->counter < 0xC00) {
        G.hitagain = -0x0B;
        uint16_t x, y;
        const uint16_t side = frandom(4);
        if (side < 2) {
            y = (uint16_t)(G.org_y + (side == 0 ? 10 : 0xBB));
            x = (uint16_t)(G.org_x + frandom(0x129) + 10);
        } else {
            x = (uint16_t)(G.org_x + (side == 2 ? 10 : 0x133));
            y = (uint16_t)(G.org_y + frandom(0xB1) + 10);
        }
        obj_t *s = spawn_obj(0x0E, LIST_MAIN, G.img[IMG_SPARKS + frandom(2)], x, y);
        s->xspeed = (int16_t)((you->x + frandom(you->xsize) - x) * 100);
        s->yspeed = (int16_t)((you->y + frandom(you->ysize) - y) * 100);
        s->animator = A_POWERUPSPARKS;
    }
    if (o->counter > 0xF00) {
        you->animator = you->old_animator;
        obj_kill(o);
    }
    return 0;
}
