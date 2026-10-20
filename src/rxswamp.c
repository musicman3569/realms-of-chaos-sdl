/* RXSWAMP.C / RXSWAMPF.C: the swamp creatures: mud men, mosquitoes, frogs. */
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

/* RXSWAMPF.C:133 FDOMOSQUITO (DOMOSQUITO calls it): a mosquito (OF_RATE, xSpeed 0x2000: a
 * run every 8 ticks), buzzing (the sample's handle is kept in FrameCountMax). It flits in
 * arcs of 21 runs (GenericVar[1] 1 or 2; GenericVar[4] counts the run): up or down a pixel
 * a run and sideways by the arc's table at DS:0cfe (fast, then slowing, or the reverse),
 * towards the player (2 chances in 3) or away. Now and then, well away from the player but
 * in view, it takes aim (GenericVar[1] 5): it stores a sixteenth of the distance to the
 * player's middle in GenericVar[2] / [4], hovers 38 runs, then dives 16 runs along it,
 * hurting. One damage point kills it. */
int do_mosquito(obj_t *o)
{
    static const uint8_t arc[21] = {5, 3, 2, 2, 1, 2, 1, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0};
    obj_t *you = G.you;
    if (!you)
        return 0;
    if (o->gv[1] > 4) {
        if (o->gv[1] == 5) {
            o->gv[2] = (int16_t)((int16_t)(you->x - o->x) >> 4);
            o->gv[4] = (int16_t)((int16_t)(you->y + 0x14 - o->y) >> 4);
            o->image = G.img[IMG_MOSQUITO + 4];
            o->xdir = o->x < you->x ? 1 : 0;
            o->gv[1]++;
        }
    } else {
        if (++o->gv[0] > 1)
            o->gv[0] = 0;
        o->image = G.img[IMG_MOSQUITO + o->gv[0]];
        if (!sound_link_active((int16_t)o->frame_count_max))
            o->frame_count_max = 0;
        if (o->frame_count_max == 0 && obj_visible(o))
            o->frame_count_max = (uint16_t)play_sample(0x50, o);
        if (o->gv[1] == 0) {
            const uint8_t a = (uint8_t)frandom(3), b = (uint8_t)frandom(3);
            o->gv[1] = (int16_t)(frandom(2) + 1);
            if (a == 1)
                o->xdir = o->x < you->x ? 0 : 1;
            else
                o->xdir = o->x <= you->x ? 1 : 0;
            if (b == 1)
                o->gv[2] = o->y < you->y ? 0 : 1;
            else
                o->gv[2] = o->y <= you->y ? 1 : 0;
            o->gv[4] = 0;
        }
        can_move(o);
        if (o->gv[2] == 0) {
            if (!can_go_up(o) || o->y < 3)
                o->gv[2] = 1;
            else
                o->y--;
        } else if (o->down_offset == 0 ||
                   (uint16_t)(G.lv->rows * 16 - 0x31) <= (uint16_t)(o->y + o->ysize)) {
            o->gv[2] = 0;
        } else {
            o->y++;
        }
        const int k = o->gv[4] < 0 ? 0 : o->gv[4] > 20 ? 20 : o->gv[4];
        const uint8_t steps = o->gv[1] == 1 ? arc[k] : arc[20 - k];
        for (uint8_t i = 0; i < steps; i++) {
            if (o->xdir == 0) {
                if (can_go_left(o) && room_left(o))
                    o->x--;
                else
                    o->xdir = 1;
            } else if (can_go_right(o) && room_right(o)) {
                o->x++;
            } else {
                o->xdir = 0;
            }
        }
        if (++o->gv[4] > 0x14)
            o->gv[1] = 0;
        if (frandom(0x0B) == 5 && (uint16_t)iabs16((int16_t)(you->x - o->x)) > 0x41 &&
            (uint16_t)iabs16((int16_t)(you->y - o->y)) > 0x2D &&
            (uint16_t)(G.org_x + 0x14) < o->x &&
            (uint16_t)(o->x + o->xsize) < (uint16_t)(G.org_x + 300))
            o->gv[1] = 5;
    }
    if (o->gv[1] > 5)
        o->gv[1]++;
    if (o->gv[1] == 0x2B) {
        play_sample(0x7E, o);
        obj_image(o, G.img[IMG_MOSQUITO + (o->gv[4] < 1 ? 2 : 3)]);
    }
    if (o->gv[1] > 0x2C) {
        o->x = (uint16_t)(o->x + o->gv[2]);
        o->y = (uint16_t)(o->y + o->gv[4]);
        const uint16_t floor = (uint16_t)(G.lv->rows * 16 - 0x31);
        if ((uint16_t)(o->y + o->ysize) > floor)
            o->y = (uint16_t)(floor - o->ysize);
        if (o->gv[1] > 0x3C)
            o->gv[1] = 0;
    }
    if (!(o->flags & OF_HIT)) {
        if (o->gv[1] > 0x2C)
            hit_player(o);
    } else {
        sound_link_kill((int16_t)o->frame_count_max);
        enemy_hit(o, 1);
    }
    return 0;
}

/* RXSWAMPF.C:237 FDOFROG (DOFROG calls it): a frog (type 0x3e, priority 0x35) sitting on the
 * ground, facing the player. A frog just spawned (GenericVar[1] 1) leaps out of the water
 * first (FROGLAUNCH). Each carry of xSpeed (6000) on the ground it croaks now and then (1 in
 * 90; frame 1 for 40 runs, one of three voices by FrameCountMax), and 1 time in 40 hops
 * (GENERICBEGINJUMP, ySpeed -9000 for 0xaf ticks; FROGJUMP steers it). In between it hurts. */
int do_frog(obj_t *o)
{
    obj_t *you = G.you;
    if (o->gv[1] == 1) {
        o->animator = A_FROGLAUNCH;
        return 0;
    }
    if (carry(&o->xpart, o->xspeed)) {
        if (o->gv[5] & 1) {
            if (o->gv[0] < 1) {
                if (frandom(0x5A) == 0) {
                    o->gv[0] = 0x28;
                    obj_image(o, G.img[IMG_FROG + 1]);
                    play_sample(o->frame_count_max == 0 ? 0x77 : o->frame_count_max == 1 ? 0x78
                                                                                         : 0x76,
                                o);
                } else {
                    obj_image(o, G.img[IMG_FROG]);
                }
            } else {
                obj_image(o, G.img[IMG_FROG + 1]);
                o->gv[0]--;
            }
            if (you && you->x < o->x)
                o->xdir = 0;
            else if (you && o->x < you->x)
                o->xdir = 1;
        }
        if (frandom(0x28) == 0 && (o->gv[5] & 8) && o->gv[0] == 0) {
            play_sample(0x5D, o);
            o->animator = A_GENERICBEGINJUMP;
            o->yspeed = -9000;
            o->gv[4] = 0xAF;
            return 0;
        }
    } else {
        hit_player(o);
    }
    if (o->flags & OF_HIT)
        enemy_hit(o, 3);
    return 0;
}

/* RXSWAMPF.C:294 FFROGLAUNCH: the leap out of the water: ySpeed -20000 for 0x104 ticks. */
int frog_launch(obj_t *o)
{
    play_sample(frandom(2) == 0 ? 0x6E : 0x6F, NULL);
    o->gv[1]++;
    o->yspeed = -20000;
    o->gv[4] = 0x104;
    o->animator = A_GENERICBEGINJUMP;
    return 0;
}

/* RXSWAMPF.C:307 FROGJUMP: GENERICUP / GENERICFALL call it for frogs. In the air it shows the
 * leap frame and, on a hop (not the leap out of the water), moves sideways a pixel each
 * xSpeed carry; between steps it hurts. */
void frog_jump(obj_t *o)
{
    if (!carry(&o->xpart, o->xspeed)) {
        hit_player(o);
        return;
    }
    obj_image(o, G.img[IMG_FROG + 2]);
    can_move(o);
    if (o->gv[1] != 0)
        return;
    if (o->xdir != 0 && can_go_right(o) && room_right(o))
        o->x++;
    else if (o->xdir == 0 && can_go_left(o) && room_left(o))
        o->x--;
}

/* RXSWAMP.C:106 FROGLAND: on land (above the water line, or anywhere in the swamp boss's
 * level 10) the frog sits (DOFROG); in the water it splashes and goes. */
int frog_land(obj_t *o)
{
    if ((uint16_t)(o->y + o->ysize) < (uint16_t)(G.lv->rows * 16 - 0x27) || G.level == 10) {
        o->gv[1] = 0;
        o->image = G.img[IMG_FROG];
        o->animator = A_DOFROG;
        play_sample(0x5C, o);
        return 0;
    }
    play_sample(frandom(2) == 0 ? 0x70 : 0x71, NULL);
    obj_t *s = spawn_obj(0x24, LIST_FORE, o->image, (uint16_t)(o->x + 6), o->y);
    s->image = NULL;
    s->gv[0] = 0;
    s->gv[1] = 10;
    s->gv[2] = 0;
    s->animator = A_SPLASHSPAWNER;
    if (o->gv[2] == 0)
        G.monster_number--;
    obj_kill(o);
    return 0;
}
