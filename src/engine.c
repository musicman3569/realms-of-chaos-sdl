#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

game_t G;

/* ------------------------------------------------------------------------------------------ */
/* Animators                                                                                   */

typedef int (*animator_fn)(obj_t *);
typedef struct {
    uint16_t offset;
    animator_fn fn;
} animator_t;

/* behave.c */
extern const animator_t *behaviours(int *count);

static int run_animator(obj_t *o)
{
    int n;
    const animator_t *a = behaviours(&n);
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (a[mid].offset == o->animator)
            return a[mid].fn(o);
        if (a[mid].offset < o->animator)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    /* Not ported yet: the object stays as it is. Log each animator once. */
    char name[24];
    snprintf(name, sizeof name, "%04x", o->animator);
    for (int i = 0; i < G.unported_count; i++)
        if (!strcmp(G.unported[i], name))
            return 0;
    if (G.unported_count < (int)(sizeof G.unported / sizeof G.unported[0])) {
        snprintf(G.unported[G.unported_count++], sizeof G.unported[0], "%s", name);
        fprintf(stderr, "roc: animator %s (priority %d) isn't ported yet\n", name, o->priority);
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------ */
/* Lists                                                                                       */

/* ADDPLINKNODE: the lists are sorted by ascending priority; a new object goes before the
 * first one with a priority >= its own. */
void obj_link(obj_t *o, int id)
{
    obj_t **head = &G.list[id];
    if (!*head) {
        *head = o;
        o->next = NULL;
    } else if ((*head)->priority < o->priority) {
        obj_t *cur = *head;
        while (cur->next && (o->priority < cur->priority || cur->next->priority < o->priority))
            cur = cur->next;
        o->next = cur->next;
        cur->next = o;
    } else {
        o->next = *head;
        *head = o;
    }
}

/* ADDLINKNODE (unsorted, for the off-screen list) */
static void add_link(obj_t *o, int id)
{
    o->next = G.list[id];
    G.list[id] = o;
}

static bool unlink_from(obj_t *o, int id)
{
    for (obj_t **p = &G.list[id]; *p; p = &(*p)->next) {
        if (*p == o) {
            *p = o->next;
            return true;
        }
    }
    return false;
}

void obj_kill(obj_t *o)
{
    if (o->dead)
        return;
    /* OBJKILL unlinks it from its home list (an object parked in the off-screen list would
     * stay linked there in the original; the port looks in both). The memory is freed after
     * the frame, so a list walk holding a pointer to it stays valid. */
    if (!unlink_from(o, o->list))
        unlink_from(o, LIST_OFF);
    if (o == G.you)
        G.you = NULL;
    o->dead = true;
    o->free_next = G.dead;
    G.dead = o;
}

/* ------------------------------------------------------------------------------------------ */
/* Objects                                                                                     */

/* Borland C++ rand(): seed = seed * 0x015a4e35 + 1, result = (seed >> 16) & 0x7fff. */
static uint16_t borland_rand(void)
{
    G.seed = G.seed * 0x015A4E35u + 1;
    return (uint16_t)(G.seed >> 16 & 0x7FFF);
}

uint16_t frandom(uint16_t n)
{
    return (uint16_t)((int32_t)borland_rand() * n / 0x8000);
}

void obj_image(obj_t *o, const sprite_t *image)
{
    /* OBJIMAGE: the image and its box size. */
    o->image = image;
    if (image) {
        o->xsize = image->box_cols;
        o->ysize = image->box_rows;
    }
}

void next_frame(obj_t *o, int img_base)
{
    /* NEXTFRAME: count down FrameCount; at 0 step FrameNo (0..MaxFrames, wrapping, or
     * ping-ponging by AnimInc) and show image[FrameNo]. ImageID changes, the size doesn't. */
    o->frame_count--;
    if ((int16_t)o->frame_count >= 1)
        return;
    o->frame_count = o->frame_count_max;
    if (o->anim_inc == 0) {
        o->frame_no = o->frame_no < o->max_frames ? o->frame_no + 1 : 0;
    } else {
        o->frame_no = (uint8_t)(o->frame_no + o->anim_inc);
        if ((o->anim_inc < 0 && o->frame_no == 0) ||
            (o->anim_inc > 0 && o->frame_no == o->max_frames))
            o->anim_inc = (int8_t)-o->anim_inc;
    }
    const int i = img_base + o->frame_no;
    o->image = i < IMG_COUNT ? G.img[i] : NULL;
}

obj_t *spawn_obj(uint8_t type, int list, const sprite_t *image, uint16_t x, uint16_t y)
{
    /* SPAWNOBJ */
    obj_t *o = calloc(1, sizeof *o);
    if (!o) {
        fprintf(stderr, "roc: out of memory\n");
        exit(1);
    }
    o->x = x;
    o->y = y;
    o->blink_color = 3;
    o->update = 0x32;
    o->frame_no = type;
    o->old_animator = A_NULLANIMATOR;
    setup_obj(o, type);
    o->frame_no = 0;
    o->frame_count = o->frame_count_max;
    obj_image(o, image);
    o->list = list;
    obj_link(o, list);
    if (list == LIST_FORE || list == LIST_SLOW)
        o->update = 0x1000;
    /* Levels 8 and 9 (the swamp) also keep "parent" lists for objects that ride rafts
     * (PARENTLISTADD); not ported yet. */
    return o;
}

/* The frame a NULL ImageID reads in the original is whatever is at 0000:0000 (the interrupt
 * table); the port uses an empty frame. */
static const sprite_t no_image;

static uint16_t left_edge(const obj_t *o, const sprite_t *f)
{
    if (o->xdir == 0)
        return (uint16_t)(o->x + f->img_xoff);
    return (uint16_t)(o->x - ((f->cols - f->box_cols) + f->img_xoff));
}

bool obj_visible(const obj_t *o)
{
    const sprite_t *f = o->image ? o->image : &no_image;
    const uint16_t cx = left_edge(o, f);
    const uint16_t y0 = (uint16_t)(o->y + f->img_yoff);
    if ((uint16_t)(G.org_x + 320) < cx || (uint16_t)(cx + f->cols - 1) < G.org_x)
        return false;
    return y0 <= (uint16_t)(G.org_y + 200) && G.org_y <= (uint16_t)(y0 + f->rows - 1);
}

bool obj_onscreen(const obj_t *o)
{
    const sprite_t *f = o->image ? o->image : &no_image;
    const uint16_t cx = left_edge(o, f);
    const uint16_t y0 = (uint16_t)(o->y + f->img_yoff);
    if (cx < G.org_x || (uint16_t)(G.org_x + 320) < (uint16_t)(cx + f->cols - 1))
        return false;
    return G.org_y <= y0 && (uint16_t)(y0 + f->rows - 1) <= (uint16_t)(G.org_y + 200);
}

/* ------------------------------------------------------------------------------------------ */
/* Collision (MOVE.C): flag bits in the background map                                         */

static uint16_t bg_cell(unsigned row, unsigned col)
{
    const unsigned i = (uint16_t)(row * G.lv->cols + col);
    return i < (unsigned)(G.lv->rows * G.lv->cols) ? G.lv->bg[i] : 0;
}

static uint8_t fg_cell(unsigned row, unsigned col)
{
    const unsigned i = (uint16_t)(row * G.lv->cols + col);
    return i < (unsigned)(G.lv->rows * G.lv->cols) ? G.lv->fg[i] : 0;
}

/* The cells an edge of the object spans; never 0 (the original would run 65536 times). */
static int span(uint16_t from, uint16_t size)
{
    const int n = (int)((uint16_t)(from + size - 1) >> 4) - (from >> 4) + 1;
    return n > 0 ? n : 1;
}

bool can_go_up(const obj_t *o)
{
    if (o->y & 15)
        return true;
    const unsigned c0 = o->x >> 4, row = (unsigned)(o->y >> 4) - 1;
    for (int k = 0, n = span(o->x, o->xsize); k < n; k++)
        if (bg_cell(row, c0 + k) & 0x1000)
            return false;
    return true;
}

bool can_go_down(const obj_t *o)
{
    const uint16_t foot = (uint16_t)(o->y + o->ysize);
    if (foot & 15)
        return true;
    const unsigned c0 = o->x >> 4;
    for (int k = 0, n = span(o->x, o->xsize); k < n; k++)
        if (bg_cell(foot >> 4, c0 + k) & 0x800)
            return false;
    return true;
}

bool can_go_left(const obj_t *o)
{
    if (o->x & 15)
        return true;
    const unsigned r0 = o->y >> 4, col = (unsigned)(o->x >> 4) - 1;
    for (int k = 0, n = span(o->y, o->ysize); k < n; k++)
        if (bg_cell(r0 + k, col) & 0x400)
            return false;
    return true;
}

bool can_go_right(const obj_t *o)
{
    const uint16_t edge = (uint16_t)(o->x + o->xsize);
    if (edge & 15)
        return true;
    const unsigned r0 = o->y >> 4;
    for (int k = 0, n = span(o->y, o->ysize); k < n; k++)
        if (bg_cell(r0 + k, edge >> 4) & 0x200)
            return false;
    return true;
}

/* The tile type CANMOVE uses for a cell: the background tile's, or (below 200) the
 * foreground / bonus tile's. In the second lookup the original tests bit 7 of the upper
 * cell's foreground byte but indexes with the lower cell's; kept. */
static uint8_t surface_type(unsigned row, unsigned col, unsigned fg_bit_row)
{
    uint8_t k = (uint8_t)G.lv->bg_type[bg_cell(row, col) & 0x1FF];
    if (k < 200) {
        const uint8_t f = fg_cell(row, col) & 0x7F;
        k = (uint8_t)((fg_cell(fg_bit_row, col) & 0x80) ? G.lv->fg_type[f] : G.lv->bon_type[f]);
    }
    return k;
}

void can_move(obj_t *o)
{
    /* CANMOVE (RXGENERL.C:264): where the object stands. UpOffset > 0 means the ground under
     * its middle is a slope above its feet (by UpOffset - 10 pixels); DownOffset 1 means it
     * can fall, 11 that the slope is one pixel below. */
    o->up_offset = 0;
    if (o->surface_flags & 2)
        return;
    if (o == G.you && (G.you->gv[5] & 4)) {
        G.you->down_offset = can_go_down(G.you) ? 1 : 0;
        return;
    }
    const uint16_t mid = (uint16_t)(o->x + (o->xsize >> 1));
    const unsigned col = mid >> 4;
    const uint16_t in_foot = (uint16_t)(o->y + o->ysize - 1); /* the feet's pixel row */
    const uint16_t below = (uint16_t)(o->y + o->ysize + 1);
    uint8_t k = surface_type(in_foot >> 4, col, in_foot >> 4);
    uint16_t base = in_foot & 0xFFF0;
    if (k < 200) {
        k = surface_type(below >> 4, col, in_foot >> 4);
        base = below & 0xFFF0;
    }
    if (k >= 200) {
        const uint8_t h = G.lv->slope[k - 200 < 13 ? k - 200 : 12][mid & 15];
        const uint16_t top = (uint16_t)(base + h - o->ysize); /* y that stands on it */
        if (top == o->y) {
            o->down_offset = 0;
            return;
        }
        if (top < o->y) {
            o->up_offset = (uint8_t)((uint8_t)o->y - (uint8_t)((uint8_t)base + h -
                                                              (uint8_t)o->ysize) + 10);
            o->down_offset = 0;
            if (o->up_offset < 12)
                return;
            o->up_offset = 0;
            if (o->gv[5] & 1)
                return;
            o->down_offset = 1;
            return;
        }
        if ((int16_t)(top - o->y) == 1) {
            o->down_offset = 11;
            return;
        }
    }
    /* Not on a slope. Some slope tiles in the head row count as a ramp up, depending on the
     * direction the object faces. */
    const uint8_t c = (uint8_t)G.lv->bg_type[bg_cell(o->y >> 4, col) & 0x1FF];
    const bool right = (o->flags & 1) || o->xdir;
    if (!(o->surface_flags & 1) &&
        ((right && (c == 200 || c == 201 || c == 204 || c == 208)) ||
         ((!(o->flags & 1) || !o->xdir) && (c == 202 || c == 203 || c == 205 || c == 209)) ||
         c == 206)) {
        o->up_offset = 11;
    } else {
        o->down_offset = can_go_down(o) ? 1 : 0;
    }
}

/* ------------------------------------------------------------------------------------------ */
/* The frame                                                                                   */

static int run_list(int id)
{
    /* One pass of ANIMATORUPDATE over a list: Counter++, then the animator (for OF_RATE
     * objects only when xPart += xSpeed carries). The next object is taken first, so the
     * animator may kill or move the current one. */
    obj_t *nx;
    for (obj_t *o = G.list[id]; o; o = nx) {
        nx = o->next;
        o->counter++;
        if (o->flags & OF_RATE) {
            const uint16_t old = o->xpart;
            o->xpart = (uint16_t)(o->xpart + o->xspeed);
            if (old <= o->xpart)
                continue;
        }
        const int r = run_animator(o);
        if (r)
            return r;
    }
    return 0;
}

static void screen_list_update(void)
{
    /* SCREENLISTUPDATE: OFFTOONUPDATE, then ONTOOFFUPDATE for the main and foreground lists.
     * An object is "on" while it is within Update pixels of the view. */
    obj_t *nx;
    for (obj_t *o = G.list[LIST_OFF]; o; o = nx) {
        nx = o->next;
        const uint16_t u = o->update;
        if (o->x < (uint16_t)(G.org_x + u + 320) && o->y < (uint16_t)(G.org_y + u + 200) &&
            (G.org_x < u || (uint16_t)(G.org_x - u) < o->x) &&
            (G.org_y < u || (uint16_t)(G.org_y - u) < o->y)) {
            unlink_from(o, LIST_OFF);
            obj_link(o, o->list);
        }
    }
    for (int id = LIST_MAIN; id <= LIST_FORE; id++) {
        for (obj_t *o = G.list[id]; o; o = nx) {
            nx = o->next;
            const uint16_t u = o->update;
            const bool on = !((uint16_t)(G.org_x + u + 320) <= o->x ||
                              (uint16_t)(G.org_y + u + 200) <= o->y) &&
                            (G.org_x < u || (uint16_t)(G.org_x - u) < o->x) &&
                            (G.org_y < u || (uint16_t)(G.org_y - u) < o->y);
            if (!on) {
                unlink_from(o, id);
                add_link(o, LIST_OFF);
            }
        }
    }
}

static void free_dead(void)
{
    while (G.dead) {
        obj_t *o = G.dead;
        G.dead = o->free_next;
        free(o);
    }
}

void game_frame(uint16_t ticks)
{
    /* RUNLEVEL's loop: ANIMATORUPDATE(_Update) runs the main and foreground lists once per
     * tick and the slow list once per frame. (RUNTIMERS and RUNANIMTILES aren't ported.) */
    G.update = ticks;
    int r = 0;
    for (uint16_t t = 0; t < ticks && !r; t++)
        r = run_list(LIST_MAIN);
    for (uint16_t t = 0; t < ticks && !r; t++)
        r = run_list(LIST_FORE);
    if (!r)
        run_list(LIST_SLOW);
    screen_list_update();
    free_dead();
}

void game_draw(surface_t *s)
{
    /* The list drawer at 3cb4:064c, for the main, foreground and slow lists in turn. */
    static const int order[] = {LIST_MAIN, LIST_FORE, LIST_SLOW};
    for (int k = 0; k < 3; k++) {
        for (const obj_t *o = G.list[order[k]]; o; o = o->next) {
            const sprite_t *f = o->image;
            if (!f || !obj_visible(o))
                continue;
            const int x = left_edge(o, f), y = (uint16_t)(o->y + f->img_yoff);
            draw_sprite(s, f, x - G.org_x, y - G.org_y, o->xdir != 0);
        }
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Starting a level                                                                            */

/* setup.c */
extern void init_obj(const sprite_t *image, uint16_t x, uint16_t y, const char *info);

static int find_set(const char *name)
{
    for (int i = 0; i < IMG_SETS; i++)
        if (!strcmp(image_sets[i].name, name))
            return i;
    return -1;
}

bool game_start(const level_t *lv, const archive_t *arc, char *err, size_t errlen)
{
    game_end();
    G.lv = lv;
    G.level = (uint8_t)lv->number;
    G.seed = 1;
    G.diff_mode = 2;
    G.game_flags = 4; /* the male character, whose sprites mark the start in every level */

    /* LOADOBJ: load each named set into its _Img slots, then place the objects with a
     * position. The list holds frame 0's top left corner, 8 pixels low. */
    for (int i = 0; i < lv->object_count; i++) {
        const int k = find_set(lv->objects[i].name);
        if (k < 0 || G.sets[k].file)
            continue;
        char path[32];
        snprintf(path, sizeof path, "%s.spr", image_sets[k].name);
        size_t len;
        uint8_t *file = arc_load(arc, path, &len);
        if (!file || !spriteset_parse(&G.sets[k], file, len)) {
            snprintf(err, errlen, "can't load %s from ROC.DAT", path);
            game_end();
            return false;
        }
        for (int f = 0; f < image_sets[k].count && f < G.sets[k].count; f++)
            G.img[image_sets[k].base + f] = &G.sets[k].frames[f];
    }
    for (int i = 0; i < lv->object_count; i++) {
        const level_object_t *e = &lv->objects[i];
        const int k = find_set(e->name);
        if (k < 0 || e->x == 0xFFFF || !G.img[image_sets[k].base])
            continue;
        const sprite_t *f0 = G.img[image_sets[k].base];
        const uint16_t x = (uint16_t)(e->x - f0->img_xoff);
        const uint16_t y = (uint16_t)(e->y - f0->img_yoff - 8);
        if (y <= lv->rows * TILE - 1)
            init_obj(f0, x, y, e->info);
    }
    return true;
}

void game_end(void)
{
    for (int id = 0; id < LIST_COUNT; id++) {
        while (G.list[id]) {
            obj_t *o = G.list[id];
            G.list[id] = o->next;
            free(o);
        }
    }
    free_dead();
    for (int i = 0; i < IMG_SETS; i++)
        spriteset_free(&G.sets[i]);
    memset(&G, 0, sizeof G);
}

/* ------------------------------------------------------------------------------------------ */
/* Not ported yet                                                                              */

int play_sample(uint8_t n, obj_t *o)
{
    (void)n;
    (void)o;
    return 0;
}

bool sound_link_active(int16_t handle)
{
    (void)handle;
    return false;
}

void sound_link_kill(int16_t handle)
{
    (void)handle;
}

void hit_player(obj_t *o)
{
    (void)o;
}

void enemy_hit(obj_t *o, uint8_t damage)
{
    (void)o;
    (void)damage;
}
