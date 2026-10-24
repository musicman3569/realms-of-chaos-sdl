/* A Yamaha YM3812 (OPL2), the AdLib's FM chip, as the original's music driver (AL_MIDI) uses it:
 * 9 two-operator channels, the 4 waveforms, envelopes, key scaling, tremolo and vibrato (no
 * rhythm mode, timers or CSM, which AL_MIDI doesn't use). It runs at the chip's own rate, its
 * 3.58 MHz clock / 72. */
#ifndef ROC_OPL_H
#define ROC_OPL_H

#include <stdint.h>

enum { OPL_RATE = 49716, OPL_CHIPS = 2 };

/* Two chips, as a Sound Blaster Pro has (the left and the right), reset together. */
void opl_reset(void);
/* A register write to chip 0 or 1 (an index port, then the value port). */
void opl_write(int chip, int reg, int value);
/* The next sample of each chip: its channels' sum (about +-4096 each). */
void opl_sample(int out[OPL_CHIPS]);

#endif
