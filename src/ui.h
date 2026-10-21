/* The windows and menus (RXWINDF.C, and the window assembly in segment 3f50): the main menu and
 * its sub-menus, saving and loading, the pause / status window, the prompts, the cheat and warp
 * windows, the level's results, the Hall of Heroes, the credits and the page reader.
 *
 * As in the original they block: WINUP freezes the game (PAUSEACTION) and keeps the screen
 * (SAVEPAGE), the window is drawn onto the screen, and its code loops on the keyboard
 * (host_frame) until it's done; WINCLEAR puts the screen back (REFRESHPAGE), WINDOWN leaves the
 * window there for the next frame to draw over. */
#ifndef ROC_UI_H
#define ROC_UI_H

#include "engine.h"

/* _MenuComing values (what PROCESSLEVEL asks for; the next frame opens it). */
enum {
    MENU_NONE = -1,
    MENU_MAIN = 0,         /* Esc: MAINMENU */
    MENU_SAVE = 4,         /* F2: SAVEMENU */
    MENU_SOUND = 5,        /* F4: SOUNDMENU */
    MENU_LOAD = 6,         /* F3: LOADMENU */
    MENU_QUICKSAVE = 8,    /* F8: QUICKMENU(1) */
    MENU_QUICKLOAD = 9,    /* F9: QUICKMENU(0) */
    MENU_QUIT = 10,        /* F10: PROMPTWIN(0), quit to DOS */
    MENU_STATUS = 11,      /* F6, Tab, Pause: STATUSWIN */
    MENU_STATUS_ON = 12,   /* F5 turning the status display on: just respawn it */
};

/* PROMPTWIN's questions. */
enum { PROMPT_QUIT, PROMPT_END_GAME, PROMPT_RESTORE, PROMPT_QUICKSAVE, PROMPT_QUICKRESTORE };

/* F11, in the windows as in the level: the gamma up a step of 4, from 16 back to 0. */
void gamma_key(uint8_t *armed);

/* DEFAULTPAL: the level's palette has the standard yellow at colour 72. */
bool default_pal(void);

void main_menu(void);                               /* MAINMENU */
void sound_menu(void);                              /* SOUNDMENU */
void save_menu(void);                               /* SAVEMENU */
void load_menu(void);                               /* LOADMENU */
void quick_menu(int saving);                        /* QUICKMENU: 1 save, 0 load, 2 a death */
void status_win(bool from_menu);                    /* STATUSWIN */
bool prompt_win(int type, const char *desc);        /* PROMPTWIN: Y pressed */
void cheater(int cheat);                            /* CHEATER(0-5) */
void warp_menu(void);                               /* WARPMENU */
void percent_win(void);                             /* PERCENTWIN: the level's results */
void final_percent_win(void);                       /* FINALPERCENTWIN: the episode's */
void hs_check_scores(int episode);                  /* HS_CHECKSCORES */
void display_credits(void);                         /* DISPLAYCREDITS (the title) */
void display_scores(bool by_user);                  /* DISPLAYSCORES: the Hall of Heroes */
bool end_win(void);                                 /* ENDWIN: on to the next episode? */
void page_reader(const char *file);                 /* PAGEREADER: a page, until a key turns it */
void win_clear(void);                               /* WINCLEAR */

#endif
