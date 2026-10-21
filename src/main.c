/* Realms of Chaos port: plays a level (the player's movement and the objects ported so far).
 *
 * Usage: roc game/ROC.EXE [--level N] [--pos X,Y] [--player X,Y] [--no-objects] [--ticks N]
 *            [--trace N] [--female] [--gems N] [--boss-hits N] [--invincible] [--difficulty N]
 *            [--collect K] [--score N]
 *            [--hold ACTION:FROM-TO,...] [--sweep] [--list] [--screenshot out.png]
 *
 * Reads ROC.DAT next to the EXE (and a few tables from the EXE itself), then runs a level in
 * a 320x200 view at the game's timer rate. The game's keys: the arrows, Ctrl jump, Alt
 * attack, Space the special item, Enter (with up / down) look. The viewer's: [ and ] change
 * level, C a free camera (the arrows then move the view), O hides the objects, P pauses,
 * F12 saves shotNNN.png, Esc quits.
 *
 * With --screenshot it runs --ticks timer ticks (default 0) without a window and saves the
 * view. --pos fixes the camera there; --player moves the player (hotspot, map pixels) before
 * the run; --hold presses action keys (left, right, up, down, jump, attack, item, look) for a
 * range of ticks, e.g. "right:0-600,jump:200-260", or a PC scancode ("0x20:0-10" holds D);
 * --trace N prints the player's state every N ticks (a multiple of 8); --female plays the female character (fireballs), --gems N
 * starts with N gems; --boss-hits N starts the level's boss with N hits taken (GenericVar[3]);
 * --invincible sets _YouInvincible (nothing hurts the player); --difficulty N sets _DiffMode
 * (0-4, default 2); --collect K hands the player prize K (CollectID[K], as DOFLYINGSHIT counts
 * it: 0x21 shields, 0x23 lightning, 0x25 death, 0x29 a stronger weapon) at the start; --score N
 * starts with N points;
 * --sweep moves the player from its start to the map's right edge during
 * the run (to wake every object); --list prints every object. */
#include "archive.h"
#include "engine.h"
#include "gfx.h"
#include "level.h"
#include "png.h"
#include "ui.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VIEW_W = 320, VIEW_H = 200, SCALE = 3, MAX_HOLDS = 32 };
enum { HOLD_SCANCODE = 0x100 }; /* a hold_t action from here on is a raw PC scancode */

typedef struct {
    int action;
    long from, to;
} hold_t;

typedef struct {
    const char *exe;
    int level;
    int x, y;
    bool pos;
    int px, py;
    bool player;
    long ticks;
    long trace;
    int gems;
    int boss_hits;
    int difficulty;
    int collect;                 /* --collect: a prize (CollectID index) to collect, or -1 */
    long score;
    hold_t holds[MAX_HOLDS];
    int hold_count;
    const char *screenshot;
    bool no_objects, sweep, list, female, invincible;
} options_t;

static const char *const action_names[] = {"left", "right", "up",     "down",
                                           "jump", "attack", "item", "look"};

static bool parse_holds(const char *s, options_t *o)
{
    while (*s) {
        char name[16];
        long from, to;
        int used;
        if (sscanf(s, "%15[a-z0-9]:%ld-%ld%n", name, &from, &to, &used) != 3 ||
            o->hold_count == MAX_HOLDS)
            return false;
        int action = -1;
        for (int i = 0; i < 8; i++)
            if (!strcmp(name, action_names[i]))
                action = i;
        if (action < 0 && name[0] == '0' && name[1] == 'x') {
            /* a raw PC scancode, e.g. 0x20 for D */
            const long code = strtol(name, NULL, 16);
            if (code > 0 && code < 0x80)
                action = HOLD_SCANCODE + (int)code;
        }
        if (action < 0)
            return false;
        o->holds[o->hold_count++] = (hold_t){action, from, to};
        s += used;
        if (*s == ',')
            s++;
    }
    return true;
}

static bool parse_args(int argc, char **argv, options_t *o)
{
    memset(o, 0, sizeof *o);
    o->difficulty = 2;
    o->collect = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--level") && i + 1 < argc)
            o->level = atoi(argv[++i]);
        else if (!strcmp(a, "--pos") && i + 1 < argc)
            o->pos = sscanf(argv[++i], "%d,%d", &o->x, &o->y) == 2;
        else if (!strcmp(a, "--player") && i + 1 < argc)
            o->player = sscanf(argv[++i], "%d,%d", &o->px, &o->py) == 2;
        else if (!strcmp(a, "--ticks") && i + 1 < argc)
            o->ticks = atol(argv[++i]);
        else if (!strcmp(a, "--trace") && i + 1 < argc)
            o->trace = atol(argv[++i]);
        else if (!strcmp(a, "--hold") && i + 1 < argc) {
            if (!parse_holds(argv[++i], o))
                return false;
        } else if (!strcmp(a, "--sweep"))
            o->sweep = true;
        else if (!strcmp(a, "--list"))
            o->list = true;
        else if (!strcmp(a, "--female"))
            o->female = true;
        else if (!strcmp(a, "--invincible"))
            o->invincible = true;
        else if (!strcmp(a, "--difficulty") && i + 1 < argc)
            o->difficulty = atoi(argv[++i]);
        else if (!strcmp(a, "--score") && i + 1 < argc)
            o->score = atol(argv[++i]);
        else if (!strcmp(a, "--collect") && i + 1 < argc)
            o->collect = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(a, "--gems") && i + 1 < argc)
            o->gems = atoi(argv[++i]);
        else if (!strcmp(a, "--boss-hits") && i + 1 < argc)
            o->boss_hits = atoi(argv[++i]);
        else if (!strcmp(a, "--no-objects"))
            o->no_objects = true;
        else if (!strcmp(a, "--screenshot") && i + 1 < argc)
            o->screenshot = argv[++i];
        else if (a[0] != '-' && !o->exe)
            o->exe = a;
        else
            return false;
    }
    return o->exe != NULL;
}

/* ROC.DAT lives next to ROC.EXE. */
static void sibling(char *out, size_t n, const char *path, const char *name)
{
    const char *slash = strrchr(path, '/'), *bslash = strrchr(path, '\\');
    if (bslash > slash)
        slash = bslash;
    const int dir = slash ? (int)(slash - path + 1) : 0;
    snprintf(out, n, "%.*s%s", dir, path, name);
}

/* The palette as the VGA gets it: SETPALETTE adds _GammaCorrect and caps each component at 63. */
static void to_argb(const surface_t *s, const uint8_t *pal6, int gamma, uint32_t *out)
{
    uint32_t lut[256];
    for (int i = 0; i < 256; i++) {
        uint32_t c[3];
        for (int k = 0; k < 3; k++) {
            const int v = pal6[i * 3 + k] + gamma;
            c[k] = (uint32_t)(v > 63 ? 63 : v) * 255u / 63;
        }
        lut[i] = 0xFF000000u | c[0] << 16 | c[1] << 8 | c[2];
    }
    for (int i = 0; i < s->w * s->h; i++)
        out[i] = lut[s->px[i]];
}

static int save_png(const char *path, const surface_t *s, const uint8_t *pal6)
{
    uint32_t *px = malloc((size_t)s->w * s->h * sizeof *px);
    if (!px)
        return -1;
    to_argb(s, pal6, G.gamma, px);
    const int rc = png_write_argb(path, px, s->w, s->h);
    free(px);
    return rc;
}

static void clamp_camera(const level_t *lv, int *x, int *y)
{
    const int max_x = lv->cols * TILE - VIEW_W, max_y = lv->rows * TILE - VIEW_H;
    *x = *x > max_x ? max_x : *x;
    *y = *y > max_y ? max_y : *y;
    *x = *x < 0 ? 0 : *x;
    *y = *y < 0 ? 0 : *y;
}

/* PC scancodes (set 1) for the SDL keys the game reads (_Keys is indexed by them). */
static int pc_scancode(SDL_Scancode s)
{
    switch (s) {
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_BACKSPACE: return 0x0E;
    case SDL_SCANCODE_TAB: return 0x0F;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x1C;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x1D;
    case SDL_SCANCODE_LSHIFT: return 0x2A;
    case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x38;
    case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_CAPSLOCK: return 0x3A;
    case SDL_SCANCODE_UP: case SDL_SCANCODE_KP_8: return 0x48;
    case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_KP_9: return 0x49;
    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_KP_4: return 0x4B;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_KP_6: return 0x4D;
    case SDL_SCANCODE_DOWN: case SDL_SCANCODE_KP_2: return 0x50;
    case SDL_SCANCODE_PAGEDOWN: case SDL_SCANCODE_KP_3: return 0x51;
    case SDL_SCANCODE_MINUS: return 0x0C;
    case SDL_SCANCODE_EQUALS: return 0x0D;
    case SDL_SCANCODE_HOME: case SDL_SCANCODE_KP_7: return 0x47;
    case SDL_SCANCODE_END: case SDL_SCANCODE_KP_1: return 0x4F;
    case SDL_SCANCODE_KP_MINUS: return 0x4A;
    case SDL_SCANCODE_KP_PLUS: return 0x4E;
    case SDL_SCANCODE_0: return 0x0B;
    default: break;
    }
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9)
        return 0x02 + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10)
        return 0x3B + (s - SDL_SCANCODE_F1);
    if (s == SDL_SCANCODE_F11)
        return 0x57;
    /* letters, by their PC row positions */
    static const char *const rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    static const int starts[] = {0x10, 0x1E, 0x2C};
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) {
        const char c = (char)('a' + (s - SDL_SCANCODE_A));
        for (int r = 0; r < 3; r++) {
            const char *p = strchr(rows[r], c);
            if (p)
                return starts[r] + (int)(p - rows[r]);
        }
    }
    return -1;
}

/* --invincible: set again at every level start (game_end clears G). */
static bool invincible;

static bool start_level(level_t *lv, const archive_t *arc, const level_list_t *list, int n,
                        char *err, size_t errlen)
{
    level_t nl;
    if (!level_load(&nl, arc, list, n, err, errlen))
        return false;
    game_end();
    level_free(lv);
    *lv = nl;
    if (!game_start(lv, arc, err, errlen)) {
        fprintf(stderr, "roc: %s\n", err);
        return true; /* the map still shows */
    }
    G.you_invincible = invincible;
    return true;
}

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

/* F11 (in the windows too, WINDOWCURSOR2): the gamma up a step of 4, from 16 back to 0. */
static void gamma_key(void)
{
    if (!G.keys[0x57] || !G.gamma_armed) {
        if (!G.keys[0x57])
            G.gamma_armed = 1;
        return;
    }
    G.gamma_armed = 0;
    G.gamma = (uint8_t)(G.gamma + 4);
    if (G.gamma > 0x10)
        G.gamma = 0;
}

/* _CheatCodes (DS:02b2): PC scancodes, NUL-ended, for PRIME, MAGMA, ASTRAL, FIRE, RAIN, WIND
 * (CHEATER 0-5) and ETHER (WARPMENU). */
static const uint8_t cheat_codes[7][7] = {
    {0x19, 0x13, 0x17, 0x32, 0x12}, {0x32, 0x1E, 0x22, 0x32, 0x1E},
    {0x1E, 0x1F, 0x14, 0x13, 0x1E, 0x26}, {0x21, 0x17, 0x13, 0x12},
    {0x13, 0x1E, 0x17, 0x31}, {0x11, 0x17, 0x31, 0x20}, {0x12, 0x14, 0x23, 0x12, 0x13},
};

/* The start of PROCESSLEVEL: the level's clock, the windows the keys ask for (opened by the
 * next frame: _MenuComing), F11's gamma, F5's status display, the cheat codes (held Backspace,
 * then the word) and the extra hit points every 10000 points. */
static void process_keys(void)
{
    /* the clock: a second every 0x247 ticks, every fifth 0x244 */
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
    if (G.menu_coming == MENU_STATUS_ON && G.status_on && !G.hit_pts) {
        G.hit_pts = spawn_obj(0x0F, LIST_SLOW, NULL, 0, 0);
        for (int i = 0; i < 5; i++)
            spawn_obj(0x09, LIST_SLOW, NULL, 0, 0x10)->gv[0] = (int16_t)i;
    }
    G.menu_coming = MENU_NONE;
    if (G.keys[0x3B])
        G.menu_coming = MENU_HELP; /* F1 (the original saves, reads, and restores) */
    if (G.keys[0x44])
        G.menu_coming = MENU_QUIT; /* F10 */
    if (G.keys[0x40] || G.keys[0x0F] || G.pause_hit)
        G.menu_coming = MENU_STATUS; /* F6, Tab, Pause */
    /* (Esc's main menu, F2 / F3 save and load, F4 sound, F8 / F9 quick save and load: not
     * ported) */
    gamma_key();
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
            G.menu_coming = G.cheat_attempt < 6 ? MENU_CHEAT + G.cheat_attempt : MENU_WARP;
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

/* PROCESSLEVEL's _quitflag cases: 0 playing (a hint may come, SPAWNHINTS), 1 quit (F10), 3 the
 * exit and 7 a warp (on to the next level, or the one typed, with at least 4 / 6 hit points and
 * the poison cured), 4 dead (100 frames later the level starts again with the inventory it
 * started with), 6 warped within the level (the view follows). Before them, once a frame,
 * process_keys, the look keys and the fly-mode cheat (D + CapsLock + X: XLEVEL). The story
 * screens, the menus and saving aren't ported. Returns true if it started a level; sets *quit
 * for _quitflag 1. */
static bool process_level(level_t *lv, const archive_t *arc, const level_list_t *list,
                          int *dead_frames, char *err, size_t errlen, bool *quit)
{
    process_keys();
    process_look();
    if (G.keys[0x20] && G.keys[0x3A] && G.keys[0x2D] && G.you)
        G.you->animator = A_XLEVEL;
    switch (G.quitflag) {
    case 0:
        if (!(G.old_game_flags & 0x4000) && G.message_on)
            spawn_hints();
        return false;
    case 1:
        *quit = true;
        return false;
    case 3: case 7: {
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
        int next = lv->number + 1 < 27 ? lv->number + 1 : 0;
        if (G.quitflag == 7)
            next = G.next_level + 1;
        if (next <= 2 || next == 8 || next == 9) {
            /* the start of an episode: the hints begin again */
            G.old_game_flags = 0;
            G.game_flags &= 0x0F;
        }
        printf(G.quitflag == 7 ? "level %d: warped to level %d\n" : "level %d done: on to level %d\n",
               lv->number, next);
        if (!start_level(lv, arc, list, next, err, errlen))
            fprintf(stderr, "roc: %s\n", err);
        return true;
    }
    case 8: {
        /* a lightning warp (DOTRANSFORMER with _LightningFlag < 0) to _nextlevel */
        const int next = G.next_level;
        printf("level %d: warped to level %d\n", lv->number, next);
        if (!start_level(lv, arc, list, next, err, errlen))
            fprintf(stderr, "roc: %s\n", err);
        return true;
    }
    case 4:
        if (++*dead_frames < 100)
            return false;
        *dead_frames = 0;
        G.next_score_up = G.old_next_score_up;
        {
            const inventory_t old = G.old_inv;
            Inv = old;
            Inv.hit_points[0] = G.diff_mode == 0 ? 10 : 4;
            Inv.hit_points[1] = G.diff_mode == 0 ? 10 : 6;
            for (int i = 0; i < 2; i++)
                if (Inv.hit_points[i] > old.max_hit_points[i])
                    Inv.hit_points[i] = old.max_hit_points[i];
            Inv.flags &= 0xFC;
        }
        printf("died on level %d: starting again\n", lv->number);
        if (!start_level(lv, arc, list, lv->number, err, errlen))
            fprintf(stderr, "roc: %s\n", err);
        return true;
    case 6:
        G.quitflag = 0;
        game_view_on_player();
        return false;
    default:
        return false;
    }
}

/* The _MenuComing values that open a window (the others aren't ported, or aren't windows). */
static bool window_menu(int menu)
{
    return menu == MENU_STATUS || menu == MENU_QUIT || menu == MENU_HELP || menu == MENU_WARP ||
           (menu >= MENU_CHEAT && menu <= MENU_CHEAT + 5);
}

/* Open the window `menu` over the frame in `view`. The reader (F1) is shown on the reader's own
 * level, 28 ("text"): its picture and its palette. */
static void open_window(int menu, surface_t *view, const archive_t *arc, const level_list_t *list)
{
    if (menu == MENU_HELP) {
        level_t text;
        char err[256];
        memset(&text, 0, sizeof text);
        if (level_load(&text, arc, list, 28, err, sizeof err)) {
            surface_fill(view, 0);
            level_draw(&text, view, 0, 0);
            ui_open(menu, view, text.palette);
            level_free(&text);
            return;
        }
    }
    ui_open(menu, view, NULL);
}

/* A frame of the open window (and F11, which works in the windows too). A warp typed in the
 * warp menu becomes _quitflag 7. True when the player chose to quit. */
static bool run_window(int ticks)
{
    gamma_key();
    int level = 0;
    const ui_result_t r = ui_update(ticks, &level);
    if (r == UI_WARP) {
        G.next_level = (uint8_t)(level - 1);
        G.quitflag = 7;
    }
    return r == UI_QUIT;
}

static void render(surface_t *view, const level_t *lv, int cam_x, int cam_y, bool objects)
{
    surface_fill(view, 0);
    level_draw(lv, view, cam_x, cam_y);
    if (objects)
        game_draw(view);
}

static void list_objects(void)
{
    static const char *const names[] = {"main", "fore", "slow", "off"};
    for (int id = 0; id < LIST_COUNT; id++)
        for (const obj_t *o = G.list[id]; o; o = o->next)
            printf("  %-4s prio %3d at %4u,%4u  animator %04x  xdir %u  gv %d %d %d %d %d %d\n",
                   names[id], o->priority, o->x, o->y, o->animator, o->xdir, o->gv[0],
                   o->gv[1], o->gv[2], o->gv[3], o->gv[4], o->gv[5]);
}

/* Run headless and save one view. */
static int run_headless(const options_t *opt, level_t *lv, const archive_t *arc,
                        const level_list_t *list, surface_t *view)
{
    char err[256];
    int dead_frames = 0;
    const uint16_t start_x = G.you ? G.you->x : 0;
    int cam_x = G.org_x, cam_y = G.org_y;
    for (long t = 0; t < opt->ticks; t += 8) {
        for (int a = 0; a < 8; a++)
            G.keys[G.key_trans[a] & 0x7F] = 0;
        for (int i = 0; i < opt->hold_count; i++)
            if (opt->holds[i].action >= HOLD_SCANCODE)
                G.keys[opt->holds[i].action - HOLD_SCANCODE] = 0;
        for (int i = 0; i < opt->hold_count; i++) {
            if (t < opt->holds[i].from || t >= opt->holds[i].to)
                continue;
            const int a = opt->holds[i].action;
            G.keys[a >= HOLD_SCANCODE ? a - HOLD_SCANCODE : G.key_trans[a] & 0x7F] = 1;
        }
        if (opt->sweep && G.you) {
            G.you->x = (uint16_t)(start_x + (lv->cols * TILE - start_x) * t / opt->ticks);
            cam_x = G.you->x - VIEW_W / 2;
            cam_y = G.you->y - VIEW_H / 2;
            clamp_camera(lv, &cam_x, &cam_y);
            G.org_x = (uint16_t)cam_x;
            G.org_y = (uint16_t)cam_y;
        } else if (opt->pos) {
            G.org_x = (uint16_t)opt->x;
            G.org_y = (uint16_t)opt->y;
        }
        const int step = (int)(opt->ticks - t < 8 ? opt->ticks - t : 8);
        if (ui_active()) {
            /* a window is open: the game waits (PAUSEACTION) */
            if (run_window(step)) {
                printf("quit\n");
                break;
            }
            continue;
        }
        game_frame((uint16_t)step);
        bool quit = false;
        process_level(lv, arc, list, &dead_frames, err, sizeof err, &quit);
        if (quit) {
            printf("quit\n");
            break;
        }
        if (window_menu(G.menu_coming)) {
            render(view, lv, G.org_x, G.org_y, !opt->no_objects);
            open_window(G.menu_coming, view, arc, list);
            G.menu_coming = MENU_NONE;
        }
        if (opt->trace && (t + 8) % opt->trace == 0 && G.you)
            printf("  t %5ld  player %4u,%4u  anim %04x  hp %u/%u  gems %u  flags %02x  "
                   "hitagain %d\n",
                   t + 8, G.you->x, G.you->y, G.you->animator, Inv.hit_points[0],
                   Inv.hit_points[1], Inv.gems, G.game_flags, G.hitagain);
    }
    if (opt->pos && !opt->sweep) {
        cam_x = opt->x;
        cam_y = opt->y;
        clamp_camera(lv, &cam_x, &cam_y);
        G.org_x = (uint16_t)cam_x;
        G.org_y = (uint16_t)cam_y;
    }
    cam_x = G.org_x;
    cam_y = G.org_y;
    render(view, lv, cam_x, cam_y, !opt->no_objects);
    if (ui_active())
        memcpy(view->px, ui_screen()->px, (size_t)view->w * view->h);
    const int rc = save_png(opt->screenshot, view, ui_palette() ? ui_palette() : lv->palette);
    if (rc)
        fprintf(stderr, "roc: can't write %s\n", opt->screenshot);
    int count[LIST_COUNT] = {0};
    for (int id = 0; id < LIST_COUNT; id++)
        for (const obj_t *o = G.list[id]; o; o = o->next)
            count[id]++;
    printf("level %d %s: %dx%d tiles, view at %d,%d after %ld ticks; objects: %d main, %d fore, "
           "%d slow, %d off screen\n",
           lv->number, list->name[lv->number], lv->cols, lv->rows, cam_x, cam_y, opt->ticks,
           count[LIST_MAIN], count[LIST_FORE], count[LIST_SLOW], count[LIST_OFF]);
    if (G.you)
        printf("player at %u,%u (size %ux%u), animator %04x\n", G.you->x, G.you->y,
               G.you->xsize, G.you->ysize, G.you->animator);
    printf("hit points %u/%u (female) %u/%u (male), gems %u, score %lu, quitflag %u, "
           "enemies %u, gems found %u, secrets %u\n",
           Inv.hit_points[0], Inv.max_hit_points[0], Inv.hit_points[1], Inv.max_hit_points[1],
           Inv.gems, (unsigned long)Inv.score, G.quitflag, G.enemy_found, G.gem_found,
           G.secret_found);
    if (opt->list)
        list_objects();
    return rc ? 1 : 0;
}

int main(int argc, char **argv)
{
    options_t opt;
    if (!parse_args(argc, argv, &opt)) {
        fprintf(stderr, "usage: roc ROC.EXE [--level N] [--pos X,Y] [--player X,Y] "
                        "[--no-objects] [--ticks N] [--trace N] [--female] [--gems N] "
                        "[--boss-hits N] [--invincible] [--difficulty N] [--collect K] [--score N] [--hold ACTION:FROM-TO,...] [--sweep] "
                        "[--list] [--screenshot out.png]\n");
        return 2;
    }

    char err[256], path[1024];
    archive_t arc;
    level_list_t list;
    sibling(path, sizeof path, opt.exe, "ROC.DAT");
    if (!arc_open(&arc, path, err, sizeof err) ||
        !level_list_load(&list, &arc, opt.exe, err, sizeof err)) {
        fprintf(stderr, "roc: %s\n", err);
        return 1;
    }

    level_t lv;
    memset(&lv, 0, sizeof lv);
    game_new(!opt.female, (uint8_t)opt.difficulty);
    Inv.gems = (uint16_t)opt.gems;
    Inv.score = (uint32_t)opt.score;
    invincible = opt.invincible;
    if (!start_level(&lv, &arc, &list, opt.level, err, sizeof err)) {
        fprintf(stderr, "roc: %s\n", err);
        return 1;
    }
    surface_t view;
    if (!surface_init(&view, VIEW_W, VIEW_H)) {
        fprintf(stderr, "roc: out of memory\n");
        return 1;
    }
    if (opt.boss_hits && G.boss)
        G.boss->gv[3] = (int16_t)opt.boss_hits;
    if (opt.player && G.you) {
        G.you->x = (uint16_t)opt.px;
        G.you->y = (uint16_t)opt.py;
        game_view_on_player();
    }
    if (opt.collect >= 0 && opt.collect < 43 && G.you)
        spawn_obj(0x13, LIST_SLOW, G.img[IMG_COLLECT + opt.collect], G.you->x, G.you->y)->gv[0] =
            (int16_t)opt.collect;
    if (opt.pos) {
        int x = opt.x, y = opt.y;
        clamp_camera(&lv, &x, &y);
        G.org_x = (uint16_t)x;
        G.org_y = (uint16_t)y;
    }

    if (opt.screenshot) {
        const int rc = run_headless(&opt, &lv, &arc, &list, &view);
        game_end();
        level_free(&lv);
        surface_free(&view);
        arc_close(&arc);
        return rc;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "roc: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("Realms of Chaos", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, VIEW_W * SCALE, VIEW_H * SCALE,
                                       SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                               SDL_TEXTUREACCESS_STREAMING, VIEW_W, VIEW_H)
                           : NULL;
    if (!tex) {
        fprintf(stderr, "roc: SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(ren, VIEW_W, VIEW_H);
    uint32_t *argb = malloc(VIEW_W * VIEW_H * sizeof *argb);

    int shot = 0, cam_x = G.org_x, cam_y = G.org_y, dead_frames = 0;
    bool running = true, show_objects = !opt.no_objects, free_camera = false;
    const double freq = (double)SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();
    double tick_acc = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                const int pc = pc_scancode(e.key.keysym.scancode);
                if (pc >= 0)
                    G.keys[pc] = e.type == SDL_KEYDOWN;
                if (e.type == SDL_KEYUP || e.key.repeat)
                    continue;
                if (e.key.keysym.scancode == SDL_SCANCODE_PAUSE)
                    G.pause_hit = 1; /* _PauseHit, from the keyboard interrupt */
                const SDL_Keycode k = e.key.keysym.sym;
                int next = -1;
                if (k == SDLK_ESCAPE && !ui_active())
                    running = false; /* (the original's Esc opens the main menu: not ported) */
                else if (k == SDLK_RIGHTBRACKET)
                    next = (lv.number + 1) % LEVEL_COUNT;
                else if (k == SDLK_LEFTBRACKET)
                    next = (lv.number + LEVEL_COUNT - 1) % LEVEL_COUNT;
                else if (k == SDLK_o && !ui_active())
                    show_objects = !show_objects;
                else if (k == SDLK_c && !ui_active()) {
                    free_camera = !free_camera;
                    cam_x = G.org_x;
                    cam_y = G.org_y;
                } else if (k == SDLK_F12) {
                    char name[32];
                    snprintf(name, sizeof name, "shot%03d.png", shot++);
                    if (save_png(name, &view, lv.palette) == 0)
                        printf("saved %s\n", name);
                }
                if (next >= 0 && !ui_active() &&
                    !start_level(&lv, &arc, &list, next, err, sizeof err))
                    fprintf(stderr, "roc: %s\n", err);
            }
        }

        const Uint64 now = SDL_GetPerformanceCounter();
        tick_acc += (double)(now - last) / freq * TICKS_PER_SECOND;
        last = now;
        int ticks = (int)tick_acc;
        tick_acc -= ticks;
        if (ticks > 100)
            ticks = 100;

        if (ui_active()) {
            /* a window is open: the game waits (PAUSEACTION) and the window takes the keys */
            if (run_window(ticks))
                running = false;
            memcpy(view.px, ui_screen()->px, (size_t)view.w * view.h);
        } else {
            if (free_camera) {
                /* The arrows move the view instead of the player. */
                const Uint8 *keys = SDL_GetKeyboardState(NULL);
                const int step = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT] ? 12 : 3;
                cam_x += (keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT]) * step;
                cam_y += (keys[SDL_SCANCODE_DOWN] - keys[SDL_SCANCODE_UP]) * step;
                clamp_camera(&lv, &cam_x, &cam_y);
                for (int a = 0; a < 4; a++)
                    G.keys[G.key_trans[a] & 0x7F] = 0;
            }
            game_frame((uint16_t)ticks);
            bool quit = false;
            process_level(&lv, &arc, &list, &dead_frames, err, sizeof err, &quit);
            if (quit)
                running = false;
            if (G.you && G.you->y > G.max_y + 64 && !G.quitflag) {
                /* Fell out of the map where no hazard tile catches it: start the level again. */
                if (!start_level(&lv, &arc, &list, lv.number, err, sizeof err))
                    fprintf(stderr, "roc: %s\n", err);
            }
            if (!free_camera) {
                cam_x = G.org_x;
                cam_y = G.org_y;
            }
            render(&view, &lv, cam_x, cam_y, show_objects);
            if (window_menu(G.menu_coming)) {
                open_window(G.menu_coming, &view, &arc, &list);
                G.menu_coming = MENU_NONE;
                memcpy(view.px, ui_screen()->px, (size_t)view.w * view.h);
            }
        }
        to_argb(&view, ui_palette() ? ui_palette() : lv.palette, G.gamma, argb);
        SDL_UpdateTexture(tex, NULL, argb, VIEW_W * sizeof *argb);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        char title[128];
        snprintf(title, sizeof title, "Realms of Chaos - level %d %s (%d,%d)%s", lv.number,
                 list.name[lv.number], cam_x, cam_y, free_camera ? " [free camera]" : "");
        SDL_SetWindowTitle(win, title);
    }

    free(argb);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    game_end();
    level_free(&lv);
    surface_free(&view);
    arc_close(&arc);
    return 0;
}
