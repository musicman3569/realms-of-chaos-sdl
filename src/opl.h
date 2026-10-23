/* A Yamaha YM3812 (OPL2), the AdLib's FM chip, as the original's music driver (AL_MIDI) uses it:
 * 9 two-operator channels, the 4 waveforms, envelopes, key scaling, tremolo and vibrato (no
 * rhythm mode, timers or CSM, which AL_MIDI doesn't use). It runs at the chip's own rate, its
 * 3.58 MHz clock / 72. */
#ifndef ROC_OPL_H
#define ROC_OPL_H

#include <stdint.h>

enum { OPL_RATE = 49716 };

void opl_reset(void);
/* A register write (port 388h: the index, then 389h: the value). */
void opl_write(int reg, int value);
/* The next sample: the channels' sum (about +-4096 each). */
int opl_sample(void);

#endif
