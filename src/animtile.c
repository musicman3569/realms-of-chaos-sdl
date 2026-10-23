/* Animated tiles: ANIMTILE.C (_AnimateBack, _AnimateFore, _AnimateMask, RUNANIMTILES,
 * DEANIMATE; assembly at 3b52, no debug info for its insides), the timer task's part that
 * clocks them (1000:c607), and the per-level lists: INITANTIL (LOADLEV.C:321, the background
 * tiles' frames) and SETUPHANDLES (RXNEWLVF.C:449, the animations each level runs). The
 * forest's waterfalls and flowers, the caves' and swamp's water, the castle's torches and gems,
 * the demon's lava... are map cells whose tile number is stepped through a cycle of frames. */
#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    ANIM_BACK,                   /* background cells: the tile in bits 0-8, flags kept */
    ANIM_FORE,                   /* foreground cells holding a .mfg tile (bit 7 set) */
    ANIM_MASK,                   /* foreground cells holding a .mbg (bonus) tile (bit 7 clear) */
    ANIM_MAX = 32,               /* _AnimHandle[32] */
    ANIM_CELLS = 100,            /* cells an animation can hold (more is a fatal error) */
    ANIM_TIL_NONE = 600,         /* INITANTIL's "no tile": never matches a cell */
};

/* An animation (the original's node: kind, rate, frame count, counter, a spare word, 100 cell
 * numbers ending at a 0, then the frames). */
typedef struct {
    uint16_t kind, rate, count, counter;
    uint16_t cell[ANIM_CELLS];
    int cells;
    uint16_t frame[10];
} anim_t;

static anim_t *anims[ANIM_MAX]; /* _AnimHandle */
static int next_anim;           /* _NextAnim */
static uint16_t anim_counter;   /* _AnimCounter: the timer task's ticks */
static uint16_t an_til[23][3];  /* _AnTil (DS:39a0): background tile frames, by level group */

/* _AnimateBack / _AnimateFore / _AnimateMask (cdecl: rate, count, frames...): record every map
 * cell that shows one of the frames. Every frame is looked at for every cell, so a frame given
 * twice records the cell twice (and RUNANIMTILES then steps it twice). _AnimateFore sets bit 7 of
 * its frames as it goes (they are .mfg tiles); the cells' values are left as they are. */
static void animate(uint16_t kind, uint16_t rate, int count, const uint16_t *frames)
{
    if (next_anim >= ANIM_MAX)
        return;
    anim_t *a = calloc(1, sizeof *a);
    if (!a)
        return;
    a->kind = kind;
    a->rate = rate;
    a->count = (uint16_t)count;
    memcpy(a->frame, frames, (size_t)count * sizeof *frames);
    const level_t *lv = G.lv;
    const int cells = lv->rows * lv->cols;
    for (int c = 0; c < cells; c++) {
        uint16_t t;
        if (kind == ANIM_BACK) {
            t = lv->bg[c] & 0x1FF;
        } else {
            t = lv->fg[c];
            if (t == 0 || (kind == ANIM_FORE) != ((t & 0x80) != 0))
                continue;
        }
        for (int i = 0; i < count; i++) {
            if (kind == ANIM_FORE)
                a->frame[i] |= 0x80;
            if (a->frame[i] != t)
                continue;
            if (a->cells == ANIM_CELLS) {
                /* the original stops with an error message here */
                fprintf(stderr, "roc: an animated tile is on more than %d cells\n", ANIM_CELLS);
                continue;
            }
            a->cell[a->cells++] = (uint16_t)c;
        }
    }
    anims[next_anim++] = a;
}

/* 3b52:042f RUNANIMTILES (once a frame, after RUNTIMERS): each animation whose counter has
 * reached its rate starts counting again and steps its cells to the frame after the one they
 * show (the last going back to the first). A cell showing none of the frames (a gem taken) is
 * left alone. The original redraws the stepped cells inside the view (MAPUPDATE); the port draws
 * the whole view every frame. The cell list ends at its first 0, so the map's top left cell can't
 * be animated, as in the original. */
void animtile_run(void)
{
    level_t *lv = G.lv;
    for (int k = 0; k < ANIM_MAX; k++) {
        anim_t *a = anims[k];
        if (!a || a->counter < a->rate)
            continue;
        a->counter = 0;
        for (int n = 0; n < a->cells && a->cell[n] != 0; n++) {
            const uint16_t c = a->cell[n];
            const uint16_t t = a->kind == ANIM_BACK ? lv->bg[c] & 0x1FF : lv->fg[c];
            for (int i = 0; i < a->count; i++) {
                if (a->frame[i] != t)
                    continue;
                const uint16_t next = a->frame[i + 1 < a->count ? i + 1 : 0];
                if (a->kind == ANIM_BACK)
                    lv->bg[c] = (uint16_t)((lv->bg[c] & 0xFE00) | (next & 0x1FF));
                else
                    lv->fg[c] = (uint8_t)next;
                break;
            }
        }
    }
}

/* The timer task (1000:c5d3) calls 1000:c607 at each of its 582 ticks a second while
 * _LevelRunning and _TicAmt are set: at every fourth, each animation's counter goes up by one.
 * So a rate of 0x13 steps about every 76 ticks (7.7 times a second), 10 every 40 (14.6), 0x23
 * every 140 (4.2), 0x3c every 240 (2.4). */
void animtile_tick(uint16_t ticks)
{
    for (uint16_t t = 0; t < ticks; t++) {
        if (anim_counter++ & 3)
            continue;
        for (int k = 0; k < ANIM_MAX; k++)
            if (anims[k])
                anims[k]->counter++;
    }
}

/* NEWLEVEL's DEANIMATE loop when the level ends. */
void animtile_free(void)
{
    for (int k = 0; k < ANIM_MAX; k++) {
        free(anims[k]);
        anims[k] = NULL;
    }
    next_anim = 0;
}

static void til(int row, uint16_t a, uint16_t b, uint16_t c)
{
    an_til[row][0] = a;
    an_til[row][1] = b;
    an_til[row][2] = c;
}

/* LOADLEV.C:321 INITANTIL: the background tile numbers SETUPHANDLES animates, by level group
 * (a CS jump table at 38d2:0efa). LOADLEVEL calls it first, as TILEBUFFOPTI (which the port
 * doesn't need) renumbers the tiles and these with them. */
static void init_an_til(int level)
{
    for (int r = 0; r < 23; r++)
        til(r, ANIM_TIL_NONE, ANIM_TIL_NONE, ANIM_TIL_NONE);
    switch (level) {
    case 0: case 1: case 2: /* the forest */
        til(0, 3, 5, 7);
        til(1, 0x16, 0x18, 0x1A);
        til(2, 0x17, 0x19, 0x1B);
        til(3, 0x2A, 0x2C, 0x2E);
        til(4, 0x3E, 0x40, 0x42);
        til(5, 0x3F, 0x41, 0x43);
        til(6, 0x53, 0x55, 0x57);
        break;
    case 3: case 4: case 13: case 21: /* the caves */
        til(0, 0x4D, 0x89, 0xC5);
        til(1, 0x5F, 0x9B, 0xD7);
        til(2, 0x60, 0x9C, 0xD8);
        til(3, 0x61, 0x9D, 0xD9);
        til(4, 0x62, 0x9E, 0xDA);
        til(5, 0x73, 0xAF, 0xEB);
        til(6, 0x74, 0xB0, 0xEC);
        til(7, 0x75, 0xB1, 0xED);
        til(8, 0x76, 0xB2, 0xEE);
        til(9, 0x77, 0xB3, 0xEF);
        til(10, 0xFF, 0x13B, 0x177);
        til(11, 0x100, 0x13C, 0x178);
        til(12, 0x101, 0x13D, 0x179);
        til(13, 0x113, 0x14F, 0x18B);
        til(14, 0x114, 0x150, 0x18C);
        til(15, 0x115, 0x151, 0x18D);
        til(16, 0x127, 0x163, 0x19F);
        til(17, 0x128, 0x164, 0x1A0);
        til(18, 0x129, 0x165, 0x1A1);
        til(19, 0x132, 0x116, ANIM_TIL_NONE);
        til(20, 0x146, 0x12A, ANIM_TIL_NONE);
        til(21, 0x133, 0x117, ANIM_TIL_NONE);
        til(22, 0x147, 0x12B, ANIM_TIL_NONE);
        break;
    case 11: case 12: /* mushrom1 / 2 */
        til(0, 0xF4, 0x108, 0x11C);
        til(1, 0xF5, 0x109, 0x11D);
        an_til[2][0] = 0xF8;
        an_til[3][0] = 0xF9;
        break;
    case 14: case 15: case 22: case 23: case 24: /* goblin1, mushromb, mushroms, goblin2, goblins */
        til(0, 0x1A4, 0x1A6, 0x1A8);
        til(1, 0x1A5, 0x1A7, 0x1A9);
        til(2, 0x1B8, 0x1BA, 0x1BC);
        til(3, 0x1B9, 0x1BB, 0x1BD);
        til(4, 0x1AA, 0x1AC, 0x1AE);
        til(5, 0x1AB, 0x1AD, 0x1AF);
        til(6, 0x1BE, 0x1C0, 0x1C2);
        til(7, 0x1BF, 0x1C1, 0x1C3);
        til(8, 0xBD, 0xC0, 0xC3);
        til(9, 0xBE, 0xC1, 0xC4);
        til(10, 0xBF, 0xC2, 0xC5);
        til(11, 0xD1, 0xD4, 0xD7);
        til(12, 0xD2, 0xD5, 0xD8);
        til(13, 0xD3, 0xD6, 0xD9);
        til(14, 0xE5, 0xE8, 0xEB);
        til(15, 0xE6, 0xE9, 0xEC);
        til(16, 0xE7, 0xEA, 0xED);
        break;
    case 25: /* the demon: eight-frame cycles, rows i, i + 6 and i + 12 */
        til(0, 0xA8, 0x105, 0x107);
        til(1, 0xBC, 0x119, 0x11B);
        til(2, 0xD0, 0x12D, 0x12F);
        til(3, 0xE4, 0x141, 0x143);
        til(4, 0xE3, 0x140, 0x142);
        til(5, 0xF7, 0x154, 0x156);
        til(6, 0x109, 0x10B, 0x10D);
        til(7, 0x11D, 0x11F, 0x121);
        til(8, 0x131, 0x133, 0x135);
        til(9, 0x145, 0x147, 0x149);
        til(10, 0x144, 0x146, 0x148);
        til(11, 0x158, 0x15A, 0x15C);
        til(12, 0x10F, 0x111, ANIM_TIL_NONE);
        til(13, 0x123, 0x125, ANIM_TIL_NONE);
        til(14, 0x137, 0x139, ANIM_TIL_NONE);
        til(15, 0x14B, 0x14D, ANIM_TIL_NONE);
        til(16, 0x14A, 0x14C, ANIM_TIL_NONE);
        til(17, 0x15E, 0x160, ANIM_TIL_NONE);
        break;
    }
}

/* _AnimateBack(rate, 3, _AnTil[row][0..2]) */
static void back3(uint16_t rate, int row)
{
    animate(ANIM_BACK, rate, 3, an_til[row]);
}

static void mask4(uint16_t a, uint16_t b, uint16_t c, uint16_t d)
{
    const uint16_t f[4] = {a, b, c, d};
    animate(ANIM_MASK, 10, 4, f);
}

/* RXNEWLVF.C:449 SETUPHANDLES (NEWLEVELSETUP, after the level's counts and before RESTOREGAME
 * or INITLEVEL): the level's animations (a CS jump table at 250e:1731; none on levels 10, 16
 * and from 27). */
void animtile_setup(void)
{
    const int level = G.level;
    animtile_free();
    init_an_til(level);
    switch (level) {
    case 0: case 1: case 2: /* the forest */
        for (int r = 0; r < 7; r++)
            back3(0x23, r);
        mask4(0x19, 0x2D, 0x41, 0x55);
        mask4(0x1A, 0x2E, 0x42, 0x56);
        break;
    case 3: case 4: case 13: case 21: /* the caves */
        for (int r = 0; r < 19; r++)
            back3(0x13, r);
        for (int r = 19; r < 23; r++)
            animate(ANIM_BACK, 0x3C, 2, an_til[r]);
        mask4(0x3C, 0x50, 0x64, 0x78);
        mask4(0x3D, 0x51, 0x65, 0x79);
        break;
    case 5: case 6: case 7: { /* the castle */
        static const uint16_t gem[5] = {0x72, 0x73, 0x74, 0x75, 0x76};
        animate(ANIM_MASK, 0x13, 5, gem);
        mask4(0x28, 0x3C, 0x50, 0x64);
        mask4(0x29, 0x3D, 0x51, 0x65);
        break;
    }
    case 8: case 9: /* the swamp */
        mask4(0x0C, 0x20, 0x34, 0x70);
        mask4(0x73, 0x74, 0x75, 0x76);
        break;
    case 11: case 12: { /* mushrom1 / 2 */
        const uint16_t a[4] = {an_til[0][0], an_til[0][1], an_til[0][2], an_til[2][0]};
        const uint16_t b[4] = {an_til[1][0], an_til[1][1], an_til[1][2], an_til[3][0]};
        animate(ANIM_BACK, 10, 4, a);
        animate(ANIM_BACK, 10, 4, b);
        mask4(0x3C, 0x50, 0x64, 0x78);
        mask4(0x3D, 0x51, 0x65, 0x79);
        break;
    }
    case 14: case 15: case 22: case 23: case 24:
        mask4(0x28, 0x3C, 0x50, 0x64);
        mask4(0x29, 0x3D, 0x51, 0x65);
        for (int r = 0; r < 17; r++)
            back3(0x13, r);
        break;
    case 17: case 18: case 19: case 20: /* the dead lands */
        mask4(0x12, 0x26, 0x3A, 0x4E);
        mask4(0x13, 0x27, 0x3B, 0x4F);
        break;
    case 25: /* the demon */
        for (int i = 0; i < 6; i++) {
            const uint16_t f[8] = {an_til[i][0], an_til[i][1], an_til[i][2],
                                   an_til[i + 6][0], an_til[i + 6][1], an_til[i + 6][2],
                                   an_til[i + 12][0], an_til[i + 12][1]};
            animate(ANIM_BACK, 0x13, 8, f);
        }
        break;
    case 26: { /* secret1: two-frame .mfg cycles at their own rates */
        static const uint16_t fore[9][3] = {
            {0x0F, 0x5B, 0x6F}, {0x14, 0x5C, 0x70}, {0x10, 0x5D, 0x71},
            {0x13, 0x5E, 0x72}, {0x11, 0x5F, 0x73}, {0x12, 0x60, 0x74},
            {0x12, 0x61, 0x75}, {0x11, 0x62, 0x76}, {0x13, 0x63, 0x77},
        };
        mask4(0x3C, 0x50, 0x64, 0x78);
        mask4(0x3D, 0x51, 0x65, 0x79);
        for (int i = 0; i < 9; i++)
            animate(ANIM_FORE, fore[i][0], 2, &fore[i][1]);
        break;
    }
    }
}
