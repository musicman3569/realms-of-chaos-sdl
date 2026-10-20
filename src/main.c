/* Realms of Chaos port: level viewer with live objects.
 *
 * Usage: roc game/ROC.EXE [--level N] [--pos X,Y] [--player X,Y] [--no-objects] [--ticks N]
 *            [--sweep] [--list] [--screenshot out.png]
 *
 * Reads ROC.DAT next to the EXE (and a few tables from the EXE itself), then runs a level in
 * a 320x200 view at the game's timer rate. The player's own behaviour isn't ported yet, so
 * the arrow keys move the player freely (Shift: faster) and the view follows; C switches to
 * a free camera. Objects react to the player and the view as in the game, as far as their
 * behaviours are ported. PgUp / PgDn change level, O toggles the objects, Space pauses,
 * F12 saves shotNNN.png, Esc quits. With --screenshot it renders one view to a PNG without a
 * window, after running --ticks timer ticks (default 0) with the camera at --pos.
 * --player moves the player (its hotspot, map pixels) before the level starts running;
 * --sweep moves it from there to the map's right edge during the run, with the view following
 * (a quick way to wake every object in a level); --list prints every object at the end. */
#include "archive.h"
#include "engine.h"
#include "gfx.h"
#include "level.h"
#include "png.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VIEW_W = 320, VIEW_H = 200, SCALE = 3 };

typedef struct {
    const char *exe;
    int level;
    int x, y;
    bool pos;
    int px, py;
    bool player;
    long ticks;
    const char *screenshot;
    bool no_objects, sweep, list;
} options_t;

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
        else if (!strcmp(a, "--sweep"))
            o->sweep = true;
        else if (!strcmp(a, "--list"))
            o->list = true;
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

/* The view centred on the player (a stand-in for the game's scrolling). */
static void follow_player(const level_t *lv, int *x, int *y)
{
    if (!G.you)
        return;
    *x = G.you->x - VIEW_W / 2;
    *y = G.you->y - VIEW_H / 2;
    clamp_camera(lv, x, y);
}

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
    return true;
}

static void render(surface_t *view, const level_t *lv, int cam_x, int cam_y, bool objects)
{
    surface_fill(view, 0);
    level_draw(lv, view, cam_x, cam_y);
    if (objects)
        game_draw(view);
}

int main(int argc, char **argv)
{
    options_t opt;
    if (!parse_args(argc, argv, &opt)) {
        fprintf(stderr, "usage: roc ROC.EXE [--level N] [--pos X,Y] [--player X,Y] "
                        "[--no-objects] [--ticks N] [--sweep] [--list] "
                        "[--screenshot out.png]\n");
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
    if (!start_level(&lv, &arc, &list, opt.level, err, sizeof err)) {
        fprintf(stderr, "roc: %s\n", err);
        return 1;
    }
    surface_t view;
    if (!surface_init(&view, VIEW_W, VIEW_H)) {
        fprintf(stderr, "roc: out of memory\n");
        return 1;
    }
    if (opt.player && G.you) {
        G.you->x = (uint16_t)opt.px;
        G.you->y = (uint16_t)opt.py;
    }
    int cam_x = opt.x, cam_y = opt.y;
    if (opt.pos)
        clamp_camera(&lv, &cam_x, &cam_y);
    else
        follow_player(&lv, &cam_x, &cam_y);

    if (opt.screenshot) {
        /* Run --ticks in frames of 8 ticks (about 70 frames a second) with a fixed camera, or
         * with --sweep moving the player (and the view with it) across the map. */
        const uint16_t start_x = G.you ? G.you->x : 0;
        G.org_x = (uint16_t)cam_x;
        G.org_y = (uint16_t)cam_y;
        for (long t = 0; t < opt.ticks; t += 8) {
            if (opt.sweep && G.you) {
                G.you->x = (uint16_t)(start_x + (lv.cols * TILE - start_x) * t / opt.ticks);
                follow_player(&lv, &cam_x, &cam_y);
                G.org_x = (uint16_t)cam_x;
                G.org_y = (uint16_t)cam_y;
            }
            game_frame((uint16_t)(opt.ticks - t < 8 ? opt.ticks - t : 8));
        }
        render(&view, &lv, cam_x, cam_y, !opt.no_objects);
        const int rc = save_png(opt.screenshot, &view, lv.palette);
        if (rc)
            fprintf(stderr, "roc: can't write %s\n", opt.screenshot);
        int count[LIST_COUNT] = {0};
        for (int id = 0; id < LIST_COUNT; id++)
            for (const obj_t *o = G.list[id]; o; o = o->next)
                count[id]++;
        printf("level %d %s: %dx%d tiles, view at %d,%d after %ld ticks; objects: %d main, "
               "%d fore, %d slow, %d off screen\n",
               lv.number, list.name[lv.number], lv.cols, lv.rows, cam_x, cam_y, opt.ticks,
               count[LIST_MAIN], count[LIST_FORE], count[LIST_SLOW], count[LIST_OFF]);
        if (opt.list) {
            static const char *const names[] = {"main", "fore", "slow", "off"};
            for (int id = 0; id < LIST_COUNT; id++)
                for (const obj_t *o = G.list[id]; o; o = o->next)
                    printf("  %-4s prio %3d at %4u,%4u  animator %04x  xdir %u  gv %d %d %d %d "
                           "%d %d\n",
                           names[id], o->priority, o->x, o->y, o->animator, o->xdir, o->gv[0],
                           o->gv[1], o->gv[2], o->gv[3], o->gv[4], o->gv[5]);
        }
        game_end();
        level_free(&lv);
        surface_free(&view);
        arc_close(&arc);
        return rc ? 1 : 0;
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

    int shot = 0;
    bool running = true, show_objects = !opt.no_objects, free_camera = opt.pos, paused = false;
    const double freq = (double)SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();
    double tick_acc = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_KEYDOWN) {
                const SDL_Keycode k = e.key.keysym.sym;
                int next = -1;
                if (k == SDLK_ESCAPE)
                    running = false;
                else if (k == SDLK_PAGEDOWN)
                    next = (lv.number + 1) % LEVEL_COUNT;
                else if (k == SDLK_PAGEUP)
                    next = (lv.number + LEVEL_COUNT - 1) % LEVEL_COUNT;
                else if (k == SDLK_o)
                    show_objects = !show_objects;
                else if (k == SDLK_c)
                    free_camera = !free_camera;
                else if (k == SDLK_SPACE)
                    paused = !paused;
                else if (k == SDLK_F12) {
                    char name[32];
                    snprintf(name, sizeof name, "shot%03d.png", shot++);
                    if (save_png(name, &view, lv.palette) == 0)
                        printf("saved %s\n", name);
                }
                if (next >= 0) {
                    if (start_level(&lv, &arc, &list, next, err, sizeof err))
                        follow_player(&lv, &cam_x, &cam_y);
                    else
                        fprintf(stderr, "roc: %s\n", err);
                }
            }
        }

        const Uint64 now = SDL_GetPerformanceCounter();
        tick_acc += (double)(now - last) / freq * TICKS_PER_SECOND;
        last = now;
        int ticks = (int)tick_acc;
        tick_acc -= ticks;
        if (ticks > 100)
            ticks = 100;

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        const int step = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT] ? 12 : 3;
        const int dx = (keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT]) * step;
        const int dy = (keys[SDL_SCANCODE_DOWN] - keys[SDL_SCANCODE_UP]) * step;
        if (free_camera || !G.you) {
            cam_x += dx;
            cam_y += dy;
            clamp_camera(&lv, &cam_x, &cam_y);
        } else {
            int px = G.you->x + dx, py = G.you->y + dy;
            const int max_x = lv.cols * TILE - 1, max_y = lv.rows * TILE - 1;
            G.you->x = (uint16_t)(px < 0 ? 0 : px > max_x ? max_x : px);
            G.you->y = (uint16_t)(py < 0 ? 0 : py > max_y ? max_y : py);
            if (dx)
                G.you->xdir = dx > 0;
            follow_player(&lv, &cam_x, &cam_y);
        }
        G.org_x = (uint16_t)cam_x;
        G.org_y = (uint16_t)cam_y;
        if (!paused)
            game_frame((uint16_t)ticks);

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
