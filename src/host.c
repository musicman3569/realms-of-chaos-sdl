/* The platform layer (see host.h): SDL2 window, timer and keyboard, or a scripted headless
 * run. */
#include "host.h"

#include "audio.h"
#include "engine.h"
#include "png.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SCALE = 3, MAX_STEP = 100, HEADLESS_STEP = 8 };

surface_t Vga;
jmp_buf host_end;

static struct {
    host_options_t opt;
    uint8_t dac[768];
    uint8_t keys[128];           /* by PC scancode */
    uint8_t prev[128];           /* the keys at the frame before (for typed characters) */
    int typed[16], typed_count;  /* characters typed, oldest first */
    bool pause_hit;
    long ticks;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    uint32_t *argb;
    Uint64 last;
    double acc;
    int shot;
} H;

void host_set_palette(const uint8_t *pal)
{
    memcpy(H.dac, pal, sizeof H.dac);
}

void host_get_palette(uint8_t *pal)
{
    memcpy(pal, H.dac, sizeof H.dac);
}

bool host_headless(void)
{
    return H.opt.headless;
}

long host_ticks(void)
{
    return H.ticks;
}

/* The palette as the VGA gets it: SETPALETTE adds _GammaCorrect and caps each component at 63. */
static void to_argb(uint32_t *out)
{
    uint32_t lut[256];
    for (int i = 0; i < 256; i++) {
        uint32_t c[3];
        for (int k = 0; k < 3; k++) {
            const int v = H.dac[i * 3 + k] + G.gamma;
            c[k] = (uint32_t)(v > 63 ? 63 : v) * 255u / 63;
        }
        lut[i] = 0xFF000000u | c[0] << 16 | c[1] << 8 | c[2];
    }
    for (int i = 0; i < Vga.w * Vga.h; i++)
        out[i] = lut[Vga.px[i]];
}

int host_screenshot(const char *path)
{
    uint32_t *px = malloc((size_t)Vga.w * Vga.h * sizeof *px);
    if (!px)
        return -1;
    to_argb(px);
    const int rc = png_write_argb(path, px, Vga.w, Vga.h);
    free(px);
    return rc;
}

bool host_init(const host_options_t *o)
{
    memset(&H, 0, sizeof H);
    H.opt = *o;
    if (!surface_init(&Vga, SCREEN_W, SCREEN_H))
        return false;
    if (o->headless)
        return true;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "roc: SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    H.win = SDL_CreateWindow("Realms of Chaos", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             SCREEN_W * SCALE, SCREEN_H * SCALE, SDL_WINDOW_RESIZABLE);
    H.ren = H.win ? SDL_CreateRenderer(H.win, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    H.tex = H.ren ? SDL_CreateTexture(H.ren, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H)
                  : NULL;
    H.argb = malloc(SCREEN_W * SCREEN_H * sizeof *H.argb);
    if (!H.tex || !H.argb) {
        fprintf(stderr, "roc: SDL: %s\n", SDL_GetError());
        return false;
    }
    SDL_RenderSetLogicalSize(H.ren, SCREEN_W, SCREEN_H);
    H.last = SDL_GetPerformanceCounter();
    return true;
}

void host_shutdown(void)
{
    free(H.argb);
    if (H.tex)
        SDL_DestroyTexture(H.tex);
    if (H.ren)
        SDL_DestroyRenderer(H.ren);
    if (H.win) {
        SDL_DestroyWindow(H.win);
        SDL_Quit();
    }
    surface_free(&Vga);
    memset(&H, 0, sizeof H);
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
    case SDL_SCANCODE_LEFTBRACKET: return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_SEMICOLON: return 0x27;
    case SDL_SCANCODE_APOSTROPHE: return 0x28;
    case SDL_SCANCODE_GRAVE: return 0x29;
    case SDL_SCANCODE_BACKSLASH: return 0x2B;
    case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;
    case SDL_SCANCODE_SLASH: return 0x35;
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

/* What the BIOS would give for a key pressed (US layout), shifted or not; 0 for none. */
static int pc_ascii(int k, bool shift)
{
    static const char plain[0x36] = {
        0,   0x1B, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', 0,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\r', 0,   'a', 's',
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,  '\\', 'z', 'x', 'c', 'v',
        'b', 'n', 'm', ',', '.', '/',
    };
    static const char shifted[0x36] = {
        0,   0x1B, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', 0,
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\r', 0,   'A', 'S',
        'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',
        'B', 'N', 'M', '<', '>', '?',
    };
    if (k == 0x39)
        return ' ';
    if (k > 0 && k < 0x36)
        return (unsigned char)(shift ? shifted[k] : plain[k]);
    return 0;
}

int host_getkey(void)
{
    if (!H.typed_count)
        return 0;
    const int c = H.typed[0];
    memmove(H.typed, H.typed + 1, (size_t)--H.typed_count * sizeof H.typed[0]);
    return c;
}

void host_flush_keys(void)
{
    H.typed_count = 0;
}

/* Keys pressed since the frame before become typed characters. */
static void type_keys(void)
{
    const bool shift = H.keys[0x2A] || H.keys[0x36];
    for (int k = 1; k < 0x40; k++) {
        if (H.keys[k] && !H.prev[k]) {
            const int c = pc_ascii(k, shift);
            if (c && H.typed_count < (int)(sizeof H.typed / sizeof H.typed[0]))
                H.typed[H.typed_count++] = c;
        }
    }
    memcpy(H.prev, H.keys, sizeof H.prev);
}

static void sync_keys(void)
{
    type_keys();
    memcpy(G.keys, H.keys, sizeof G.keys);
    if (H.pause_hit) {
        G.pause_hit = 1; /* _PauseHit, from the keyboard interrupt */
        H.pause_hit = false;
    }
}

static int headless_frame(void)
{
    if (H.ticks >= H.opt.ticks)
        longjmp(host_end, 1);
    long step = H.opt.ticks - H.ticks < HEADLESS_STEP ? H.opt.ticks - H.ticks : HEADLESS_STEP;
    memset(H.keys, 0, sizeof H.keys);
    for (int i = 0; i < H.opt.hold_count; i++) {
        const host_hold_t *h = &H.opt.holds[i];
        if (H.ticks >= h->from && H.ticks < h->to && h->scancode > 0 && h->scancode < 128)
            H.keys[h->scancode] = 1;
    }
    H.ticks += step;
    audio_advance((int)step);
    sync_keys();
    return (int)step;
}

int host_frame(void)
{
    if (H.opt.headless)
        return headless_frame();
    to_argb(H.argb);
    SDL_UpdateTexture(H.tex, NULL, H.argb, SCREEN_W * sizeof *H.argb);
    SDL_RenderClear(H.ren);
    SDL_RenderCopy(H.ren, H.tex, NULL, NULL);
    SDL_RenderPresent(H.ren);

    const double freq = (double)SDL_GetPerformanceFrequency();
    int ticks = 0;
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                longjmp(host_end, 2);
            if (e.type != SDL_KEYDOWN && e.type != SDL_KEYUP)
                continue;
            const int pc = pc_scancode(e.key.keysym.scancode);
            if (pc >= 0)
                H.keys[pc] = e.type == SDL_KEYDOWN;
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                if (e.key.keysym.scancode == SDL_SCANCODE_PAUSE)
                    H.pause_hit = true;
                if (e.key.keysym.scancode == SDL_SCANCODE_F12) {
                    char name[32];
                    snprintf(name, sizeof name, "shot%03d.png", H.shot++);
                    if (host_screenshot(name) == 0)
                        printf("saved %s\n", name);
                }
            }
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        H.acc += (double)(now - H.last) / freq * TICKS_PER_SECOND;
        H.last = now;
        ticks = (int)H.acc;
        if (ticks >= 1)
            break;
        SDL_Delay(1);
    }
    H.acc -= ticks;
    if (ticks > MAX_STEP)
        ticks = MAX_STEP;
    H.ticks += ticks;
    audio_advance(ticks); /* (only when there's no sound device) */
    sync_keys();
    return ticks;
}

void wait_tics(int n)
{
    for (int t = 0; t < n;)
        t += host_frame();
}

/* RXPALF.C:60 MYFADEOUT: 32 steps, each taking 1 / (32 - step) of what's left. */
void fade_out(void)
{
    uint8_t pal[768];
    host_get_palette(pal);
    if (H.opt.headless) {
        memset(pal, 0, sizeof pal);
        host_set_palette(pal);
        return;
    }
    for (int y = 0; y < 0x20; y++) {
        for (int x = 0; x < 0x300; x++)
            pal[x] = (uint8_t)(pal[x] - pal[x] / (0x20 - y));
        host_set_palette(pal);
        wait_tics(1);
    }
}

/* RXPALF.C:81 FADETOPAL: the same towards `target`. */
void fade_to(const uint8_t *target)
{
    uint8_t pal[768], to[768];
    memcpy(to, target, sizeof to);
    host_get_palette(pal);
    if (H.opt.headless) {
        host_set_palette(to);
        return;
    }
    for (int y = 0; y < 0x20; y++) {
        for (int x = 0; x < 0x300; x++)
            if (to[x] != pal[x])
                /* (16-bit unsigned maths: a step down wraps round to the low byte) */
                pal[x] = (uint8_t)(pal[x] + (uint8_t)((uint16_t)(to[x] - pal[x]) / (0x20 - y)));
        host_set_palette(pal);
        wait_tics(1);
    }
}
