/* RXGOBLIN.C: the goblins (fist or club) of the goblin levels. */
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
