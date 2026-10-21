/* The game loop: _MachControl (RXMAIN.C), NEWLEVEL / NEWLEVELSETUP (RXNEWLVF.C), RUNLEVEL
 * (3cb4:157c), PROCESSLEVEL (RXLEVEL.C) and the title's and the reader's frame hooks
 * (RXTITLE.C). See game.h. */
#include "game.h"

#include "audio.h"
#include "host.h"
#include "save.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

loop_t L;
const archive_t *Arc;
const level_list_t *Levels;

static level_t lv;
static const game_hooks_t *hooks;
static int (*frame_hook)(void); /* _FrameHook */
bool game_no_fall_check;
static uint16_t title_timer;    /* ticks towards the next TITLETIMER run */

void game_render(void)
{
    surface_fill(&Vga, 0);
    if (!G.lv)
        return;
    level_draw(G.lv, &Vga, G.org_x, G.org_y);
    if (!game_no_objects)
        game_draw(&Vga);
}

/* RXWINDF.C:210 GAMEINIT: a new game: no score or gems (100 on Super Easy), the hit points and
 * weapons of the difficulty, the male hero, an extra hit point every 10000 points (15000 on
 * Psycho Warrior), the hero arriving in lightning, the totals cleared. */
void game_init(void)
{
    const uint8_t diff = G.lv ? G.diff_mode : Sess.diff_mode;
    L.quick_file = SAVE_NONE;
    memset(&Inv, 0, sizeof Inv);
    const uint32_t next_up = diff == 4 ? 15000 : 10000;
    L.second_total = L.minute_total = L.hour_total = 0;
    L.gem_final = L.gem_total = L.enemy_final = L.enemy_total = 0;
    L.statue_final = L.statue_total = L.secret_final = L.secret_total = 0;
    if (diff == 0) {
        Inv.hit_points[0] = Inv.max_hit_points[0] = 10;
        Inv.hit_points[1] = Inv.max_hit_points[1] = 10;
        Inv.flags = 0x3C;
        Inv.gems = 100;
    } else if (diff == 1) {
        Inv.hit_points[0] = Inv.max_hit_points[0] = 6;
        Inv.hit_points[1] = Inv.max_hit_points[1] = 8;
        Inv.flags = 2 << 2 | 2 << 4;
    } else {
        Inv.hit_points[0] = Inv.max_hit_points[0] = 4;
        Inv.hit_points[1] = Inv.max_hit_points[1] = 6;
        Inv.flags = 1 << 2 | 1 << 4;
    }
    if (diff == 4)
        Inv.hit_points[1] = Inv.max_hit_points[1] = 4;
    Sess.next_score_up = Sess.old_next_score_up = next_up;
    Sess.old_game_flags = 0;
    Sess.lightning_flag = 1;
    Sess.game_flags = 4;
    Sess.diff_mode = diff;
    if (G.lv) {
        /* (G holds them while a level runs) */
        G.next_score_up = G.old_next_score_up = next_up;
        G.old_game_flags = 0;
        G.hitagain = 0;
        G.lightning_flag = 1;
        G.game_flags = 4;
        G.old_inv = Inv;
    }
}

void game_title_picture(const char *name)
{
    char path[32];
    size_t len;
    snprintf(path, sizeof path, "%s.pal", name);
    uint8_t *pal = arc_load(Arc, path, &len);
    if (pal && len >= 768 && G.lv) {
        memcpy(G.lv->palette, pal, 768);
        memcpy(G.level_pal, pal, 768);
    }
    free(pal);
    snprintf(path, sizeof path, "%s.bg", name);
    uint8_t *bg = arc_load(Arc, path, &len);
    if (bg && G.lv) {
        const size_t n = len < 0x10400 ? len : 0x10400;
        if (G.lv->bg_tiles_len < n) {
            uint8_t *t = realloc(G.lv->bg_tiles, n);
            if (t) {
                G.lv->bg_tiles = t;
                G.lv->bg_tiles_len = n;
            }
        }
        memcpy(G.lv->bg_tiles, bg, n < G.lv->bg_tiles_len ? n : G.lv->bg_tiles_len);
    }
    free(bg);
}

/* ------------------------------------------------------------------------------------------ */
/* PROCESSLEVEL                                                                                */

/* PROCESSLEVEL's looking (once a frame): while the player runs, crouches or climbs (not on the
 * boss levels 7, 10, 16 and 25, nor while a camera has let go, _CenterOff < 0), Enter with up
 * (or PgUp) moves the view up a pixel a frame, as long as the player stays above view y 0xa9, and
 * Enter with down (or PgDn) moves it down while the player stays below view y 0x1e. Starting to
 * look makes the dead zone 0x8c high and lets the camera go (_CenterOff 0); letting go gives
 * SCREENCENTER its usual _CenterOff back (86, or 102 crouching), and it brings the view back. */
static void process_look(void)
{
    obj_t *you = G.you;
    const uint16_t a = you ? you->animator : 0;
    if (G.center_off < 0 || !(a == A_YOURUN || a == A_YOUCROUCH || a == A_YOUCLIMB) ||
        G.level == 7 || G.level == 10 || G.level == 0x10 || G.level == 0x19) {
        G.looking = 0;
        return;
    }
    const bool look = G.keys[G.key_trans[K_LOOK] & 0x7F] != 0;
    const bool up = key(K_UP), down = key(K_DOWN);
    const bool pgup = G.keys[0x49] != 0, pgdn = G.keys[0x51] != 0;
    for (int dir = 1; dir >= -1; dir -= 2) {
        const bool held = dir == 1 ? (look && up && !down) || (pgup && !pgdn)
                                   : (look && down && !up) || (pgdn && !pgup);
        if (!held) {
            if (G.looking == dir) {
                G.looking = 0;
                if (a == A_YOURUN || a == A_YOUCLIMB)
                    G.center_off = 'V';
                else if (a == A_YOUCROUCH)
                    G.center_off = 'f';
            }
        } else if (G.looking == dir) {
            if (dir == 1 && G.org_y != 0 &&
                (uint16_t)(you->y + you->ysize) < (uint16_t)(G.org_y + 0xA9))
                scroll_up(1);
            else if (dir == -1 && (uint16_t)(G.org_y + 199) < (uint16_t)(G.lv->rows * 16) &&
                     (uint16_t)(G.org_y + 0x1E) < you->y)
                scroll_down(1);
        } else {
            G.looking = (int8_t)dir;
            set_scroll_bounds(0x8C, G.window_xsize);
            G.window_ysize = 0x8C;
            G.center_off = 0;
        }
    }
}

/* _CheatCodes (DS:02b2): PC scancodes, NUL-ended, for PRIME, MAGMA, ASTRAL, FIRE, RAIN, WIND
 * (CHEATER 0-5) and ETHER (WARPMENU). */
static const uint8_t cheat_codes[7][7] = {
    {0x19, 0x13, 0x17, 0x32, 0x12}, {0x32, 0x1E, 0x22, 0x32, 0x1E},
    {0x1E, 0x1F, 0x14, 0x13, 0x1E, 0x26}, {0x21, 0x17, 0x13, 0x12},
    {0x13, 0x1E, 0x17, 0x31}, {0x11, 0x17, 0x31, 0x20}, {0x12, 0x14, 0x23, 0x12, 0x13},
};

/* The window _MenuComing asked for at the frame before (not while dying or between levels for
 * saving), and the status display F5 turned on. */
static void open_menu_coming(void)
{
    const uint16_t a = G.you ? G.you->animator : 0;
    if ((G.menu_coming == MENU_SAVE || G.menu_coming == MENU_QUICKSAVE) &&
        (G.quitflag || L.quit_game || !Inv.hit_points[0] || !Inv.hit_points[1] ||
         a == A_YOUDIE || a == A_NULLANIMATOR || a == A_YOUCRUSH || a == A_YOUBEGINIMPALE))
        G.menu_coming = MENU_NONE;
    if (G.menu_coming == MENU_NONE)
        return;
    switch (G.menu_coming) {
    case MENU_MAIN: main_menu(); break;
    case MENU_SAVE: save_menu(); break;
    case MENU_LOAD: load_menu(); break;
    case MENU_QUICKSAVE: quick_menu(1); break;
    case MENU_QUICKLOAD: quick_menu(0); break;
    case MENU_STATUS: status_win(false); break;
    case MENU_SOUND: sound_menu(); break;
    case MENU_QUIT:
        if (prompt_win(PROMPT_QUIT, NULL))
            G.quitflag = 1;
        break;
    default: break;
    }
    if (G.status_on && !G.hit_pts) {
        G.hit_pts = spawn_obj(0x0F, LIST_SLOW, NULL, 0, 0);
        for (int i = 0; i < 5; i++)
            spawn_obj(0x09, LIST_SLOW, NULL, 0, 0x10)->gv[0] = (int16_t)i;
    }
}

/* The keys: the windows (the next frame opens them), F1 the help (the game is saved, read on
 * level 28 and restored), F11's gamma, F5's status display, the cheat codes (held Backspace, then
 * the word) and the extra hit points every 10000 points. */
static void process_keys(void)
{
    G.menu_coming = MENU_NONE;
    if (G.keys[0x01])
        G.menu_coming = MENU_MAIN;
    if (G.keys[0x3B] && !L.quit_game) {
        G.quitflag = 5;
        L.reader_file = "rocinst.txt";
        L.reader_page = 11;
    }
    if (G.keys[0x3D])
        G.menu_coming = MENU_LOAD;
    if (G.keys[0x3E])
        G.menu_coming = MENU_SOUND;
    if (G.keys[0x42])
        G.menu_coming = MENU_QUICKSAVE;
    if (G.keys[0x43])
        G.menu_coming = MENU_QUICKLOAD;
    if (G.keys[0x44])
        G.menu_coming = MENU_QUIT;
    if (G.keys[0x3C])
        G.menu_coming = MENU_SAVE;
    if (G.keys[0x40] || G.keys[0x0F] || G.pause_hit)
        G.menu_coming = MENU_STATUS;
    gamma_key(&G.gamma_armed);
    if (++G.status_count > 5) {
        G.status_count = 6;
        if (G.keys[0x3F] && G.status_armed) {
            G.status_count = 0;
            G.status_armed = 0;
            G.status_on = !G.status_on;
            if (G.status_on)
                G.menu_coming = MENU_STATUS_ON;
        }
        if (!G.keys[0x3F] && !G.status_armed) {
            G.status_count = 0;
            G.status_armed = 1;
        }
    }
    if (!G.keys[0x0E]) {
        if (G.cheat_attempt == 100) {
            for (int c = 0; c < 7; c++)
                if (G.keys[cheat_codes[c][0]]) {
                    G.cheat_count = 1;
                    G.cheat_attempt = (uint8_t)c;
                }
        } else if (G.cheat_attempt < 7 &&
                   G.keys[cheat_codes[G.cheat_attempt][G.cheat_count]] &&
                   cheat_codes[G.cheat_attempt][++G.cheat_count] == 0) {
            if (G.cheat_attempt < 6)
                cheater(G.cheat_attempt);
            else
                warp_menu();
            G.cheat_attempt = 200;
        }
    } else {
        G.cheat_count = 0;
        G.cheat_attempt = 100;
    }
    if (Inv.score >= G.next_score_up) {
        play_sample(0xB3, G.you);
        G.next_score_up += 10000;
        for (int i = 0; i < 2; i++) {
            Inv.max_hit_points[i] = (uint8_t)(Inv.max_hit_points[i] + 2);
            if (Inv.max_hit_points[i] > 10)
                Inv.max_hit_points[i] = 10;
        }
        if (G.message_on)
            G.game_flags |= Inv.max_hit_points[0] < 10 ? 0x800 : 0x8000;
        Inv.hit_points[0] = Inv.max_hit_points[0];
        Inv.hit_points[1] = Inv.max_hit_points[1];
    }
}

/* The level's end (_quitflag 3, or 7 for a warp, a restore or a new game): at least 4 / 6 hit
 * points (all 10 on Super Easy) and the poison cured; after the exit the results, and at an
 * episode's end the episode's, the Hall of Heroes and the story on level 28. */
static int level_done(void)
{
    L.next_level++;
    if (G.diff_mode == 0) {
        Inv.hit_points[0] = Inv.hit_points[1] = 10;
    } else {
        if (Inv.hit_points[0] < 4)
            Inv.hit_points[0] = 4;
        if (Inv.hit_points[1] < 6)
            Inv.hit_points[1] = 6;
    }
    for (int i = 0; i < 2; i++)
        if (Inv.hit_points[i] > Inv.max_hit_points[i])
            Inv.hit_points[i] = Inv.max_hit_points[i];
    Inv.flags &= 0xFC;
    if (G.quitflag == 3) {
        percent_win();
        static const struct { uint8_t next; int episode; const char *story; } ends[3] = {
            {8, 0, "roccast.txt"}, {0x11, 1, "rocgobl.txt"}, {0x1A, 2, "rocdemon.txt"}};
        bool end = false;
        for (int i = 0; i < 3; i++) {
            if (L.next_level == ends[i].next) {
                final_percent_win();
                hs_check_scores(ends[i].episode);
                L.reader_file = ends[i].story;
                L.next_level = LEVEL_TEXT;
                end = true;
            }
        }
        if (!end && L.next_level == LEVEL_TITLE)
            L.next_level = L.return_level; /* back from the secret level */
    } else if (L.next_level == 0x1A) {
        L.return_level = G.level;
    }
    if (G.quitflag == 3)
        printf("level %d done: on to level %d\n", G.level, L.next_level);
    else if (L.restore_flag)
        printf("level %d: restoring saved game %d\n", G.level, L.restore_flag);
    else
        printf("level %d: on to level %d\n", G.level, L.next_level);
    fade_out();
    L.quit_game = 0;
    if (L.next_level <= 2 || L.next_level == 8 || L.next_level == 9) {
        /* the start of an episode: the hints begin again */
        G.old_game_flags = 0;
        G.game_flags &= 0x0F;
    }
    return 1;
}

/* RXLEVEL.C:54 PROCESSLEVEL, once a frame of a level: the clock, the window asked for, the
 * keys, looking and the fly mode, then _quitflag: 0 playing (a hint may come), 1 quit, 2 back to
 * the title, 3 and 7 the level's end, 4 dead (100 frames later a QuickRestore is offered, or the
 * level starts again with the inventory it started with), 5 the help (the game is saved for the
 * reader on level 28), 6 warped within the level (RUNLEVEL again, the view on the player). */
static int process_level(void)
{
    if (!L.screen_on)
        return 0;
    /* the level's clock: a second every 0x247 ticks, every fifth 0x244 */
    while (G.level_tics > G.next_second) {
        G.next_second += ++G.timer_fix == 5 ? 0x244 : 0x247;
        if (G.timer_fix == 5)
            G.timer_fix = 0;
        if (++G.second_in_level > 0x3B) {
            G.second_in_level = 0;
            if (G.minute_in_level < 100)
                G.minute_in_level++;
        }
    }
    open_menu_coming();
    process_keys();
    process_look();
    if (G.keys[0x20] && G.keys[0x3A] && G.keys[0x2D] && G.you)
        G.you->animator = A_XLEVEL; /* D + CapsLock + X: the fly mode */
    if (G.you && G.you->y > G.max_y + 64 && !G.quitflag && !game_no_fall_check) {
        /* (the port's: fell out of the map where no hazard tile catches it) */
        printf("fell out of level %d\n", G.level);
        G.quitflag = 4;
    }
    for (;;) {
        switch (G.quitflag) {
        case 0:
            if (!(G.old_game_flags & 0x4000) && G.message_on)
                spawn_hints();
            return 0;
        case 1:
            L.quit_game = 1;
            return 1;
        case 2:
            fade_out();
            L.quit_game = 0;
            L.next_level = LEVEL_TITLE;
            return 1;
        case 3:
        case 7:
            return level_done();
        case 4:
            if (++L.quit_game < 100)
                return 0;
            quick_menu(2);
            if (G.quitflag == 7)
                continue;
            fade_out();
            printf("died on level %d: starting again\n", G.level);
            Inv.hit_points[0] = G.diff_mode == 0 ? 10 : 4;
            Inv.hit_points[1] = G.diff_mode == 0 ? 10 : 6;
            for (int i = 0; i < 2; i++)
                if (Inv.hit_points[i] > G.old_inv.max_hit_points[i])
                    Inv.hit_points[i] = G.old_inv.max_hit_points[i];
            L.quit_game = 0;
            G.next_score_up = G.old_next_score_up;
            Inv.score = G.old_inv.score;
            Inv.gems = G.old_inv.gems;
            Inv.max_hit_points[0] = G.old_inv.max_hit_points[0];
            Inv.max_hit_points[1] = G.old_inv.max_hit_points[1];
            Inv.flags &= 0xFC;
            return 1;
        case 5:
            G.quitflag = 0;
            if (!save_game(RESTORE_TEMP, "Buy this game!"))
                return 0;
            L.next_level = LEVEL_TEXT;
            return 1;
        case 6:
            G.quitflag = 0;
            L.restore_flag = RESTORE_RERUN;
            return 1;
        case 8:
            /* (the port's: a lightning warp out, DOTRANSFORMER with _LightningFlag < 0) */
            L.next_level = G.next_level;
            fade_out();
            return 1;
        default:
            return 0;
        }
    }
}

/* ------------------------------------------------------------------------------------------ */
/* The title and the reader                                                                    */

/* RXTITLE.C:11 PROCESSTITLEPAGE, once a frame of the title: Esc, Enter or Space open the main
 * menu, F1 the help, F3 loading, F9 a quick load, F10 quits. After 61 runs of TITLETIMER (about
 * 15 seconds) the credits' picture replaces the title's; the credits show, then the Hall of
 * Heroes' pages, then the title comes back. */
static int process_title(void)
{
    if (G.hitagain == 0) {
        G.hitagain = 1;
        if (!L.title_voc_done) {
            L.title_voc_done = 1;
            sound_fx((uint8_t)(0xB0 + frandom(2))); /* (_MenuVOC: one of the two) */
        }
        return 0;
    }
    if (G.quitflag != 0 && G.quitflag != 5) {
        music_fade(0, 1000);
        L.quit_game = 1;
        return 1;
    }
    if (L.next_level == LEVEL_TITLE && L.restore_flag == 0 && G.quitflag == 0) {
        if (G.keys[0x01] || G.keys[0x1C] || G.keys[0x39]) {
            main_menu();
        } else if (G.keys[0x3B]) {
            L.reader_page = 11;
            G.quitflag = 5;
            L.reader_file = "rocinst.txt";
        } else if (G.keys[0x3D]) {
            load_menu();
        } else if (G.keys[0x3E]) {
            sound_menu();
        } else if (G.keys[0x43]) {
            quick_menu(0);
        } else if (G.keys[0x44] && prompt_win(PROMPT_QUIT, NULL)) {
            G.quitflag = 1;
        }
        gamma_key(&L.title_gamma_armed);
    }
    if (G.hitagain < 0x3D || G.vine_y == 0xFFFE) {
        if (G.vine_y == 0xFFFE) {
            if (G.vine_x == 0) {
                G.vine_x = 1;
                display_credits();
            } else {
                display_scores(false);
            }
        }
        if (G.quitflag == 5) {
            G.quitflag = 0;
            if (!save_game(RESTORE_TEMP, "Buy this game!"))
                return 0;
            fade_out();
            L.next_level = LEVEL_TEXT;
        } else {
            if (L.next_level == LEVEL_TITLE && L.restore_flag == 0)
                return 0;
            L.next_level++;
            fade_out();
        }
    } else {
        fade_out();
        game_title_picture("text");
        L.skip_fade = 1;
        L.restore_flag = RESTORE_RERUN;
        G.vine_y = 0xFFFE;
        G.hitagain = 0;
    }
    return 1;
}

/* RXTITLE.C:234 PROCESSTEXT, once a frame of level 28: a page of the story or the help (or the
 * Hall of Heroes, picked from the title's menu). After the help the game the reader left is
 * restored; after an episode's story ENDWIN offers the next episode; after the last the Hall of
 * Heroes shows, then the title. */
static int process_text(void)
{
    if (G.vine_x == 0) {
        G.vine_x = 1;
        return 0;
    }
    if (G.level != LEVEL_TEXT)
        return 1;
    if (!L.reader_file) {
        display_scores(true);
    } else {
        page_reader(L.reader_file);
        if (L.reader_done == 1)
            return L.restore_flag == 0xFF;
        if (L.reader_done == 2)
            L.reader_done = 0;
    }
    if (L.reader_file && strcmp(L.reader_file, "rocinst.txt")) {
        if (!strcmp(L.reader_file, "rocdemon.txt")) {
            L.reader_file = NULL;
        } else {
            const bool more = end_win();
            if (more) {
                game_init();
                L.next_level = !strcmp(L.reader_file, "roccast.txt") ? 8 : 0x11;
                L.reader_file = NULL;
                return 1;
            }
            L.reader_file = NULL;
        }
        fade_to(G.level_pal);
        return 0;
    }
    L.restore_flag = RESTORE_TEMP;
    L.reader_file = NULL;
    L.next_level = LEVEL_TITLE;
    return 1;
}

/* ------------------------------------------------------------------------------------------ */
/* NEWLEVEL                                                                                    */

/* NEWLEVELSETUP's easy maps: on Super Easy, Practice and Normal (_DiffMode < 3) these levels
 * load another .led (level 26 only on Super Easy). */
static const char *easy_map(int level, int diff)
{
    static const struct { uint8_t level; const char *led; } maps[] = {
        {1, "frsteasy.led"}, {3, "riveasy.led"}, {4, "caveeasy.led"}, {5, "casteasy.led"},
        {6, "cas2easy.led"}, {11, "musheas1.led"}, {12, "musheas2.led"}, {13, "cavebeas.led"},
        {0x15, "cave1eas.led"}, {0x16, "mushseas.led"}, {0x18, "gobseas.led"},
    };
    if (diff >= 3)
        return NULL;
    for (size_t i = 0; i < sizeof maps / sizeof maps[0]; i++)
        if (maps[i].level == level)
            return maps[i].led;
    return level == 0x1A && diff == 0 ? "secreasy.led" : NULL;
}

/* DISPLAYVGA("loader.vga") and a fade in: the loading screen. */
static void loading_screen(void)
{
    static const uint8_t black[768];
    host_set_palette(black);
    size_t len;
    uint8_t *v = arc_load(Arc, "loader.vga", &len);
    if (v && len >= 768 + 64000) {
        memcpy(Vga.px, v + 768, 64000);
        fade_to(v);
    }
    free(v);
}

/* Load level n (with its easy map) and start it. */
static bool load_level(int n)
{
    char err[256];
    level_t nl;
    if (!level_load(&nl, Arc, Levels, n, n < LEVEL_TITLE ? easy_map(n, Sess.diff_mode) : NULL,
                    err, sizeof err)) {
        fprintf(stderr, "roc: %s\n", err);
        return false;
    }
    game_end();
    level_free(&lv);
    lv = nl;
    if (!game_start(&lv, Arc, err, sizeof err)) {
        fprintf(stderr, "roc: %s\n", err);
        return false;
    }
    return true;
}

/* RXNEWLVF.C:155 NEWLEVELSETUP: a restored game's level, the loading screen, the level (and its
 * counts), the restore; 0 for the title, 6 for the reader, 1 for a level. */
static int new_level_setup(void)
{
    if (L.restore_flag) {
        const int level = save_level(L.restore_flag);
        if (level == SAVE_NONE) {
            L.restore_flag = 0;
            L.next_level = LEVEL_TITLE;
        } else {
            L.next_level = (uint8_t)level;
            if (level == LEVEL_TITLE) {
                save_delete(RESTORE_TEMP);
                L.restore_flag = 0;
            }
        }
    }
    const int level = L.next_level < LEVEL_COUNT ? L.next_level : LEVEL_TITLE;
    L.next_level = (uint8_t)level;
    sound_song(level);
    if (level < LEVEL_TITLE)
        loading_screen();
    if (!load_level(level))
        return -1;
    L.save_pal[0] = 100;
    if (level == LEVEL_TITLE)
        G.vine_y = 0xFFFF;
    if (L.restore_flag) {
        if (!restore_game(L.restore_flag)) {
            fprintf(stderr, "roc: can't restore saved game %d\n", L.restore_flag);
            L.restore_flag = 0;
            if (!load_level(level))
                return -1;
        } else if (L.restore_flag == RESTORE_TEMP) {
            save_delete(RESTORE_TEMP);
        }
    }
    if (L.restore_flag == 0) {
        if (level == LEVEL_TITLE)
            return 0;
        if (level == LEVEL_TEXT)
            return 6;
    }
    return 1;
}

/* RUNLEVEL: the screen fades out (not on the title) and in on the level, then a frame at a
 * time: the frame hook (which may open windows, or end the run), the timer ticks gone by, the
 * objects' ticks (ANIMATORUPDATE), the palette the behaviours set, the view drawn. */
static void run_level(void)
{
    if (G.level != LEVEL_TITLE && !L.skip_fade) {
        /* MYTRANSIT1 */
        fade_out();
        static const uint8_t black[768];
        host_set_palette(black);
    }
    game_render();
    if (!L.skip_fade) {
        /* MYTRANSIT2 */
        fade_to(L.save_pal[0] < 100 ? L.save_pal : G.level_pal);
    }
    L.skip_fade = 0;
    L.screen_on = 1;
    uint8_t shown[768];
    memcpy(shown, G.lv->palette, sizeof shown);
    for (;;) {
        if (frame_hook())
            break;
        const int ticks = host_frame();
        if (G.level == LEVEL_TITLE) {
            /* TITLETIMER */
            title_timer = (uint16_t)(title_timer + ticks);
            if (title_timer >= TITLE_TIMER_RATE * 4) {
                title_timer = 0;
                G.hitagain++;
            }
        }
        game_frame((uint16_t)ticks);
        if (hooks && hooks->frame && G.level < LEVEL_TITLE)
            hooks->frame();
        if (memcmp(shown, G.lv->palette, sizeof shown)) {
            memcpy(shown, G.lv->palette, sizeof shown);
            host_set_palette(shown);
        }
        game_render();
    }
    L.screen_on = 0;
}

/* RXNEWLVF.C:42 NEWLEVEL: set the level up, then RUNLEVEL, again for a warp within the level or
 * the title's next picture (_RestoreFlag 0xfe). */
static void new_level(void)
{
    const int setup = new_level_setup();
    if (setup < 0) {
        L.quit_game = 1;
        return;
    }
    frame_hook = setup == 0 ? process_title : setup == 1 ? process_level : process_text;
    title_timer = 0;
    if (setup == 1 && hooks && hooks->level_start)
        hooks->level_start();
    for (;;) {
        if (L.restore_flag == 0) {
            game_view_on_player();
        } else {
            L.restore_flag = 0;
            G.org_x = L.restore_x;
            G.org_y = L.restore_y;
        }
        if (G.hit_pts && G.level < LEVEL_TITLE) {
            G.hit_pts->x = (uint16_t)(G.org_x + 8);
            G.hit_pts->y = (uint16_t)(G.org_y + 7);
        }
        run_level();
        if (L.restore_flag != RESTORE_RERUN)
            break;
        L.restore_flag = 0;
    }
}

void game_run(const archive_t *arc, const level_list_t *list, int start_level,
              const game_hooks_t *h)
{
    Arc = arc;
    Levels = list;
    hooks = h;
    memset(&lv, 0, sizeof lv);
    L.quick_file = SAVE_NONE;
    if (start_level < 0) {
        L.next_level = LEVEL_TITLE;
    } else {
        L.next_level = (uint8_t)start_level;
        game_init();
        if (hooks && hooks->new_game)
            hooks->new_game();
    }
    while (!L.quit_game) {
        new_level();
        game_keep_session();
        config_save();
    }
}

void game_shutdown(void)
{
    game_end();
    level_free(&lv);
}
