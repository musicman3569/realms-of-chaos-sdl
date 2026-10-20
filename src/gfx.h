/* Graphics formats and blitters. The original draws into VGA Mode X (four planes) with the
 * assembly blitters in DRWSPRIT.ASM, DRWBLOCK.ASM and MCOPY.ASM; the port draws the same data
 * into an ordinary 8-bit surface, one byte per pixel. */
#ifndef ROC_GFX_H
#define ROC_GFX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int w, h;
    uint8_t *px; /* w * h palette indices */
} surface_t;

bool surface_init(surface_t *s, int w, int h);
void surface_free(surface_t *s);
void surface_fill(surface_t *s, uint8_t c);

/* One sprite frame: the SPRITE struct (SPRITE.C) and its pixel stream. */
typedef struct {
    uint8_t rows, cols;          /* size */
    int8_t img_xoff, img_yoff;   /* ImgXoff / ImgYoff */
    uint8_t box_cols, box_rows;  /* BoxCols / BoxRows */
    uint8_t hit_cols, hit_rows;  /* HitCols / HitRows */
    int8_t hit_xoff, hit_yoff;   /* HitXoff / HitYoff */
    const uint8_t *data;         /* SPRITE.Data: (op, count) stream, column by column */
    size_t len;                  /* bytes from data to the end of the frame */
} sprite_t;

/* A .spr file (LOADSPRITE): a 21-byte header, then u16 size + SPRITE per frame. */
typedef struct {
    uint8_t *file;
    sprite_t *frames;
    int count;
} spriteset_t;

bool spriteset_parse(spriteset_t *set, uint8_t *file, size_t len); /* takes ownership */
void spriteset_free(spriteset_t *set);

/* _DrawSprite / _RevSprite: (x, y) is the top left corner; flip mirrors left to right.
 * Index 0 bytes in the stream are drawn like any other colour; only skipped rows are
 * transparent. */
void draw_sprite(surface_t *s, const sprite_t *sp, int x, int y, bool flip);
/* _DrawColorSprite / _RevColorSprite: every pixel drawn in one colour (blinking). */
void draw_sprite_color(surface_t *s, const sprite_t *sp, int x, int y, bool flip, uint8_t color);

/* _DrawSolidBlock: a 16x16 background tile, 256 bytes in Mode X plane order. */
void draw_tile(surface_t *s, const uint8_t *tile, int x, int y);

/* _DrawMaskBlock / _CopyMaskBlock: a masked 16x16 tile from a .mfg / .mbg stream. */
void draw_mask_tile(surface_t *s, const uint8_t *data, size_t len, int x, int y);

#endif
