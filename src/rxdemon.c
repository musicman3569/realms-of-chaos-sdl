/* RXDEMONF.C: the demon, the last boss (demon, level 25), and the game's ending. The DEMON*
 * animators in RXDEAD.C each call their FDEMON* worker here.
 *
 * The demon (type 0x5f, priority 0x4c, `_Boss`) is a face over the room's background: its body
 * is background tiles drawn in colours 128-239, which the palette brings out of the dark
 * (DEMONWAIT, with the PALCH table rocdemfd.txt). It slides a little after the player
 * (DEMONFACE) and runs a cycle of attacks, each in two parts kept in GenericVar[0]: a pause
 * (0) and then the attack: fireballs (DEMONSPIT), two fire boars (DEMONSUMMON), a rain of
 * spikes and potions (DEMONSPOUT) and a boulder (DEMONDROP). GenericVar[1] counts within an
 * attack and GenericVar[2] holds the roar's sound handle (the view shakes while it plays). A hit
 * makes it wince (DemStrID[4] / DemSideID[4]) and cuts its pause short. */
#include "behave.h"

#include <stdlib.h>
#include <string.h>

/* The PALCH table (rocdemfd.txt): 16 steps of colours 128-239 (336 bytes each). */
enum { PALCH_STEP = 0x150, PALCH_STEPS = 16 };

static uint8_t demon_hits(void)
{
    return boss_max_hits(4);
}

/* SETPALETTE / GETPALETTE: the port's current palette is the level's. */
static void set_palette(const uint8_t *pal)
{
    memcpy(G.lv->palette, pal, 768);
}

/* RXDEMONF.C:537 DEMONFACE: the demon looks at the player: DemStrID (straight on) within 0x33
 * pixels, else DemSideID turned towards them, and slides to x 0x9c + a sixteenth of the
 * player's place in the view. Index 0 keeps the face, which every 0x65 calls goes back to
 * frame 0, or frame 1 (the mouth open) while the roar plays; the roar also shakes the view
 * (SCROLLDOWN while _OrgY < 0x54, the camera pulling it back to 0x50). */
static void demon_face(int index)
{
    obj_t *boss = G.boss, *you = G.you;
    G.face_timer++;
    const uint16_t mid = (uint16_t)(boss->x + (boss->xsize >> 1));
    boss->xdir = mid < (uint16_t)(you->x + 7) ? 1 : 0;
    const uint16_t d = (uint16_t)iabs16((int16_t)(you->x + 7 - mid));
    const int set = d < 0x33 ? IMG_DEMSTR : IMG_DEMSIDE;
    boss->x = (uint16_t)(((uint16_t)(you->x - G.org_x) >> 4) + 0x9C);
    if (index != 0) {
        G.face_timer = 0;
        obj_image(boss, G.img[set + index]);
    }
    int frame = 0;
    if (sound_link_active(boss->gv[2])) {
        if (G.org_y < 0x54)
            scroll_down(1);
        frame = 1;
    }
    if (G.face_timer < 0x65)
        return;
    obj_image(boss, G.img[set + frame]);
    G.face_timer = 0;
}

/* The start of every attack: a hit makes it wince, ends the pause, cuts the roar and counts
 * (ENEMYHIT with the demon's hits). True when that killed it. */
static bool demon_hurt(obj_t *o)
{
    if (!(o->flags & OF_HIT))
        return false;
    demon_face(4);
    if (G.boss->gv[0] == 0)
        G.boss->gv[0] = 1;
    if (sound_link_active(G.boss->gv[2]))
        sound_link_kill(G.boss->gv[2]);
    play_sample(0x9C, G.boss);
    enemy_hit(o, demon_hits());
    return o->animator == A_DEMONDIE;
}

/* The next attack, after a roar. */
static void demon_next(obj_t *o, uint16_t animator)
{
    o->counter = 0;
    G.boss->gv[0] = 0;
    G.boss->gv[1] = 0;
    o->animator = animator;
    G.boss->gv[2] = (int16_t)play_sample(0xA1, G.boss);
}

static int count_priority(int16_t priority)
{
    int n = 0;
    for (const obj_t *p = G.list[LIST_MAIN]; p; p = p->next)
        if (p->priority == priority && !p->dead)
            n++;
    return n;
}

/* RXDEMONF.C:14 FDEMONCENTER: the camera of level 25 (its own rate, xSpeed 22000): the dead
 * zone is the whole view and the view's top goes back to y 0x50 a pixel a run. The player is
 * kept between x _OrgX + 10 and _OrgX + 300; on the harder difficulties the ground between x
 * 0xbe and 0xd7 (under the demon) hurts. */
int demon_center(obj_t *o)
{
    obj_t *you = G.you;
    if (carry(&o->xpart, o->xspeed) &&
        (G.window_xsize != 0x140 || G.window_ysize != 200 || G.org_y != 0x50)) {
        set_scroll_bounds(200, 0x140);
        G.window_xsize = 0x140;
        G.window_ysize = 200;
        if (G.org_y > 0x50)
            scroll_up(1);
        else if (G.org_y < 0x50)
            scroll_down(1);
    }
    if (!you)
        return 0;
    if (you->x < (uint16_t)(G.org_x + 10)) {
        you->x = (uint16_t)(G.org_x + 10);
        if (you->xdir == 0)
            you->xspeed = 0;
    } else if ((uint16_t)(G.org_x + 300) < you->x) {
        you->x = (uint16_t)(G.org_x + 300);
        if (you->xdir != 0)
            you->xspeed = 0;
    }
    if (0xBD < you->x && you->x < 0xD8 && (you->gv[5] & 1) && G.diff_mode > 1 && G.boss) {
        G.free_hit = 1;
        hit_player(G.boss);
    }
    return 0;
}

/* RXDEMONF.C:53 FDEMONWAIT: once the player has landed, and 500 ticks later, the demon comes
 * out of the dark: at each carry of 0x400 (about 9 a second) the level's palette takes the
 * next of the 16 PALCH steps for colours 128-239. After the last it can be hit, the bar shows
 * and it roars into its first attack (DEMONSPIT). */
int demon_wait(obj_t *o)
{
    obj_t *boss = G.boss;
    if ((G.you->gv[5] & 1) && boss->gv[0] == 0) {
        boss->gv[0] = 1;
        o->counter = 0;
    }
    if (boss->gv[0] == 0 || o->counter < 500)
        return 0;
    demon_face(0);
    if (boss->gv[0] != 1 || !carry(&o->xpart, 0x400))
        return 0;
    boss->gv[1]--;
    memcpy(G.level_pal, G.virt_pal, 768);
    if (G.torch_change && boss->gv[1] >= 0 && boss->gv[1] < PALCH_STEPS)
        memcpy(G.level_pal + 0x180, G.torch_change + boss->gv[1] * PALCH_STEP, PALCH_STEP);
    set_palette(G.level_pal);
    if (boss->gv[1] == 0) {
        o->flags |= OF_HITTABLE;
        G.enemy_in_level++;
        boss->gv[0] = 0;
        o->animator = A_DEMONSPIT;
        o->counter = 0;
        spawn_obj(0x34, LIST_FORE, G.img[IMG_BOSSHP + 10], (uint16_t)(G.org_x + 0x89),
                  (uint16_t)(G.org_y + 7));
        boss->gv[2] = (int16_t)play_sample(0xA1, boss);
    }
    return 0;
}

/* RXDEMONF.C:89 FDEMONSPIT: after a pause of 0x578 ticks, eight fireballs 300 ticks apart
 * (type 0x4c, DODRAGONFIRE: they fly at |dx| * 0xb4 towards the player and burst on landing),
 * from its mouth; then DEMONSUMMON. */
int demon_spit(obj_t *o)
{
    obj_t *boss = G.boss;
    demon_face(0);
    if (demon_hurt(o))
        return 0;
    if (boss->gv[0] == 0) {
        if (o->counter > 0x578)
            boss->gv[0] = 1;
        return 0;
    }
    if (o->counter <= 300)
        return 0;
    demon_face(2);
    boss->gv[1]++;
    o->counter = 0;
    play_sample(0x61, o);
    uint16_t x = o->x, y;
    const uint16_t bottom = (uint16_t)(o->y + o->ysize);
    bool side = false;
    for (int i = 0; i < 5; i++)
        side = side || o->image == G.img[IMG_DEMSIDE + i];
    if (side) {
        if (o->xdir != 0)
            x = (uint16_t)(x + 0x14);
        y = (uint16_t)(bottom - 0x14);
    } else {
        x = (uint16_t)(x + 10);
        y = (uint16_t)(bottom - 10);
    }
    obj_t *f = spawn_obj(0x4C, LIST_MAIN, G.img[IMG_CHIMFIRE], x, y);
    f->xspeed = (int16_t)(iabs16((int16_t)(G.you->x - f->x)) * 0xB4);
    f->xdir = o->xdir;
    if (boss->gv[1] > 7)
        demon_next(o, A_DEMONSUMMON);
    return 0;
}

/* RXDEMONF.C:143 FDEMONSUMMON: after a pause of 1000 ticks, two fire boars (type 0x51,
 * Boar3ID) walk in from the view's sides; once both are dead, or after 0x1000 ticks (when the
 * demon kills them itself), DEMONSPOUT. */
int demon_summon(obj_t *o)
{
    obj_t *boss = G.boss;
    demon_face(0);
    if (demon_hurt(o))
        return 0;
    if (boss->gv[0] == 0) {
        if (o->counter > 1000)
            boss->gv[0] = 1;
        return 0;
    }
    if (boss->gv[0] == 1) {
        o->counter = 0;
        boss->gv[0] = 2;
        G.enemy_in_level += 2;
        obj_t *b = spawn_obj(0x51, LIST_MAIN, G.img[IMG_BOAR3], (uint16_t)(G.org_x - 0x14), 0xD8);
        b->xdir = 1;
        b->xspeed = 8000;
        b = spawn_obj(0x51, LIST_MAIN, G.img[IMG_BOAR3], (uint16_t)(G.org_x + 0x140), 0xD8);
        b->xdir = 0;
        b->xspeed = 8000;
        return 0;
    }
    int boars = 0;
    for (obj_t *p = G.list[LIST_MAIN], *next; p; p = next) {
        next = p->next;
        if (p->priority == 0x3B && !p->dead) {
            boars++;
            if (o->counter > 0x1000) {
                p->gv[3] = (int16_t)(p->gv[3] + 3);
                enemy_hit(p, 2);
            }
        }
    }
    if (boars == 0 || o->counter > 0x1000)
        demon_next(o, A_DEMONSPOUT);
    return 0;
}

/* RXDEMONF.C:207 FDEMONSPOUT: after a pause of 1000 ticks, two sweeps of 13 drops from the top
 * of the view, 0x14 pixels apart (left to right, then right to left), 1 in 40 a tick after 0x50
 * / 0xb4 ticks: mostly a falling spike (type 0x1c, DemSpikID, DOPROJECTILE kind 6, which on
 * this level lands as a flame vent), sometimes a potion (1 in _DiffMode + 3, not on the
 * hardest). Once the spikes have gone (priority 0x65; checked after 0x800 ticks), DEMONDROP. */
int demon_spout(obj_t *o)
{
    obj_t *boss = G.boss;
    demon_face(0);
    if (demon_hurt(o))
        return 0;
    if (boss->gv[0] == 0) {
        if (o->counter > 1000)
            boss->gv[0] = 1;
        return 0;
    }
    if (boss->gv[0] == 1 || boss->gv[0] == 2) {
        const uint16_t wait = boss->gv[0] == 1 ? 0x50 : 0xB4;
        if (o->counter <= wait || frandom(0x28) != 0)
            return 0;
        o->counter = 0;
        const uint16_t x = boss->gv[0] == 1 ? (uint16_t)(G.org_x + boss->gv[1] * 0x14)
                                            : (uint16_t)(G.org_x + 0x122 - boss->gv[1] * 0x14);
        obj_t *p;
        if (frandom((uint16_t)(G.diff_mode + 3)) == 0 && G.diff_mode < 4) {
            p = spawn_obj(0x14, LIST_MAIN, G.img[IMG_COLLECT + 0x12], x, (uint16_t)(G.org_y - 10));
            p->max_frames = 3;
            p->frame_count_max = 0x28;
            p->xspeed = 0;
            p->yspeed = 0;
            p->gv[1] = 0x12;
        } else {
            p = spawn_obj(0x1C, LIST_MAIN, G.img[IMG_DEMSPIK], x, (uint16_t)(G.org_y - 10));
            p->flags &= 0xFFBF;
            p->gv[0] = 6;
            p->xspeed = 0;
            p->yspeed = 0;
        }
        p->animator = A_GENERICBEGINFALL;
        if (++boss->gv[1] == 0x0D) {
            boss->gv[1] = 0;
            boss->gv[0]++;
        }
        return 0;
    }
    if (o->counter > 0x800 && count_priority(0x65) == 0)
        demon_next(o, A_DEMONDROP);
    return 0;
}

/* RXDEMONF.C:279 FDEMONDROP: after a pause of 1000 ticks, a boulder (type 0x50, priority 0x97,
 * ROLLINGBOULDER) drops at the view's left edge (1 in 3) or 0x104 pixels right of it and rolls
 * across; once it has gone, DEMONSPIT again. The demon's face hurts at this stage. */
int demon_drop(obj_t *o)
{
    obj_t *boss = G.boss;
    demon_face(0);
    hit_player(o);
    if (demon_hurt(o))
        return 0;
    if (boss->gv[0] == 0) {
        if (o->counter > 1000)
            boss->gv[0] = 1;
    } else if (boss->gv[0] == 1) {
        boss->gv[0]++;
        const uint16_t x = frandom(3) != 0 ? (uint16_t)(G.org_x + 0x104) : G.org_x;
        obj_t *b = spawn_obj(0x50, LIST_MAIN, G.img[IMG_DEADRCK1], x, 0x14);
        b->xdir = x == G.org_x ? 1 : 0;
    } else if (count_priority(0x97) == 0) {
        demon_next(o, A_DEMONSPIT);
    }
    return 0;
}

/* A ball of fire flying off the dying demon (type 0x4c with GenericVar[0] 1: DODRAGONFIRE
 * lets it fly off the screen), away from its middle. */
static void spawn_death_fire(const obj_t *o, uint16_t x, uint16_t y)
{
    obj_t *f = spawn_obj(0x4C, LIST_MAIN, G.img[IMG_CHIMFIRE], x, y);
    f->gv[0] = 1;
    f->xspeed = (int16_t)frandom(30000);
    f->yspeed = (int16_t)frandom(30000);
    if ((int16_t)(f->xspeed + f->yspeed) < 10000) { /* a 16-bit sum, as in the original */
        f->xspeed = (int16_t)(f->xspeed + 5000);
        f->yspeed = (int16_t)(f->yspeed + 5000);
    }
    f->xdir = (uint16_t)(o->x + (o->xsize >> 1) - 8) < f->x ? 1 : 0;
    f->ydir = (uint16_t)(o->y + (o->ysize >> 1)) < f->y ? 1 : 0;
    play_sample(0x62, NULL);
}

/* RXDEMONF.C:330 FDEMONDIE (ENEMYHIT starts it), the ending. GenericVar[0] is the stage:
 * 0-1  the demon shakes about where it was hit (GenericVar[3] / [4]) in a random direction
 *      (GenericVar[5]) with the view, bursting into explosions and flying fire; after 4000
 *      ticks the screen fades to white (each colour a step at each carry of 0x800);
 * 2    the room is redrawn without the demon (demgone.bg replaces the level's tiles) and the
 *      two heroes stand side by side (both frozen: NULLANIMATOR);
 * 3    500 ticks later the palette fades back to the level's;
 * 4    1000 ticks later a light (ChimFireID, hidden) appears at view (0x8c, 0x3c);
 * 5    sparks fly into it from the view's edges (POWERUPSPARKS) while it grows (ChimFireID[3]
 *      at 600 ticks, blinking, to [0] at 1500);
 * 6    1000 ticks later it sinks (and drifts right) to just above the heroes' heads;
 * 7    2000 ticks later the level, and the game, is done (_quitflag 3). */
int demon_die(obj_t *o)
{
    obj_t *boss = G.boss, *you = G.you;
    uint8_t pal[768];
    if (boss->gv[0] < 2) {
        if (carry(&o->ypart, 20000)) {
            boss->gv[5] = (int16_t)frandom(8);
            o->x = (uint16_t)boss->gv[3];
            o->y = (uint16_t)boss->gv[4];
            scroll_down(1);
            const uint16_t r = frandom(0x1E);
            if (r < 9) {
                const uint16_t x = (uint16_t)(o->x + frandom((uint16_t)(o->xsize + 0x0E)) - 10);
                const uint16_t y = (uint16_t)(o->y + frandom((uint16_t)(o->ysize + 6)) - 6);
                if (r < 6)
                    spawn_obj(0x0D, LIST_MAIN, G.img[IMG_EXPLODE], x, y);
                else
                    spawn_death_fire(o, x, y);
            }
        }
        switch (boss->gv[5]) {
        case 0: o->y--; break;
        case 1: o->y++; break;
        case 2: o->x--; break;
        case 3: o->x++; break;
        case 4: o->y--; o->x--; break;
        case 5: o->y--; o->x++; break;
        case 6: o->y++; o->x--; break;
        default: o->y++; o->x++; break;
        }
    } else if (boss->gv[0] > 5) {
        next_frame(o, IMG_CHIMFIRE);
    }

    if (boss->gv[0] == 0) {
        if (o->counter > 4000 && carry(&o->xpart, 0x800)) {
            /* fade to white */
            memcpy(pal, G.lv->palette, 768);
            boss->gv[0] = 1;
            const uint16_t c = o->counter;
            o->counter = 0;
            for (int i = 0; i < 768; i++) {
                if (pal[i] < 0x3F) {
                    boss->gv[0] = 0;
                    o->counter = c;
                    pal[i]++;
                }
            }
            set_palette(pal);
        }
        return 0;
    }
    if (boss->gv[0] == 1) {
        /* AOPEN(0xb4): the room's tiles without the demon */
        boss->gv[0] = 2;
        size_t len;
        uint8_t *tiles = G.arc ? arc_load(G.arc, "demgone.bg", &len) : NULL;
        if (tiles) {
            free(G.lv->bg_tiles);
            G.lv->bg_tiles = tiles;
            G.lv->bg_tiles_len = len;
        }
        return 0;
    }
    if (boss->gv[0] == 2) {
        boss->gv[0] = 3;
        o->counter = 0;
        o->image = NULL;
        you->animator = A_NULLANIMATOR;
        you->xdir = 0;
        you->x = 0x132;
        you->y = 0xD7;
        obj_image(you, G.img[IMG_MALWALK + 9]);
        obj_t *f = spawn_obj(0x24, LIST_MAIN, G.img[IMG_FEMWALK + 9], 0x154, 0xD7);
        f->animator = A_NULLANIMATOR;
        f->xdir = 0;
        return 0;
    }
    if (boss->gv[0] == 3) {
        if (o->counter > 500 && carry(&o->xpart, 0x800)) {
            /* fade back to the level's palette */
            memcpy(pal, G.lv->palette, 768);
            boss->gv[0] = 4;
            const uint16_t c = o->counter;
            o->counter = 0;
            for (int i = 0; i < 768; i++) {
                if (G.level_pal[i] < pal[i]) {
                    o->counter = c;
                    boss->gv[0] = 3;
                    pal[i]--;
                }
            }
            set_palette(pal);
        }
        return 0;
    }
    if (boss->gv[0] == 4) {
        if (o->counter > 1000) {
            obj_image(o, G.img[IMG_CHIMFIRE]);
            o->image = NULL;
            o->x = (uint16_t)(G.org_x + 0x8C);
            o->y = (uint16_t)(G.org_y + 0x3C);
            boss->gv[0] = 5;
            boss->gv[1] = 0;
            o->max_frames = 3;
            o->frame_no = 0;
            o->anim_inc = 1;
            o->frame_count = o->frame_count_max = 0x28;
            o->counter = 0;
            play_sample(0xB4, NULL);
        }
        return 0;
    }
    if (boss->gv[0] == 5) {
        if (o->counter >= 0x60E) {
            o->counter = 0;
            boss->gv[0] = 6;
            return 0;
        }
        if (o->counter == 600) {
            o->blink_color = 0x4A;
            obj_blink(o, 0x1E, 3, 3);
            obj_image(o, G.img[IMG_CHIMFIRE + 3]);
        } else if (o->counter == 900) {
            obj_image(o, G.img[IMG_CHIMFIRE + 2]);
        } else if (o->counter == 0x4B0) {
            obj_image(o, G.img[IMG_CHIMFIRE + 1]);
        } else if (o->counter == 0x5DC) {
            obj_image(o, G.img[IMG_CHIMFIRE]);
        }
        if (!carry(&o->xpart, 6000))
            return 0;
        /* a spark from a random point of the view's edge */
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
        s->xspeed = (int16_t)((o->x + frandom(o->xsize) - x) * 100);
        s->yspeed = (int16_t)((o->y + frandom(o->ysize) - y) * 100);
        s->animator = A_POWERUPSPARKS;
        return 0;
    }
    if (boss->gv[0] == 6) {
        if (o->counter > 1000) {
            if (carry(&o->xpart, 0x640)) {
                o->y++;
                if ((uint16_t)(o->y + o->ysize) == (uint16_t)(you->y - 10)) {
                    o->counter = 0;
                    boss->gv[0] = 7;
                }
            }
            if (carry(&o->ypart, 800))
                o->x++;
        }
        return 0;
    }
    if (boss->gv[0] == 7 && o->counter > 0x7D0)
        G.quitflag = 3;
    return 0;
}
