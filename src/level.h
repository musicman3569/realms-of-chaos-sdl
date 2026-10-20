/* Levels (LOADLEV.C): the .led map plus the tile set it is drawn with. */
#ifndef ROC_LEVEL_H
#define ROC_LEVEL_H

#include "archive.h"
#include "gfx.h"

enum { LEVEL_COUNT = 29, TILE = 16 };

/* The level list: roclevel.txt names (9-character slots) and, for each level, which name's
 * .bg/.mfg/.mbg it uses (the byte table at DGROUP:033e in ROC.EXE). */
typedef struct {
    char name[LEVEL_COUNT][10];
    uint8_t tile_set[LEVEL_COUNT];
} level_list_t;

bool level_list_load(level_list_t *list, const archive_t *arc, const char *exe_path,
                     char *err, size_t errlen);

/* An object placement from the .led (SPRITE_LIST, 62 bytes). x / y are 0xFFFF for entries
 * that only load a sprite set (fonts, borders...). */
typedef struct {
    char name[21];
    char info[33];
    uint16_t x, y;
} level_object_t;

/* Masked tiles from a .mfg / .mbg: 128 offsets into the data. */
typedef struct {
    uint8_t *file;
    size_t len;
    uint16_t offset[128];
    const uint8_t *data;
    size_t data_len;
} mask_tiles_t;

typedef struct {
    int number;
    uint8_t palette[768];        /* 6-bit VGA components */
    int rows, cols;
    uint16_t *bg;                /* BgMap: tile (& 0x1ff) and flag bits */
    uint8_t *fg;                 /* FgMap: 0, or a 7-bit tile (bit 7: .mfg, else .mbg) */
    uint16_t tile_count[5];      /* bg1, bg2, bg3, fg, bonus */
    level_object_t *objects;
    int object_count;
    uint8_t *bg_tiles;           /* the .bg file: layer 1, 2 and 3 tiles of 256 bytes */
    size_t bg_tiles_len;
    mask_tiles_t fg_tiles, bon_tiles;
} level_t;

bool level_load(level_t *lv, const archive_t *arc, const level_list_t *list, int number,
                char *err, size_t errlen);
void level_free(level_t *lv);

/* Draw the map with its top left corner at map pixel (cam_x, cam_y). */
void level_draw(const level_t *lv, surface_t *s, int cam_x, int cam_y);

#endif
