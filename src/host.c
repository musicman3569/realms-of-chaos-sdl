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
    uint8_t hold_prev[128];      /* headless: the keys the holds held at the frame before */
    uint8_t joy_held[4 + JOY_BUTTONS]; /* headless: the scripted joystick (HOLD_JOY + n) */
    SDL_GameController *pad;     /* the joystick, as a game controller */
    SDL_Joystick *stick;         /* or as a plain joystick */
    uint8_t joy_x, joy_y;        /* DS:53ca / 53cb: the stick holds the arrow keys */
    uint8_t joy_button[JOY_BUTTONS]; /* DS:53c6-53c9: each button's state when last read */
    uint8_t joy_key[JOY_BUTTONS];    /* the key each button pressed */
    int pad_mode;                /* the buttons' layout (PAD_GAME...) */
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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
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
    if ((Sess.joystick_on || o->joystick) && SDL_NumJoysticks() < 1)
        fprintf(stderr, "roc: no joystick found (it's used once one is plugged in)\n");
    return true;
}

static void close_joystick(void)
{
    if (H.pad)
        SDL_GameControllerClose(H.pad);
    if (H.stick)
        SDL_JoystickClose(H.stick);
    H.pad = NULL;
    H.stick = NULL;
}

/* The first joystick plugged in: as a game controller when SDL knows its layout (the left stick
 * and the D-pad, buttons A, B, X, Y), else the first two axes, the first hat and buttons 0-3.
 * SDL reports the joysticks already plugged in as added at the start. */
static void open_joystick(void)
{
    close_joystick();
    for (int i = 0; i < SDL_NumJoysticks() && !H.pad && !H.stick; i++) {
        if (SDL_IsGameController(i))
            H.pad = SDL_GameControllerOpen(i);
        else
            H.stick = SDL_JoystickOpen(i);
    }
}

void host_shutdown(void)
{
    close_joystick();
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

bool host_joystick_on(void)
{
    if (!Sess.joystick_on && !H.opt.joystick)
        return false;
    return H.opt.headless ? H.opt.joystick : H.pad || H.stick;
}

void host_pad_mode(int mode)
{
    H.pad_mode = mode;
}

/* The stick (-32768 to 32767 each way) and the buttons as they are now: bits 0-3 the gameport's
 * buttons 0-3, bit 4 a game controller's Start. A game controller's A, X, B, Y are buttons 0-3,
 * so with ROC.CFG's MyBUTTON0-3 as shipped (jump, attack, the special item, look) A jumps, X
 * attacks and B changes hero, as on an Xbox 360 controller's layout. */
static void read_joystick(int *x, int *y, unsigned *buttons)
{
    *x = *y = 0;
    *buttons = 0;
    if (H.opt.headless) {
        *x = H.joy_held[0] ? -32768 : H.joy_held[1] ? 32767 : 0;
        *y = H.joy_held[2] ? -32768 : H.joy_held[3] ? 32767 : 0;
        for (int b = 0; b < JOY_BUTTONS; b++)
            *buttons |= (unsigned)H.joy_held[4 + b] << b;
    } else if (H.pad) {
        static const SDL_GameControllerButton face[JOY_BUTTONS] = {
            SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_B,
            SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_START};
        *x = SDL_GameControllerGetAxis(H.pad, SDL_CONTROLLER_AXIS_LEFTX);
        *y = SDL_GameControllerGetAxis(H.pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (SDL_GameControllerGetButton(H.pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
            *x = -32768;
        else if (SDL_GameControllerGetButton(H.pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
            *x = 32767;
        if (SDL_GameControllerGetButton(H.pad, SDL_CONTROLLER_BUTTON_DPAD_UP))
            *y = -32768;
        else if (SDL_GameControllerGetButton(H.pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
            *y = 32767;
        for (int b = 0; b < JOY_BUTTONS; b++)
            *buttons |= (unsigned)(SDL_GameControllerGetButton(H.pad, face[b]) != 0) << b;
    } else if (H.stick) {
        if (SDL_JoystickNumAxes(H.stick) >= 2) {
            *x = SDL_JoystickGetAxis(H.stick, 0);
            *y = SDL_JoystickGetAxis(H.stick, 1);
        }
        if (SDL_JoystickNumHats(H.stick) >= 1) {
            const Uint8 hat = SDL_JoystickGetHat(H.stick, 0);
            if (hat & SDL_HAT_LEFT)
                *x = -32768;
            else if (hat & SDL_HAT_RIGHT)
                *x = 32767;
            if (hat & SDL_HAT_UP)
                *y = -32768;
            else if (hat & SDL_HAT_DOWN)
                *y = 32767;
        }
        for (int b = 0; b < 4 && b < SDL_JoystickNumButtons(H.stick); b++)
            *buttons |= (unsigned)(SDL_JoystickGetButton(H.stick, b) != 0) << b;
    }
}

/* How far the stick must go to count, from ROC.CFG's calibration: SETUP stores the centre's
 * reading less and plus a margin (JoyXlo / JoyXhi, as JOYCALIBRATE makes them), so the margin
 * over the centre is the part of the throw that doesn't count. Half the throw without one. */
static int joy_threshold(uint16_t lo, uint16_t hi)
{
    if (lo >= hi)
        return 16384;
    const long t = (long)(hi - lo) * 32767 / ((long)lo + hi);
    return t < 4000 ? 4000 : t > 28000 ? 28000 : (int)t;
}

static void set_action(int action, uint8_t on)
{
    H.keys[Sess.key_trans[action & 7] & 0x7F] = on;
}

/* The key button b presses (the port's): Start is Esc everywhere (the main menu in a level, back
 * in the menus). In a level each gameport button presses the key its _MyBUTTON names, as
 * MYJOYREADER does. In the menus and windows button 0 (A) is Enter and button 2 (B) Esc; at a
 * "Press Y" prompt the attack button (X) and button 3 (Y) are Y as well. */
static int button_scancode(int b)
{
    if (b == 4)
        return 0x01;
    const int k = Sess.key_trans[Sess.my_button[b] & 7] & 0x7F;
    if (H.pad_mode == PAD_GAME)
        return k;
    if (b == 0)
        return 0x1C;
    if (b == 2)
        return 0x01;
    if (H.pad_mode == PAD_PROMPT && ((Sess.my_button[b] & 7) == K_ATTACK || b == 3))
        return 0x15;
    return k;
}

/* RXUTILSF.C:30 MYJOYREADER: the stick past its threshold holds the arrow key that way and lets
 * go of the other; back inside, it lets go of both, once. Each button (port 201h bits 4-7)
 * presses or lets go of the key its _MyBUTTON names when it changes. So the joystick writes into
 * _Keys as the keyboard does, and a key held on either shows. The port picks a button's key by
 * the layout (button_scancode) when it's pressed and lets go of that same key when it's let go,
 * even if the layout changed meanwhile. */
static void my_joy_reader(void)
{
    int x, y;
    unsigned buttons;
    read_joystick(&x, &y, &buttons);
    const int tx = joy_threshold(Sess.joy_cal[1], Sess.joy_cal[2]);
    const int ty = joy_threshold(Sess.joy_cal[4], Sess.joy_cal[5]);
    if (x < -tx || x > tx) {
        H.joy_x = 1;
        set_action(K_LEFT, x < 0);
        set_action(K_RIGHT, x > 0);
    } else if (H.joy_x) {
        H.joy_x = 0;
        set_action(K_LEFT, 0);
        set_action(K_RIGHT, 0);
    }
    if (y < -ty || y > ty) {
        H.joy_y = 1;
        set_action(K_UP, y < 0);
        set_action(K_DOWN, y > 0);
    } else if (H.joy_y) {
        H.joy_y = 0;
        set_action(K_UP, 0);
        set_action(K_DOWN, 0);
    }
    for (int b = 0; b < JOY_BUTTONS; b++) {
        const uint8_t on = (buttons >> b) & 1;
        if (on != H.joy_button[b]) {
            H.joy_button[b] = on;
            if (on)
                H.joy_key[b] = (uint8_t)button_scancode(b);
            H.keys[H.joy_key[b]] = on;
        }
    }
}

/* The original reads the joystick in RUNLEVEL (_JoyReader) and in the windows' key loops; the
 * port reads it at every frame, which covers both. */
static void sync_keys(void)
{
    if (host_joystick_on())
        my_joy_reader();
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
    /* a held key goes down and up as its holds start and end, like the keyboard's events, so
     * the joystick can write into the keys too */
    uint8_t held[128] = {0};
    memset(H.joy_held, 0, sizeof H.joy_held);
    for (int i = 0; i < H.opt.hold_count; i++) {
        const host_hold_t *h = &H.opt.holds[i];
        if (H.ticks < h->from || H.ticks >= h->to)
            continue;
        if (h->scancode > 0 && h->scancode < 128)
            held[h->scancode] = 1;
        else if (h->scancode >= HOLD_JOY && h->scancode < HOLD_JOY + 4 + JOY_BUTTONS)
            H.joy_held[h->scancode - HOLD_JOY] = 1;
    }
    for (int k = 0; k < 128; k++)
        if (held[k] != H.hold_prev[k])
            H.keys[k] = held[k];
    memcpy(H.hold_prev, held, sizeof held);
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
            if (e.type == SDL_JOYDEVICEADDED || e.type == SDL_JOYDEVICEREMOVED)
                open_joystick();
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
