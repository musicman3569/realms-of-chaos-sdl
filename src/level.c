#include "level.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ROC.EXE: DGROUP is paragraph 3a77 of the load image (Ghidra 4a77 with base 1000). */
enum { DGROUP_PARA = 0x3A77, LEVEL_SET_TABLE = 0x033E, SLOPE_TABLE = 0x00DF, OBJECT_SIZE = 62 };

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

bool level_list_load(level_list_t *list, const archive_t *arc, const char *exe_path,
                     char *err, size_t errlen)
{
    memset(list, 0, sizeof *list);
    size_t len;
    uint8_t *txt = arc_load(arc, "roclevel.txt", &len);
    if (!txt || len < LEVEL_COUNT * 9) {
        free(txt);
        snprintf(err, errlen, "ROC.DAT has no usable roclevel.txt");
        return false;
    }
    for (int i = 0; i < LEVEL_COUNT; i++) {
        memcpy(list->name[i], txt + i * 9, 9);
        for (int k = 8; k >= 0 && (list->name[i][k] == ' ' || list->name[i][k] == 0); k--)
            list->name[i][k] = 0;
    }
    free(txt);

    FILE *f = fopen(exe_path, "rb");
    uint8_t mz[10];
    bool ok = f && fread(mz, 1, sizeof mz, f) == sizeof mz && mz[0] == 'M' && mz[1] == 'Z';
    if (ok) {
        const long at = (long)rd16(mz + 8) * 16 + DGROUP_PARA * 16L + LEVEL_SET_TABLE;
        ok = fseek(f, at, SEEK_SET) == 0 &&
             fread(list->tile_set, 1, LEVEL_COUNT, f) == LEVEL_COUNT &&
             fseek(f, at - LEVEL_SET_TABLE + SLOPE_TABLE, SEEK_SET) == 0 &&
             fread(list->slope, 1, sizeof list->slope, f) == sizeof list->slope;
    }
    if (f)
        fclose(f);
    for (int i = 0; ok && i < LEVEL_COUNT; i++)
        ok = list->tile_set[i] < LEVEL_COUNT;
    if (!ok)
        snprintf(err, errlen, "%s: can't read the level table (is it the Realms of Chaos EXE?)",
                 exe_path);
    return ok;
}

static bool load_mask_tiles(mask_tiles_t *m, const archive_t *arc, const char *name)
{
    memset(m, 0, sizeof *m);
    m->file = arc_load(arc, name, &m->len);
    if (!m->file || m->len < 258) {
        free(m->file);
        m->file = NULL;
        return false;
    }
    for (int i = 0; i < 128; i++)
        m->offset[i] = rd16(m->file + i * 2);
    const size_t size = rd16(m->file + 256);
    m->data = m->file + 258;
    m->data_len = size <= m->len - 258 ? size : m->len - 258;
    return true;
}

bool level_load(level_t *lv, const archive_t *arc, const level_list_t *list, int number,
                char *err, size_t errlen)
{
    memset(lv, 0, sizeof *lv);
    if (number < 0 || number >= LEVEL_COUNT) {
        snprintf(err, errlen, "no level %d", number);
        return false;
    }
    lv->number = number;
    memcpy(lv->slope, list->slope, sizeof lv->slope);
    char path[32];
    snprintf(path, sizeof path, "%s.led", list->name[number]);
    size_t len;
    uint8_t *d = arc_load(arc, path, &len);
    if (!d) {
        snprintf(err, errlen, "can't load %s from ROC.DAT", path);
        return false;
    }

    /* LOADLEVEL: palette, Rows, Cols, BgMap table, FgMap (3 header bytes + table), the five
     * tile counts, then the object list. */
    bool ok = len >= 770;
    size_t p = 768;
    if (ok) {
        memcpy(lv->palette, d, 768);
        lv->rows = d[p];
        lv->cols = d[p + 1];
        p += 2;
        const size_t cells = (size_t)lv->rows * lv->cols;
        ok = cells > 0 && p + cells * 2 + 3 + cells + 12 <= len;
        if (ok) {
            lv->bg = malloc(cells * sizeof *lv->bg);
            lv->fg = malloc(cells);
            ok = lv->bg && lv->fg;
        }
        if (ok) {
            for (size_t i = 0; i < cells; i++)
                lv->bg[i] = rd16(d + p + i * 2);
            p += cells * 2;
            memcpy(lv->fg, d + p + 3, cells);
            p += 3 + cells;
            for (int i = 0; i < 5; i++)
                lv->tile_count[i] = rd16(d + p + i * 2);
            p += 10;
            lv->object_count = rd16(d + p);
            p += 2;
            ok = p + (size_t)lv->object_count * OBJECT_SIZE + 0x600 <= len;
        }
        if (ok && lv->object_count) {
            lv->objects = calloc((size_t)lv->object_count, sizeof *lv->objects);
            ok = lv->objects != NULL;
            for (int i = 0; ok && i < lv->object_count; i++, p += OBJECT_SIZE) {
                level_object_t *o = &lv->objects[i];
                memcpy(o->name, d + p + 6, 20);
                memcpy(o->info, d + p + 26, 32);
                o->x = rd16(d + p + 58);
                o->y = rd16(d + p + 60);
            }
        }
        if (ok) {
            for (int i = 0; i < 512; i++)
                lv->bg_type[i] = rd16(d + p + i * 2);
            for (int i = 0; i < 128; i++) {
                lv->fg_type[i] = rd16(d + p + 0x400 + i * 2);
                lv->bon_type[i] = rd16(d + p + 0x500 + i * 2);
            }
        }
    }
    free(d);
    if (!ok) {
        snprintf(err, errlen, "%s is damaged", path);
        level_free(lv);
        return false;
    }

    const char *set = list->name[list->tile_set[number]];
    snprintf(path, sizeof path, "%s.bg", set);
    lv->bg_tiles = arc_load(arc, path, &lv->bg_tiles_len);
    if (lv->bg_tiles) {
        snprintf(path, sizeof path, "%s.mfg", set);
        if (load_mask_tiles(&lv->fg_tiles, arc, path)) {
            snprintf(path, sizeof path, "%s.mbg", set);
            load_mask_tiles(&lv->bon_tiles, arc, path);
        }
    }
    if (!lv->bg_tiles || !lv->fg_tiles.file || !lv->bon_tiles.file) {
        snprintf(err, errlen, "can't load %s from ROC.DAT", path);
        level_free(lv);
        return false;
    }
    return true;
}

void level_free(level_t *lv)
{
    free(lv->bg);
    free(lv->fg);
    free(lv->objects);
    free(lv->bg_tiles);
    free(lv->fg_tiles.file);
    free(lv->bon_tiles.file);
    memset(lv, 0, sizeof *lv);
}

static void draw_mask(surface_t *s, const mask_tiles_t *m, int tile, int x, int y)
{
    const size_t off = m->offset[tile];
    if (off < m->data_len)
        draw_mask_tile(s, m->data + off, m->data_len - off, x, y);
}

void level_draw(const level_t *lv, surface_t *s, int cam_x, int cam_y)
{
    /* As the cell drawer at 3cb4:0d58: the background tile, then a foreground or bonus tile
     * masked over it. Background tiles are numbered across the three layers, 240 each. */
    const int c0 = cam_x >= 0 ? cam_x / TILE : -1 - (-cam_x - 1) / TILE;
    const int r0 = cam_y >= 0 ? cam_y / TILE : -1 - (-cam_y - 1) / TILE;
    const int ntiles = (int)(lv->bg_tiles_len / 256);
    for (int r = r0; r * TILE - cam_y < s->h; r++) {
        if (r < 0 || r >= lv->rows)
            continue;
        for (int c = c0; c * TILE - cam_x < s->w; c++) {
            if (c < 0 || c >= lv->cols)
                continue;
            const int x = c * TILE - cam_x, y = r * TILE - cam_y;
            const int cell = r * lv->cols + c;
            const int t = lv->bg[cell] & 0x1FF;
            if (t < ntiles)
                draw_tile(s, lv->bg_tiles + t * 256, x, y);
            const int f = lv->fg[cell];
            if (f & 0x7F)
                draw_mask(s, f & 0x80 ? &lv->fg_tiles : &lv->bon_tiles, f & 0x7F, x, y);
        }
    }
}
