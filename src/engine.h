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
/* INSTALLTIMER rates are in fours of ticks (the timer task counts the timers every fourth run):
 * NEWLEVEL installs LEVELTIMER at 0x0c (about 12 runs a second) and the title's TITLETIMER at
 * 0x24. */
enum { LEVEL_TIMER_RATE = 0x0C, TITLE_TIMER_RATE = 0x24 };

enum { LIST_MAIN, LIST_FORE, LIST_SLOW, LIST_OFF, LIST_COUNT };

/* Object flags (OBJstruct.Flags) seen so far. */
enum {
    OF_HIT = 0x02,      /* hit by the player's weapon (ENEMYHIT) */
    OF_PLACED = 0x04,   /* from the level's object list (INITOBJ); spawned objects clear it */
    OF_HITTABLE = 0x10, /* the whip and fireballs can hit it */
    OF_RATE = 0x40,     /* ANIMATORUPDATE runs the animator only when xPart += xSpeed carries */
};

/* _YouInventory (INVENTORY, DS:554b). The arrays are indexed by the character: 0 the female,
 * 1 the male ((_GameFlags & 4) != 0). It lasts from level to level. */
typedef struct {
    uint32_t score;
    uint16_t gems;               /* also the fireballs' ammunition */
    uint8_t max_hit_points[2];
    uint8_t hit_points[2];
    uint8_t weapon_status[2];    /* fireballs (two at a time) / the whip in flight */
    uint8_t flags;               /* bit 0 male poisoned, 1 female poisoned, 2-3 whip power,
                                  * 4-5 fireball power (Poisoned1/2, WeaponPower1/2) */
} inventory_t;

extern inventory_t Inv;

static inline unsigned whip_power(void) { return Inv.flags >> 2 & 3; }
static inline unsigned fire_power(void) { return Inv.flags >> 4 & 3; }

/* Animator values: offsets of behaviour functions in segment 1000. */
enum {
    A_DOKNIFEGUARD = 0x0291, A_DOGUARDKNIFE = 0x0699, A_DOBOMBGUARD = 0x0761, A_DOBOMB = 0x0857, A_BOMBEXPLODER = 0x08fc, A_DOBACKSTARS = 0x09e5, A_ROSSWAIT = 0x0b92,
    A_ROSSCRASH = 0x0ba1, A_ROSSPIECE = 0x0bb0, A_ROSSATTACK = 0x0bce, A_ROSSTHROW = 0x0bdd,
    A_ROSSLUNGE = 0x0bec, A_ROSSSWORD = 0x0bfb, A_ROSSSHAKE = 0x0c0a, A_ROSSKNIFE = 0x0c19,
    A_ROSSDIE = 0x0c28, A_ROSSCLEAR = 0x0c37, A_BOSSHITPOINTS = 0x54cb, A_DODROP = 0x0c46, A_DOTORCH = 0x0d75, A_SHIELDGEM = 0x1caf, A_LIGHTNINGGEM = 0x21f4, A_DEATHGEM = 0x23fd,
    A_SWORDGEM = 0x2561, A_DOLIGHTNING = 0x2fcb, A_DOCATSKELETON = 0x0d95,
    A_DOCAVESPIKES = 0x0da4, A_DOCAVEFALLSPIKES = 0x0ea0, A_DOCAVESPIDER = 0x0fd9,
    A_DOGARGOYLE = 0x0fe8, A_DOGARGOYLEFIRE = 0x0ff7, A_DOCAVEHOLE = 0x1006,
    A_DOTORCHPUFF = 0x1015, A_DOWISP = 0x10b4, A_DOBOAR = 0x1513,
    A_DOBOARATTACK = 0x1522, A_DOBOARSPEAR = 0x1531, A_DOSKELHEAD = 0x1540, A_SKELWAIT = 0x154f,
    A_SKELRISE = 0x155e, A_SKELSINK = 0x156d, A_SKELWALK = 0x157c, A_SKELHACK = 0x158b,
    A_SKELLUNGE = 0x159a, A_SKELWAIT2 = 0x15a9, A_SKELDIE = 0x15b8,
    A_DEMONCENTER = 0x15c7, A_DEMONWAIT = 0x15d6, A_DEMONSPIT = 0x15e5, A_DEMONSUMMON = 0x15f4,
    A_DEMONSPOUT = 0x1603, A_DEMONDROP = 0x1612, A_DEMONDIE = 0x1621, A_POWERUPSPARKS = 0x26eb,
    A_DOLEAF = 0x1630, A_DOBIRD = 0x163f, A_DOBAT = 0x1987,
    A_BANDITWALK = 0x192d, A_BOLOCATFALL = 0x193c, A_BOLOCATCOCK = 0x194b,
    A_BOLOCATCOCK2 = 0x195a, A_BOLOCATTHROW = 0x1969, A_DOBOLO = 0x1978, A_WOLFRUN = 0x1ca0, A_DOARROWSIGN = 0x3010,
    A_DOEXPLOSION = 0x2e48, A_EXPLODERSPAWNER = 0x2d81, A_DOFLYINGSHIT = 0x27ed,
    A_DOCOLLECTABLE = 0x3093, A_XLEVEL = 0x3b1a, A_SCREENCENTER = 0x53f4, A_GENERICBEGINJUMP = 0x3e54,
    A_ENEMYHITPAUSE = 0x500d, A_SPLASHSPAWNER = 0x57cc, A_DOHINTS = 0x5728, A_OFFKILLER = 0xae48,
    A_DOFIREBALL = 0xb6a0, A_DOWHIP = 0xbaa6, A_DROPGEMSPAWNER = 0xbeda, A_DOHITPOINTS = 0xbee9,
    A_DOTRANSFORMER = 0xbef8, A_DOGEMNUMBER = 0xbf07, A_DOBLOOD = 0xbff4,
    A_GENERICJUMP = 0x3f78, A_GENERICUP = 0x3fa4, A_GENERICBEGINFALL = 0x415b,
    A_GENERICFALL = 0x4295,
    A_DOPROJECTILE = 0x46bb, A_DOCONTAINER = 0x5216, A_DOROCKMONSTER = 0x5a18,
    A_GOBLINSTAND = 0x5fc3, A_GOBLINRUN = 0x60e5, A_GOBLINHIT = 0x6448, A_GOBLINFIST = 0x65be, A_BOSSCENTER = 0x685d,
    A_BOSSHEADSPIT = 0x686c, A_BOSSHEADSHAKE = 0x687b, A_BOSSHEADFLOAT = 0x688a,
    A_BOSSHEADBOUNCE = 0x6899, A_BOSSHEADSPAWN = 0x68a8, A_BOSSHEADRETURN = 0x68b7,
    A_BOSSHEADDIE = 0x68c6, A_DOSPITTLE = 0x68d5, A_DOSPITTLE2 = 0x68e4, A_BOSSSHAKE = 0x68f3,
    A_SPINNINGPLATFORM = 0xaa91, A_NOSPINPLAT = 0xabdd, A_SPINPLATDIE = 0xac83, A_CRUSHERCENTER = 0x74c8, A_DOSPOREHOLE = 0x725c, A_DOSPORE = 0x7357, A_DOCHAIN = 0x7a52, A_AUTOSCROLLER = 0x7cb1, A_CAVESCROLLER = 0x7f3b,
    A_DOCHAINEND = 0x7c96, A_FROGPLAT = 0x849b, A_FLOATINGPLATFORM = 0x86a4,
    A_DOCONTAINERBOMB = 0x8893, A_CRUMBLINGPLATFORM = 0x8a20, A_CRUMBLEPLATSHAKE = 0x8a6c,
    A_CRUMBLEPLATFALL = 0x8bd7, A_DOPULLEY = 0x8d31, A_PULLYPLATWAIT = 0x8de5,
    A_PULLYPLATMOVE = 0x8e2b, A_PULLYPLATRETURN = 0x8fbc, A_PULLYPLATDIP = 0x9144,
    A_DOSQUAREPLAT = 0x9207, A_DOFALLINGPLAT = 0x938c, A_BRIKSHAKE = 0x9659,
    A_SWAMPRAFT = 0x9744, A_DOREFLECTION = 0xae8b, A_DOSNAKEPLAT = 0x9fd1, A_BOULDERSPAWNER = 0xa3dc, A_ROLLINGBOULDER = 0xa63a, A_STILLBOULDER = 0xaa0e, A_MUDMANWALK = 0xafca,
    A_DOMOSQUITO = 0xafd9, A_DOFROG = 0xafe8, A_FROGLAUNCH = 0xaff7,
    A_FROGLAND = 0xb006, A_DRAGONCENTER = 0xb0df, A_DODRAGONBOB = 0xb0ee,
    A_DODRAGONSPIT = 0xb0fd, A_DODRAGONFIRE = 0xb10c, A_DODRAGONBOOM = 0xb11b,
    A_DODRAGONNECK = 0xb12a, A_DRAGONHEADDIE = 0xb139, A_DRAGONNECKDIE = 0xb148, A_YOUBEGINRUN = 0xb5dd, A_YOURUN = 0xb5ec,
    A_YOUBEGINJUMP = 0xb5fb, A_YOUJUMP = 0xb60a, A_YOUUP = 0xb619, A_YOUBEGINFALL = 0xb628,
    A_YOUFALL = 0xb637, A_YOUHORIZ = 0xb646, A_YOUBEGINCROUCH = 0xb655, A_YOUBEGINSTUN = 0xb664,
    A_YOUCROUCH = 0xb673, A_YOUBEGINSHOOT = 0xb682, A_YOUSHOOT = 0xb691,
    A_YOUBEGINWHIP = 0xba88, A_YOUWHIP = 0xba97, A_YOUBEGINCLIMB = 0xbe53, A_YOUCLIMB = 0xbe62,
    A_YOUCLIMBMOVE = 0xbe71, A_YOUBEGINSCRAMBLE = 0xbe80, A_YOUSCRAMBLE = 0xbe8f,
    A_YOUHIT = 0xbe9e, A_YOUBEGINIMPALE = 0xbead, A_YOUDIE = 0xbebc, A_YOUCRUSH = 0xbecb,
    A_NULLANIMATOR = 0xc25f, A_DOAMBIENTNOISE = 0xc02d,
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
    obj_t *child;                /* SWAMPRAFT's front piece (a near pointer in GenericVar[0]) */
    obj_t *parent;               /* a wisp's light: the wisp; a reflection: what it reflects
                                  * (near pointers in GenericVar[0]) */
    bool dead;                   /* unlinked by OBJKILL, freed after the frame */
    obj_t *free_next;
};

/* The game state the behaviours share (the original's globals). */
typedef struct {
    level_t *lv;                 /* GRABSTUFF changes its maps */
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
    int8_t lightning_flag;       /* _LightningFlag: a warp's lightning (> 0 in, < 0 out) */
    uint8_t next_level;          /* _nextlevel: where a lightning warp out goes */
    uint8_t message_on;          /* _MessageOn: hint messages enabled */
    uint8_t you_invincible;      /* _YouInvincible */
    uint16_t window_xsize;       /* _WindowXsize: width of the scrolling dead zone */
    uint8_t window_ysize;        /* _WindowYsize: its height */
    int8_t center_off;           /* _CenterOff: SCREENCENTER keeps the player this far down */
    int8_t looking;              /* _Looking: 1 up, -1 down */
    int8_t wisp_walk;            /* _WispWalk: forced walking (-1 left, 1 right) */
    uint16_t vine_x, vine_y;     /* _Vine_x / _Vine_y: the vine cell being climbed */
    uint16_t scroll_left, scroll_right, scroll_upper, scroll_lower; /* _Scroll*Bound */
    uint16_t max_x, max_y;       /* _MaxX / _MaxY: map size in pixels */
    uint8_t keys[128];           /* _Keys: pressed, by PC scancode */
    uint16_t key_trans[8];       /* _KeyTrans: left, right, up, down, jump, attack, item, look */
    uint8_t quitflag;            /* _quitflag: 3 level done, 4 dead, 6 warped */
    uint8_t blood_on, status_on; /* _BloodOn, _StatusOn (both on in ROC.EXE) */
    int16_t hitagain;            /* _hitagain: LEVELTIMER runs left before another hit counts */
    uint8_t free_hit;            /* _FreeHit: the next HITPLAYER needn't touch the player */
    uint8_t hit_release;         /* _HitRelease */
    obj_t *hit_pts;              /* _HitPts: the hit points display */
    int8_t whip_fc, whip_fcm, whip_fn, whip_ai; /* _WhippingFC / FCM / FN / AI */
    obj_t *whip_hits[8];         /* DS:553b: objects this crack of the whip has hit */
    uint8_t whip_hit_count;      /* DS:553a */
    uint16_t secret_found, gem_found, enemy_found, statue_found, enemy_in_level;
    uint16_t gem_in_level, secret_in_level, statue_in_level; /* _GemInLevel... (NEWLEVELSETUP) */
    uint8_t monster_counter, monster_number, max_monster_number;
    uint8_t monster_spawns;      /* DS:38da: GRABSTUFF's spawns from one tile (3 clear it) */
    uint16_t warp[10][2];        /* _WarpCoords: castle1 / castle2 tile types 10-19 */
    uint16_t timer_ticks;        /* ticks towards the next LEVELTIMER run */
    uint16_t splash_side;        /* DS:38c2: SPLASHSPAWNER's side for the next bit */
    inventory_t old_inv;         /* _OldScore, _OldGems, _OldMaxHitPoints: back after a death */
    uint16_t old_game_flags;     /* _OldGameFlags: the hints already shown (0x4000: one is up) */
    uint8_t gamma;               /* _GammaCorrect: 0-16, added to the palette (F11) */
    uint8_t gamma_armed;         /* DS:395d: F11 was let go */
    uint8_t status_armed, status_count; /* DS:395c / 395e: F5's debounce */
    int8_t menu_coming;          /* _MenuComing: the window the next frame opens (ui.h) */
    uint8_t pause_hit, main_pausing; /* _PauseHit (the Pause key), _MainPausing */
    uint8_t cheat_attempt, cheat_count; /* _CheatAttempt / _CheatCount */
    uint32_t next_score_up, old_next_score_up; /* _NextScoreUp: the next 10000 points */
    uint32_t level_tics, next_second; /* the level's clock (_TimerTics, _NextSecond) */
    uint8_t timer_fix, second_in_level, minute_in_level; /* _TimerFix, _SecondInLevel... */
    int16_t parent_priority;     /* _ParentPriority */
    uint16_t update;             /* _Update: ticks since the last frame */
    const archive_t *arc;        /* ROC.DAT, for what the behaviours load (AOPEN / READITEM) */
    uint8_t virt_pal[768];       /* _VirtPal: the level's palette as loaded */
    uint8_t level_pal[768];      /* _LevelPal: the palette the level fades back to */
    uint8_t *torch_change;       /* _TorchChange: level 25's PALCH table (rocdemfd.txt) */
    uint16_t face_timer;         /* DS:38c0: DEMONFACE's calls since the face last changed */
    obj_t *dead;                 /* killed this frame, to free */
    char unported[64][24];       /* animators reached that have no port yet (logged once) */
    int unported_count;
} game_t;

extern game_t G;

/* What outlives a level (the original's globals NEWLEVELSETUP leaves alone): G holds them while
 * a level runs; game_start takes them from here and game_end puts them back. */
typedef struct {
    uint8_t diff_mode;           /* _DiffMode: 0 Super Easy ... 4 Psycho Warrior */
    uint16_t game_flags;         /* _GameFlags: bit 2 the male hero, the hints' bits */
    uint16_t old_game_flags;     /* _OldGameFlags: the hints already shown */
    int8_t lightning_flag;       /* _LightningFlag: 1 when a new game starts (the hero arrives in
                                  * lightning) */
    uint8_t status_on, message_on, blood_on, gamma; /* the settings (ROC.CFG) */
    char blood_pass[20];         /* _BloodPass (ROC.CFG) */
    uint32_t next_score_up, old_next_score_up;
    uint16_t key_trans[8];       /* _KeyTrans (ROC.CFG) */
    int16_t sound_card, music_card; /* _SoundCardType / _MusicCardType (ROC.CFG; -1 none) */
    int16_t sound_voices, sound_volume, music_volume; /* _SoundVoices, _SoundVolume (0-255),
                                  * _MusicVolume (ROC.CFG) */
    uint8_t joystick_on;         /* _JoystickOn (ROC.CFG: SETUP's joystick choice) */
    uint8_t my_button[4];        /* _MyBUTTON0-3: the _KeyTrans entry each button presses */
    uint16_t joy_cal[6];         /* _Joy as ROC.CFG keeps it: Xsense, Xlo, Xhi, Ysense, Ylo, Yhi */
} session_t;

extern session_t Sess;

/* Start a level: load the sprite sets its object list names and create the objects
 * (LOADOBJ / INITOBJ / INITLEVEL's part for the level objects). */
bool game_start(level_t *lv, const archive_t *arc, char *err, size_t errlen);
void game_keep_session(void);                       /* copy G's lasting state to Sess */
void game_clear_objects(void);                      /* free every object */
int game_image_index(const sprite_t *image);        /* _Img index of a frame, or -1 */
const sprite_t *game_image(int index);              /* _Img[index], loading its set */
void game_end(void);
void game_view_on_player(void);                     /* NEWLEVEL's starting view */
bool game_need_set(int base);                       /* load an IMG_ set if the level didn't */
void spawn_hints(void);                             /* SPAWNHINTS (rxgenerl.c) */

/* Animated tiles (animtile.c) */
void animtile_setup(void);                          /* INITANTIL + SETUPHANDLES for G.level */
void animtile_tick(uint16_t ticks);                 /* the timer task's counting */
void animtile_run(void);                            /* RUNANIMTILES: step the due cells */
void animtile_free(void);                           /* DEANIMATE them all */

/* One frame of RUNLEVEL: run the animators for `ticks` timer ticks (ANIMATORUPDATE), then
 * move objects between the on- and off-screen lists (SCREENLISTUPDATE). */
void game_frame(uint16_t ticks);
void game_draw(surface_t *s);

/* Engine routines the behaviours call. */
uint16_t frandom(uint16_t n);                       /* FRANDOM: rand() * n / 0x8000 */
obj_t *spawn_obj(uint8_t type, int list, const sprite_t *image, uint16_t x, uint16_t y);
void obj_kill(obj_t *o);                            /* OBJKILL */
void reflection_check(obj_t *o);                    /* the swamp: PARENTLISTADD if it reflects */
void obj_link(obj_t *o, int list);                  /* ADDPLINKNODE: insert by priority */
void obj_image(obj_t *o, const sprite_t *image);    /* OBJIMAGE / FOBJIMAGE */
void next_frame(obj_t *o, int img_base);            /* NEXTFRAME(objp, &_Img.xxxID) */
bool obj_visible(const obj_t *o);                   /* OBJVISIBLE: overlaps the view */
bool obj_onscreen(const obj_t *o);                  /* OBJONSCREEN: wholly in the view */
bool obj_overlap(const obj_t *a, const obj_t *b);   /* OBJOVERLAP: boxes overlap */
bool obj_collide(const obj_t *a, const obj_t *b);   /* OBJCOLLIDE: the frames' hit boxes overlap */
void obj_blink(obj_t *o, uint16_t blinks, uint16_t on, uint16_t off); /* OBJBLINK */
uint16_t map_cell(unsigned row, unsigned col);      /* BgMap cell (0 outside the map) */
uint8_t map_fg(unsigned row, unsigned col);         /* FgMap cell (0 outside the map) */
bool can_go_up(const obj_t *o);
bool can_go_down(const obj_t *o);
bool can_go_left(const obj_t *o);
bool can_go_right(const obj_t *o);
void can_move(obj_t *o);                            /* CANMOVE: sets UpOffset / DownOffset */
void setup_obj(obj_t *o, uint8_t type);             /* SETUPOBJ (setup.c) */

/* MOVE.C: move an object (the player) n pixels, check the tiles it enters for bit 0x2000
 * (grabbable: _Grabber, i.e. GRABSTUFF) and scroll the view when it leaves the dead zone
 * (PLAYERUP / PLAYERDOWN / PLAYERLEFT / PLAYERRIGHT). */
void player_up(uint8_t n, obj_t *o);
void player_down(uint8_t n, obj_t *o);
void player_left(uint8_t n, obj_t *o);
void player_right(uint8_t n, obj_t *o);

/* 3cb4: scrolling. SETSCROLLBOUNDS centres a w x h dead zone in the view. */
void set_scroll_bounds(uint16_t h, uint16_t w);
void scroll_right(uint8_t n);
void scroll_left(uint8_t n);
void scroll_up(uint8_t n);
void scroll_down(uint8_t n);

/* An action key (_Keys[_KeyTrans[i]]). */
enum { K_LEFT, K_RIGHT, K_UP, K_DOWN, K_JUMP, K_ATTACK, K_ITEM, K_LOOK };
static inline bool key(int action)
{
    return G.keys[G.key_trans[action] & 0x7F] != 0;
}

void grab_stuff(unsigned row, unsigned col);        /* GRABSTUFF (rxgrab.c) */
void map_set_cell(unsigned row, unsigned col, uint16_t v);
void map_set_fg(unsigned row, unsigned col, uint8_t v);

/* The player's hit points (_YouInventory.HitPoints[(_GameFlags & 4) != 0]). */
static inline uint8_t *you_hp(void)
{
    return &Inv.hit_points[(G.game_flags & 4) != 0];
}

/* Damage (rxgenerl.c, rxforest.c). */
int hit_player(obj_t *o);                           /* HITPLAYER */
void enemy_hit(obj_t *o, uint8_t max_hits);         /* ENEMYHIT */
void spike_check(obj_t *o, uint8_t is_wall);        /* SPIKECHECK */
void maintain_block(obj_t *o);                      /* MAINTAINBLOCK: solid objects push the player */

/* Sound (sound.c, RXSAMPLF.C). */
void sound_init(const archive_t *arc, bool on, int voices); /* rocsound.txt, rocsong.txt */
int sound_voices(void);                             /* _SoundVoices */
int play_sample(uint8_t n, obj_t *o);               /* PLAYSAMPLE: a handle, or < 0 */
bool sound_link_active(int16_t handle);             /* SOUNDLINKACTIVE */
void sound_link_kill(int16_t handle);               /* SOUNDLINKKILL */
void maintain_sound(void);                          /* MAINTAINSOUND */
void sound_forget_obj(const obj_t *o);              /* an object is freed */
void sound_level_end(void);                         /* a level's sounds don't go on */
int sound_fx(uint8_t n);                            /* the menus' sounds: a voice handle */
void sound_song(int level);                         /* NEWLEVELSETUP's music */

#endif
