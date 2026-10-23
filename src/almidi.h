/* The original's MIDI music on an AdLib: Apogee's AL_MIDI driver (segment 45b5) and the part of
 * MIDI.C (42b4) between a song's events and the driver, driving the OPL2 emulator (opl.c). The
 * instruments (_ADLIB_TimbreBank, DS:25f2), the drum map (DS:24f2) and the pitch tables are read
 * from ROC.EXE. */
#ifndef ROC_ALMIDI_H
#define ROC_ALMIDI_H

#include <stdbool.h>
#include <stdint.h>

/* Read the tables from ROC.EXE; false if it isn't the game's EXE. */
bool al_load_tables(const char *exe_path);
/* AL_Init / AL_Reset: the chip and every voice and channel to their start. */
void al_init(void);

/* MIDI.C: a song's channel message (status with its channel, and its data bytes). */
void midi_event(uint8_t status, uint8_t d1, uint8_t d2);
/* MIDI_Reset: every note off, the controllers reset, each channel's volume 90. */
void midi_reset(void);
/* MIDI_AllNotesOff */
void midi_all_notes_off(void);
/* MIDI_SetVolume (0-255): each channel's volume is scaled by it. */
void midi_set_volume(int volume);

#endif
