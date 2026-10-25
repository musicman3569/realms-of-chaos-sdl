/* Realms of Chaos port.
 *
 * Usage: roc [game/ROC.EXE] [--level N] [--difficulty N] [--data-dir DIR] [test options]
 *
 * Without ROC.EXE it looks for the game in the usual places (files.h: next to the AppImage or
 * the program, and so on). Reads ROC.DAT next to the EXE (and a few tables from the EXE itself) and runs the game: the
 * title, its menus, and the levels at the game's timer rate. ROC.CFG (the keys and settings),
 * roc.sco (the Hall of Heroes) and saved games are read and written in the game's folder, or in
 * --data-dir. --level N starts a new game at level N (0-26) instead of the title, as the
 * original's level warp does, but without the hero's lightning entrance; --difficulty N sets
 * _DiffMode for it (0 Super Easy ... 4 Psycho Warrior; default 2, Normal).
 *
 * With --screenshot out.png it runs --ticks timer ticks (default 0) without a window, holding
 * the keys --hold gives, and saves the screen as it was last shown; it reads and writes files
 * only with --data-dir. Test options (for the first level started):
 *   --pos X,Y        keep the view's top left corner there
 *   --player X,Y     move the player (hotspot, map pixels) at the start
 *   --hold A:F-T,... hold an action key (left, right, up, down, jump, attack, item, look) or a PC
 *                    scancode ("0x20:0-10" holds D) from tick F to T; with --joystick also the
 *                    scripted joystick: joyleft, joyright, joyup, joydown, button0-button3
 *                    (a game controller's A, X, B, Y), start, and lookup / lookdown (the
 *                    right stick)
 *   --trace N        print the player's state every N ticks (a multiple of 8)
 *   --female         play the female hero (fireballs); --gems N, --score N start with them
 *   --boss-hits N    start the level's boss with N hits taken (GenericVar[3])
 *   --invincible     set _YouInvincible (at every level start)
 *   --collect K      hand the player prize K (CollectID[K], as DOFLYINGSHIT counts it: 0x21
 *                    shields, 0x23 lightning, 0x25 death, 0x29 a stronger weapon)
 *   --sweep          move the player from its start to the map's right edge over the run
 *   --list           print every object at the end
 *   --no-objects     don't draw the objects
 * --no-sound plays without sound (the game still times its sounds, as headless runs do) and
 * --no-music without music; the music plays in stereo on two emulated OPL2s (the driver's own
 * stereo, which ROC.EXE never turns on), or with --mono-music as the original plays it. The title's death
 * scream as the game starts is left out; --title-scream plays it as the original does. --fullscreen starts in full screen (Alt+Enter switches between full
 * screen and the window at any time). --crt starts with the CRT look, after RetroArch's
 * 2xsal-level2-crt preset (Alt+C turns it on and off; screenshots then show it at 640 x 400).
 * --joystick uses the joystick (a game controller or any joystick SDL finds) even if ROC.CFG
 * doesn't turn it on. Game Options' Enhancements menu remembers these choices in ROCP.CFG (all on
 * at first, but the title's scream); the flags hold for their run only. */
#include "almidi.h"
#include "archive.h"
#include "audio.h"
#include "engine.h"
#include "files.h"
#include "game.h"
#include "host.h"
#include "level.h"
#include "save.h"

#include <SDL.h> /* (SDL_main) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_HOLDS = 64 };

typedef struct {
    const char *exe, *data_dir, *screenshot;
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
    const char *hold_names[MAX_HOLDS];
    host_hold_t holds[MAX_HOLDS];
    int hold_count;
    bool no_objects, sweep, list, female, invincible, no_sound, no_music, joystick, fullscreen,
        crt;
    bool mono_music, title_scream;
} options_t;

static options_t opt;

static const char *const action_names[] = {"left", "right", "up",     "down",
                                           "jump", "attack", "item", "look"};
/* the scripted joystick's holds (HOLD_JOY + index) */
static const char *const joy_names[] = {"joyleft", "joyright", "joyup",   "joydown", "button0",
                                        "button1", "button2",  "button3", "start",   "lookup",
                                        "lookdown"};

/* Holds name an action (its key in _KeyTrans) or a raw PC scancode. */
static bool parse_holds(const char *s, options_t *o)
{
    while (*s) {
        char name[16];
        long from, to;
        int used;
        if (sscanf(s, "%15[a-z0-9]:%ld-%ld%n", name, &from, &to, &used) != 3 ||
            o->hold_count == MAX_HOLDS)
            return false;
        int code = -1;
        for (int i = 0; i < 8; i++)
            if (!strcmp(name, action_names[i]))
                code = Sess.key_trans[i] & 0x7F;
        for (int i = 0; i < JOY_HOLDS; i++)
            if (!strcmp(name, joy_names[i]))
                code = HOLD_JOY + i;
        if (code < 0 && name[0] == '0' && name[1] == 'x') {
            const long c = strtol(name, NULL, 16);
            if (c > 0 && c < 0x80)
                code = (int)c;
        }
        if (code < 0)
            return false;
        o->holds[o->hold_count++] = (host_hold_t){code, from, to};
        s += used;
        if (*s == ',')
            s++;
    }
    return true;
}

static bool parse_args(int argc, char **argv, options_t *o)
{
    memset(o, 0, sizeof *o);
    o->level = -1;
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
            if (o->hold_count < MAX_HOLDS)
                o->hold_names[o->hold_count] = argv[i + 1];
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
        else if (!strcmp(a, "--no-sound"))
            o->no_sound = true;
        else if (!strcmp(a, "--no-music"))
            o->no_music = true;
        else if (!strcmp(a, "--joystick"))
            o->joystick = true;
        else if (!strcmp(a, "--fullscreen"))
            o->fullscreen = true;
        else if (!strcmp(a, "--crt"))
            o->crt = true;
        else if (!strcmp(a, "--mono-music"))
            o->mono_music = true;
        else if (!strcmp(a, "--title-scream"))
            o->title_scream = true;
        else if (!strcmp(a, "--data-dir") && i + 1 < argc)
            o->data_dir = argv[++i];
        else if (!strcmp(a, "--screenshot") && i + 1 < argc)
            o->screenshot = argv[++i];
        else if (a[0] != '-' && !o->exe)
            o->exe = a;
        else
            return false;
    }
    if (o->level >= LEVEL_COUNT - 2)
        return false;
    if (o->difficulty < 0 || o->difficulty > 4)
        o->difficulty = 2;
    return true;
}

/* The game's folder: where ROC.EXE is. */
static void folder_of(char *out, size_t n, const char *path)
{
    const char *slash = strrchr(path, '/'), *bslash = strrchr(path, '\\');
    if (bslash > slash)
        slash = bslash;
    const int dir = slash ? (int)(slash - path) : 0;
    snprintf(out, n, "%.*s", dir, slash ? path : ".");
}

static void clamp_view(int *x, int *y)
{
    const int max_x = G.lv->cols * TILE - SCREEN_W, max_y = G.lv->rows * TILE - SCREEN_H;
    *x = *x > max_x ? max_x : *x;
    *y = *y > max_y ? max_y : *y;
    *x = *x < 0 ? 0 : *x;
    *y = *y < 0 ? 0 : *y;
}

/* ------------------------------------------------------------------------------------------ */
/* Test hooks                                                                                  */

static bool first_level = true;
static uint16_t sweep_start;
static long sweep_from;

static void hook_level_start(void)
{
    G.you_invincible = opt.invincible;
    if (!first_level)
        return;
    first_level = false;
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
        clamp_view(&x, &y);
        G.org_x = (uint16_t)x;
        G.org_y = (uint16_t)y;
    }
    sweep_start = G.you ? G.you->x : 0;
    sweep_from = host_ticks();
}

/* After each frame's ticks, before the view is drawn: --sweep, --pos and --trace. */
static void hook_frame(void)
{
    const long t = host_ticks();
    if (opt.sweep && G.you && opt.ticks > sweep_from) {
        G.you->x = (uint16_t)(sweep_start + (G.lv->cols * TILE - sweep_start) * (t - sweep_from) /
                                                (opt.ticks - sweep_from));
        int x = G.you->x - SCREEN_W / 2, y = G.you->y - SCREEN_H / 2;
        clamp_view(&x, &y);
        G.org_x = (uint16_t)x;
        G.org_y = (uint16_t)y;
    } else if (opt.pos) {
        int x = opt.x, y = opt.y;
        clamp_view(&x, &y);
        G.org_x = (uint16_t)x;
        G.org_y = (uint16_t)y;
    }
    if (opt.trace && t % opt.trace == 0 && G.you)
        printf("  t %5ld  player %4u,%4u  anim %04x  hp %u/%u  gems %u  flags %02x  "
               "hitagain %d\n",
               t, G.you->x, G.you->y, G.you->animator, Inv.hit_points[0], Inv.hit_points[1],
               Inv.gems, G.game_flags, G.hitagain);
}

static void hook_new_game(void)
{
    /* (--level: no lightning entrance, as the port's tests have always started) */
    Sess.lightning_flag = 0;
    if (opt.female)
        Sess.game_flags &= (uint16_t)~4;
    Inv.gems = (uint16_t)(Inv.gems + opt.gems);
    Inv.score = (uint32_t)opt.score;
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

/* A headless run's end: the screen and what the level holds. */
static int report(const level_list_t *list)
{
    const int rc = host_screenshot(opt.screenshot);
    if (rc)
        fprintf(stderr, "roc: can't write %s\n", opt.screenshot);
    if (!G.lv) {
        printf("no level running after %ld ticks\n", host_ticks());
        return rc ? 1 : 0;
    }
    int count[LIST_COUNT] = {0};
    for (int id = 0; id < LIST_COUNT; id++)
        for (const obj_t *o = G.list[id]; o; o = o->next)
            count[id]++;
    printf("level %d %s: %dx%d tiles, view at %d,%d after %ld ticks; objects: %d main, %d fore, "
           "%d slow, %d off screen\n",
           G.level, list->name[G.level], G.lv->cols, G.lv->rows, G.org_x, G.org_y, host_ticks(),
           count[LIST_MAIN], count[LIST_FORE], count[LIST_SLOW], count[LIST_OFF]);
    if (G.you)
        printf("player at %u,%u (size %ux%u), animator %04x\n", G.you->x, G.you->y,
               G.you->xsize, G.you->ysize, G.you->animator);
    printf("hit points %u/%u (female) %u/%u (male), gems %u, score %lu, quitflag %u, "
           "enemies %u/%u, gems found %u/%u, statues %u/%u, secrets %u/%u\n",
           Inv.hit_points[0], Inv.max_hit_points[0], Inv.hit_points[1], Inv.max_hit_points[1],
           Inv.gems, (unsigned long)Inv.score, G.quitflag, G.enemy_found, G.enemy_in_level,
           G.gem_found, G.gem_in_level, G.statue_found, G.statue_in_level, G.secret_found,
           G.secret_in_level);
    if (opt.list)
        list_objects();
    return rc ? 1 : 0;
}

bool game_no_objects;

int main(int argc, char **argv)
{
    if (!parse_args(argc, argv, &opt)) {
        fprintf(stderr, "usage: roc [ROC.EXE] [--level N] [--difficulty N] [--data-dir DIR] "
                        "[--screenshot out.png --ticks N] [--pos X,Y] [--player X,Y] "
                        "[--hold ACTION:FROM-TO,...] [--trace N] [--female] [--gems N] "
                        "[--score N] [--boss-hits N] [--invincible] [--collect K] [--sweep] "
                        "[--list] [--no-objects] [--no-sound] [--no-music] [--joystick] "
                        "[--fullscreen] [--crt] [--mono-music] [--title-scream]\n");
        return 2;
    }
    game_no_objects = opt.no_objects;
    game_no_fall_check = opt.sweep;

    char err[256], path[1100], folder[1024], exe[1100];
    if (!opt.exe) {
        if (!find_game(exe, sizeof exe))
            return 1;
        opt.exe = exe;
    }
    archive_t arc;
    level_list_t list;
    folder_of(folder, sizeof folder, opt.exe);
    file_path(path, sizeof path, folder, "ROC.DAT");
    if (!arc_open(&arc, path, err, sizeof err) ||
        !level_list_load(&list, &arc, opt.exe, err, sizeof err)) {
        fprintf(stderr, "roc: %s\n", err);
        return 1;
    }
    Arc = &arc;

    const bool headless = opt.screenshot != NULL;
    save_set_dir(opt.data_dir ? opt.data_dir : headless ? NULL : folder);
    config_load();
    enhance_load();
    if (!save_load_tables(opt.exe))
        fprintf(stderr, "roc: can't read the animator table; the original's saved games won't load\n");
    Sess.diff_mode = (uint8_t)opt.difficulty;
    /* the holds name keys by the layout just read */
    const int holds = opt.hold_count;
    opt.hold_count = 0;
    for (int i = 0; i < holds; i++)
        if (opt.hold_names[i] && !parse_holds(opt.hold_names[i], &opt))
            return 2;

    const host_options_t ho = {.headless = headless, .ticks = opt.ticks, .holds = opt.holds,
                               .hold_count = opt.hold_count, .fullscreen = opt.fullscreen || (!headless && Enhance.fullscreen),
                               .crt = opt.crt || (!headless && Enhance.crt),
                               .joystick = opt.joystick || (!headless && Enhance.controller)};
    if (!host_init(&ho)) {
        fprintf(stderr, "roc: can't open the screen\n");
        return 1;
    }
    /* SETUPSOUND: the effects on _SoundVoices voices, the music on the (emulated) AdLib */
    if (Sess.sound_voices < 1)
        Sess.sound_voices = 8;
    const bool adlib = al_load_tables(opt.exe);
    al_set_stereo(!opt.mono_music && Enhance.stereo_music);
    game_title_scream = opt.title_scream || Enhance.title_scream;
    audio_init(!headless && !opt.no_sound && Sess.sound_card != -1,
               adlib && !headless && !opt.no_music && Sess.music_card != -1, Sess.sound_voices);
    sound_init(&arc, Sess.sound_card != -1, Sess.sound_voices);
    fx_set_volume(Sess.sound_volume);
    music_set_volume(Sess.music_volume);
    const game_hooks_t hooks = {hook_level_start, hook_frame, hook_new_game};
    int rc = 0;
    if (setjmp(host_end) == 0) {
        game_run(&arc, &list, opt.level, &hooks);
        if (headless)
            rc = report(&list);
    } else if (headless) {
        rc = report(&list);
    }
    game_keep_session();
    config_save();
    enhance_save();
    game_shutdown();
    audio_shutdown();
    host_shutdown();
    arc_close(&arc);
    return rc;
}
