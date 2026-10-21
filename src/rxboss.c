/* RXBOSSF.C: the goblin boss (goblinb, level 16), a huge head living in the walls of a room
 * two views wide. The BOSS* / DOSPITTLE* animators in RXGOBLIN.C each call their F worker here.
 *
 * The head (type 0x4d, priority 0x47, `_Boss`) sits in the left (GenericVar[0] 0) or right
 * (1) wall and spits. Every seventh spitting round it shakes loose and bounces across the
 * room, rocks falling at each landing, settles by the far wall, raises three platforms for the
 * player to climb, floats, and sinks back into that wall, whose tiles are rebuilt around it. */
#include "behave.h"

static bool shows(const obj_t *o, int img)
{
    return o->image == G.img[img];
}

static uint16_t room_right_view(void)
{
    return (uint16_t)(G.lv->cols * 16 - 0x141);
}

/* A hit on the head: two grunts, and ENEMYHIT with the goblin boss's hits. */
static void boss_hurt(obj_t *o)
{
    if (o->flags & OF_HIT) {
        play_sample(0x8F, G.boss);
        play_sample(0x83, G.boss);
        enemy_hit(o, boss_max_hits(2));
    }
}

/* A BOSSSHAKE (type 0x18 with its own animator): `shakes` times the view shakes and a rock
 * falls; with `monsters` set the hard difficulties may drop a small rock monster instead. */
static void spawn_boss_shake(int16_t shakes, int16_t monsters)
{
    obj_t *s = spawn_obj(0x18, LIST_MAIN, NULL, G.org_x, G.org_y);
    s->animator = A_BOSSSHAKE;
    s->gv[0] = shakes;
    s->gv[1] = monsters;
    s->counter = 0;
}

/* RXBOSSF.C:11 FBOSSCENTER: the camera. When the player first lands it lets go of the view
 * (a dead zone the whole view, _CenterOff -2); 0x44c ticks later it scrolls, a pixel every 16
 * ticks, towards the head's side, and once the waiting head is wholly in view the fight starts
 * (BOSSHEADSPIT) and the bar shows. While the head spits, shakes or returns, the player is
 * kept between x 0x90 and 0x172 (the walls), and always inside the view. It goes when the head
 * dies. The decompile runs into the C startup code halfway; Ghidra resolved the near jumps into
 * the wrong segment, so this half was read from the machine code. */
int boss_center(obj_t *o)
{
    obj_t *you = G.you;
    obj_t *b = G.boss;
    if (o->gv[0] == 0 && (you->gv[5] & 1)) {
        o->gv[0]++;
        G.window_xsize = 0x140;
        G.window_ysize = 200;
        G.center_off = -2;
        set_scroll_bounds(200, 0x140);
    } else if (o->gv[0] == 1 && o->counter > 0x44C) {
        o->gv[0]++;
    }
    if (o->gv[0] == 2 && carry(&o->xpart, 0x1000)) {
        if (b->gv[0] != 0) {
            if (room_right_view() != G.org_x && Inv.hit_points[0] != 0)
                scroll_right(1);
            if (b->animator == A_NULLANIMATOR && obj_onscreen(b)) {
                G.enemy_in_level++;
                b->flags |= OF_HITTABLE;
                b->animator = A_BOSSHEADSPIT;
                spawn_obj(0x34, LIST_FORE, G.img[IMG_BOSSHP + 10], (uint16_t)(G.org_x + 0x89),
                          (uint16_t)(G.org_y + 7));
            }
        } else if (G.org_x != 0 && Inv.hit_points[0] != 0) {
            scroll_left(1);
        }
    }
    if (you->x < (uint16_t)(G.org_x + 5)) {
        player_right(1, you);
        if (you->xspeed < 0)
            you->xspeed = 0;
    } else if (you->x > (uint16_t)(G.org_x + 0x12D)) {
        player_left(1, you);
        if (you->xspeed > 0)
            you->xspeed = 0;
    } else if (b->animator == A_BOSSHEADSPIT || b->animator == A_BOSSHEADSHAKE ||
               b->animator == A_BOSSHEADRETURN) {
        if (you->x < 0x90) {
            player_right(1, you);
            if (you->xspeed < 0)
                you->xspeed = 0;
        } else if (you->x > 0x172) {
            player_left(1, you);
            if (you->xspeed > 0)
                you->xspeed = 0;
        }
    }
    if (b->animator == A_BOSSHEADDIE)
        obj_kill(o);
    return 0;
}

/* RXBOSSF.C:72 FBOSSHEADSPIT: mouth shut (BossHeadID[0], the only time it can be hurt) for 0x1c1
 * ticks, then open ([1]) for a round of three gobs (type 0x4e, at Counter 0x4e, 0x5e, 0x6e;
 * BossSpitID[0..2]) thrown at a speed that reaches the player. The seventh round is a shake
 * instead (BOSSHEADSHAKE, [2]). */
int boss_head_spit(obj_t *o)
{
    hit_player(o);
    obj_t *b = G.boss, *you = G.you;
    if (shows(o, IMG_BOSSHEAD)) {
        boss_hurt(o);
        if (o->counter > 0x1C1) {
            o->counter = 0;
            if (++o->gv[1] == 7) {
                o->gv[1] = 0;
                o->animator = A_BOSSHEADSHAKE;
                obj_image(o, G.img[IMG_BOSSHEAD + 2]);
                o->gv[4] = (int16_t)play_sample(0x36, b);
            } else {
                obj_image(o, G.img[IMG_BOSSHEAD + 1]);
                const int16_t d = o->gv[0] == 0 ? (int16_t)(b->x + b->xsize - you->x)
                                                : (int16_t)(b->x - (you->x + 0x0E));
                o->gv[2] = (int16_t)(iabs16(d) * 0x80 + 6000);
            }
        }
    } else if (o->counter == 0x4E || o->counter == 0x5E || o->counter == 0x6E) {
        play_sample(0x60, b);
        const int k = (o->counter - 0x4E) >> 4;
        obj_t *s = spawn_obj(0x4E, LIST_MAIN, G.img[IMG_BOSSSPIT + k],
                             (uint16_t)(o->x + (o->gv[0] == 0 ? 0x20 : -2)), (uint16_t)(o->y + 0x2A));
        s->animator = A_DOSPITTLE;
        s->xspeed = o->gv[2];
        if (o->gv[0] == 0)
            s->flags |= 1;
        else
            s->flags &= 0xFFFE;
    } else if (o->counter == 0x7E) {
        o->counter = 0;
        obj_image(o, G.img[IMG_BOSSHEAD]);
    }
    return 0;
}

/* RXBOSSF.C:117 FBOSSHEADSHAKE: the head rattles a pixel up and down, ten times, changing sides
 * on the first, then leaps out of the wall (ySpeed -14000, 32000 sideways; GENERICUP /
 * GENERICFALL hand it to BOSSHEADBOUNCE). */
int boss_head_shake(obj_t *o)
{
    hit_player(o);
    if (o->counter == 10) {
        o->y--;
    } else if (o->counter == 0x14) {
        o->y++;
        o->counter = 0;
        if (++o->gv[1] == 1) {
            o->gv[0] = !o->gv[0];
        } else if (o->gv[1] == 0x0B) {
            o->flags |= 1;
            sound_link_kill(o->gv[4]);
            o->animator = A_GENERICBEGINJUMP;
            o->yspeed = -14000;
            o->gv[4] = 0xB4;
            o->xspeed = o->gv[0] == 0 ? -32000 : 32000;
            o->counter = 0;
            o->gv[1] = 0;
            play_sample(0x84, G.boss);
        }
    }
    boss_hurt(o);
    return 0;
}

/* RXBOSSF.C:154 FBOSSHEADFLOAT: the head (BossHeadID[3], facing the player) sways from side to
 * side, its speed swinging between -16000 and 16000 by 0x14 a tick. (Going left the original
 * clamps ySpeed instead of xSpeed, so that end overshoots by a step; kept.) */
int boss_head_float(obj_t *o)
{
    hit_player(o);
    o->xdir = G.you->x >= o->x;
    obj_image(o, G.img[IMG_BOSSHEAD + 3]);
    if (!(o->flags & 1)) {
        o->xspeed = (int16_t)(o->xspeed - 0x14);
        if (o->xspeed < -16000) {
            o->yspeed = -16000;
            o->flags |= 1;
        }
    } else {
        o->xspeed = (int16_t)(o->xspeed + 0x14);
        if (o->xspeed > 16000) {
            o->xspeed = 16000;
            o->flags &= 0xFFFE;
        }
    }
    if (carry(&o->xpart, iabs16(o->xspeed))) {
        if (o->xspeed < 1)
            o->x--;
        else
            o->x++;
    }
    boss_hurt(o);
    return 0;
}

/* RXBOSSF.C:196 FBOSSHEADBOUNCE: in the air (GENERICUP / GENERICFALL, priority 0x47) and on
 * landing. It flies sideways at |xSpeed|, facing the player (BossHeadID[0] low down, [3]
 * higher). Each landing it bounces again (ySpeed -26000, GenericVar[4] 0xd2) towards the
 * player, or at the room's far end towards x 0x40 / 0x1ae, and the view shakes with rocks
 * falling (BOSSSHAKE, _DiffMode + 4 times). Coming down at y 0x72 within 32 pixels of that
 * place, it settles there (BOSSHEADSPAWN). */
int boss_head_bounce(obj_t *o)
{
    hit_player(o);
    obj_t *you = G.you;
    o->xdir = you->x >= o->x;
    obj_image(o, G.img[IMG_BOSSHEAD + ((uint16_t)(G.lv->rows * 16 - 0x97) < o->y ? 0 : 3)]);
    if (carry(&o->xpart, iabs16(o->xspeed))) {
        if (o->xspeed < 0 && can_go_left(o))
            o->x--;
        else if (o->xspeed > 0 && can_go_right(o))
            o->x++;
    }
    const bool at_end = o->gv[0] == 0 ? G.org_x == 0 : G.org_x == room_right_view();
    const uint16_t home = o->gv[0] == 0 ? 0x40 : 0x1AE;
    if (at_end && o->y == 0x72 && o->yspeed > 0 &&
        (uint16_t)iabs16((int16_t)(home - o->x)) < 0x20) {
        o->x = home;
        o->animator = A_BOSSHEADSPAWN;
        o->counter = 0;
        o->yspeed = 0;
        o->xspeed = 0;
        if (o->gv[0] == 0)
            o->flags |= 1;
        else
            o->flags &= 0xFFFE;
    } else if (o->gv[5] & 1) {
        play_sample(0x73, o);
        o->animator = A_GENERICBEGINJUMP;
        o->gv[4] = 0xD2;
        o->yspeed = -26000;
        if (at_end) {
            if (o->gv[0] == 0) {
                if (o->x > 0x40)
                    o->xspeed = -10000;
                else if (o->x < 0x40)
                    o->xspeed = 2000;
            } else if (o->x >= 0x1A5) {
                o->xspeed = -2000;
            } else if (o->x < 0x1A4) {
                o->xspeed = 10000;
            }
        } else {
            o->xspeed = you->x < o->x ? -10000 : 10000;
        }
        spawn_boss_shake((int16_t)(G.diff_mode + 4), 0);
    }
    boss_hurt(o);
    return 0;
}

/* RXBOSSF.C:269 FBOSSHEADSPAWN: settled by the wall, the head floats (FBOSSHEADFLOAT) and every
 * 0x276 ticks raises a spinning platform (type 0x4f) on the free one of three heights (y 0xf8,
 * 0xd6, 0xb4; the tables at DS:00aa / 00ad) across the room, and kills any rock monsters. With
 * the third, it moves on to BOSSHEADFLOAT. */
int boss_head_spawn(obj_t *o)
{
    static const uint16_t ypos[3] = {0xF8, 0xD6, 0xB4};
    if (o->counter > 0x276) {
        o->counter = 0;
        bool taken[3] = {false, false, false};
        int plats = 0;
        for (obj_t *p = G.list[LIST_MAIN]; p; p = p->next) {
            if (shows(p, IMG_BOSSPLAT)) {
                plats++;
                taken[p->y == ypos[0] ? 0 : p->y == ypos[1] ? 1 : 2] = true;
            }
            if (p->animator == A_DOROCKMONSTER) {
                p->gv[3] = 0x1E;
                p->flags |= OF_HIT;
            }
        }
        int free = 0;
        while (free < 3 && taken[free])
            free++;
        if (free < 3) { /* (with all three taken the original reads past the table) */
            obj_t *p = spawn_obj(0x4F, LIST_MAIN, G.img[IMG_BOSSPLAT],
                                 o->gv[0] == 0 ? 0x15E : 0x82, ypos[free]);
            if (o->gv[0] == 0)
                p->flags &= 0xFFFE;
            else
                p->flags |= 1;
        }
        if (plats == 2)
            o->animator = A_BOSSHEADFLOAT;
    }
    return boss_head_float(o);
}

/* RXBOSSF.C:301 FBOSSHEADRETURN: the head sinks into its wall, two pixels every 4 ticks, to y
 * 0x80, where the view shakes six times (rocks, and rock monsters on the hard difficulties);
 * 30 ticks later the wall is rebuilt around it: rows 11-16 of columns 3-9 (left) or 23-29
 * (right) get the wall's bonus tiles and are made solid, the other side's are cleared, the
 * platforms go (OFFKILLER) and it spits again. */
int boss_head_return(obj_t *o)
{
    hit_player(o);
    if (o->gv[1] == 0) {
        if (carry(&o->ypart, 0x4000)) {
            o->y = (uint16_t)(o->y + 2);
            if (o->y == 0x80) {
                spawn_boss_shake(6, 1);
                o->counter = 0;
                o->gv[1] = 1;
                play_sample(0xA3, G.boss);
            }
        }
    } else if (o->gv[1] == 1) {
        if (o->counter > 0x1E)
            o->gv[1] = 2;
    } else {
        const bool left = o->gv[0] == 0;
        for (unsigned by = 0; by < 6; by++) {
            for (unsigned bx = 0; bx < 7; bx++) {
                const unsigned row = by + 11;
                const unsigned wall = left ? bx + 3 : bx + 23;  /* the side being built */
                const unsigned gone = left ? bx + 23 : bx + 3;  /* the side being cleared */
                map_set_fg(row, wall, (uint8_t)(bx + by * 20 + (left ? 6 : 13)));
                map_set_fg(row, gone, 0);
                map_set_cell(row, gone, (uint16_t)(map_cell(row, gone) & ~(0x800 | 0x400 | 0x200 | 0x1000)));
                /* (the corner where the head's mouth is stays open) */
                if ((left ? bx != 6 : bx != 0) || by > 3)
                    map_set_cell(row, wall, (uint16_t)(map_cell(row, wall) | 0x800 | 0x400 | 0x200 | 0x1000));
            }
        }
        for (obj_t *p = G.list[LIST_MAIN]; p; p = p->next)
            if (shows(p, IMG_BOSSPLAT))
                p->animator = A_OFFKILLER;
        G.you->surface_flags &= 0xFD;
        o->animator = A_BOSSHEADSPIT;
        o->counter = 0;
        o->gv[1] = 0;
    }
    if (o->flags & OF_HIT) {
        play_sample(0x83, G.boss);
        play_sample(0x8F, G.boss);
        enemy_hit(o, boss_max_hits(2));
    }
    return 0;
}

/* RXBOSSF.C:381 FBOSSHEADDIE (ENEMYHIT gives it to the head): every 20 ticks the head flickers
 * (BossHeadID[4]) and shakes the view, shedding rocks (type 0, RockPiecID[1..2]) and
 * explosions; after 0xb4 of them the level is won. */
int boss_head_die(obj_t *o)
{
    obj_t *you = G.you;
    if (you->x < (uint16_t)(G.org_x + 5)) {
        you->x++;
        if (you->xspeed < 0)
            you->xspeed = 0;
    } else if (you->x > (uint16_t)(G.org_x + 0x12D)) {
        you->x--;
        if (you->xspeed > 0)
            you->xspeed = 0;
    }
    if (o->counter == 10) {
        o->image = NULL;
        if (o->gv[1] < 100)
            scroll_up(1);
    } else if (o->counter == 0x14) {
        o->counter = 0;
        if (o->gv[1] < 0x3C) {
            obj_image(o, G.img[IMG_BOSSHEAD + 4]);
            if (frandom(10) == 0)
                play_sample(0x8F, NULL);
        } else {
            sound_link_kill(o->gv[4]);
        }
        if (o->gv[1] < 100) {
            scroll_down(1);
            const sprite_t *image = G.img[IMG_ROCKPIEC + frandom(2) + 1];
            const uint16_t x = (uint16_t)(o->x + frandom((uint16_t)(o->xsize - 10)));
            const uint16_t y = (uint16_t)(o->y + frandom((uint16_t)(o->ysize - 10)));
            obj_t *r = spawn_obj(0x00, LIST_MAIN, image, x, y);
            r->yspeed = (int16_t)-frandom(27000);
            r->xspeed = (int16_t)frandom(20000);
            r->xdir = (uint8_t)frandom(2);
            if (frandom(2) == 0)
                r->flags &= 0xFFFE;
            else
                r->flags |= 1;
        }
        if (++o->gv[1] > 0xB4)
            G.quitflag = 3;
        if (frandom(0x0F) == 0)
            play_sample(0xA2, NULL);
        const uint16_t x = (uint16_t)(o->x + frandom((uint16_t)(o->xsize - 10)));
        const uint16_t y = (uint16_t)(o->y + frandom((uint16_t)(o->ysize - 10)));
        spawn_obj(0x0D, LIST_MAIN, G.img[IMG_EXPLODE], x, y);
        if (frandom(6) == 0)
            play_sample(0x62, NULL);
    }
    return 0;
}

/* RXBOSSF.C:425 FDOSPITTLE: a gob of spit (type 0x4e) flies at its rate and falls (gravity
 * 0x5a a tick, to 32000); on the floor or the player it splashes into three bits (type 0x57:
 * BossSpitID[2..3], or [3] from the small gob) and goes. It hurts. */
int do_spittle(obj_t *o)
{
    hit_player(o);
    if (carry(&o->xpart, o->xspeed)) {
        if (o->flags & 1)
            o->x++;
        else
            o->x--;
    }
    o->yspeed = (int16_t)(o->yspeed + 0x5A);
    if ((uint16_t)o->yspeed > 32000)
        o->yspeed = 32000;
    if (!carry(&o->ypart, o->yspeed))
        return 0;
    if (o->y < (uint16_t)(G.lv->rows * 16 - 0x29) && !obj_collide(o, G.you)) {
        o->y++;
        return 0;
    }
    if (shows(o, IMG_BOSSSPIT))
        play_sample(0x86, G.boss);
    for (int i = 0; i < 3; i++) {
        obj_t *p;
        if (shows(o, IMG_BOSSSPIT) || shows(o, IMG_BOSSSPIT + 1)) {
            const sprite_t *image = G.img[IMG_BOSSSPIT + frandom(2) + 2];
            const uint16_t x = (uint16_t)(o->x + frandom(2));
            const uint16_t y = (uint16_t)(o->y + frandom(2));
            p = spawn_obj(0x57, LIST_MAIN, image, x, y);
        } else {
            p = spawn_obj(0x57, LIST_MAIN, G.img[IMG_BOSSSPIT + 3], o->x, o->y);
        }
        p->animator = A_GENERICBEGINJUMP;
        p->xspeed = (int16_t)frandom(10000);
        p->yspeed = (int16_t)-frandom(0x4000);
        if (o->flags & 1)
            p->flags |= 1;
        else
            p->flags &= 0xFFFE;
    }
    obj_kill(o);
    return 0;
}

/* RXBOSSF.C:460 FDOSPITTLE2: a bit of splashed spit (priority 0x49) flies sideways and hurts,
 * and goes when it lands. */
int do_spittle2(obj_t *o)
{
    if (carry(&o->xpart, o->xspeed)) {
        if (o->flags & 1)
            o->x++;
        else
            o->x--;
    }
    hit_player(o);
    if (o->gv[5] & 1)
        obj_kill(o);
    return 0;
}

/* RXBOSSF.C:477 FBOSSSHAKE: the boss's screen shake (an invisible type 0x18), GenericVar[0]
 * times: at tick 20 the view jumps up a pixel and a rock falls (type 0x38, from the room's
 * ceiling at y 0x60: anywhere while the head bounces, else on the half away from it); with
 * GenericVar[1] set, above difficulty 2, it's 1 time in 5 (in 2 on the hardest) a small rock
 * monster (type 0x3a). At tick 40 the view comes back. The rumble (sample 0x36) keeps its
 * handle in GenericVar[4]. */
int boss_shake(obj_t *o)
{
    const obj_t *b = G.boss;
    if (o->counter == 0x14) {
        if (o->gv[4] != 0 && !sound_link_active(o->gv[4]))
            o->gv[4] = 0;
        if (o->gv[4] == 0)
            o->gv[4] = (int16_t)play_sample(0x36, G.boss);
        G.center_off = -1;
        G.window_ysize = 200;
        set_scroll_bounds(200, 0x140);
        scroll_up(1);
        uint16_t x;
        if (b->animator == A_GENERICBEGINJUMP || b->animator == A_GENERICJUMP ||
            b->animator == A_BOSSHEADBOUNCE) {
            x = (uint16_t)(G.org_x + frandom(300));
            const uint16_t max = (uint16_t)(G.lv->cols * 16 - 0x37);
            if (x < 0x3C)
                x = 0x3C;
            else if (x > max)
                x = max;
        } else {
            x = (uint16_t)(G.org_x + frandom(0x9F));
            if (b->gv[0] == 0)
                x = (uint16_t)(x + 0x9F);
        }
        const uint16_t r = frandom(G.diff_mode == 4 ? 2 : 5);
        if (r == 0 && G.diff_mode > 2 && o->gv[1] != 0) {
            G.enemy_in_level++;
            obj_t *m = spawn_obj(0x3A, LIST_MAIN, G.img[IMG_ROCKMON3], x, 0x60);
            m->xdir = x < G.you->x;
            m->xspeed = 0;
            m->gv[0] = 2;
            m->frame_count_max = 3;
        } else {
            obj_t *p = spawn_obj(0x38, LIST_MAIN, G.img[IMG_ROCKPIEC + (r < 4 ? 1 : 0)], x, 0x60);
            p->xspeed = 0;
            p->gv[0] = 5;
        }
    } else if (o->counter == 0x28) {
        if (G.org_y < (uint16_t)(G.lv->rows * 16 - 200))
            scroll_down(1);
        if (--o->gv[0] == 0) {
            G.center_off = 0;
            sound_link_kill(o->gv[4]);
            obj_kill(o);
        } else {
            o->counter = 0;
        }
    }
    return 0;
}
