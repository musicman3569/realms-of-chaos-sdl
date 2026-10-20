/* The object engine: game objects, their lists and the per-tick update, as in SPRITE.C /
 * MOVE.C / RXOBJF.C / RXGENERL.C.
 *
 * An object is the original OBJstruct (48 bytes) and USERstruct (27 bytes) in one struct,
 * with the same fields and the same 16-bit arithmetic: positions are map pixels, and
 * xPart / yPart accumulate xSpeed / ySpeed so that a carry out of 16 bits means "one pixel"
 * (or "run the animator now", for objects with flag 0x40).
 *
 * The Animator stays what it was in the original: a 16-bit offset of a behaviour function in
 * code segment 1000 (ANIMATORUPDATE calls it near). Behaviours compare and assign these
 * values (GENERICFALL checks "Animator == GENERICFALL"), so the port keeps them and looks the
 * C function up in a table. Behaviours that aren't ported yet do nothing. */
#ifndef ROC_ENGINE_H
#define ROC_ENGINE_H

#include "archive.h"
#include "gfx.h"
#include "imagetab.h"
#include "level.h"

/* Timer: _MachInit calls _speedup(0x20), so the timer task runs at 32 x 18.2 Hz and adds
 * _TicAmt (1 while the game runs) to _TimerTics each time. */
#define TICKS_PER_SECOND (1193182.0 / (int)(1193182.0 / 582.4))

enum { LIST_MAIN, LIST_FORE, LIST_SLOW, LIST_OFF, LIST_COUNT };

/* Object flags (OBJstruct.Flags) seen so far. */
enum {
    OF_HIT = 0x02,      /* hit by the player's weapon (ENEMYHIT) */
    OF_PLACED = 0x04,   /* from the level's object list (INITOBJ); spawned objects clear it */
    OF_RATE = 0x40,     /* ANIMATORUPDATE runs the animator only when xPart += xSpeed carries */
};

/* Animator values: offsets of behaviour functions in segment 1000. */
enum {
    A_DOBOMBGUARD = 0x0761, A_DOBOMB = 0x0857, A_DOBACKSTARS = 0x09e5, A_ROSSWAIT = 0x0b92,
    A_ROSSPIECE = 0x0bb0, A_ROSSATTACK = 0x0bce, A_DODROP = 0x0c46, A_DOCATSKELETON = 0x0d95,
    A_DOCAVESPIKES = 0x0da4, A_DOCAVEFALLSPIKES = 0x0ea0, A_DOCAVESPIDER = 0x0fd9,
    A_DOGARGOYLE = 0x0fe8, A_DOCAVEHOLE = 0x1006, A_DOWISP = 0x10b4, A_DOBOAR = 0x1513,
    A_DOBOARSPEAR = 0x1531, A_DOSKELHEAD = 0x1540, A_SKELWALK = 0x157c, A_SKELLUNGE = 0x159a,
    A_DEMONWAIT = 0x15d6, A_DOLEAF = 0x1630, A_DOBIRD = 0x163f, A_DOBAT = 0x1987,
    A_BANDITWALK = 0x192d, A_WOLFRUN = 0x1ca0, A_DOARROWSIGN = 0x3010,
    A_DOCOLLECTABLE = 0x3093, A_XLEVEL = 0x3b1a, A_GENERICBEGINJUMP = 0x3e54,
    A_GENERICJUMP = 0x3f78, A_GENERICUP = 0x3fa4, A_GENERICBEGINFALL = 0x415b,
    A_GENERICFALL = 0x4295,
    A_DOPROJECTILE = 0x46bb, A_DOCONTAINER = 0x5216, A_DOROCKMONSTER = 0x5a18,
    A_GOBLINSTAND = 0x5fc3, A_GOBLINRUN = 0x60e5, A_BOSSHEADBOUNCE = 0x6899,
    A_DOSPITTLE2 = 0x68e4, A_DOSPOREHOLE = 0x725c, A_DOSPORE = 0x7357, A_DOCHAIN = 0x7a52,
    A_DOCHAINEND = 0x7c96, A_FROGPLAT = 0x849b, A_FLOATINGPLATFORM = 0x86a4,
    A_DOCONTAINERBOMB = 0x8893, A_CRUMBLINGPLATFORM = 0x8a20, A_CRUMBLEPLATSHAKE = 0x8a6c,
    A_CRUMBLEPLATFALL = 0x8bd7, A_DOPULLEY = 0x8d31, A_PULLYPLATWAIT = 0x8de5,
    A_PULLYPLATMOVE = 0x8e2b, A_PULLYPLATRETURN = 0x8fbc, A_PULLYPLATDIP = 0x9144,
    A_DOSQUAREPLAT = 0x9207, A_DOFALLINGPLAT = 0x938c,
    A_SWAMPRAFT = 0x9744, A_ROLLINGBOULDER = 0xa63a, A_MUDMANWALK = 0xafca,
    A_DOMOSQUITO = 0xafd9, A_FROGLAND = 0xb006, A_DODRAGONBOB = 0xb0ee,
    A_DRAGONHEADDIE = 0xb139, A_YOUBEGINRUN = 0xb5dd, A_YOUBEGINFALL = 0xb628,
    A_YOUBEGINCROUCH = 0xb655, A_YOUCROUCH = 0xb673, A_YOUBEGINIMPALE = 0xbead,
    A_YOUDIE = 0xbebc, A_YOUCRUSH = 0xbecb, A_NULLANIMATOR = 0xc25f,
};

typedef struct obj obj_t;

struct obj {
    /* OBJstruct */
    obj_t *next;                 /* Link.next */
    int16_t priority;            /* Link.priority: the lists are sorted by it */
    int list;                    /* List: the object's home list (LIST_*) */
    uint16_t blinks, blink_ct, draw_on, draw_off, blink_color;
    const sprite_t *image;       /* ImageID; NULL = not drawn */
    uint16_t flags;
    uint8_t xdir, ydir;          /* xDir 1 = facing right (drawn mirrored) */
    uint16_t x, y;               /* hotspot, map pixels */
    uint16_t xpart, ypart;
    int16_t xspeed, yspeed;
    uint16_t xsize, ysize;       /* BoxCols / BoxRows of the image */
    uint16_t update;             /* how far off screen it keeps running (pixels) */
    uint16_t counter;            /* +1 every tick */
    uint16_t animator;
    /* USERstruct */
    uint16_t old_animator;
    const char *name;            /* the level list's info string, or NULL */
    uint8_t friction, frame_no, max_frames;
    int8_t anim_inc;
    uint16_t frame_count, frame_count_max;
    uint8_t down_offset, up_offset;
    int16_t gv[6];               /* GenericVar */
    uint8_t surface_flags;
    /* port */
    bool dead;                   /* unlinked by OBJKILL, freed after the frame */
    obj_t *free_next;
};

/* The game state the behaviours share (the original's globals). */
typedef struct {
    const level_t *lv;
    uint8_t level;               /* _level */
    uint16_t org_x, org_y;       /* _OrgX / _OrgY: the view's top left corner, map pixels */
    obj_t *list[LIST_COUNT];     /* _MainListHead, _ForeListHead, _SlowListHead, _OffListHead */
    obj_t *you, *boss;           /* _You, _Boss */
    int you_walk, you_jump, you_duck, you_climb, you_attack; /* _YouWalkID...: IMG_ bases */
    obj_t *neck[5];              /* _Neck */
    uint8_t neck_count;          /* DS:50e6 */
    const sprite_t *img[IMG_COUNT]; /* _Img */
    spriteset_t sets[IMG_SETS];    /* the loaded .spr files, by image_sets[] index */
    uint32_t seed;               /* Borland rand() state */
    uint8_t diff_mode;           /* _DiffMode */
    uint16_t game_flags;         /* _GameFlags: bit 2 = playing the male character */
    uint8_t lightning_flag;      /* _LightningFlag */
    uint8_t message_on;          /* _MessageOn: hint messages enabled */
    uint8_t you_invincible;      /* _YouInvincible */
    uint16_t window_xsize;       /* _WindowXsize */
    int16_t center_off;          /* _CenterOff */
    uint16_t old_game_flags;     /* _OldGameFlags */
    int16_t parent_priority;     /* _ParentPriority */
    uint16_t update;             /* _Update: ticks since the last frame */
    obj_t *dead;                 /* killed this frame, to free */
    char unported[64][24];       /* animators reached that have no port yet (logged once) */
    int unported_count;
} game_t;

extern game_t G;

/* Start a level: load the sprite sets its object list names and create the objects
 * (LOADOBJ / INITOBJ / INITLEVEL's part for the level objects). */
bool game_start(const level_t *lv, const archive_t *arc, char *err, size_t errlen);
void game_end(void);

/* One frame of RUNLEVEL: run the animators for `ticks` timer ticks (ANIMATORUPDATE), then
 * move objects between the on- and off-screen lists (SCREENLISTUPDATE). */
void game_frame(uint16_t ticks);
void game_draw(surface_t *s);

/* Engine routines the behaviours call. */
uint16_t frandom(uint16_t n);                       /* FRANDOM: rand() * n / 0x8000 */
obj_t *spawn_obj(uint8_t type, int list, const sprite_t *image, uint16_t x, uint16_t y);
void obj_kill(obj_t *o);                            /* OBJKILL */
void obj_link(obj_t *o, int list);                  /* ADDPLINKNODE: insert by priority */
void obj_image(obj_t *o, const sprite_t *image);    /* OBJIMAGE / FOBJIMAGE */
void next_frame(obj_t *o, int img_base);            /* NEXTFRAME(objp, &_Img.xxxID) */
bool obj_visible(const obj_t *o);                   /* OBJVISIBLE: overlaps the view */
bool obj_onscreen(const obj_t *o);                  /* OBJONSCREEN: wholly in the view */
bool obj_overlap(const obj_t *a, const obj_t *b);   /* OBJOVERLAP: boxes overlap */
uint16_t map_cell(unsigned row, unsigned col);      /* BgMap cell (0 outside the map) */
bool can_go_up(const obj_t *o);
bool can_go_down(const obj_t *o);
bool can_go_left(const obj_t *o);
bool can_go_right(const obj_t *o);
void can_move(obj_t *o);                            /* CANMOVE: sets UpOffset / DownOffset */
void setup_obj(obj_t *o, uint8_t type);             /* SETUPOBJ (setup.c) */

/* MOVE.C: move the player n pixels (PLAYERUP / PLAYERDOWN / PLAYERLEFT / PLAYERRIGHT). The
 * original also checks the tiles entered for bit 0x2000 (grabbable: GRABSTUFF via _Grabber)
 * and scrolls the view; both belong to the player, which isn't ported yet. */
void player_up(uint8_t n);
void player_down(uint8_t n);
void player_left(uint8_t n);
void player_right(uint8_t n);

/* Not ported yet: the player, sound, damage. */
int play_sample(uint8_t n, obj_t *o);               /* PLAYSAMPLE: returns a handle, 0 */
bool sound_link_active(int16_t handle);
void sound_link_kill(int16_t handle);
void hit_player(obj_t *o);                          /* HITPLAYER */
void enemy_hit(obj_t *o, uint8_t damage);           /* ENEMYHIT */
void spike_check(obj_t *o, uint8_t up);             /* SPIKECHECK */

#endif
