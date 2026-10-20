/* RXGOBLIN.C: the goblins (fist or club) and the rock monsters of the goblin levels. */
#include "behave.h"

/* GenericVar[1]: 0 a goblin with fists (GobFistID), 1 with a club (GobClubID, levels 14 on). */
static int gob_set(const obj_t *o)
{
    return o->gv[1] == 0 ? IMG_GOBFIST : IMG_GOBCLUB;
}

/* The hit the goblin states share: five damage points kill. */
static void goblin_hit_check(obj_t *o)
{
    hit_player(o);
    if (o->flags & OF_HIT) {
        play_sample(0x8C, NULL);
        enemy_hit(o, 5);
    }
}

/* Fell off the bottom of the map. */
static bool goblin_gone(const obj_t *o)
{
    return (uint16_t)(o->y + o->ysize) >= (uint16_t)(G.lv->rows * 16 - 9);
}

/* RXGOBLIN.C:160 GOBLINSTAND: the goblin stands and turns round every 300 ticks; once it has
 * turned three times and ends up facing the player with room to go, it runs (GOBLINRUN). */
int goblin_stand(obj_t *o)
{
    obj_t *you = G.you;
    if (o->counter < 300) {
        hit_player(o);
        if (o->flags & OF_HIT) {
            play_sample(0x8C, NULL);
            enemy_hit(o, 5);
            return 0;
        }
        if (goblin_gone(o))
            obj_kill(o);
        return 0;
    }
    o->gv[0]++;
    o->counter = 0;
    obj_image(o, G.img[gob_set(o)]);
    bool go;
    if (o->xdir == 0) {
        o->xdir = 1;
        go = you && o->x < you->x && can_go_right(o);
    } else {
        o->xdir = 0;
        go = you && you->x < o->x && can_go_left(o);
    }
    if (go && o->gv[0] > 2) {
        o->animator = A_GOBLINRUN;
        o->frame_count = o->frame_count_max;
        o->frame_no = 0;
        o->counter = 0;
        o->gv[2] = 1;
        o->gv[0] = 0;
    }
    goblin_hit_check(o);
    return 0;
}

/* RXGOBLIN.C:212 GOBLINRUN: a step at each carry of 20000 (17000 once it has run 0x240
 * ticks; then GenericVar[2] is clear and it only stands and strikes). It punches or swings
 * (GOBLINHIT) when the player is within reach (14 pixels, 22 with a club) at about its
 * height, jumps off ledges (GENERICBEGINJUMP; GOBLINJUMP steers it), and stands again at a
 * wall or the map's edge, almost ready to turn (Counter 250). */
int goblin_run(obj_t *o)
{
    obj_t *you = G.you;
    if (o->counter > 0x240 && o->gv[2] != 0)
        o->gv[2] = 0;
    if (carry(&o->xpart, o->gv[2] == 0 ? 17000 : 20000) && you) {
        can_move(o);
        next_frame(o, gob_set(o) + 1);
        if (o->frame_count == o->frame_count_max && (o->frame_no == 0 || o->frame_no == 2))
            play_sample(0x5E, o);
        const uint16_t reach = o->gv[1] == 0 ? 0x0E : 0x16;
        const uint16_t dy = (uint16_t)iabs16((int16_t)(o->y - you->y));
        const uint16_t dx =
            o->xdir == 0 ? (uint16_t)iabs16((int16_t)(o->x - (you->x + you->xsize)))
                         : (uint16_t)iabs16((int16_t)(o->x + o->xsize - you->x));
        bool way;
        if (dx < reach && dy < 0x16) {
            obj_image(o, G.img[gob_set(o) + 7]);
            o->animator = A_GOBLINHIT;
            o->counter = 0;
            o->gv[2] = 0;
            play_sample(0x7B, o);
        } else if (o->gv[2] != 0 &&
                   (way = o->xdir == 0 ? can_go_left(o) && room_left(o)
                                       : can_go_right(o) && room_right(o))) {
            if (o->down_offset == 1) {
                /* off a ledge */
                o->update = 0x1000;
                o->gv[2] = 1;
                o->gv[4] = 0x5A;
                o->yspeed = -13000;
                o->animator = A_GENERICBEGINJUMP;
            } else if (goblin_gone(o)) {
                obj_kill(o);
                return 0;
            } else if (o->xdir == 0) {
                o->x--;
            } else {
                o->x++;
            }
        } else {
            o->animator = A_GOBLINSTAND;
            o->counter = 0xFA;
        }
        can_move(o);
        if (o->down_offset > 10)
            o->y++;
        if (o->up_offset > 10)
            o->y--;
    }
    hit_player(o);
    if (o->flags & OF_HIT) {
        play_sample(0x8C, NULL);
        enemy_hit(o, 5);
        return 0;
    }
    if (goblin_gone(o))
        obj_kill(o);
    return 0;
}

/* RXGOBLIN.C:297 GOBLINHIT: the blow. After 300 ticks (100 with a club) the goblin strikes:
 * the follow-through frame (8) and the fist or club itself (type 0x3f, GOBLINFIST) in front
 * of it; as long again later it runs on (GOBLINRUN), facing away from the player. */
int goblin_hit(obj_t *o)
{
    obj_t *you = G.you;
    const bool club = o->gv[1] != 0;
    if (o->counter >= (club ? 100 : 300)) {
        if (o->gv[2] == 0) {
            o->counter = 0;
            o->gv[2]++;
            obj_image(o, G.img[gob_set(o) + 8]);
            const uint16_t x = o->xdir == 0 ? (uint16_t)(o->x - (club ? 0x2C : 0x15))
                                            : (uint16_t)(o->x + (club ? 0x20 : 0x10));
            play_sample(0x80, o);
            obj_t *f = spawn_obj(0x3F, LIST_MAIN, G.img[club ? IMG_CLUB : IMG_FIST], x,
                                 (uint16_t)(o->y + (club ? 0x16 : 0x0D)));
            f->gv[1] = o->gv[1];
            f->xdir = o->xdir;
        } else {
            o->xdir = you && o->x < you->x ? 0 : 1;
            o->animator = A_GOBLINRUN;
            o->frame_count = o->frame_count_max;
            o->frame_no = 0;
            o->counter = 0;
            o->gv[2] = 1;
        }
    }
    goblin_hit_check(o);
    return 0;
}

/* RXGOBLIN.C:346 GOBLINFIST: the fist or club, an invisible-ish hit box that hurts for 300
 * ticks (100 for a club); a fist touching the player makes a smack once. */
int goblin_fist(obj_t *o)
{
    if (o->gv[1] == 0 && G.you && obj_collide(G.you, o) && o->gv[2] == 0) {
        o->gv[2]++;
        play_sample(0x7A, o);
    }
    hit_player(o);
    if (o->counter >= (o->gv[1] == 0 ? 300 : 100))
        obj_kill(o);
    return 0;
}

/* RXGOBLIN.C:360 GOBLINJUMP: GENERICUP / GENERICFALL call it for goblins (priority 0x33).
 * At each carry of the run speed it shows the rising or falling frame and moves on; between
 * steps it hurts, and landing on stakes (tile type 0x6d under either foot, background or
 * bonus tile) kills it. */
void goblin_jump(obj_t *o)
{
    if (carry(&o->xpart, o->gv[2] == 0 ? 17000 : 20000)) {
        obj_image(o, G.img[gob_set(o) + (o->yspeed < 0 ? 2 : 1)]);
        if (o->xdir != 0 && can_go_right(o) && room_right(o))
            o->x++;
        else if (o->xdir == 0 && can_go_left(o) && room_left(o))
            o->x--;
    } else {
        hit_player(o);
        const unsigned row = (unsigned)(uint16_t)(o->y + o->ysize) >> 4;
        const unsigned c0 = o->x >> 4, c1 = (unsigned)(uint16_t)(o->x + o->xsize) >> 4;
        const level_t *lv = G.lv;
        if (lv->bg_type[map_cell(row, c0) & 0x1FF] == 0x6D ||
            lv->bon_type[map_fg(row, c0) & 0x7F] == 0x6D ||
            lv->bg_type[map_cell(row, c1) & 0x1FF] == 0x6D ||
            lv->bon_type[map_fg(row, c1) & 0x7F] == 0x6D) {
            o->gv[3] += 3;
            enemy_hit(o, 2);
        }
    }
    o->counter = 0;
}

/* RXGOBLIN.C:87 ROCKMONSTERHIT: each hit costs a rock monster one more point (40, 32 or 24
 * by size: GenericVar[0] 0 big, 1 medium, 2 small) and chips rock off it (type 0x38: 2
 * pieces, 6 against the male's whip; bigger monsters shed smaller pieces). When it breaks,
 * a big one splits into two medium ones (RockMon2ID) and a medium one into two faster
 * small ones (RockMon3ID), in a shower of 9 pieces; a small one just crumbles. */
static void rock_monster_hit(obj_t *o)
{
    obj_t *you = G.you;
    const int16_t size = o->gv[0];
    if (++o->gv[3] < 0x28 - size * 8) {
        o->flags &= 0xFD;
        o->old_animator = o->animator;
        o->animator = A_ENEMYHITPAUSE;
        G.hit_release = 0;
        play_sample(0x83, NULL);
        for (uint8_t i = 0; i < ((G.game_flags & 4) ? 6 : 2); i++) {
            const uint8_t r = (uint8_t)frandom(size == 0 ? 10 : size == 1 ? 9 : 5);
            const int piece = r < 5 ? 3 : r < 7 ? 2 : r < 9 ? 1 : 0;
            const uint16_t x = (uint16_t)(o->x + frandom((uint16_t)(o->xsize - (o->xsize >> 1))));
            const uint16_t y = (uint16_t)(o->y + frandom((uint16_t)(o->ysize >> 1)));
            obj_t *p = spawn_obj(0x38, LIST_MAIN, G.img[IMG_ROCKPIEC + piece], x, y);
            if (you && o->x < you->x)
                p->flags &= 0xFFFE;
            else
                p->flags |= 1;
        }
        return;
    }
    play_sample(0x84, NULL);
    G.enemy_found++;
    if (size == 0) {
        G.enemy_in_level += 2;
        spawn_obj(0x3A, LIST_MAIN, G.img[IMG_ROCKMON2], (uint16_t)(o->x + 5), o->y)->xdir = 0;
        spawn_obj(0x3A, LIST_MAIN, G.img[IMG_ROCKMON2], (uint16_t)(o->x + 5),
                  (uint16_t)(o->y + 0x28))->xdir = 1;
    } else if (size == 1) {
        G.enemy_in_level += 2;
        for (int i = 0; i < 2; i++) {
            obj_t *m = spawn_obj(0x3A, LIST_MAIN, G.img[IMG_ROCKMON3], (uint16_t)(o->x + 7),
                                 (uint16_t)(o->y + (i ? 0x14 : 0)));
            m->xdir = i ? 0 : 1;
            m->xspeed = (int16_t)(m->xspeed + 2000);
            m->gv[0] = 2;
            m->frame_count_max = 3;
        }
    }
    for (uint8_t i = 0; i < 9; i++) {
        const uint8_t r = size == 0 ? (uint8_t)frandom(5) : size == 1 ? (uint8_t)frandom(4) : 4;
        const int piece = r < 2 ? 2 : r < 4 ? 1 : size == 2 ? 3 : 0;
        const uint16_t x = (uint16_t)(o->x + frandom((uint16_t)(o->xsize - (o->xsize >> 1))));
        const uint16_t y = (uint16_t)(o->y + frandom((uint16_t)(o->ysize >> 1)));
        obj_t *p = spawn_obj(0x38, LIST_MAIN, G.img[IMG_ROCKPIEC + piece], x, y);
        if (frandom(2) == 0)
            p->flags |= 1;
        else
            p->flags &= 0xFFFE;
    }
    obj_kill(o);
}

/* RXGOBLIN.C:13 DOROCKMONSTER: a rock monster (OF_RATE, xSpeed 0xdac: about 31 steps a
 * second) rolling along, following slopes. It turns at walls, ledges, a ceiling it can't
 * climb past and, after 0x640 ticks, once it has passed the player. The big one rumbles. */
int do_rock_monster(obj_t *o)
{
    obj_t *you = G.you;
    bool turn = false;
    can_move(o);
    if (o->down_offset > 10)
        o->y++;
    if (o->up_offset > 10) {
        if (!can_go_up(o))
            turn = true;
        else
            o->y--;
    }
    if (o->gv[0] == 0) {
        next_frame(o, IMG_ROCKMON1);
        if ((o->frame_no == 1 || o->frame_no == 4) && o->frame_count == o->frame_count_max)
            play_sample(0x73, o);
    } else {
        next_frame(o, o->gv[0] == 1 ? IMG_ROCKMON2 : IMG_ROCKMON3);
    }
    if (o->xdir == 0) {
        o->x--;
        if (o->counter > 0x640 && you && o->x < you->x)
            turn = true;
    } else {
        o->x++;
        if (o->counter > 0x640 && you && you->x < o->x)
            turn = true;
    }
    const bool way = o->xdir == 0 ? can_go_left(o) && room_left(o)
                                  : can_go_right(o) && room_right(o);
    if (!way || o->down_offset == 1 || turn) {
        o->counter = 0;
        o->frame_count = 1;
        if (o->xdir == 0) {
            o->xdir = 1;
            o->x = (uint16_t)(o->x + 2);
        } else {
            o->xdir = 0;
            o->x = (uint16_t)(o->x - 2);
        }
    }
    hit_player(o);
    if (o->flags & OF_HIT)
        rock_monster_hit(o);
    return 0;
}

/* RXGOBLIN.C:69 ROCKMONSTERFALL: GENERICFALL calls it for rock monsters (priority 0x31): in
 * the air it keeps rolling sideways at its speed, bouncing off walls. */
void rock_monster_fall(obj_t *o)
{
    if (!carry(&o->xpart, o->xspeed))
        return;
    if (o->xdir == 0) {
        if (can_go_left(o))
            o->x--;
        else
            o->xdir = 1;
    } else if (can_go_right(o)) {
        o->x++;
    } else {
        o->xdir = 0;
    }
}
