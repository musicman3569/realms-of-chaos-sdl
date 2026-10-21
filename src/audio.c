/* The sound devices (see audio.h): an SDL2 mixer for the effects, a MIDI sequencer for the
 * music. */
#include "audio.h"

#include "engine.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#endif

enum { MIX_RATE = 44100, MAX_VOICES = 32 };

/* ------------------------------------------------------------------------------------------ */
/* Effects                                                                                     */

typedef struct {
    const fx_sample_t *s;
    uint32_t pos;                /* 16.16 fixed point, in the sample */
    uint32_t step;
    int priority;
    int handle;                  /* 0: free */
} voice_t;

static struct {
    bool open, device;
    SDL_AudioDeviceID dev;
    voice_t voice[MAX_VOICES];
    int voices;
    int next_handle;
    int volume;                  /* 0-255 */
    double tick_acc;             /* audio_advance's fraction of a sample */
} A = {.volume = 255, .voices = 8};

static void lock(void)
{
    if (A.device)
        SDL_LockAudioDevice(A.dev);
}

static void unlock(void)
{
    if (A.device)
        SDL_UnlockAudioDevice(A.dev);
}

/* Mix n frames into out (stereo, or NULL to only move the voices on). */
static void mix(int16_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        int32_t acc = 0;
        for (int v = 0; v < A.voices; v++) {
            voice_t *vc = &A.voice[v];
            if (!vc->handle)
                continue;
            const uint32_t k = vc->pos >> 16;
            if (k >= vc->s->len) {
                vc->handle = 0;
                continue;
            }
            if (out) {
                /* linear between two samples */
                const int32_t a = vc->s->pcm[k];
                const int32_t b = k + 1 < vc->s->len ? vc->s->pcm[k + 1] : a;
                acc += a + (int32_t)(((int64_t)(b - a) * (vc->pos & 0xFFFF)) >> 16);
            }
            vc->pos += vc->step;
        }
        if (out) {
            acc = acc * A.volume / 255;
            if (acc > 32767)
                acc = 32767;
            if (acc < -32768)
                acc = -32768;
            out[i * 2] = out[i * 2 + 1] = (int16_t)acc;
        }
    }
}

static void SDLCALL callback(void *user, Uint8 *stream, int len)
{
    (void)user;
    mix((int16_t *)stream, len / 4);
}

static bool midi_open(void);
static void midi_close(void);

bool audio_init(bool device, bool music, int voices)
{
    A.voices = voices < 1 ? 1 : voices > MAX_VOICES ? MAX_VOICES : voices;
    A.next_handle = 1;
    A.open = true;
    if (device && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want = {0}, have;
        want.freq = MIX_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 512;
        want.callback = callback;
        A.dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (A.dev) {
            A.device = true;
            SDL_PauseAudioDevice(A.dev, 0);
        } else {
            fprintf(stderr, "roc: no sound: %s\n", SDL_GetError());
        }
    }
    if (music)
        midi_open();
    return A.device;
}

void audio_shutdown(void)
{
    midi_close();
    if (A.device) {
        SDL_CloseAudioDevice(A.dev);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    A.device = false;
    A.open = false;
    memset(A.voice, 0, sizeof A.voice);
}

bool audio_has_device(void)
{
    return A.device;
}

void audio_advance(int ticks)
{
    if (A.device || !A.open)
        return;
    A.tick_acc += ticks * (MIX_RATE / TICKS_PER_SECOND);
    const int n = (int)A.tick_acc;
    A.tick_acc -= n;
    mix(NULL, n);
}

/* A Creative Voice File: "Creative Voice File\x1a", the offset of the first block, then blocks
 * (type, 24-bit length): type 1 is sound data (a rate byte, 1000000 / (256 - rate) Hz, and the
 * codec, 0 for 8-bit unsigned PCM), 2 more of it, 0 the end. All of ROC.DAT's are one type 1
 * block at 10870 or 10989 Hz. */
bool fx_load_voc(fx_sample_t *s, const uint8_t *voc, size_t len)
{
    memset(s, 0, sizeof *s);
    if (len < 26 || memcmp(voc, "Creative Voice File", 19))
        return false;
    size_t p = (size_t)(voc[20] | voc[21] << 8);
    size_t total = 0;
    int16_t *pcm = NULL;
    while (p + 4 <= len && voc[p] != 0) {
        const int type = voc[p];
        size_t n = (size_t)(voc[p + 1] | voc[p + 2] << 8 | voc[p + 3] << 16);
        const uint8_t *d = voc + p + 4;
        if (p + 4 + n > len)
            n = len - p - 4;
        const uint8_t *data = NULL;
        size_t count = 0;
        if (type == 1 && n >= 2 && d[1] == 0) {
            s->rate = 1000000 / (256 - d[0]);
            data = d + 2;
            count = n - 2;
        } else if (type == 2) {
            data = d;
            count = n;
        }
        if (data && count) {
            int16_t *grown = realloc(pcm, (total + count) * sizeof *pcm);
            if (!grown) {
                free(pcm);
                return false;
            }
            pcm = grown;
            for (size_t i = 0; i < count; i++)
                pcm[total + i] = (int16_t)((data[i] - 0x80) << 8);
            total += count;
        }
        p += 4 + n;
    }
    if (!pcm || !s->rate) {
        free(pcm);
        return false;
    }
    s->pcm = pcm;
    s->len = total;
    return true;
}

void fx_free(fx_sample_t *s)
{
    free(s->pcm);
    memset(s, 0, sizeof *s);
}

int fx_play(const fx_sample_t *s, int priority)
{
    if (!A.open || !s || !s->pcm)
        return -1;
    lock();
    int best = -1;
    for (int v = 0; v < A.voices && best < 0; v++)
        if (!A.voice[v].handle)
            best = v;
    if (best < 0) {
        /* MV_AllocVoice: the lowest priority (the oldest of those), if it isn't above this one */
        for (int v = 0; v < A.voices; v++)
            if (best < 0 || A.voice[v].priority < A.voice[best].priority ||
                (A.voice[v].priority == A.voice[best].priority &&
                 A.voice[v].handle < A.voice[best].handle))
                best = v;
        if (A.voice[best].priority > priority)
            best = -1;
    }
    int handle = -1;
    if (best >= 0) {
        voice_t *vc = &A.voice[best];
        vc->s = s;
        vc->pos = 0;
        vc->step = (uint32_t)(((uint64_t)s->rate << 16) / MIX_RATE);
        vc->priority = priority;
        handle = vc->handle = A.next_handle++;
        if (A.next_handle > 0x7FFF)
            A.next_handle = 1;
    }
    unlock();
    return handle;
}

bool fx_active(int handle)
{
    if (handle <= 0)
        return false;
    bool on = false;
    lock();
    for (int v = 0; v < A.voices; v++)
        on |= A.voice[v].handle == handle;
    unlock();
    return on;
}

void fx_stop(int handle)
{
    if (handle <= 0)
        return;
    lock();
    for (int v = 0; v < A.voices; v++)
        if (A.voice[v].handle == handle)
            A.voice[v].handle = 0;
    unlock();
}

void fx_stop_all(void)
{
    lock();
    for (int v = 0; v < A.voices; v++)
        A.voice[v].handle = 0;
    unlock();
}

void fx_set_volume(int volume)
{
    lock();
    A.volume = volume < 0 ? 0 : volume > 255 ? 255 : volume;
    unlock();
}

int fx_voices_playing(void)
{
    int n = 0;
    lock();
    for (int v = 0; v < A.voices; v++)
        n += A.voice[v].handle != 0;
    unlock();
    return n;
}

/* ------------------------------------------------------------------------------------------ */
/* Music: a standard MIDI file played on the system's synthesiser                             */

typedef struct {
    uint64_t us;                 /* from the song's start */
    uint8_t status, d1, d2, len; /* a channel message of len bytes */
} midi_event_t;

static struct {
    SDL_mutex *lock;
    SDL_Thread *thread;
    volatile bool quit;
    midi_event_t *ev;
    size_t count, next;
    uint64_t length_us;
    bool playing, paused;
    uint64_t start;              /* performance counter at the song's time 0 */
    uint64_t paused_at;
    int volume;                  /* MUSIC_SetVolume: 0-255 */
    int fade_from, fade_to, fade_ms;
    uint64_t fade_start;
    bool fading;
    int sent_volume;             /* the volume the channels' CC 7 were last scaled to */
    uint8_t chan_vol[16];        /* each channel's CC 7 as the song set it */
#ifdef _WIN32
    HMIDIOUT out;
#endif
    bool open;
} M = {.volume = 255, .sent_volume = -1};

static uint64_t now_us(void)
{
    return SDL_GetPerformanceCounter() * 1000000 / SDL_GetPerformanceFrequency();
}

static void midi_send(uint8_t status, uint8_t d1, uint8_t d2)
{
#ifdef _WIN32
    if (M.out)
        midiOutShortMsg(M.out, (DWORD)status | (DWORD)d1 << 8 | (DWORD)d2 << 16);
#else
    (void)status;
    (void)d1;
    (void)d2;
#endif
}

static void notes_off(void)
{
    for (int c = 0; c < 16; c++) {
        midi_send((uint8_t)(0xB0 | c), 0x40, 0);   /* sustain off */
        midi_send((uint8_t)(0xB0 | c), 0x7B, 0);   /* all notes off */
    }
}

/* The volume now (MUSIC_SetVolume, or where a fade has got to). */
static int current_volume(uint64_t now)
{
    if (!M.fading)
        return M.volume;
    const uint64_t done = (now - M.fade_start) / 1000;
    if (done >= (uint64_t)M.fade_ms) {
        M.fading = false;
        M.volume = M.fade_to;
        return M.volume;
    }
    return M.fade_from + (int)((M.fade_to - M.fade_from) * (int64_t)done / M.fade_ms);
}

static void send_volumes(int volume)
{
    if (volume == M.sent_volume)
        return;
    M.sent_volume = volume;
    for (int c = 0; c < 16; c++)
        midi_send((uint8_t)(0xB0 | c), 7, (uint8_t)(M.chan_vol[c] * volume / 255));
}

static int SDLCALL midi_thread(void *user)
{
    (void)user;
    while (!M.quit) {
        SDL_LockMutex(M.lock);
        const uint64_t now = now_us();
        if (M.playing && !M.paused && M.count) {
            send_volumes(current_volume(now));
            const uint64_t t = now - M.start;
            while (M.next < M.count && M.ev[M.next].us <= t) {
                const midi_event_t *e = &M.ev[M.next++];
                if ((e->status & 0xF0) == 0xB0 && e->d1 == 7) {
                    /* the song's channel volume, scaled to the music volume */
                    M.chan_vol[e->status & 0x0F] = e->d2;
                    midi_send(e->status, 7, (uint8_t)(e->d2 * M.sent_volume / 255));
                } else {
                    midi_send(e->status, e->d1, e->d2);
                }
            }
            if (M.next >= M.count && t >= M.length_us) {
                /* MUSIC_PlaySong(song, loop): again from the start */
                notes_off();
                M.next = 0;
                M.start = now;
            }
        }
        SDL_UnlockMutex(M.lock);
        SDL_Delay(1);
    }
    return 0;
}

static bool midi_open(void)
{
#ifdef _WIN32
    if (midiOutOpen(&M.out, MIDI_MAPPER, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        M.out = NULL;
        fprintf(stderr, "roc: no MIDI output, so no music\n");
        return false;
    }
    M.lock = SDL_CreateMutex();
    M.quit = false;
    M.thread = SDL_CreateThread(midi_thread, "music", NULL);
    M.open = M.lock && M.thread;
    return M.open;
#else
    return false;
#endif
}

static void midi_close(void)
{
    if (!M.open)
        return;
    M.quit = true;
    SDL_WaitThread(M.thread, NULL);
    notes_off();
#ifdef _WIN32
    midiOutReset(M.out);
    midiOutClose(M.out);
#endif
    SDL_DestroyMutex(M.lock);
    free(M.ev);
    memset(&M, 0, sizeof M);
    M.volume = 255;
    M.sent_volume = -1;
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* A variable-length quantity. */
static uint32_t vlq(const uint8_t *d, size_t end, size_t *p)
{
    uint32_t v = 0;
    while (*p < end) {
        const uint8_t b = d[(*p)++];
        v = v << 7 | (b & 0x7F);
        if (!(b & 0x80))
            break;
    }
    return v;
}

typedef struct {
    uint32_t tick;
    uint32_t order;              /* keeps equal ticks in file order */
    uint32_t tempo;              /* a tempo change (microseconds a quarter), or 0 */
    uint8_t status, d1, d2, len;
} raw_event_t;

static int raw_cmp(const void *a, const void *b)
{
    const raw_event_t *x = a, *y = b;
    if (x->tick != y->tick)
        return x->tick < y->tick ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order;
}

/* Read a format 0 or 1 MIDI file into events timed in microseconds. */
static bool midi_parse(const uint8_t *d, size_t len)
{
    if (len < 14 || memcmp(d, "MThd", 4))
        return false;
    const unsigned tracks = (unsigned)(d[10] << 8 | d[11]);
    const unsigned division = (unsigned)(d[12] << 8 | d[13]);
    if (division & 0x8000 || division == 0)
        return false; /* (SMPTE time: none of ROC.DAT's) */
    raw_event_t *raw = NULL;
    size_t n = 0, cap = 0;
    size_t p = 8 + be32(d + 4);
    for (unsigned t = 0; t < tracks && p + 8 <= len; t++) {
        if (memcmp(d + p, "MTrk", 4))
            break;
        size_t end = p + 8 + be32(d + p + 4);
        if (end > len)
            end = len;
        p += 8;
        uint32_t tick = 0;
        uint8_t running = 0;
        while (p < end) {
            tick += vlq(d, end, &p);
            if (p >= end)
                break;
            uint8_t status = d[p];
            if (status & 0x80)
                p++;
            else
                status = running;
            raw_event_t e = {tick, (uint32_t)n, 0, status, 0, 0, 0};
            if (status == 0xFF) {
                if (p >= end)
                    break;
                const uint8_t type = d[p++];
                const uint32_t l = vlq(d, end, &p);
                if (type == 0x51 && l == 3 && p + 3 <= end)
                    e.tempo = (uint32_t)(d[p] << 16 | d[p + 1] << 8 | d[p + 2]);
                p += l;
                if (type == 0x2F)
                    p = end;
                if (!e.tempo)
                    continue;
            } else if (status == 0xF0 || status == 0xF7) {
                p += vlq(d, end, &p);
                continue;
            } else if (status >= 0x80) {
                running = status;
                const int hi = status & 0xF0;
                e.len = (uint8_t)(hi == 0xC0 || hi == 0xD0 ? 2 : 3);
                if (p + e.len - 1 > end)
                    break;
                e.d1 = d[p++];
                if (e.len == 3)
                    e.d2 = d[p++];
            } else {
                break; /* a data byte with no running status */
            }
            if (n == cap) {
                cap = cap ? cap * 2 : 4096;
                raw_event_t *g = realloc(raw, cap * sizeof *raw);
                if (!g) {
                    free(raw);
                    return false;
                }
                raw = g;
            }
            raw[n++] = e;
        }
        p = end;
    }
    qsort(raw, n, sizeof *raw, raw_cmp);
    midi_event_t *ev = malloc((n ? n : 1) * sizeof *ev);
    if (!ev) {
        free(raw);
        return false;
    }
    size_t count = 0;
    uint64_t us = 0;
    uint32_t last = 0, tempo = 500000;
    for (size_t i = 0; i < n; i++) {
        us += (uint64_t)(raw[i].tick - last) * tempo / division;
        last = raw[i].tick;
        if (raw[i].tempo) {
            tempo = raw[i].tempo;
            continue;
        }
        ev[count++] = (midi_event_t){us, raw[i].status, raw[i].d1, raw[i].d2, raw[i].len};
    }
    free(raw);
    free(M.ev);
    M.ev = ev;
    M.count = count;
    M.length_us = us;
    return true;
}

void music_play(const uint8_t *mid, size_t len)
{
    if (!M.open)
        return;
    SDL_LockMutex(M.lock);
    notes_off();
    M.playing = false;
    if (midi_parse(mid, len)) {
        for (int c = 0; c < 16; c++) {
            M.chan_vol[c] = 100;
            midi_send((uint8_t)(0xB0 | c), 0x79, 0); /* reset the controllers */
        }
        M.sent_volume = -1;
        M.next = 0;
        M.start = now_us();
        M.paused = false;
        M.playing = true;
    }
    SDL_UnlockMutex(M.lock);
}

void music_stop(void)
{
    if (!M.open)
        return;
    SDL_LockMutex(M.lock);
    M.playing = false;
    M.fading = false;
    notes_off();
    SDL_UnlockMutex(M.lock);
}

void music_set_volume(int volume)
{
    if (!M.open)
        return;
    SDL_LockMutex(M.lock);
    M.volume = volume < 0 ? 0 : volume > 255 ? 255 : volume;
    M.fading = false;
    SDL_UnlockMutex(M.lock);
}

void music_fade(int to, int ms)
{
    if (!M.open)
        return;
    SDL_LockMutex(M.lock);
    const uint64_t now = now_us();
    M.fade_from = current_volume(now);
    M.fade_to = to;
    M.fade_ms = ms > 0 ? ms : 1;
    M.fade_start = now;
    M.fading = true;
    SDL_UnlockMutex(M.lock);
}

bool music_fade_active(void)
{
    if (!M.open)
        return false;
    SDL_LockMutex(M.lock);
    current_volume(now_us());
    const bool f = M.fading;
    SDL_UnlockMutex(M.lock);
    return f;
}

bool music_playing(void)
{
    return M.open && M.playing;
}

void music_pause(void)
{
    if (!M.open)
        return;
    SDL_LockMutex(M.lock);
    if (M.playing && !M.paused) {
        M.paused = true;
        M.paused_at = now_us();
        notes_off();
    }
    SDL_UnlockMutex(M.lock);
}

void music_continue(void)
{
    if (!M.open)
        return;
    SDL_LockMutex(M.lock);
    if (M.paused) {
        M.paused = false;
        M.start += now_us() - M.paused_at;
    }
    SDL_UnlockMutex(M.lock);
}
