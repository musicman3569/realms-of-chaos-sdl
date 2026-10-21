/* RXDEAD.C: the will-o'-the-wisp (the dead lands' other behaviours call their workers in
 * RXSKELF.C: rxskel.c). */
#include "behave.h"

/* _Circle1 (468a:0000, a far data segment): 135 (dx, dy) steps round a circle about 70
 * pixels across, followed by "cc", the end mark. The x steps add up to -1, which the wisp
 * (and the shields, SHIELDGEM) make up for at the end of each lap. */
const int8_t circle1[CIRCLE_LEN] = {
    1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0,
    1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1,
    1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1,
    1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, -1, 1, 0, 1, 0, 1,
    0, 1, -1, 1, 0, 1, 0, 1, -1, 1, 0, 1, -1, 1, -1, 1, -1, 1, 0, 1,
    -1, 1, -1, 1, -1, 0, -1, 1, -1, 1, -1, 1, -1, 0, -1, 1, -1, 0, -1, 0,
    -1, 1, -1, 0, -1, 0, -1, 0, -1, 1, -1, 0, -1, 0, -1, 0, -1, 0, -1, 0,
    -1, 0, -1, -1, -1, 0, -1, 0, -1, 0, -1, -1, -1, 0, -1, 0, -1, -1, -1, 0,
    -1, -1, -1, -1, -1, -1, -1, 0, -1, -1, -1, -1, 0, -1, -1, -1, -1, -1, -1, -1,
    0, -1, -1, -1, 0, -1, 0, -1, -1, -1, 0, -1, 0, -1, 0, -1, -1, -1, 0, -1,
    0, -1, 0, -1, 0, -1, 0, -1, 0, -1, 1, -1, 0, -1, 0, -1, 0, -1, 1, -1,
    0, -1, 0, -1, 1, -1, 0, -1, 1, -1, 1, -1, 1, -1, 0, -1, 1, -1, 1, -1,
    1, 0, 1, -1, 1, -1, 1, -1, 1, 0, 1, -1, 1, 0, 1, 0, 1, -1, 1, 0,
    1, 0, 1, 0, 1, -1, 1, 0, 1, 0,
};

static obj_t *spawn_orbiter(obj_t *wisp, int16_t kind)
{
    obj_t *p = spawn_obj(0x5E, LIST_MAIN, G.img[IMG_WISP], wisp->x, wisp->y);
    p->parent = wisp;
    p->gv[1] = kind;
    p->frame_count_max = 10;
    return p;
}

/* RXDEAD.C:14 DOWISP: the will-o'-the-wisp (type 0x5e, runs at each carry of xSpeed 10000,
 * about 89 times a second) and its pieces, by GenericVar[1]:
 *   0  the wisp: on its first run it makes four lights that circle it; it flies round
 *      _Circle1 (GenericVar[2] is the place in it), either way, reversing 1 time in 50. Within
 *      160 x 75 pixels of the player it calls (sample 0x51) and sets _WispWalk, which makes
 *      the player walk towards it whatever the keys say (FYOURUN, FYOUHORIZ).
 *   1  a light swinging 10 pixels to either side of it, dropping sparks (1 in 8);
 *   2  a light swinging 12 pixels above and below it;
 *   3  a spark, falling 32 pixels and going.
 * The wisp and its lights show only every other tick (Counter odd hides them); FrameNo
 * counts 0-3 at FrameCountMax runs a frame, and the sparks use WispID[3] / [4]. */
int do_wisp(obj_t *o)
{
    if (!((o->counter & 1) && o->gv[1] < 3)) {
        const bool spark = o->gv[1] > 2;
        switch (o->frame_no) {
        case 1: o->image = G.img[IMG_WISP + (spark ? 4 : 1)]; break;
        case 3: o->image = G.img[IMG_WISP + (spark ? 4 : 2)]; break;
        default: o->image = G.img[IMG_WISP + (spark ? 3 : 0)]; break;
        }
    } else {
        o->image = NULL;
    }
    if (!carry(&o->xpart, o->xspeed))
        return 0;
    if (--o->frame_count == 0) {
        o->frame_count = o->frame_count_max;
        if (++o->frame_no > 3)
            o->frame_no = 0;
    }
    obj_t *you = G.you;
    switch (o->gv[1]) {
    case 0:
        if (o->gv[0] == 0) {
            o->gv[0] = 1;
            spawn_orbiter(o, 1);
            spawn_orbiter(o, 2);
            spawn_orbiter(o, 1)->xdir = 1;
            spawn_orbiter(o, 2)->ydir = 1;
        }
        if (o->xdir == 0) {
            if (o->gv[2] == 0) {
                o->gv[2] = CIRCLE_LEN;
                o->x--;
            }
            o->y = (uint16_t)(o->y - circle1[--o->gv[2]]);
            o->x = (uint16_t)(o->x - circle1[--o->gv[2]]);
        } else {
            o->x = (uint16_t)(o->x + circle1[o->gv[2]++]);
            o->y = (uint16_t)(o->y + circle1[o->gv[2]++]);
            if (o->gv[2] >= CIRCLE_LEN) {
                o->gv[2] = 0;
                o->x++;
            }
        }
        if (frandom(0x32) == 0)
            o->xdir = !o->xdir;
        if ((uint16_t)iabs16((int16_t)(you->x - o->x)) < 0xA0 &&
            (uint16_t)iabs16((int16_t)(you->y - o->y)) < 0x4B) {
            if (!sound_link_active(o->gv[4]))
                o->gv[4] = (int16_t)play_sample(0x51, o);
            if (o->x < you->x)
                G.wisp_walk = -1;
            else if (o->x > you->x)
                G.wisp_walk = 1;
            return 0;
        }
        sound_link_kill(o->gv[4]);
        G.wisp_walk = 0;
        break;
    case 1:
        if (o->xdir == 0) {
            if (--o->gv[2] < -10)
                o->xdir = 1;
        } else if (++o->gv[2] > 10) {
            o->xdir = 0;
        }
        o->x = (uint16_t)(o->parent->x + o->gv[2]);
        o->y = o->parent->y;
        if (frandom(8) == 0)
            spawn_obj(0x5E, LIST_MAIN, G.img[IMG_WISP + 3], o->x, (uint16_t)(o->y + 5))->gv[1] = 3;
        break;
    case 2:
        if (o->ydir == 0) {
            if (--o->gv[2] < -12)
                o->ydir = 1;
        } else if (++o->gv[2] > 12) {
            o->ydir = 0;
        }
        o->x = o->parent->x;
        o->y = (uint16_t)(o->parent->y + o->gv[2]);
        break;
    case 3:
        o->y++;
        if (++o->gv[2] > 0x20)
            obj_kill(o);
        break;
    default:
        break;
    }
    return 0;
}
