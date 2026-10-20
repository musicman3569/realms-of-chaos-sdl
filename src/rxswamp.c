/* RXSWAMPF.C: the swamp creatures (only the mud man so far). */
#include "behave.h"

/* RXSWAMPF.C:111 MUDMANHIT: each hit costs the mud man one more point (10 in all, 60 for the
 * lava men of level 26) and knocks blobs of mud off it (type 0x38: 2, or 6 against the
 * male's whip; 9 when it dies). */
static void mud_man_hit(obj_t *o)
{
    const bool lava = G.level == 0x1A;
    uint8_t blobs;
    if (++o->gv[3] < (lava ? 0x3C : 10)) {
        o->flags &= 0xFD;
        o->old_animator = o->animator;
        o->animator = A_ENEMYHITPAUSE;
        G.hit_release = 0;
        blobs = (G.game_flags & 4) ? 6 : 2;
    } else {
        blobs = 9;
    }
    for (uint8_t i = 0; i < blobs; i++) {
        const sprite_t *image = G.img[IMG_MUDPIECE + frandom(4) + (lava ? 4 : 0)];
        const uint16_t x = (uint16_t)(o->x + frandom((uint16_t)(o->xsize - (o->xsize >> 1))));
        const uint16_t y =
            (uint16_t)(o->y + (o->ysize >> 2) + frandom((uint16_t)(o->ysize >> 1)));
        spawn_obj(0x38, LIST_MAIN, image, x, y)->xdir = (uint8_t)frandom(2);
    }
    if (o->gv[3] > (lava ? 0x3B : 9))
        enemy_hit(o, 9);
}

/* RXSWAMPF.C:46 FMUDMANWALK (MUDMANWALK calls it): the mud man (OF_RATE, xSpeed 0xa8c: about
 * 25 steps a second) first rises out of the ground, then walks, following slopes, and turns
 * round at walls, ledges, the map's edges and every 0x835 ticks. In view it gurgles now and
 * then; on level 26 (lava men, MudManID[4..7]) it gives off burning puffs (DOTORCHPUFF). */
int mud_man_walk(obj_t *o)
{
    can_move(o);
    if (o->down_offset == 1 && o->gv[0] == 0) {
        o->y--;
        return 0;
    }
    if (o->gv[0] == 0) {
        o->gv[0] = 1;
    } else if (o->gv[0] == 1 && obj_visible(o)) {
        o->gv[4] = (int16_t)play_sample(0x7C, o);
        o->gv[0] = 2;
    } else if (obj_visible(o)) {
        if (!sound_link_active(o->gv[4]) && ++o->gv[0] > 0x32 && frandom(0x4B) == 0)
            o->gv[0] = 1;
        if ((o->counter & 1) && G.level == 0x1A) {
            const uint16_t x = (uint16_t)(o->x - 5 + frandom(0x0F));
            const uint16_t y = (uint16_t)(o->y + frandom(0x1E));
            obj_t *p = spawn_obj(0x24, LIST_MAIN, G.img[IMG_EXPLODE], x, y);
            p->xspeed = 0;
            p->yspeed = 10000;
            p->animator = A_DOTORCHPUFF;
            p->max_frames = 6;
            p->anim_inc = 0;
            p->frame_count_max = 0x19;
            p->frame_count = 0x19;
            p->gv[0] = 1;
            p->gv[1] = 0;
        }
    }
    if (o->down_offset > 10)
        o->y = (uint16_t)(o->y + (o->down_offset - 10));
    if (o->up_offset > 10)
        o->y = (uint16_t)(o->y - (o->up_offset - 10));
    if (o->xdir == 0) {
        if (can_go_left(o) && room_left(o))
            o->x--;
    } else if (can_go_right(o) && room_right(o)) {
        o->x++;
    }
    next_frame(o, IMG_MUDMAN + (G.level == 0x1A ? 4 : 0));
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
        sound_link_kill(o->gv[4]);
        play_sample(0x8A, NULL);
        mud_man_hit(o);
    }
    return 0;
}
