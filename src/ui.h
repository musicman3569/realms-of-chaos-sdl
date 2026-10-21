/* The in-level windows (RXWINDF.C and the window assembly in segment 3f50): the pause /
 * status window, the quit prompt, the cheat and warp windows and the help reader.
 *
 * The original's windows are blocking: WINUP freezes the game (PAUSEACTION), draws a frame of
 * Border2ID tiles onto the screen, and the window's code then loops on the keyboard until it is
 * done (WINDOWCURSOR2 and friends). The port opens a window on a copy of the last frame
 * (SAVEPAGE) and runs it a frame at a time from the main loop, which shows that copy and runs no
 * game ticks while a window is open. */
#ifndef ROC_UI_H
#define ROC_UI_H

#include "engine.h"

/* _MenuComing values (what PROCESSLEVEL asks for; the next frame opens it). */
enum {
    MENU_NONE = -1,
    MENU_MAIN = 0,         /* Esc: MAINMENU (not ported: the viewer quits instead) */
    MENU_SAVE = 4,         /* F2: SAVEMENU (not ported) */
    MENU_SOUND = 5,        /* F4: SOUNDMENU (not ported) */
    MENU_LOAD = 6,         /* F3: LOADMENU (not ported) */
    MENU_QUICKSAVE = 8,    /* F8: QUICKMENU(1) (not ported) */
    MENU_QUICKLOAD = 9,    /* F9: QUICKMENU(0) (not ported) */
    MENU_QUIT = 10,        /* F10: PROMPTWIN(0), quit to DOS */
    MENU_STATUS = 11,      /* F6, Tab, Pause: STATUSWIN */
    MENU_STATUS_ON = 12,   /* F5 turning the status display on: just respawn it */
    /* the port's own requests for the windows PROCESSLEVEL opens at once */
    MENU_CHEAT = 0x20,     /* + n: CHEATER(n), 0-5 */
    MENU_WARP = 0x30,      /* WARPMENU */
    MENU_HELP = 0x31,      /* F1: the reader on rocinst.txt's page 11 */
};

/* What a closed window decided. */
typedef enum { UI_CONTINUE, UI_QUIT, UI_WARP } ui_result_t;

bool ui_active(void);
/* Open the window `menu` asks for over `frame` (the frame just shown). The reader isn't over the
 * game: the original leaves the level for the reader's own, level 28 ("text"), so the caller
 * passes that level's picture as the frame and its palette as `palette` (NULL otherwise). */
void ui_open(int menu, const surface_t *frame, const uint8_t *palette);
/* The palette to show while the window is open, or NULL for the level's. */
const uint8_t *ui_palette(void);
/* Run the open window for `ticks` timer ticks of keyboard (G.keys). When it closes it returns
 * what it decided (a warp's level in *level). */
ui_result_t ui_update(int ticks, int *level);
/* The screen while a window is open. */
const surface_t *ui_screen(void);

#endif
