/* Level objects (RXOBJF.C): the .led object list turned into sprites placed on the map.
 *
 * LOADOBJ walks the list, loads each named sprite set (LOADSPRITE into its _Img slot) and,
 * for entries with a position, calls INITOBJ, which allocates the OBJstruct, picks the
 * behaviour by sprite set (SETUPOBJ) and the first frame (FOBJIMAGE). The viewer stops
 * there: objects are placed with their first frame and don't move yet. */
#ifndef ROC_OBJECT_H
#define ROC_OBJECT_H

#include "archive.h"
#include "gfx.h"
#include "level.h"

typedef struct {
    char name[21];
    spriteset_t set;
} sprite_slot_t;

/* The part of OBJstruct the viewer needs. x / y are map pixels, as in the original: the
 * sprite is drawn at (x + ImgXoff, y + ImgYoff) of the current frame. */
typedef struct {
    const sprite_slot_t *slot;
    int frame;
    int x, y;
    bool flip;        /* OBJstruct.xDir: drawn mirrored by _RevSprite */
    const char *info; /* SPRITE_LIST.info, a per-object parameter ("l", "vt07", "27"...) */
} object_t;

typedef struct {
    sprite_slot_t *slots;
    int slot_count;
    object_t *objs;
    int count;
} objects_t;

/* Load the sprite sets a level names and place its objects. */
bool objects_load(objects_t *o, const level_t *lv, const archive_t *arc, char *err,
                  size_t errlen);
void objects_free(objects_t *o);

void objects_draw(const objects_t *o, surface_t *s, int cam_x, int cam_y);

#endif
