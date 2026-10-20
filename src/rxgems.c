/* RXGEMS.C: collected prizes and the magic gems. */
#include "behave.h"

/* RXGEMS.C:325 DOFLYINGSHIT: a collected prize (CollectID[GenericVar[0]]) flies to the top
 * left corner of the view (an eighth of the way a frame, the slow list running once a frame),
 * then counts: coins 50 / 100 / 200 / 500 / 1000 points, gems 1 or 5, a potion 2 hit points
 * (and the poison cured), a big potion all of them, poison takes one. */
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
    case 0x21: case 0x23: case 0x25:
        /* the death, lightning and shield gems (DEATHGEM, LIGHTNINGGEM, SHIELDGEM): not ported
         * yet */
        break;
    case 0x29:
        /* a stronger weapon (the original also stops the player while SWORDGEM, not ported
         * yet, shows it) */
        Inv.gems = Inv.gems < 0x33 ? 0 : (uint16_t)(Inv.gems - 0x32);
        {
            const unsigned p = (whip_power() + 1) & 3;
            Inv.flags = (uint8_t)((Inv.flags & ~0x3C) | p << 2 | p << 4);
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
