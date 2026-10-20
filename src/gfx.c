#include "gfx.h"

#include <stdlib.h>
#include <string.h>

bool surface_init(surface_t *s, int w, int h)
{
    s->w = w;
    s->h = h;
    s->px = calloc((size_t)w * h, 1);
    return s->px != NULL;
}

void surface_free(surface_t *s)
{
    free(s->px);
    s->px = NULL;
}

void surface_fill(surface_t *s, uint8_t c)
{
    memset(s->px, c, (size_t)s->w * s->h);
}

static inline void put(surface_t *s, int x, int y, uint8_t c)
{
    if ((unsigned)x < (unsigned)s->w && (unsigned)y < (unsigned)s->h)
        s->px[y * s->w + x] = c;
}

/* ------------------------------------------------------------------------------------------ */
/* Sprites                                                                                    */

enum { SPR_HEADER = 21, SPR_STRUCT = 19 }; /* file header; SPRITE fields before Data */

bool spriteset_parse(spriteset_t *set, uint8_t *file, size_t len)
{
    memset(set, 0, sizeof *set);
    set->file = file;
    int cap = 0;
    size_t p = SPR_HEADER;
    while (p + 2 <= len) {
        const size_t size = (size_t)(file[p] | file[p + 1] << 8);
        p += 2;
        if (size < SPR_STRUCT || p + size > len) {
            spriteset_free(set);
            return false;
        }
        if (set->count == cap) {
            cap = cap ? cap * 2 : 16;
            sprite_t *f = realloc(set->frames, (size_t)cap * sizeof *f);
            if (!f) {
                spriteset_free(set);
                return false;
            }
            set->frames = f;
        }
        const uint8_t *b = file + p;
        sprite_t *sp = &set->frames[set->count++];
        sp->rows = b[0];
        sp->cols = b[1];
        sp->img_xoff = (int8_t)b[2];
        sp->img_yoff = (int8_t)b[3];
        sp->box_cols = b[4];
        sp->box_rows = b[5];
        sp->hit_cols = b[6];
        sp->hit_rows = b[7];
        sp->hit_xoff = (int8_t)b[8];
        sp->hit_yoff = (int8_t)b[9];
        /* b[10..11] PostOff: the column offset table, for _ClipSprite; b[12..18] memory
         * bookkeeping filled in at load time. */
        sp->data = b + SPR_STRUCT;
        sp->len = size - SPR_STRUCT;
        p += size;
    }
    return true;
}

void spriteset_free(spriteset_t *set)
{
    free(set->frames);
    free(set->file);
    memset(set, 0, sizeof *set);
}

void draw_sprite(surface_t *s, const sprite_t *sp, int x, int y, bool flip)
{
    /* Ops (DRWSPRIT.ASM CODE0..CODE3): 0 skip count rows, 1 copy count pixels downwards,
     * 2 next column, 3 end. */
    const uint8_t *d = sp->data;
    size_t q = 0;
    int col = 0, row = 0;
    while (q + 2 <= sp->len) {
        const int op = d[q], count = d[q + 1];
        q += 2;
        if (op == 0) {
            row += count;
        } else if (op == 1) {
            if (q + (size_t)count > sp->len)
                return;
            const int px = flip ? x + sp->cols - 1 - col : x + col;
            for (int k = 0; k < count; k++)
                put(s, px, y + row + k, d[q + k]);
            q += (size_t)count;
            row += count;
        } else if (op == 2) {
            col++;
            row = 0;
        } else {
            return;
        }
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Tiles                                                                                      */

void draw_tile(surface_t *s, const uint8_t *tile, int x, int y)
{
    /* Plane p holds pixels p, p+4, p+8, p+12 of each row: 4 bytes per row, 64 per plane. */
    for (int p = 0; p < 4; p++)
        for (int row = 0; row < 16; row++)
            for (int b = 0; b < 4; b++)
                put(s, x + b * 4 + p, y + row, tile[p * 64 + row * 4 + b]);
}

void draw_mask_tile(surface_t *s, const uint8_t *d, size_t len, int x, int y)
{
    /* Ops (DRWBLOCK.ASM MCODE0..MCODE5), each row a word: 0 skip count rows, 1 write both
     * bytes, 2 only the first, 3 only the second, 4 next plane (after plane 3, the right
     * half of the tile: plane bytes 2 and 3), 5 end. */
    size_t q = 0;
    int plane = 0, half = 0, row = 0;
    while (q + 2 <= len) {
        const int op = d[q], count = d[q + 1];
        q += 2;
        switch (op) {
        case 0:
            row += count;
            break;
        case 1: case 2: case 3:
            for (int k = 0; k < count; k++, row++, q += 2) {
                if (q + 2 > len)
                    return;
                if (op != 3)
                    put(s, x + half * 8 + plane, y + row, d[q]);
                if (op != 2)
                    put(s, x + half * 8 + 4 + plane, y + row, d[q + 1]);
            }
            break;
        case 4:
            if (++plane == 4) {
                plane = 0;
                half++;
            }
            row = 0;
            break;
        default:
            return;
        }
    }
}
