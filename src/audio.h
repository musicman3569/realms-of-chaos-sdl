/* The sound devices, in place of the Apogee Sound System (SOURCE\*.C): FX_* plays digitised
 * sound effects on a few voices, MUSIC_* plays a MIDI song on an AdLib.
 *
 * Both are mixed by the port in the SDL2 audio callback: the effects' samples, and the music
 * as the original's AdLib driver plays it (almidi.c) on an emulated OPL2 (opl.c). In a headless
 * run, or when there's no audio device, the effects' mixer still runs, advanced by the game's
 * ticks, so a sound lasts as long as it would and the game (which waits for sounds to end) runs
 * the same; nothing is heard, and there's no music. */
#ifndef ROC_AUDIO_H
#define ROC_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* `device`: open the audio device (else the mixer runs silently, see audio_advance); `music`:
 * start the AdLib (al_load_tables must have read the driver's tables). */
bool audio_init(bool device, bool music, int voices);
void audio_shutdown(void);
/* A silent mixer moves on by `ticks` timer ticks (host_frame calls it when there's no device). */
void audio_advance(int ticks);
bool audio_has_device(void);
/* Mix the next `frames` stereo frames into out (with no device: tests). */
void audio_render(int16_t *out, int frames);

/* A sound effect: 8-bit unsigned PCM at `rate` Hz (a VOC file's first sound block). */
typedef struct {
    int16_t *pcm;
    size_t len;
    int rate;
} fx_sample_t;

bool fx_load_voc(fx_sample_t *s, const uint8_t *voc, size_t len);
void fx_free(fx_sample_t *s);

/* FX_PlayVOC: on a free voice, else instead of the lowest-priority one if that one's priority
 * isn't higher; the voice's handle (> 0), or -1. FX_SoundActive, FX_StopSound, FX_SetVolume
 * (0-255, all effects), FX_VoicesPlaying. The sample must outlive the sound. */
int fx_play(const fx_sample_t *s, int priority);
bool fx_active(int handle);
void fx_stop(int handle);
void fx_stop_all(void);
void fx_set_volume(int volume);
int fx_voices_playing(void);

/* MUSIC_PlaySong (a standard MIDI file, looped), MUSIC_StopSong, MUSIC_SetVolume (0-255),
 * MUSIC_FadeVolume(to, ms), MUSIC_FadeActive, MUSIC_Pause / MUSIC_Continue. The song is
 * copied. */
void music_play(const uint8_t *mid, size_t len);
void music_stop(void);
void music_set_volume(int volume);
void music_fade(int to, int ms);
bool music_fade_active(void);
bool music_playing(void);
void music_pause(void);
void music_continue(void);

#endif
