/* Realms of Chaos port: plays a level (the player's movement and the objects ported so far).
 *
 * Usage: roc game/ROC.EXE [--level N] [--pos X,Y] [--player X,Y] [--no-objects] [--ticks N]
 *            [--trace N] [--female] [--gems N] [--boss-hits N] [--invincible]
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
 * range of ticks, e.g. "right:0-600,jump:200-260"; --trace N prints the player's state every N
 * ticks (a multiple of 8); --female plays the female character (fireballs), --gems N
 * starts with N gems; --boss-hits N starts the level's boss with N hits taken (GenericVar[3]);
 * --invincible sets _YouInvincible (nothing hurts the player);
 * --sweep moves the player from its start to the map's right edge during
 * the run (to wake every object); --list prints every object. */
#include "archive.h"
#include "engine.h"
#include "gfx.h"
#include "level.h"
#include "png.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VIEW_W = 320, VIEW_H = 200, SCALE = 3, MAX_HOLDS = 32 };

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
        if (sscanf(s, "%15[a-z]:%ld-%ld%n", name, &from, &to, &used) != 3 ||
            o->hold_count == MAX_HOLDS)
            return false;
        int action = -1;
        for (int i = 0; i < 8; i++)
            if (!strcmp(name, action_names[i]))
                action = i;
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

static void to_argb(const surface_t *s, const uint8_t *pal6, uint32_t *out)
{
    uint32_t lut[256];
    for (int i = 0; i < 256; i++) {
        const uint32_t r = pal6[i * 3] * 255u / 63, g = pal6[i * 3 + 1] * 255u / 63,
                       b = pal6[i * 3 + 2] * 255u / 63;
        lut[i] = 0xFF000000u | r << 16 | g << 8 | b;
    }
    for (int i = 0; i < s->w * s->h; i++)
        out[i] = lut[s->px[i]];
}

static int save_png(const char *path, const surface_t *s, const uint8_t *pal6)
{
    uint32_t *px = malloc((size_t)s->w * s->h * sizeof *px);
    if (!px)
        return -1;
    to_argb(s, pal6, px);
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
    default: break;
    }
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

/* PROCESSLEVEL's _quitflag cases: 3 the exit (on to the next level, with at least 4 / 6 hit
 * points and the poison cured), 4 dead (100 frames later the level starts again with the
 * inventory it started with), 6 warped (the view follows). The story screens, the menu and
 * saving aren't ported. Returns true if it started a level. */
static bool process_level(level_t *lv, const archive_t *arc, const level_list_t *list,
                          int *dead_frames, char *err, size_t errlen)
{
    switch (G.quitflag) {
    case 3: {
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
        const int next = lv->number + 1 < 27 ? lv->number + 1 : 0;
        printf("level %d done: on to level %d\n", lv->number, next);
        if (!start_level(lv, arc, list, next, err, errlen))
            fprintf(stderr, "roc: %s\n", err);
        return true;
    }
    case 4:
        if (++*dead_frames < 100)
            return false;
        *dead_frames = 0;
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
            if (t >= opt->holds[i].from && t < opt->holds[i].to)
                G.keys[G.key_trans[opt->holds[i].action] & 0x7F] = 1;
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
        game_frame((uint16_t)(opt->ticks - t < 8 ? opt->ticks - t : 8));
        process_level(lv, arc, list, &dead_frames, err, sizeof err);
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
    const int rc = save_png(opt->screenshot, view, lv->palette);
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
                        "[--boss-hits N] [--invincible] [--hold ACTION:FROM-TO,...] [--sweep] "
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
    game_new(!opt.female);
    Inv.gems = (uint16_t)opt.gems;
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
    bool running = true, show_objects = !opt.no_objects, free_camera = false, paused = false;
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
                const SDL_Keycode k = e.key.keysym.sym;
                int next = -1;
                if (k == SDLK_ESCAPE)
                    running = false;
                else if (k == SDLK_RIGHTBRACKET)
                    next = (lv.number + 1) % LEVEL_COUNT;
                else if (k == SDLK_LEFTBRACKET)
                    next = (lv.number + LEVEL_COUNT - 1) % LEVEL_COUNT;
                else if (k == SDLK_o)
                    show_objects = !show_objects;
                else if (k == SDLK_c) {
                    free_camera = !free_camera;
                    cam_x = G.org_x;
                    cam_y = G.org_y;
                } else if (k == SDLK_p)
                    paused = !paused;
                else if (k == SDLK_F12) {
                    char name[32];
                    snprintf(name, sizeof name, "shot%03d.png", shot++);
                    if (save_png(name, &view, lv.palette) == 0)
                        printf("saved %s\n", name);
                }
                if (next >= 0 && !start_level(&lv, &arc, &list, next, err, sizeof err))
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
        if (!paused) {
            game_frame((uint16_t)ticks);
            process_level(&lv, &arc, &list, &dead_frames, err, sizeof err);
        }
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
        to_argb(&view, lv.palette, argb);
        SDL_UpdateTexture(tex, NULL, argb, VIEW_W * sizeof *argb);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        char title[128];
        snprintf(title, sizeof title, "Realms of Chaos - level %d %s (%d,%d)%s%s", lv.number,
                 list.name[lv.number], cam_x, cam_y, free_camera ? " [free camera]" : "",
                 paused ? " [paused]" : "");
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
