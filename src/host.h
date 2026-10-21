/* The platform layer: the screen (the VGA's visible page and its palette), the timer and the
 * keyboard, on SDL2, or scripted for the headless tests.
 *
 * The original draws into VGA memory and sets the DAC; its loops wait on the timer (WAITTICS)
 * and read _Keys, which the keyboard interrupt keeps up to date. The port's equivalent: code
 * draws into `Vga` and sets the palette with host_set_palette; host_frame shows the screen,
 * waits for the next timer tick or more, reads the keyboard into G.keys and returns how many
 * ticks went by. Blocking code (the menus) can loop on host_frame just as the original loops
 * on WAITTICS. */
#ifndef ROC_HOST_H
#define ROC_HOST_H

#include "gfx.h"

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>

enum { SCREEN_W = 320, SCREEN_H = 200 };

/* The screen and its palette (6-bit VGA components; _GammaCorrect is added when it's shown). */
extern surface_t Vga;
void host_set_palette(const uint8_t *pal);  /* SETPALETTE */
void host_get_palette(uint8_t *pal);        /* GETPALETTE */

/* A key held for a range of ticks in a headless run: a PC scancode. */
typedef struct {
    int scancode;
    long from, to;
} host_hold_t;

typedef struct {
    bool headless;
    long ticks;                  /* headless: the run's length in timer ticks */
    const host_hold_t *holds;    /* headless: the keys to hold */
    int hold_count;
} host_options_t;

bool host_init(const host_options_t *o);
void host_shutdown(void);

/* Show the screen, wait for at least one timer tick, read the keyboard; the ticks since the
 * last call (at most 100). A headless run returns 8 ticks a call and holds the scripted keys;
 * when its ticks are used up it longjmps to host_end (which the caller sets up), with the
 * screen as it was last shown. Closing the window does the same. */
int host_frame(void);
extern jmp_buf host_end;
long host_ticks(void);           /* the ticks host_frame has returned so far */
bool host_headless(void);

/* WAITTICS: frames until n ticks have gone by. */
void wait_tics(int n);
/* MYFADEOUT and FADETOPAL: 32 steps from the palette shown, a frame and a tick each (headless:
 * at once). */
void fade_out(void);
void fade_to(const uint8_t *pal);

/* The keys the game reads (_Keys) are PC scancodes; host_frame also sets G.pause_hit for the
 * Pause key. The last character typed (for WINDOWCURSOR: letters, digits, space, punctuation,
 * Enter, Esc, Backspace), or 0. */
int host_getkey(void);
void host_flush_keys(void);

/* Save the screen as a PNG with its palette. */
int host_screenshot(const char *path);

#endif
