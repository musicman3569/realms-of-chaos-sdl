/* RXSWAMP.C / RXSWAMPF.C: the swamp creatures: mud men, mosquitoes, frogs, and the dragon (the
 * boss of swampb, level 10). */
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

/* The dragon (swampb, level 10). Its head (type 0x4a, priority 0x66, `_Boss` and `_Neck[0]`)
 * bobs about the room's right half; four necks (type 0x4b, `_Neck[1..4]`, spawned by
 * INITLEVEL) join it to the body at x 0x193. Each run the head writes its vertical move (-1, 0
 * or 1) into `_DragonMove[0]`, a 180-entry ring indexed by its FrameNo; neck i replays
 * `_DragonMove[i - 1]` into `_DragonMove[i]`, starting when neck i - 1's index reaches 90, so
 * every move ripples down the neck 90 runs a segment later. The necks keep within 12 pixels of
 * their neighbours and space themselves evenly between the head and the body
 * (GenericVar[4] is a fifth of the head's distance from x 0x193). */
int8_t dragon_move[4][180]; /* (not static: saved games keep it) */

static void keep_player_between(uint16_t left, uint16_t right)
{
    if (G.you->x < left) {
        G.you->x = left;
        G.you->xspeed = 0;
    } else if (G.you->x > right) {
        G.you->x = right;
        G.you->xspeed = 0;
    }
}

/* The head's bobbing (FDODRAGONBOB), also the first neck's once the head is gone
 * (FDRAGONNECKDIE, faster and bleeding). GenericVar[0] steps the vertical motion (0 pick a
 * way, Flags bit 0 down; 1 speed up; 2 cruise until 1 in 350; 3 slow down) and GenericVar[1]
 * the horizontal one the same way (GenericVar[2] the way, 1 right). */
static void dragon_bob(obj_t *o, bool head)
{
    switch (o->gv[0]) {
    case 0:
        if (!head || frandom(100) == 0) {
            if (frandom(2) == 0)
                o->flags &= 0xFFFE;
            else
                o->flags |= 1;
            o->gv[0]++;
        }
        break;
    case 1: {
        const int16_t max = head ? 7000 : 18000;
        o->yspeed = (int16_t)(o->yspeed + (head ? 0x78 : 0xF0));
        if (o->yspeed > max) {
            o->yspeed = max;
            o->gv[0]++;
        }
        break;
    }
    case 2:
        if (frandom(0x15E) == 0)
            o->gv[0]++;
        break;
    default: {
        const int16_t v = o->yspeed;
        o->yspeed = (int16_t)(v - 0x78);
        if (v < 0x78) {
            o->yspeed = 0;
            o->gv[0] = 0;
            o->flags ^= 1;
        }
        break;
    }
    }
    switch (o->gv[1]) {
    case 0:
        if (!head || frandom(100) == 0) {
            o->gv[2] = (int16_t)frandom(2);
            o->gv[1]++;
        }
        break;
    case 1: {
        const int16_t max = head ? 0x1000 : 10000;
        o->xspeed = (int16_t)(o->xspeed + (head ? 0x78 : 0xF0));
        if (o->xspeed > max) {
            o->xspeed = max;
            o->gv[1]++;
        }
        break;
    }
    case 2:
        if (frandom(400) == 0)
            o->gv[1]++;
        break;
    default: {
        const int16_t v = o->xspeed;
        o->xspeed = (int16_t)(v - 0x78);
        if (v < 0x78) {
            o->xspeed = 0;
            o->gv[1] = 0;
        }
        break;
    }
    }
    if (carry(&o->ypart, o->yspeed)) {
        if (head) {
            obj_image(o, G.img[IMG_DRAGHEAD]);
        } else if (!(o->flags & 1) && G.blood_on && frandom(2) == 0) {
            const sprite_t *image = G.img[IMG_BLOOD + frandom(4)];
            const uint16_t x = (uint16_t)(o->x + frandom(0x14) + 4);
            const uint16_t y = (uint16_t)(o->y + frandom(0x14) + 0x10);
            obj_t *b = spawn_obj(0x0A, LIST_MAIN, image, x, y);
            b->animator = A_GENERICBEGINFALL;
            b->xspeed = o->xspeed;
            b->yspeed = 0;
            b->gv[0] = 2;
            b->flags &= 0xFFFE;
        }
        if (!(o->flags & 1)) {
            if (o->y < 0x3B) {
                o->gv[0] = 0;
                o->yspeed = 0;
                o->flags |= 1;
            } else {
                o->y--;
            }
        } else if (o->y < 0x62) {
            o->y++;
        } else {
            o->gv[0] = 0;
            o->yspeed = 0;
            o->flags &= 0xFFFE;
        }
    }
    if (carry(&o->xpart, o->xspeed)) {
        if (o->gv[2] == 0 && o->x >= (head ? 0x14B : 0x15A)) {
            o->x--;
        } else if (o->gv[2] != 0 && o->x < 0x172) {
            o->x++;
        } else {
            o->gv[1] = 0;
            o->xspeed = 0;
        }
        o->gv[4] = (int16_t)(head ? (0x193 - o->x) / 5 : (0x193 - o->x) >> 2);
    }
    dragon_move[head ? 0 : 1][o->frame_no] = (int8_t)((uint8_t)o->y - (uint8_t)o->frame_count_max);
    if (++o->frame_no == 180)
        o->frame_no = 0;
    o->frame_count_max = o->y;
}

/* A hit on the head: it winces (DragHeadID[2]), roars (its sound handle is in FrameCount) and
 * the view shakes. */
static void dragon_hurt(obj_t *o)
{
    obj_image(o, G.img[IMG_DRAGHEAD + 2]);
    enemy_hit(o, boss_max_hits(1));
    sound_link_kill((int16_t)o->frame_count);
    play_sample(0x9D, o);
    spawn_shake();
}

/* RXSWAMPF.C:326 FDRAGONCENTER: the camera of level 10. Once the view passes x 0x28 the player
 * freezes (Animator NULLANIMATOR, the old one kept) and the view pans right a pixel a tick to x
 * 0xbe, where the player moves again, the boss's bar appears and SCREENCENTER takes over. It
 * also follows the player up and down like SCREENCENTER. */
int dragon_center(obj_t *o)
{
    obj_t *you = G.you;
    if (G.center_off > 0) {
        if (you->y < (uint16_t)(G.org_y + 0x4B) && G.org_y != 0) {
            scroll_up(1);
        } else if ((uint16_t)(G.org_y + G.center_off) < you->y &&
                   G.org_y < (uint16_t)(G.lv->rows * 16 - 200)) {
            scroll_down(1);
        } else if (G.window_ysize != 50) {
            set_scroll_bounds(0x32, G.window_xsize);
            G.window_ysize = 50;
        }
    }
    if (G.org_x > 0x28 && o->gv[0] == 0) {
        o->gv[0]++;
        you->old_animator = you->animator;
        you->animator = A_NULLANIMATOR;
        G.window_xsize = 0x140;
        G.window_ysize = 0x8C;
        set_scroll_bounds(0x8C, 0x140);
    }
    if (G.org_x >= 0x29 && G.org_x <= 0xBD) {
        scroll_right(1);
    } else if (G.org_x == 0xBE) {
        you->animator = you->old_animator;
        o->animator = A_SCREENCENTER;
        spawn_obj(0x34, LIST_FORE, G.img[IMG_BOSSHP + 10], (uint16_t)(G.org_x + 0x6B),
                  (uint16_t)(G.org_y + 7));
    }
    return 0;
}

/* RXSWAMPF.C:354 FDODRAGONBOB: the head waits for the camera, then keeps the player between x
 * 0xc3 and 0x142, bobs, and spits fire (DODRAGONSPIT) when its Counter reaches 200 (0x23a off
 * the default difficulty); the fire's burst sets the Counter back to 0. */
int do_dragon_bob(obj_t *o)
{
    if (G.org_x != 0xBE) {
        o->counter = 0;
        return 0;
    }
    keep_player_between(0xC3, 0x142);
    hit_player(o);
    if (o->flags & OF_HIT)
        dragon_hurt(o);
    dragon_bob(o, true);
    if (o->counter == (G.diff_mode == 2 ? 200 : 0x23A)) {
        o->animator = A_DODRAGONSPIT;
        o->xspeed = 0x4000;
        o->yspeed = 0;
        o->gv[0] = 0;
    }
    return 0;
}

/* RXSWAMPF.C:508 FDODRAGONSPIT: a run every 4 ticks (xSpeed 0x4000), GenericVar[0] counting:
 * 1-29 the head rears up and right (to x 0x172, y 0x3b), 30-59 lunges down and left with its
 * mouth open (DragHeadID[1]; to x 0x14b, y 0x62), and at 60 spits a fireball (type 0x4c) and
 * goes back to bobbing. */
int do_dragon_spit(obj_t *o)
{
    keep_player_between((uint16_t)(G.org_x + 5), 0x142);
    hit_player(o);
    if (o->flags & OF_HIT)
        dragon_hurt(o);
    if (carry(&o->xpart, o->xspeed)) {
        o->gv[0]++;
        if (o->gv[0] < 0x1E) {
            if (o->x < 0x172)
                o->x++;
            else
                o->gv[0] = 0x1E;
            if (o->y < 0x3B)
                o->gv[0] = 0x1E;
            else
                o->y--;
        } else if (o->gv[0] > 0x3B) {
            if (o->gv[0] == 0x3C) {
                obj_image(o, G.img[IMG_DRAGHEAD + 2]);
                o->xspeed = 0;
                o->gv[0] = 0;
                o->animator = A_DODRAGONBOB;
                o->frame_count = (uint16_t)play_sample(0x9A, o);
                play_sample(0x61, NULL);
                obj_t *f = spawn_obj(0x4C, LIST_MAIN, G.img[IMG_CHIMFIRE], (uint16_t)(o->x - 10),
                                     (uint16_t)(o->y + 0x1C));
                f->xspeed = 0x639C;
                f->xdir = 0;
            }
        } else {
            obj_image(o, G.img[IMG_DRAGHEAD + 1]);
            if (o->x < 0x14B)
                o->gv[0] = 0x3B;
            else
                o->x--;
            if (o->y < 0x62)
                o->y++;
            else
                o->gv[0] = 0x3B;
        }
        o->gv[4] = (int16_t)((0x193 - o->x) / 5);
    }
    dragon_move[0][o->frame_no] = (int8_t)((uint8_t)o->y - (uint8_t)o->frame_count_max);
    if (++o->frame_no == 180)
        o->frame_no = 0;
    o->frame_count_max = o->y;
    return 0;
}

/* RXSWAMPF.C:585 FDODRAGONFIRE: the fireball (type 0x4c; ChimFireID, shared with the demon's
 * level 25) flies left at its rate (xSpeed 0x639c) and drops (ySpeed 17000) until it lands,
 * where it bursts (DODRAGONBOOM) and the view shakes (except on level 25). It hurts in flight;
 * with GenericVar[0] set (the demon's) it falls through and goes off screen instead. */
int do_dragon_fire(obj_t *o)
{
    if (carry(&o->xpart, o->xspeed)) {
        next_frame(o, IMG_CHIMFIRE);
        if (o->xdir == 0)
            o->x--;
        else
            o->x++;
    }
    if (carry(&o->ypart, o->yspeed)) {
        can_move(o);
        if (o->ydir == 0) {
            o->y--;
        } else if (o->down_offset == 1 || o->gv[0] != 0) {
            o->y++;
        } else {
            play_sample(0x62, NULL);
            o->y = (uint16_t)(o->y - 10);
            o->animator = A_DODRAGONBOOM;
            o->frame_no = 0;
            o->max_frames = 5;
            o->frame_count_max = 0x1E;
            o->frame_count = 0x1E;
            o->anim_inc = 0;
            obj_image(o, G.img[IMG_CHIMBOOM]);
            if (G.level != 0x19)
                spawn_shake();
        }
    }
    if (o->gv[0] == 0)
        hit_player(o);
    else if (!obj_visible(o))
        obj_kill(o);
    return 0;
}

/* RXSWAMPF.C:630 FDODRAGONBOOM: the fireball's burst (ChimBoomID) hurts; as it ends (frame 5)
 * frogs leap out of the swamp (type 0x3e, as GRABSTUFF's spawner, up to _MaxMonsterNumber),
 * left of the player or more often right of it, and the head's Counter starts again. */
int do_dragon_boom(obj_t *o)
{
    next_frame(o, IMG_CHIMBOOM);
    hit_player(o);
    if (o->frame_no != 5 || o->frame_count != 2)
        return 0;
    if (G.level != 0x19) {
        obj_t *you = G.you;
        while (G.monster_number < G.max_monster_number && G.monster_counter == 0) {
            const uint16_t rel = (uint16_t)(you->x - G.org_x);
            uint16_t x;
            if ((rel < 0x33 ? 1 : frandom(3)) == 0)
                x = (uint16_t)(G.org_x + frandom((uint16_t)(rel - 0x14)));
            else
                x = (uint16_t)(you->x + frandom((uint16_t)(G.org_x + 0x10E - you->x)) + 0x1E);
            if (G.org_x < x && x < (uint16_t)(G.lv->cols * 16 - 0x29)) {
                G.monster_number++;
                obj_t *f = spawn_obj(0x3E, LIST_FORE, G.img[IMG_FROG + 2], x,
                                     (uint16_t)(G.org_y + 0xB8));
                f->xdir = you->x < x ? 0 : 1;
                f->gv[1] = 1;
                f->gv[2] = 0;
                f->surface_flags |= 8;
                G.enemy_in_level++;
            }
        }
        G.boss->counter = 0;
    }
    obj_kill(o);
    return 0;
}

/* The necks' following (FDODRAGONNECK, and FDRAGONNECKDIE for all but the first): sideways
 * towards their share of the way from the body to `lead` (the head, later the first neck) at
 * lead's rate; up and down as their parent's moves come through the delay line, held to 12
 * pixels (18 / 6 for the first neck) from the parent and 12 from the child, the lowest neck
 * between y 0x3c and 0x3f. */
static void neck_follow(obj_t *o, const obj_t *lead)
{
    const int i = o->gv[1];
    if (i < 1 || i > 4)
        return;
    const obj_t *parent = G.neck[i - 1];
    const obj_t *child = i < 4 ? G.neck[i + 1] : NULL;
    if (carry(&o->xpart, lead->xspeed)) {
        const uint16_t x = (uint16_t)(0x193 - lead->gv[4] * (4 - i));
        if (o->x < x)
            o->x++;
        else if (o->x > x)
            o->x--;
    }
    if (o->gv[0] == 0 && parent->frame_no == 90)
        o->gv[0]++;
    const int8_t m = o->gv[0] ? dragon_move[i - 1][o->frame_no] : 0;
    bool up = false, down = false;
    if (o->gv[0] && m == 1)
        down = true;
    else if ((uint16_t)(parent->y - (i == 1 ? 0x12 : 0x0C)) == o->y)
        down = true;
    else if ((o->gv[0] && m == -1) || (uint16_t)(parent->y + (i == 1 ? 6 : 0x0C)) == o->y)
        up = true;
    if (up) {
        if (o->max_frames == 0 &&
            (i == 4 ? o->y >= 0x3C : o->y > (uint16_t)(child->y - 0x0C)))
            o->y--;
    } else if (down) {
        if (i == 4 ? o->y <= 0x3E : o->y < (uint16_t)(child->y + 0x0C))
            o->y++;
    }
    if (o->gv[0]) {
        if (i != 4)
            dragon_move[i][o->frame_no] = m;
        if (++o->frame_no == 180)
            o->frame_no = 0;
    }
}

/* RXSWAMPF.C:667 FDODRAGONNECK: a neck blinks and flinches with the head, and follows it; when
 * the head is struck off (it jumps away: GENERICBEGINJUMP / GENERICJUMP) every neck turns to
 * DRAGONNECKDIE, and the first shows its stump (DragNeckID[1]). */
int do_dragon_neck(obj_t *o)
{
    const obj_t *b = G.boss;
    o->blink_color = b->blink_color;
    o->blinks = b->blinks;
    o->blink_ct = b->blink_ct;
    o->draw_on = b->draw_on;
    o->draw_off = b->draw_off;
    if (b->animator == A_ENEMYHITPAUSE) {
        o->old_animator = o->animator;
        o->animator = A_ENEMYHITPAUSE;
        G.hit_release = 0;
    }
    neck_follow(o, b);
    if (b->animator == A_GENERICBEGINJUMP || b->animator == A_GENERICJUMP) {
        if (o == G.neck[1]) {
            o->image = G.img[IMG_DRAGNECK + 1];
            o->counter = 0;
        }
        o->animator = A_DRAGONNECKDIE;
    }
    return 0;
}

/* RXSWAMPF.C:727 FDRAGONNECKDIE: the headless neck thrashes, the first segment bobbing (and
 * bleeding) like the head did, the others following it. */
int dragon_neck_die(obj_t *o)
{
    keep_player_between((uint16_t)(G.org_x + 5), 0x182);
    if (o != G.neck[1])
        neck_follow(o, G.neck[1]);
    else
        dragon_bob(o, false);
    return 0;
}

/* RXSWAMPF.C:877 FDRAGONHEADDIE: the struck-off head (ENEMYHIT made it jump at -10000, flying
 * left at 0x4000) bounces once (ySpeed -8000), lands, and 2000 ticks later the level is won,
 * if HitPoints[0] (the female's) isn't 0. GENERICUP / GENERICFALL call it for priority 0x66. */
int dragon_head_die(obj_t *o)
{
    if (o->image) {
        if (carry(&o->xpart, o->xspeed))
            o->x--;
        switch (o->gv[1]) {
        case 0: o->image = G.img[IMG_DRAGHEAD]; break;
        case 0x32: o->image = G.img[IMG_DRAGHEAD + 2]; break;
        case 0x19: case 0x4B: o->image = G.img[IMG_DRAGHEAD + 1]; break;
        default: break;
        }
    }
    if (!(o->gv[5] & 1) && o->y < (uint16_t)(G.lv->rows * 16 - 9))
        return 0;
    if (o->gv[0] == 0) {
        o->gv[0] = 1;
        o->yspeed = -8000;
        o->gv[4] = 0x5A;
        o->animator = A_GENERICBEGINJUMP;
        o->xspeed = (int16_t)(o->xspeed - 100);
    } else if (o->gv[0] == 1) {
        o->gv[0] = 2;
        o->counter = 0;
        o->xspeed = 0;
    } else {
        if (o->gv[0] == 2 && o->counter > 0x7D0 && Inv.hit_points[0] != 0)
            G.quitflag = 3;
        return 0;
    }
    play_sample(0x74, o);
    return 0;
}

/* _ReflecTable (DS:0d13): the sprite sets that have an upside-down twin for the water, frame by
 * frame (its 15th entry, BothDeadID, is past the end DOREFLECTION looks at). */
static const struct {
    int parent, child, frames;
} reflectable[14] = {
    {IMG_COLLECT, IMG_U_COLLEC, 43}, {IMG_FIREBALL, IMG_U_FIREBA, 9}, {IMG_FROG, IMG_U_FROG, 3},
    {IMG_MOSQUITO, IMG_U_MOSQUI, 5}, {IMG_MUDMAN, IMG_U_MUDMAN, 4}, {IMG_STATUE, IMG_U_STATUE, 2},
    {IMG_MALATTAC, IMG_U_MALATT, 4}, {IMG_MALDUCK, IMG_U_MALDUC, 5}, {IMG_MALJUMP, IMG_U_MALJUM, 5},
    {IMG_MALWALK, IMG_U_MALWAL, 30}, {IMG_FEMATTAC, IMG_U_FEMATT, 8}, {IMG_FEMDUCK, IMG_U_FEMDUC, 4},
    {IMG_FEMJUMP, IMG_U_FEMJUM, 4}, {IMG_FEMWALK, IMG_U_FEMWAL, 30},
};

/* RXSWAMP.C:44 DOREFLECTION (type 0x5a, levels 8 and 9): an object's reflection in the swamp's
 * water. It mirrors its parent's place about the water line (y = 2 * Rows * 16 - 0x4d - the
 * parent's bottom, kept within the map's last 0x26 pixels), facing and blinking, and shows the
 * upside-down frame of the parent's image if it has one (else it keeps the last). */
int do_reflection(obj_t *o)
{
    const obj_t *p = o->parent;
    if (!p || p->dead)
        return 0;
    const uint16_t bottom = (uint16_t)(G.lv->rows * 16);
    o->x = p->x;
    o->y = (uint16_t)(bottom + (bottom - 0x4D - (p->y + p->ysize)));
    o->xdir = p->xdir;
    if (bottom < o->y)
        o->y = bottom;
    else if (o->y < (uint16_t)(bottom - 0x26))
        o->y = (uint16_t)(bottom - 0x26);
    bool found = false;
    for (int t = 0; t < 14 && !found; t++)
        for (int f = 0; f < reflectable[t].frames && !found; f++)
            if (p->image && p->image == G.img[reflectable[t].parent + f]) {
                obj_image(o, G.img[reflectable[t].child + f]);
                found = true;
            }
    if (p->blinks != 0) {
        o->blink_color = p->blink_color;
        obj_blink(o, p->blink_ct, p->draw_on, p->draw_off);
        o->blinks = p->blinks;
    }
    return 0;
}
