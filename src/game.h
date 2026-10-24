/* The game loop (RXMAIN.C _MachControl, RXNEWLVF.C NEWLEVEL / NEWLEVELSETUP, RUNLEVEL in 3cb4,
 * RXLEVEL.C PROCESSLEVEL, RXTITLE.C PROCESSTITLEPAGE / PROCESSTEXT) and the state it keeps from
 * level to level. */
#ifndef ROC_GAME_H
#define ROC_GAME_H

#include "archive.h"
#include "engine.h"
#include "level.h"

enum {
    LEVEL_TITLE = 0x1B,          /* "title": the title screen, credits and Hall of Heroes */
    LEVEL_TEXT = 0x1C,           /* "text": the story and help reader */
    RESTORE_TEMP = 'E',          /* _RestoreFlag for ROC.SGT (the game saved for the reader) */
    RESTORE_RERUN = 0xFE,        /* _RestoreFlag: RUNLEVEL again, keeping the level */
};

/* The loop's globals (none of them is cleared by a new level). */
typedef struct {
    uint8_t next_level;          /* _nextlevel: the level NEWLEVELSETUP loads */
    uint8_t quit_game;           /* _QuitGame: 1 leaves _MachControl (or counts a death's frames) */
    uint8_t restore_flag;        /* _RestoreFlag: a save to restore (1-8, RESTORE_TEMP), or 0xfe */
    uint8_t return_level;        /* _ReturnLevel: where the secret level returns to */
    uint8_t quick_file;          /* _QuickFile: the last save used (1-8), 'd' none */
    uint8_t skip_fade;           /* _SkipFade: the next RUNLEVEL doesn't fade */
    uint8_t screen_on;           /* _ScreenOn: the level is shown (PROCESSLEVEL waits for it) */
    const char *reader_file;     /* _ReaderFile: the story or help to read on level 28 */
    int8_t reader_page;          /* _ReaderPage */
    uint8_t reader_done;         /* _ReaderDone: 1 reading, 2 done */
    uint16_t restore_x, restore_y; /* _RestoreX / _RestoreY: the view of a restored game */
    uint8_t save_pal[768];       /* _SavePal: the palette a restored game fades to (when
                                  * save_pal[0] < 100) */
    /* the game's totals (PERCENTWIN adds each level's) */
    uint16_t second_total, minute_total, hour_total;
    uint16_t gem_total, enemy_total, statue_total, secret_total;
    uint16_t gem_final, enemy_final, statue_final, secret_final;
    uint8_t title_voc_done;      /* DS:53c4 (the title's sound is started once) */
    uint8_t title_gamma_armed;   /* DS:53c5 */
} loop_t;

extern loop_t L;
extern const archive_t *Arc;
extern const level_list_t *Levels;

/* Tests (main.c): called when a level starts, after each frame's ticks of a level (before
 * it's drawn), and after GAMEINIT when the game starts at a level. */
typedef struct {
    void (*level_start)(void);
    void (*frame)(void);
    void (*new_game)(void);
} game_hooks_t;

extern bool game_no_objects;     /* (tests: don't draw the objects) */
extern bool game_no_fall_check;  /* (tests: --sweep moves the player through floors) */
/* The title's scream as the game starts (the port leaves it out; --title-scream plays it) */
extern bool game_title_scream;

/* _MachControl: the title (or `start_level`, if >= 0, as with the original's level warp on the
 * command line), then a level at a time until the player quits. */
void game_run(const archive_t *arc, const level_list_t *list, int start_level,
              const game_hooks_t *hooks);

void game_shutdown(void);

/* GAMEINIT: a new game's inventory and totals for _DiffMode. */
void game_init(void);

/* Draw the level's view into the screen (the level loop does it every frame). */
void game_render(void);

/* The title's pictures: name.pal and the first 260 tiles of name.bg replace the level's
 * ("title", or "text" for the credits and the Hall of Heroes). */
void game_title_picture(const char *name);

#endif
