#include "object.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sprite_slot_t *find_slot(objects_t *o, const char *name)
{
    for (int i = 0; i < o->slot_count; i++)
        if (!strcmp(o->slots[i].name, name))
            return &o->slots[i];
    return NULL;
}

/* LOADSPRITE: each sprite set is loaded once, the first time the list names it. */
static sprite_slot_t *load_slot(objects_t *o, const archive_t *arc, const char *name)
{
    sprite_slot_t *slot = find_slot(o, name);
    if (slot)
        return slot;
    char path[32];
    snprintf(path, sizeof path, "%s.spr", name);
    size_t len;
    uint8_t *file = arc_load(arc, path, &len);
    if (!file)
        return NULL;
    slot = &o->slots[o->slot_count];
    snprintf(slot->name, sizeof slot->name, "%s", name);
    if (!spriteset_parse(&slot->set, file, len) || slot->set.count == 0) {
        spriteset_free(&slot->set);
        return NULL;
    }
    o->slot_count++;
    return slot;
}

static const sprite_t *frame_of(const object_t *ob)
{
    return ob->frame < ob->slot->set.count ? &ob->slot->set.frames[ob->frame] : NULL;
}

/* INITOBJ's choice of first frame where it isn't frame 0 of the object's own set. */
static void first_frame(objects_t *o, const level_t *lv, object_t *ob)
{
    const char *name = ob->slot->name;
    if (!strcmp(name, "malwalk")) {
        /* The player: _YouWalkID[9], or _YouJumpID[1] on the levels that start in mid-air.
         * _YouWalkID is the male or female set by _GameFlags bit 2; the viewer uses male. */
        const int n = lv->number;
        if (n == 13 || n == 16 || n == 22 || n == 25) {
            const sprite_slot_t *jump = find_slot(o, "maljump");
            if (jump && jump->set.count > 1) {
                ob->slot = jump;
                ob->frame = 1;
            }
        } else {
            ob->frame = 9;
        }
    } else if (!strcmp(name, "bat")) {
        /* SETUPOBJ, type 0x1f: info "l" or "r" sets xDir (the sprites face left). */
        ob->flip = ob->info[0] == 'r';
    } else if (lv->number == 4 && (!strcmp(name, "gargoyle") || !strcmp(name, "gobgarg"))) {
        const sprite_slot_t *garg = find_slot(o, "gargoyle");
        if (garg && garg->set.count > 1) {
            ob->slot = garg;
            ob->frame = 1;
        }
    }
}

bool objects_load(objects_t *o, const level_t *lv, const archive_t *arc, char *err,
                  size_t errlen)
{
    memset(o, 0, sizeof *o);
    if (!lv->object_count)
        return true;
    o->slots = calloc((size_t)lv->object_count, sizeof *o->slots);
    o->objs = calloc((size_t)lv->object_count, sizeof *o->objs);
    if (!o->slots || !o->objs) {
        objects_free(o);
        snprintf(err, errlen, "out of memory");
        return false;
    }

    /* LOADOBJ loads every set first come, first served, and places an object for each entry
     * with a position. The list stores the top left corner of frame 0, 8 pixels low; the
     * object's x / y is its hotspot. The arithmetic is 16-bit unsigned, as in the original,
     * so an entry above the map wraps round and fails the bottom check. */
    for (int i = 0; i < lv->object_count; i++) {
        const level_object_t *e = &lv->objects[i];
        sprite_slot_t *slot = load_slot(o, arc, e->name);
        if (!slot) {
            snprintf(err, errlen, "can't load sprite set %s.spr from ROC.DAT", e->name);
            objects_free(o);
            return false;
        }
        if (e->x == 0xFFFF)
            continue;
        const sprite_t *f0 = &slot->set.frames[0];
        const uint16_t x = (uint16_t)(e->x - f0->img_xoff);
        const uint16_t y = (uint16_t)(e->y - f0->img_yoff - 8);
        if (y > lv->rows * TILE - 1)
            continue;
        object_t *ob = &o->objs[o->count++];
        ob->slot = slot;
        ob->x = x;
        ob->y = y;
        ob->info = e->info;
    }
    /* After all the sets are in: the player's jump frames may come later in the list. */
    for (int i = 0; i < o->count; i++)
        first_frame(o, lv, &o->objs[i]);
    return true;
}

void objects_free(objects_t *o)
{
    for (int i = 0; o->slots && i < o->slot_count; i++)
        spriteset_free(&o->slots[i].set);
    free(o->slots);
    free(o->objs);
    memset(o, 0, sizeof *o);
}

void objects_draw(const objects_t *o, surface_t *s, int cam_x, int cam_y)
{
    /* As the list drawer at 3cb4:064c: unflipped at x + ImgXoff; flipped (xDir set) the
     * sprite is mirrored about its box, at x - (Cols - BoxCols + ImgXoff). */
    for (int i = 0; i < o->count; i++) {
        const object_t *ob = &o->objs[i];
        const sprite_t *f = frame_of(ob);
        if (!f)
            continue;
        const int x = ob->flip ? ob->x - (f->cols - f->box_cols + f->img_xoff)
                               : ob->x + f->img_xoff;
        const int y = ob->y + f->img_yoff;
        if (x - cam_x >= s->w || y - cam_y >= s->h || x + f->cols <= cam_x ||
            y + f->rows <= cam_y)
            continue;
        draw_sprite(s, f, x - cam_x, y - cam_y, ob->flip);
    }
}
