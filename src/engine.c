#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

game_t G;
inventory_t Inv;

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

/* Levels 8 and 9 (the swamp): what shows a reflection in the water: priorities 0x35 (frogs),
 * 2 (statues), 0x34, 0xe5 (prizes), 0xdc, 0x36, and the player. */
static bool reflects(const obj_t *o)
{
    const int16_t pr = o->priority;
    return (G.level == 8 || G.level == 9) &&
           (pr == 0x35 || pr == 2 || pr == 0x34 || pr == 0xE5 || pr == 0xDC || pr == 0x36 ||
            o == G.you);
}

/* PARENTLISTADD (RXSWAMPF.C:11), as SPAWNOBJ and INITOBJ call it: a reflection (type 0x5a,
 * DOREFLECTION, at the parent's priority) mirrored in the water. The original keeps the pairs
 * in _ParentListHead; the port keeps the parent in the reflection. */
void reflection_check(obj_t *o)
{
    if (!reflects(o) || o->animator == A_DOREFLECTION)
        return;
    G.parent_priority = o->priority;
    const uint16_t bottom = (uint16_t)(G.lv->rows * 16);
    obj_t *r = spawn_obj(0x5A, LIST_MAIN, NULL, o->x,
                         (uint16_t)(bottom + (bottom - 0x4D - (o->y + o->ysize))));
    r->parent = o;
}

void obj_kill(obj_t *o)
{
    if (o->dead)
        return;
    if (o->list == LIST_MAIN && reflects(o)) {
        /* PARENTLISTKILL: its reflections go too */
        static const int lists[2] = {LIST_MAIN, LIST_OFF};
        for (int i = 0; i < 2; i++)
            for (obj_t *p = G.list[lists[i]]; p; p = p->next)
                if (p->parent == o && p->animator == A_DOREFLECTION) {
                    p->animator = A_OFFKILLER;
                    p->parent = NULL;
                }
    }
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
    else
        reflection_check(o);
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

uint16_t map_cell(unsigned row, unsigned col)
{
    const unsigned i = (uint16_t)(row * G.lv->cols + col);
    return i < (unsigned)(G.lv->rows * G.lv->cols) ? G.lv->bg[i] : 0;
}

uint8_t map_fg(unsigned row, unsigned col)
{
    const unsigned i = (uint16_t)(row * G.lv->cols + col);
    return i < (unsigned)(G.lv->rows * G.lv->cols) ? G.lv->fg[i] : 0;
}

static uint16_t bg_cell(unsigned row, unsigned col)
{
    return map_cell(row, col);
}

/* GRABSTUFF changes the maps (MAPUPDATE redraws the cell; the port redraws every frame). */
void map_set_cell(unsigned row, unsigned col, uint16_t v)
{
    const unsigned i = (uint16_t)(row * G.lv->cols + col);
    if (i < (unsigned)(G.lv->rows * G.lv->cols))
        G.lv->bg[i] = v;
}

void map_set_fg(unsigned row, unsigned col, uint8_t v)
{
    const unsigned i = (uint16_t)(row * G.lv->cols + col);
    if (i < (unsigned)(G.lv->rows * G.lv->cols))
        G.lv->fg[i] = v;
}

bool obj_overlap(const obj_t *a, const obj_t *b)
{
    return a->x < (uint16_t)(b->x + b->xsize - 1) && b->x < (uint16_t)(a->x + a->xsize - 1) &&
           a->y < (uint16_t)(b->y + b->ysize - 1) && b->y < (uint16_t)(a->y + a->ysize - 1);
}

/* OBJCOLLIDE: the hit boxes of the objects' current frames overlap (mirrored like the image
 * when xDir is 1). x is compared signed, y unsigned, as in the original. */
bool obj_collide(const obj_t *a, const obj_t *b)
{
    const sprite_t *fa = a->image ? a->image : &no_image;
    const sprite_t *fb = b->image ? b->image : &no_image;
    const int16_t xa = a->xdir == 0 ? (int16_t)(a->x + fa->hit_xoff)
                                    : (int16_t)(a->x - ((fa->hit_cols - fa->box_cols) +
                                                        fa->hit_xoff));
    const int16_t xb = b->xdir == 0 ? (int16_t)(b->x + fb->hit_xoff)
                                    : (int16_t)(b->x - ((fb->hit_cols - fb->box_cols) +
                                                        fb->hit_xoff));
    if (!(xa < (int16_t)(xb + fb->hit_cols) && xb < (int16_t)(xa + fa->hit_cols)))
        return false;
    const uint16_t ya = (uint16_t)(a->y + fa->hit_yoff), yb = (uint16_t)(b->y + fb->hit_yoff);
    return ya < (uint16_t)(yb + fb->hit_rows) && yb < (uint16_t)(ya + fa->hit_rows);
}

/* OBJBLINK: blink `blinks` times, on (drawn in BlinkColor) for `on` frames and off for `off`;
 * 0 stops it. */
void obj_blink(obj_t *o, uint16_t blinks, uint16_t on, uint16_t off)
{
    o->blinks = blinks;
    o->blink_ct = on;
    o->draw_on = on;
    o->draw_off = off;
}

static int span(uint16_t from, uint16_t size);

/* FUN_3c7c_0060: the cells in pixel column px along the object's height. */
static void grab_column(uint16_t px, const obj_t *o)
{
    const unsigned r0 = o->y >> 4;
    for (int k = 0, n = span(o->y, o->ysize); k < n; k++)
        if (map_cell(r0 + k, px >> 4) & 0x2000)
            grab_stuff(r0 + k, px >> 4);
}

/* FUN_3c7c_0002: the cells in pixel row py along the object's width. */
static void grab_row(uint16_t py, const obj_t *o)
{
    const unsigned c0 = o->x >> 4;
    for (int k = 0, n = span(o->x, o->xsize); k < n; k++)
        if (map_cell(py >> 4, c0 + k) & 0x2000)
            grab_stuff(py >> 4, c0 + k);
}

void player_right(uint8_t n, obj_t *o)
{
    o->x = (uint16_t)(o->x + n);
    const uint16_t edge = (uint16_t)(o->x + o->xsize - 1);
    grab_column(edge, o);
    if ((uint16_t)(G.org_x + G.scroll_right) <= edge)
        scroll_right(n);
}

void player_left(uint8_t n, obj_t *o)
{
    o->x = (uint16_t)(o->x - n);
    grab_column(o->x, o);
    if (o->x <= (uint16_t)(G.org_x + G.scroll_left))
        scroll_left(n);
}

void player_up(uint8_t n, obj_t *o)
{
    for (int i = 0; i < n; i++) {
        o->y--;
        grab_row(o->y, o);
        if (o->y <= (uint16_t)(G.org_y + G.scroll_upper))
            scroll_up(1);
    }
}

void player_down(uint8_t n, obj_t *o)
{
    for (int i = 0; i < n; i++) {
        o->y++;
        const uint16_t foot = (uint16_t)(o->y + o->ysize - 1);
        grab_row(foot, o);
        if ((uint16_t)(G.org_y + G.scroll_lower) <= foot)
            scroll_down(1);
    }
}

void set_scroll_bounds(uint16_t h, uint16_t w)
{
    G.scroll_left = (uint16_t)(320 - w) >> 1;
    G.scroll_right = (uint16_t)(320 - G.scroll_left);
    G.scroll_upper = (uint16_t)(200 - h) >> 1;
    G.scroll_lower = (uint16_t)(200 - G.scroll_upper);
}

/* SCROLLRIGHT... (the originals also draw the newly exposed column / row into the Mode X
 * pages; the port redraws the whole view each frame). */
void scroll_right(uint8_t n)
{
    if (G.org_x < (uint16_t)(G.max_x - 320))
        G.org_x = (uint16_t)(G.org_x + n);
}

void scroll_left(uint8_t n)
{
    if (G.org_x != 0)
        G.org_x = (uint16_t)(G.org_x - n);
}

void scroll_up(uint8_t n)
{
    if (G.org_y != 0)
        G.org_y = (uint16_t)(G.org_y - n);
}

void scroll_down(uint8_t n)
{
    if (G.org_y < (uint16_t)(G.max_y - 200))
        G.org_y = (uint16_t)(G.org_y + n);
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

static uint8_t fg_cell(unsigned row, unsigned col)
{
    return map_fg(row, col);
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
        sound_forget_obj(o);
        free(o);
    }
}

/* RXTIMERF.C:12 LEVELTIMER: NEWLEVEL installs it with a rate of 0x0c, so it runs every 0x30
 * ticks (12 a second). It counts down the hit protection (bleeding meanwhile) and the attack's recovery, and makes
 * spawned objects that went off screen kill themselves (OFFKILLER). */
static void level_timer(void)
{
    if (G.monster_counter)
        G.monster_counter--;
    if (++G.hit_release > 3)
        G.hit_release = 0;
    if (G.hitagain < 1) {
        if (G.hitagain < 0)
            G.hitagain++;
    } else {
        G.hitagain--;
        if (G.blood_on && G.you) {
            const sprite_t *f = G.you->image ? G.you->image : &no_image;
            const uint16_t r = frandom(5);
            const uint16_t x = (uint16_t)(G.you->x + f->hit_xoff + (f->hit_cols >> 1) +
                                          (frandom(7) - 3));
            const uint16_t y = (uint16_t)(G.you->y + f->hit_yoff +
                                          frandom((uint16_t)(f->hit_rows - 8)) + 8);
            obj_t *b = spawn_obj(0x0A, LIST_MAIN, G.img[IMG_BLOOD + r], x, y);
            b->animator = A_GENERICBEGINFALL;
            b->gv[0] = 2;
            b->gv[1] = 0;
            b->gv[2] = (int16_t)r;
            b->xspeed = 0;
            b->yspeed = 0;
        }
    }
    if (G.you && G.you->gv[0] != 0) {
        if (G.you->gv[0] < 11) {
            G.you->gv[0]--;
            if (!(G.game_flags & 4) && !key(K_ATTACK))
                G.you->gv[0] = 0;
        } else if (!key(K_ATTACK)) {
            G.you->gv[0] -= 10;
        }
    }
    for (obj_t *o = G.list[LIST_OFF]; o; o = o->next) {
        if (!(o->flags & OF_PLACED)) {
            o->animator = A_OFFKILLER;
            o->update = 0x1000;
            if (o->flags & OF_RATE)
                o->xspeed = 0x7FFF;
        }
    }
}

/* RXTIMERF.C:85 FOFFKILLER */
int off_killer(obj_t *o)
{
    if (o->surface_flags & 8) {
        G.monster_counter = 5;
        G.monster_number--;
    }
    obj_kill(o);
    return 0;
}

void game_frame(uint16_t ticks)
{
    /* RUNLEVEL's loop: RUNTIMERS (LEVELTIMER), RUNANIMTILES, then ANIMATORUPDATE(_Update) runs
     * the main and foreground lists once per tick and the slow list once per frame. */
    G.timer_ticks = (uint16_t)(G.timer_ticks + ticks);
    G.level_tics += ticks; /* (_TimerTics, for the level's clock) */
    animtile_tick(ticks);
    if (G.timer_ticks >= LEVEL_TIMER_RATE * 4 && G.level < 0x1B) {
        G.timer_ticks = 0;
        level_timer();
    }
    animtile_run();
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

/* 3cb4:0bec: put the foreground (.mfg) tiles back over an object. RUNLEVEL calls it for every
 * visible object of the main list once the list is drawn, for the cells its image covers
 * (within the view's 22 x 14 cells), so tree trunks and bushes stand in front of the player
 * and the enemies. Bonus (.mbg) tiles stay behind them, and the foreground and slow lists are
 * drawn after this, over everything. */
static void fg_over(surface_t *s, const obj_t *o)
{
    const sprite_t *f = o->image;
    const uint16_t x = left_edge(o, f), y = (uint16_t)(o->y + f->img_yoff);
    const unsigned c0 = x >> 4, c1 = (uint16_t)(x + f->cols - 1) >> 4;
    const unsigned r0 = y >> 4, r1 = (uint16_t)(y + f->rows - 1) >> 4;
    const unsigned vc = G.org_x >> 4, vr = G.org_y >> 4;
    for (unsigned r = r0; r <= r1; r++)
        for (unsigned c = c0; c <= c1; c++)
            if (c >= vc && c <= vc + 0x15 && r >= vr && r <= vr + 0x0D)
                level_draw_fg_cell(G.lv, s, (int)r, (int)c, G.org_x, G.org_y);
}

void game_draw(surface_t *s)
{
    /* The list drawer at 3cb4:064c, for the main, foreground and slow lists in turn. A
     * blinking object counts BlinkCt down each frame; at 0 Blinks steps down and the count
     * restarts from DrawOn (odd Blinks) or DrawOff (even). While Blinks is odd every pixel is
     * drawn in BlinkColor. */
    static const int order[] = {LIST_MAIN, LIST_FORE, LIST_SLOW};
    for (int k = 0; k < 3; k++) {
        if (order[k] == LIST_FORE) {
            for (const obj_t *o = G.list[LIST_MAIN]; o; o = o->next)
                if (o->image && obj_visible(o))
                    fg_over(s, o);
        }
        for (obj_t *o = G.list[order[k]]; o; o = o->next) {
            const sprite_t *f = o->image;
            if (!f)
                continue;
            bool color = false;
            if (o->blinks) {
                if (--o->blink_ct == 0) {
                    o->blinks--;
                    o->blink_ct = (o->blinks & 1) ? o->draw_on : o->draw_off;
                }
                color = (o->blinks & 1) != 0;
            }
            if (!obj_visible(o))
                continue;
            const int x = left_edge(o, f) - G.org_x;
            const int y = (uint16_t)(o->y + f->img_yoff) - G.org_y;
            if (color)
                draw_sprite_color(s, f, x, y, o->xdir != 0, (uint8_t)o->blink_color);
            else
                draw_sprite(s, f, x, y, o->xdir != 0);
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

static bool load_set(const archive_t *arc, int k)
{
    char path[32];
    snprintf(path, sizeof path, "%s.spr", image_sets[k].name);
    size_t len;
    uint8_t *file = arc_load(arc, path, &len);
    if (!file || !spriteset_parse(&G.sets[k], file, len))
        return false;
    for (int f = 0; f < image_sets[k].count && f < G.sets[k].count; f++)
        G.img[image_sets[k].base + f] = &G.sets[k].frames[f];
    return true;
}

bool game_need_set(int base)
{
    for (int k = 0; k < IMG_SETS; k++)
        if (image_sets[k].base == base)
            return G.sets[k].file || (G.arc && load_set(G.arc, k));
    return false;
}

/* The defaults ROC.EXE starts with: the status display, hints and blood on, _KeyTrans the arrow
 * keys, Ctrl jump, Alt attack, Space the special item and Enter look (ROC.CFG can change them). */
session_t Sess = {
    .diff_mode = 2, .game_flags = 4, .status_on = 1, .message_on = 1, .blood_on = 1,
    .next_score_up = 10000, .old_next_score_up = 10000,
    .key_trans = {0x4B, 0x4D, 0x48, 0x50, 0x1D, 0x38, 0x39, 0x1C},
    /* (with no ROC.CFG, the port's: sound and music on, 8 voices, full volume) */
    .sound_card = 0, .music_card = 0, .sound_voices = 8, .sound_volume = 255,
    .music_volume = 255,
};

bool game_start(level_t *lv, const archive_t *arc, char *err, size_t errlen)
{
    game_end();
    G.lv = lv;
    G.level = (uint8_t)lv->number;
    G.seed = 1;
    G.diff_mode = Sess.diff_mode;
    /* NEWLEVELSETUP keeps _GameFlags but for climbing, attacking and recovering */
    G.game_flags = Sess.game_flags & 0xFFF4;
    G.old_game_flags = Sess.old_game_flags & ~0x4000;
    G.lightning_flag = Sess.lightning_flag;
    G.blood_on = Sess.blood_on;
    G.status_on = Sess.status_on;
    G.message_on = Sess.message_on;
    G.gamma = Sess.gamma;
    G.next_score_up = G.old_next_score_up = Sess.next_score_up;
    G.next_second = 0x246;
    G.menu_coming = -1;
    G.cheat_attempt = 200; /* (the EXE starts it at 0x32, which only Backspace leaves) */
    /* NEWLEVELSETUP: what a death goes back to, and the castle levels' warp table
     * (roc1warp.008 for castle1, .009 for castle2). */
    Inv.weapon_status[0] = Inv.weapon_status[1] = 0;
    G.old_inv = Inv;
    G.max_monster_number = G.diff_mode < 3 ? 1 : G.diff_mode == 3 ? 2 : 3;
    if (G.level == 5 || G.level == 6) {
        size_t len;
        uint8_t *w = arc_load(arc, G.level == 5 ? "roc1warp.008" : "roc1warp.009", &len);
        for (size_t i = 0; w && i < 20 && i * 2 + 1 < len; i++)
            G.warp[i / 2][i % 2] = (uint16_t)(w[i * 2] | w[i * 2 + 1] << 8);
        free(w);
    }
    if (G.level == 0x19) {
        /* READITEM(0x62): the demon's palette steps */
        size_t len;
        G.torch_change = arc_load(arc, "rocdemfd.txt", &len);
        if (G.torch_change && len < 16 * 0x150) {
            free(G.torch_change);
            G.torch_change = NULL;
        }
    }
    G.arc = arc;
    memcpy(G.virt_pal, lv->palette, sizeof G.virt_pal);
    memcpy(G.level_pal, lv->palette, sizeof G.level_pal);
    G.max_x = (uint16_t)(lv->cols * TILE);
    G.max_y = (uint16_t)(lv->rows * TILE);
    memcpy(G.key_trans, Sess.key_trans, sizeof G.key_trans);
    /* NEWLEVELSETUP */
    G.window_xsize = 0x2A;
    G.center_off = 'V';

    /* LOADOBJ: load each named set into its _Img slots, then place the objects with a
     * position. The list holds frame 0's top left corner, 8 pixels low. */
    for (int i = 0; i < lv->object_count; i++) {
        const int k = find_set(lv->objects[i].name);
        if (k < 0 || G.sets[k].file)
            continue;
        if (!load_set(arc, k)) {
            snprintf(err, errlen, "can't load %s.spr from ROC.DAT", image_sets[k].name);
            game_end();
            return false;
        }
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

    /* NEWLEVELSETUP counts what PERCENTWIN shows of a level: the gems (tile type 0x71 one, 0x72
     * five, in the background or a bonus tile) and secrets (0x74; one fewer on level 18), and
     * the placed objects the weapons can hit: statues (priority 2) and enemies. */
    if (G.level < 0x1B) {
        for (int i = 0; i < lv->rows * lv->cols; i++) {
            const uint16_t bt = lv->bg_type[lv->bg[i] & 0x1FF];
            const uint8_t f = lv->fg[i];
            const uint16_t ft = f & 0x80 ? 0 : lv->bon_type[f & 0x7F];
            if (bt == 0x72 || ft == 0x72)
                G.gem_in_level = (uint16_t)(G.gem_in_level + 5);
            else if (bt == 0x71 || ft == 0x71)
                G.gem_in_level++;
            if (bt == 0x74)
                G.secret_in_level++;
        }
        if (G.level == 0x12)
            G.secret_in_level--;
        for (const obj_t *o = G.list[LIST_MAIN]; o; o = o->next) {
            if (o->flags & OF_HITTABLE) {
                if (o->priority == 2)
                    G.statue_in_level++;
                else
                    G.enemy_in_level++;
            }
        }
    }
    /* SETUPHANDLES: the level's animated tiles */
    animtile_setup();

    /* INITLEVEL: the ambient sound and camera objects, the hit points display and the gem
     * counter (a gem and four digits); then each level's own objects: the scrollers, the
     * dragon's necks, the cave's torches, the night stars, the boulders and the skeleton
     * boss; a level entered by a lightning warp (_LightningFlag) starts with the lightning. */
    if (G.level < 0x1B) {
        spawn_obj(0x5C, LIST_FORE, NULL, 0, 0);
        spawn_obj(0x08, LIST_FORE, NULL, 0, 0);
        if (G.status_on) {
            G.hit_pts = spawn_obj(0x0F, LIST_SLOW, NULL, 0, 0);
            if (!G.lightning_flag)
                G.hit_pts->gv[0] = 0;
            for (int i = 0; i < 5; i++)
                spawn_obj(0x09, LIST_SLOW, NULL, 0, 0x10)->gv[0] = (int16_t)i;
        }
        if (G.lightning_flag && G.you) {
            spawn_obj(0x37, LIST_FORE, NULL, G.you->x, G.you->y);
            G.you->gv[5] &= 0xFFEF;
        }
    }
    if (G.diff_mode > 2 && (G.level == 1 || G.level == 3 || G.level == 0x16))
        spawn_obj(0x29, LIST_FORE, NULL, 0, 0); /* AUTOSCROLLER */
    if (G.level == 10 && G.boss)
        for (int i = 0; i < 4; i++)
            spawn_obj(0x4B, LIST_MAIN, G.img[IMG_DRAGNECK], G.boss->x, (uint16_t)(G.boss->y - 4));
    if (G.level == 0x0D) {
        spawn_obj(0x2B, LIST_FORE, NULL, 0, 0); /* CAVESCROLLER */
        spawn_obj(0x2C, LIST_MAIN, G.img[IMG_TORCH], 0xB0, 0xE4);
        for (uint16_t x = 0x1F0; x < 0xA00; x = (uint16_t)(x + 0x140))
            spawn_obj(0x2C, LIST_MAIN, G.img[IMG_TORCH], x, 0x96);
    }
    if (G.level == 0x0B || G.level == 0x0C)
        for (uint16_t i = 0; i < 0x10; i++) {
            const uint16_t x = (uint16_t)(i * 0x12 + 0x10);
            spawn_obj(0x16, LIST_MAIN, NULL, x, frandom(0x82));
        }
    if (G.level == 0x11 || G.level == 0x13)
        spawn_obj(0x11, LIST_FORE, NULL, 0, 0); /* BOULDERSPAWNER */
    if (G.level == 0x14) {
        /* the skeleton boss and its head, under the floor */
        G.enemy_in_level++;
        G.boss = spawn_obj(0x53, LIST_MAIN, NULL, 0xEF, 0x11E);
        obj_t *head = spawn_obj(0x53, LIST_MAIN, NULL, 0xEF, 0x11E);
        G.neck[0] = head;
        head->animator = A_DOSKELHEAD;
        head->gv[0] = 1;
        head->flags |= OF_HITTABLE;
    }
    game_view_on_player();
    return true;
}

void game_view_on_player(void)
{
    /* NEWLEVEL: the view starts with the player 0x8c pixels from the left and 0x4c from the
     * top, and a 42 x 50 dead zone; the boss rooms of levels 15, 20 and 25 (and the levels
     * after 26) start at a fixed place. */
    if (G.level == 0x0F || G.level > 0x1A || G.level == 0x14 || G.level == 0x19) {
        G.org_x = G.level == 0x19 ? 0x50 : 0;
        G.org_y = G.level == 0x14 ? 0x60 : G.level == 0x19 ? 0x50 : 0;
        return;
    }
    if (G.you) {
        int x = G.you->x - 0x8C, y = G.you->y - 0x4C;
        x = x < 0 ? 0 : x > G.max_x - 320 ? G.max_x - 320 : x;
        y = y < 0 ? 0 : y > G.max_y - 200 ? G.max_y - 200 : y;
        G.org_x = (uint16_t)x;
        G.org_y = (uint16_t)y;
    }
    set_scroll_bounds(0x32, G.window_xsize);
    G.window_ysize = 50;
}

int game_image_index(const sprite_t *image)
{
    if (!image)
        return -1;
    for (int i = 0; i < IMG_COUNT; i++)
        if (G.img[i] == image)
            return i;
    return -1;
}

const sprite_t *game_image(int index)
{
    if (index < 0 || index >= IMG_COUNT)
        return NULL;
    for (int k = 0; k < IMG_SETS && !G.img[index]; k++)
        if (index >= image_sets[k].base && index < image_sets[k].base + image_sets[k].count)
            game_need_set(image_sets[k].base);
    return G.img[index];
}

void game_clear_objects(void)
{
    sound_level_end();
    free_dead();
    for (int id = 0; id < LIST_COUNT; id++) {
        while (G.list[id]) {
            obj_t *o = G.list[id];
            G.list[id] = o->next;
            free(o);
        }
    }
}

void game_keep_session(void)
{
    Sess.game_flags = G.game_flags;
    Sess.old_game_flags = G.old_game_flags;
    Sess.lightning_flag = G.lightning_flag;
    Sess.status_on = G.status_on;
    Sess.message_on = G.message_on;
    Sess.blood_on = G.blood_on;
    Sess.gamma = G.gamma;
    Sess.next_score_up = G.next_score_up;
    Sess.diff_mode = G.diff_mode;
}

void game_end(void)
{
    if (G.lv)
        game_keep_session();
    game_clear_objects();
    animtile_free();
    free(G.torch_change);
    for (int i = 0; i < IMG_SETS; i++)
        spriteset_free(&G.sets[i]);
    memset(&G, 0, sizeof G);
}
