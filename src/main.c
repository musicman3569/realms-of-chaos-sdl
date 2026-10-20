/* Realms of Chaos port: level viewer (first milestone).
 *
 * Usage: roc game/ROC.EXE [--level N] [--pos X,Y] [--no-objects] [--screenshot out.png]
 *
 * Reads ROC.DAT next to the EXE (and the level table from the EXE itself), then shows a level
 * in a 320x200 view with its objects in their starting frames: arrow keys scroll (Shift:
 * faster), PgUp / PgDn change level, O toggles the objects, F12 saves shotNNN.png, Esc quits. With --screenshot it renders one view to a PNG without a window. */
#include "archive.h"
#include "gfx.h"
#include "level.h"
#include "object.h"
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
    const char *screenshot;
    bool no_objects;
} options_t;

static bool parse_args(int argc, char **argv, options_t *o)
{
    memset(o, 0, sizeof *o);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--level") && i + 1 < argc)
            o->level = atoi(argv[++i]);
        else if (!strcmp(a, "--pos") && i + 1 < argc)
            sscanf(argv[++i], "%d,%d", &o->x, &o->y);
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

int main(int argc, char **argv)
{
    options_t opt;
    if (!parse_args(argc, argv, &opt)) {
        fprintf(stderr, "usage: roc ROC.EXE [--level N] [--pos X,Y] [--no-objects] "
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
    objects_t objs;
    if (!level_load(&lv, &arc, &list, opt.level, err, sizeof err) ||
        !objects_load(&objs, &lv, &arc, err, sizeof err)) {
        fprintf(stderr, "roc: %s\n", err);
        return 1;
    }
    surface_t view;
    if (!surface_init(&view, VIEW_W, VIEW_H)) {
        fprintf(stderr, "roc: out of memory\n");
        return 1;
    }
    int cam_x = opt.x, cam_y = opt.y;
    clamp_camera(&lv, &cam_x, &cam_y);

    if (opt.screenshot) {
        level_draw(&lv, &view, cam_x, cam_y);
        if (!opt.no_objects)
            objects_draw(&objs, &view, cam_x, cam_y);
        const int rc = save_png(opt.screenshot, &view, lv.palette);
        if (rc)
            fprintf(stderr, "roc: can't write %s\n", opt.screenshot);
        printf("level %d %s: %dx%d tiles, %d objects (%d placed), view at %d,%d\n", lv.number,
               list.name[lv.number], lv.cols, lv.rows, lv.object_count, objs.count, cam_x,
               cam_y);
        objects_free(&objs);
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
    bool running = true, show_objects = !opt.no_objects;
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
                else if (k == SDLK_F12) {
                    char name[32];
                    snprintf(name, sizeof name, "shot%03d.png", shot++);
                    if (save_png(name, &view, lv.palette) == 0)
                        printf("saved %s\n", name);
                }
                if (next >= 0) {
                    level_t nl;
                    objects_t no;
                    bool ok = level_load(&nl, &arc, &list, next, err, sizeof err);
                    if (ok && !objects_load(&no, &nl, &arc, err, sizeof err)) {
                        level_free(&nl);
                        ok = false;
                    }
                    if (ok) {
                        objects_free(&objs);
                        level_free(&lv);
                        lv = nl;
                        objs = no;
                        cam_x = cam_y = 0;
                    } else {
                        fprintf(stderr, "roc: %s\n", err);
                    }
                }
            }
        }
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        const int step = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT] ? 12 : 3;
        cam_x += (keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT]) * step;
        cam_y += (keys[SDL_SCANCODE_DOWN] - keys[SDL_SCANCODE_UP]) * step;
        clamp_camera(&lv, &cam_x, &cam_y);

        surface_fill(&view, 0);
        level_draw(&lv, &view, cam_x, cam_y);
        if (show_objects)
            objects_draw(&objs, &view, cam_x, cam_y);
        to_argb(&view, lv.palette, argb);
        SDL_UpdateTexture(tex, NULL, argb, VIEW_W * sizeof *argb);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        char title[96];
        snprintf(title, sizeof title, "Realms of Chaos - level %d %s (%d,%d)", lv.number,
                 list.name[lv.number], cam_x, cam_y);
        SDL_SetWindowTitle(win, title);
    }

    free(argb);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    objects_free(&objs);
    level_free(&lv);
    surface_free(&view);
    arc_close(&arc);
    return 0;
}
