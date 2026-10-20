/* RXGRABF.C: what the player touches. MOVE.C calls GRABSTUFF for each cell with bit 0x2000
 * that the player's edge moves into (grab_row / grab_column in engine.c). */
#include "behave.h"

static void clear_grab(unsigned row, unsigned col)
{
    map_set_cell(row, col, map_cell(row, col) & 0xDFFF);
}

/* A gem in the cell: the bonus tile's (cleared), or the background's (on the mushroom levels
 * 11 and 12 the whole cell is cleared). It flies to the counter (DOFLYINGSHIT). */
static void take_gem(unsigned row, unsigned col, bool in_bonus, uint8_t sample, int prize,
                     uint16_t worth)
{
    if (in_bonus)
        map_set_fg(row, col, 0);
    else if (G.level == 11 || G.level == 12)
        map_set_cell(row, col, 0);
    clear_grab(row, col);
    play_sample(sample, NULL);
    spawn_obj(0x13, LIST_SLOW, G.img[IMG_COLLECT + prize], (uint16_t)(col << 4),
              (uint16_t)(row << 4))->gv[0] = (int16_t)prize;
    G.gem_found = (uint16_t)(G.gem_found + worth);
    if (G.message_on)
        G.game_flags |= 0x10;
}

/* RXGRABF.C:11 GRABSTUFF(x, y): the cell's tile types (background, and the foreground byte's
 * bonus tile when bit 7 is clear or its foreground tile when set) say what it is:
 *   10-19 (castle1 / castle2)  a warp (_WarpCoords)
 *   0x74  a secret             0x71 / 0x72  a gem / a big gem (5)
 *   0x73  a rare noise         0x6d  a spike to be impaled on
 *   0x65  water or a pit       0x67  the level's exit (_quitflag 3)
 *   0x70  back to the start    0x75 / 0x6e  spikes (0x6e with blood)
 *   0x68 / 0x69  spawn bolo-throwing cats (not ported yet) / frogs. */
void grab_stuff(unsigned row, unsigned col)
{
    obj_t *you = G.you;
    if (!you || you->animator == A_XLEVEL || you->animator == A_YOUDIE ||
        you->animator == A_YOUBEGINIMPALE || !you->image)
        return;
    const uint16_t cell = map_cell(row, col);
    const uint8_t fg = map_fg(row, col);
    const bool fg_tile = (fg & 0x80) != 0;
    const uint16_t bt = G.lv->bg_type[cell & 0x1FF];
    const uint16_t bon = G.lv->bon_type[fg & 0x7F];
    const uint16_t fgt = G.lv->fg_type[fg & 0x7F];

    if ((G.level == 5 || G.level == 6) && G.quitflag == 0 && bt > 9 && bt < 0x15) {
        /* (MYFADEOUT) */
        you->x = G.warp[(bt - 10) % 10][0];
        you->y = G.warp[(bt - 10) % 10][1];
        you->animator = A_YOUBEGINRUN;
        you->xspeed = 0;
        you->yspeed = 0;
        you->xdir = 1;
        obj_image(you, G.img[G.you_walk + 6]);
        G.quitflag = 6;
    }
    if (bt == 0x74) {
        clear_grab(row, col);
        G.secret_found++;
        return;
    }
    if (bt == 0x71 || (bon == 0x71 && !fg_tile)) {
        take_gem(row, col, bon == 0x71 && !fg_tile, 0x67, 0x12, 1);
        return;
    }
    if (bt == 0x72 || (bon == 0x72 && !fg_tile)) {
        take_gem(row, col, bon == 0x72 && !fg_tile, 0x68, 0x1D, 5);
        return;
    }
    if (bt == 0x73 && frandom(100) == 0) {
        clear_grab(row, col);
        play_sample(0x59, NULL);
        return;
    }
    if (bt == 0x6D || bon == 0x6D) {
        /* impaled: sink onto the spike (YOUBEGINIMPALE) */
        you->x = (uint16_t)(col << 4);
        G.vine_y = (uint16_t)(row * 16 - 0x1C);
        you->counter = 0;
        you->animator = A_YOUBEGINIMPALE;
        play_sample((G.game_flags & 4) ? 0xAA : 0xAE, NULL);
        if (G.blood_on) {
            obj_t *b = spawn_obj(0x21, LIST_FORE, you->image, (uint16_t)(you->x - 5),
                                 (uint16_t)(you->y - 5));
            b->image = NULL;
            b->gv[0] = (int16_t)(you->ysize << 1);
            b->gv[1] = 3;
        }
        obj_image(you, G.img[G.you_jump + 3]);
        return;
    }
    if (bt == 0x65 || (bon == 0x65 && !fg_tile) ||
        (fgt == 0x65 && fg_tile && you->animator != A_NULLANIMATOR)) {
        you_drown(you);
        return;
    }
    if (!(G.game_flags & 2) && !(G.game_flags & 8) && (bt == 0x67 || bon == 0x67)) {
        map_set_cell(row, col, cell & 0xDFFF);
        G.quitflag = 3;
        return;
    }
    if (bon == 0x68 && G.monster_number < G.max_monster_number && G.monster_counter == 0)
        return; /* a cat drops in throwing bolos (BOLOCATFALL): not ported yet */
    if (fg == 0x19 && G.level == 0x18 && (you->gv[5] & 1))
        return; /* goblin2's snake platform (DOSNAKEPLAT): not ported yet */
    if (bon == 0x69 && G.monster_number < G.max_monster_number && G.monster_counter == 0) {
        /* A frog leaps out of the water 50-150 pixels to one side of the player, in front of
         * the water (the foreground list; GENERICBEGINFALL moves it to the main list), facing
         * the player. The tile stops spawning after three frogs (at once on the easy
         * difficulties). */
        const uint16_t r = frandom(100);
        const uint16_t x = frandom(2) == 0 ? (uint16_t)(you->x - r - 0x32)
                                           : (uint16_t)(r + you->x + 0x41);
        if (x < 0x15 || x >= (uint16_t)(G.lv->cols * 16 - 0x29))
            return;
        if (++G.monster_spawns > 2 || G.diff_mode < 2) {
            G.monster_spawns = 0;
            map_set_cell(row, col, cell & 0xDFFF);
        }
        G.monster_number++;
        obj_t *f = spawn_obj(0x3E, LIST_FORE, G.img[IMG_FROG + 2], x, (uint16_t)(G.org_y + 0xB8));
        f->xdir = you->x < x ? 0 : 1;
        f->gv[1] = 1;
        f->gv[2] = 0;
        f->surface_flags |= 8;
        G.enemy_in_level++;
        return;
    }
    if (G.quitflag == 0 && (bt == 0x70 || bon == 0x70)) {
        /* back to the level's start (with a message window, WINUP: not ported) */
        you->x = 0x33;
        if (G.level == 0x0E) {
            you->y = 8;
            you->animator = A_YOUBEGINFALL;
            obj_image(you, G.img[G.you_jump + 1]);
        } else {
            you->y = 0x357;
            you->animator = A_YOUBEGINRUN;
            obj_image(you, G.img[G.you_walk + 9]);
        }
        you->xspeed = 0;
        you->yspeed = 0;
        G.quitflag = 6;
        you->xdir = 1;
        you->gv[5] &= ~1;
        if (G.message_on && G.level < 0x17)
            G.old_game_flags |= 1;
        return;
    }
    if (fgt != 0x75 || !fg_tile || G.you_invincible ||
        (uint16_t)(you->y + you->ysize) <= (uint16_t)(row * 16 + 10)) {
        if (fgt != 0x6E || !fg_tile || (G.game_flags & 8) || G.hitagain || G.you_invincible ||
            you->y <= (uint16_t)(row * 16 - 0x10))
            return;
        play_sample(0xA9, NULL);
        if (G.blood_on) {
            obj_t *b = spawn_obj(0x21, LIST_FORE, you->image, (uint16_t)(you->x - 5),
                                 (uint16_t)(you->y - 5));
            b->image = NULL;
            b->gv[0] = (int16_t)you->ysize;
            b->gv[1] = 3;
        }
    }
    G.free_hit = 1;
    hit_player(you);
}
