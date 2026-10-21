/* RXCAVE.C: cave objects (and the forest's stakes, which share their code). */
#include "behave.h"

/* RXCAVE.C:96 DOCAVESPIKES: spikes (stakes in the forest) on a 0x520-tick cycle. Frame 0
 * is fully up, frame 4 down. At the start of the cycle (on screen) they're up and sink
 * through frames 1-4, 16 ticks apart; from tick 0x2fc they rise again. SPIKECHECK tests the
 * player while they're up (1: ticks 0-0x1f and from 0x31c) and half-way up (0). */
int do_cave_spikes(obj_t *o)
{
    const int img = G.level < 4 ? IMG_STAKE1 : IMG_CAVESPIK;
    switch (o->counter) {
    case 1:
        if (obj_visible(o)) {
            obj_image(o, G.img[img]);
            play_sample(G.level < 4 ? 0x63 : 0x65, o);
        }
        break;
    case 0x10: case 0x31C: obj_image(o, G.img[img + 1]); break;
    case 0x20: case 0x30C: obj_image(o, G.img[img + 2]); break;
    case 0x30: obj_image(o, G.img[img + 3]); break;
    case 0x40: obj_image(o, G.img[img + 4]); break;
    case 0x2FC:
        obj_image(o, G.img[img + 3]);
        play_sample(G.level < 4 ? 0x63 : 0x66, o);
        break;
    case 0x32C: obj_image(o, G.img[img]); break;
    case 0x520: o->counter = 0; break;
    default: break;
    }
    if (o->counter < 0x20 || o->counter > 0x31B)
        spike_check(o, 1);
    else if (o->counter >= 0x30C)
        spike_check(o, 0);
    return 0;
}

/* RXCAVE.C:134 DOCAVEFALLSPIKES: a spike hanging from the ceiling (OF_RATE, a run every 16
 * ticks). Nothing happens while the player is above it. With the player within 32 pixels
 * (16 on the hard difficulties) of either side, plus the player's 14-pixel width, it rattles,
 * jiggling a pixel left and back; within half that it drops: GENERICBEGINFALL, every tick,
 * and DOPROJECTILE (kind 6) hurts the player and sticks it in the ground. */
int do_cave_fall_spikes(obj_t *o)
{
    obj_t *you = G.you;
    if (!you)
        return 0;
    if (you->y < o->y) {
        o->flags |= OF_RATE; /* (sic: "|| (Flags |= 0x40) == 0", so this always returns) */
        return 0;
    }
    const uint16_t d = G.diff_mode < 3 ? 0x20 : 0x10;
    if ((uint16_t)(o->x - d * 2) < (uint16_t)(you->x + 0x0E) &&
        you->x < (uint16_t)(o->x + o->xsize + d * 2)) {
        if (o->gv[1] != 0 && !sound_link_active(o->gv[1]))
            o->gv[1] = 0;
        if (o->gv[1] == 0)
            o->gv[1] = (int16_t)play_sample(0x53, o);
        if (o->gv[4] != 0) {
            o->gv[4] = 0;
            o->x++;
        } else if (frandom(2) != 0) {
            o->gv[4]++;
            o->x--;
        }
        if ((uint16_t)(you->x + 0x0E) <= (uint16_t)(o->x - d) ||
            (uint16_t)(o->x + o->xsize + d) <= you->x)
            return 0;
        o->flags &= 0xBF;
        o->gv[0] = 6;
        o->xspeed = 0;
        o->yspeed = 0;
        o->animator = A_GENERICBEGINFALL;
    }
    sound_link_kill(o->gv[1]);
    return 0;
}

/* RXCAVE.C:195 DOTORCHPUFF: a puff of flame or gas (ExplodeID[6..11]) drifting at xSpeed /
 * ySpeed (Flags bit 0: right, else left; GenericVar[1]: down, else up) until its sixth frame.
 * GenericVar[0] set: it burns the player. Cave holes and the lava level's mud men make them. */
int do_torch_puff(obj_t *o)
{
    next_frame(o, IMG_EXPLODE + 6);
    if (o->gv[0] != 0)
        hit_player(o);
    if (carry(&o->xpart, o->xspeed)) {
        if ((o->flags & 1) && can_go_right(o))
            o->x++;
        else if (can_go_left(o))
            o->x--;
    }
    if (carry(&o->ypart, o->yspeed)) {
        if (o->gv[1] != 0 && can_go_down(o))
            o->y++;
        else if (can_go_up(o))
            o->y--;
    }
    if (o->frame_no == 6)
        obj_kill(o);
    return 0;
}

/* RXCAVE.C:14 DODROP: water dripping in the caves (type 0x10 from level 3 on). GenericVar[0]
 * 0 is the invisible tap: 1 chance in 4000 a tick it lets a drop go (GENERICBEGINFALL): a
 * small one near the top of the map (y < 15: kind 3, gone 257 calls into its fall), else a
 * big one (y < 31: kind 2, splashes on landing; lower: kind 1, in the foreground, falling
 * until it leaves the view or nears the map's bottom). */
int do_drop(obj_t *o)
{
    if (o->gv[0] == 0) {
        o->image = NULL;
        if (frandom(4000) != 0)
            return 0;
        obj_t *d;
        if (o->y < 0x1F) {
            const bool small = o->y < 0x0F;
            d = spawn_obj(0x10, LIST_MAIN, G.img[IMG_CAVEDROP + (small ? 3 : 2)], o->x, o->y);
            d->gv[0] = small ? 3 : 2;
        } else {
            d = spawn_obj(0x10, LIST_FORE, G.img[IMG_CAVEDROP + 2], o->x, o->y);
            d->gv[0] = 1;
        }
        d->animator = A_GENERICBEGINFALL;
        d->gv[2] = 0;
        d->flags &= 0xFB;
        return 0;
    }
    if (o->gv[0] == 1) {
        if (obj_visible(o) && o->y <= (uint16_t)(G.lv->rows * 16 - 6))
            return 0;
    } else if (o->gv[0] == 2) {
        if (o->animator != A_DODROP)
            return 0;
    } else if (++o->gv[2] < 0x101) {
        return 0;
    }
    play_sample(0x56, NULL);
    obj_kill(o);
    return 0;
}

/* RXCAVE.C:80 DOTORCH (type 0x2c, level 13's torches, which INITLEVEL makes): a flame that
 * stays at view y 0x81 as the cave scrolls, flickering through TorchID. */
int do_torch(obj_t *o)
{
    o->y = (uint16_t)(G.org_y + 0x81);
    next_frame(o, IMG_TORCH);
    return 0;
}
