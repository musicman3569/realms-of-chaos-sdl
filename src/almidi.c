/* AL_MIDI and MIDI.C's volume layer (see almidi.h). Read from the machine code (the sound
 * library has no debug information); the names are those the library exports, or its source's
 * where the export has none.
 *
 * The driver gives each note one of the chip's 9 channels ("voices"), taken from a free list in
 * order and given back to its end. A note's channel (MIDI) has an instrument (program), a volume
 * and a pitch bend. Channel 10 (9 here) is the drums: the key picks an instrument from the bank's
 * second half and the note it is played at (the drum map). A voice gets the instrument's
 * registers (AL_SetVoiceTimbre), the carrier's level from the instrument, the velocity and the
 * channel's volume (AL_SetVoiceVolume), and the frequency from the key, the channel's bend and
 * the pitch tables (AL_SetVoicePitch).
 *
 * Stereo: the driver can also play on two chips, one for each side, as a Sound Blaster Pro has
 * them (_AL_Stereo, DS:3488; _AL_LeftPort / _AL_RightPort, DS:3484 / 3486; DS:348a selects an
 * OPL3's two halves instead). Every register write then goes to both, and the right chip plays
 * each note 5/32 of a semitone sharp, which widens the sound like a chorus (the pan and detune
 * controllers are kept but used nowhere). ROC.EXE never turns it on: AL_Init, its only setter,
 * clears it, so the original plays in mono on every card. The port plays this stereo by default
 * (al_set_stereo) and the original's mono on request. */
#include "almidi.h"

#include "opl.h"

#include <stdio.h>
#include <string.h>

enum {
    VOICES = 9, CHANNELS = 16, DRUMS = 9, KEY_ON = 0x2000,
    /* DGROUP offsets of the tables, and of DGROUP in the EXE's image */
    DGROUP_PARA = 0x3A77, PERC_MAP = 0x24F2, TIMBRES = 0x25F2, SLOT_VOICE = 0x312C,
    OFFSET_SLOT = 0x3150, NOTE_PITCH = 0x3162, OCTAVE_PITCH = 0x3462, TABLES_END = 0x3472,
};

/* TIMBRE: SAVEK (20h), Level (40h), Env1 (60h), Env2 (80h), Wave (E0h) for the modulator and
 * the carrier, then Feedback (C0h). */
typedef struct {
    uint8_t savek[2], level[2], env1[2], env2[2], wave[2], feedback;
} timbre_t;

typedef struct {
    int next, prev;              /* in the free list or a channel's list (-1 ends) */
    int key, velocity, channel;
    int timbre;
    int pitch;                   /* pitchleft: the A0h / B0h value without the key bit */
    int pitch_right;             /* pitchright: the right chip's (stereo) */
    int status;                  /* KEY_ON while sounding */
} voice_t;

typedef struct {
    int first, last;             /* its voices */
} list_t;

typedef struct {
    list_t voices;
    int timbre;                  /* the program */
    int pitchbend;
    int key_offset;              /* semitones from the bend */
    int key_detune;              /* 1/32 semitones from the bend */
    int volume;                  /* 0-127 */
    int pan, detune;             /* controllers 10 and 94 (only the stereo OPL3 uses them) */
} channel_t;

static struct {
    timbre_t timbre[256];
    uint8_t perc_map[128][2];    /* a drum key's instrument and note */
    int slot_voice[VOICES][2];   /* slotVoice */
    int offset_slot[18];         /* offsetSlot: a slot's register offset */
    uint16_t note_pitch[32][12]; /* NotePitch[detune][note]: F-numbers */
    uint16_t octave_pitch[8];    /* OctavePitch: the block bits */
    bool loaded;
    voice_t voice[VOICES];
    list_t free_voices;          /* Voice_Pool */
    channel_t channel[CHANNELS];
    int voice_level[18];         /* VoiceLevel: 63 - the slot's total level */
    int voice_ksl[18];           /* VoiceKsl */
    int bend_range;              /* PitchBendRange: 2 semitones */
    /* MIDI.C */
    int midi_volume[CHANNELS];   /* _MIDI_ChannelVolume */
    int total_volume;            /* _MIDI_TotalVolume */
    bool stereo;                 /* _AL_Stereo (the port's choice) */
} A = {.total_volume = 255};

void al_set_stereo(bool on)
{
    A.stereo = on;
}

bool al_stereo(void)
{
    return A.stereo;
}

/* AL_SendOutputToPort's ports: 388h (the AdLib's: on a Sound Blaster Pro both chips answer to
 * it) and the left and right chips' own. Without stereo there's one chip, chip 0. */
enum { PORT_ADLIB, PORT_LEFT, PORT_RIGHT };

static void out(int port, int reg, int v)
{
    if (!A.stereo || port == PORT_LEFT) {
        opl_write(0, reg, v);
    } else if (port == PORT_RIGHT) {
        opl_write(1, reg, v);
    } else {
        opl_write(0, reg, v);
        opl_write(1, reg, v);
    }
}

/* AL_SendOutput: to both chips' own ports in stereo, else to 388h. */
static void send_output(int reg, int v)
{
    if (A.stereo) {
        out(PORT_LEFT, reg, v);
        out(PORT_RIGHT, reg, v);
    } else {
        out(PORT_ADLIB, reg, v);
    }
}

bool al_load_tables(const char *exe_path)
{
    FILE *f = fopen(exe_path, "rb");
    uint8_t mz[10], d[TABLES_END - PERC_MAP];
    bool ok = f && fread(mz, 1, sizeof mz, f) == sizeof mz && mz[0] == 'M' && mz[1] == 'Z';
    if (ok) {
        const long at = (long)(mz[8] | mz[9] << 8) * 16 + DGROUP_PARA * 16L + PERC_MAP;
        ok = fseek(f, at, SEEK_SET) == 0 && fread(d, 1, sizeof d, f) == sizeof d;
    }
    if (f)
        fclose(f);
    if (!ok)
        return false;
#define AT(o) (d + (o) - PERC_MAP)
#define W(o) ((int)(AT(o)[0] | AT(o)[1] << 8))
    memcpy(A.perc_map, AT(PERC_MAP), sizeof A.perc_map);
    for (int i = 0; i < 256; i++) {
        const uint8_t *t = AT(TIMBRES) + i * 11;
        timbre_t *x = &A.timbre[i];
        for (int k = 0; k < 2; k++) {
            x->savek[k] = t[k];
            x->level[k] = t[2 + k];
            x->env1[k] = t[4 + k];
            x->env2[k] = t[6 + k];
            x->wave[k] = t[8 + k];
        }
        x->feedback = t[10];
    }
    for (int v = 0; v < VOICES; v++)
        for (int k = 0; k < 2; k++)
            A.slot_voice[v][k] = W(SLOT_VOICE + (v * 2 + k) * 2);
    for (int s = 0; s < 18; s++)
        A.offset_slot[s] = AT(OFFSET_SLOT)[s];
    for (int t = 0; t < 32; t++)
        for (int n = 0; n < 12; n++)
            A.note_pitch[t][n] = (uint16_t)W(NOTE_PITCH + (t * 12 + n) * 2);
    for (int o = 0; o < 8; o++)
        A.octave_pitch[o] = (uint16_t)W(OCTAVE_PITCH + o * 2);
#undef AT
#undef W
    A.loaded = true;
    return true;
}

/* LL_AddToTail / LL_Remove on a voice list */
static void list_add(list_t *l, int v)
{
    A.voice[v].next = -1;
    A.voice[v].prev = l->last;
    if (l->last >= 0)
        A.voice[l->last].next = v;
    else
        l->first = v;
    l->last = v;
}

static void list_remove(list_t *l, int v)
{
    voice_t *x = &A.voice[v];
    if (x->prev >= 0)
        A.voice[x->prev].next = x->next;
    else
        l->first = x->next;
    if (x->next >= 0)
        A.voice[x->next].prev = x->prev;
    else
        l->last = x->prev;
    x->next = x->prev = -1;
}

/* AL_SetVoiceTimbre: the instrument's registers, unless the voice has it already. The carrier
 * starts silent (its level is AL_SetVoiceVolume's). */
static void set_voice_timbre(int v)
{
    voice_t *x = &A.voice[v];
    const int patch = x->channel == DRUMS ? A.perc_map[x->key & 0x7F][0] : A.channel[x->channel].timbre;
    if (x->timbre == patch)
        return;
    x->timbre = patch;
    const timbre_t *t = &A.timbre[patch & 0xFF];
    int slot = A.slot_voice[v][0];
    int off = A.offset_slot[slot];
    A.voice_level[slot] = 63 - (t->level[0] & 0x3F);
    A.voice_ksl[slot] = t->level[0] & 0xC0;
    send_output(0xA0 + v, 0);
    send_output(0xB0 + v, 0);
    send_output(0x80 + off, 0xFF); /* (a quick release, so the old note ends) */
    send_output(0x60 + off, t->env1[0]);
    send_output(0x80 + off, t->env2[0]);
    send_output(0x20 + off, t->savek[0]);
    send_output(0xE0 + off, t->wave[0]);
    send_output(0x40 + off, t->level[0]);
    out(PORT_ADLIB, 0xC0 + v, t->feedback); /* (straight to 388h) */
    slot = A.slot_voice[v][1];
    off = A.offset_slot[slot];
    A.voice_level[slot] = 63 - (t->level[1] & 0x3F);
    A.voice_ksl[slot] = t->level[1] & 0xC0;
    send_output(0x40 + off, 0x3F);
    send_output(0x80 + off, 0xFF);
    send_output(0x60 + off, t->env1[1]);
    send_output(0x80 + off, t->env2[1]);
    send_output(0x20 + off, t->savek[1]);
    send_output(0xE0 + off, t->wave[1]);
}

/* AL_SetVoiceVolume: the carrier's level, (63 - level) * (velocity + 128) * volume >> 15. */
static void set_voice_volume(int v)
{
    const voice_t *x = &A.voice[v];
    int velocity = x->velocity;
    if (velocity > 0x7F)
        velocity = 0x7F;
    const int slot = A.slot_voice[v][1];
    uint32_t t = (uint32_t)A.voice_level[slot] * (uint32_t)(velocity | 0x80);
    t = (A.channel[x->channel].volume * t) >> 15;
    const int level = ((int)t ^ 0x3F) | A.voice_ksl[slot];
    send_output(0x40 + A.offset_slot[slot], level);
}

/* AL_SetVoicePitch: key (or the drum's note) + the bend's semitones - 12, kept to 0-95, gives
 * the block and the F-number (with the bend's 1/32 semitones); the key bit if it sounds. In
 * stereo the right chip's note is 5/32 of a semitone higher (into the next note past 31). */
static void set_voice_pitch(int v)
{
    voice_t *x = &A.voice[v];
    const channel_t *c = &A.channel[x->channel];
    int note = x->channel == DRUMS ? A.perc_map[x->key & 0x7F][1] : x->key;
    note += c->key_offset - 12;
    if (note > 0x5F)
        note = 0x5F;
    if (note < 0)
        note = 0;
    x->pitch = A.octave_pitch[note / 12] | A.note_pitch[c->key_detune & 31][note % 12];
    const int p = x->pitch | x->status;
    if (!A.stereo) {
        out(PORT_ADLIB, 0xA0 + v, p & 0xFF);
        out(PORT_ADLIB, 0xB0 + v, p >> 8);
        return;
    }
    out(PORT_LEFT, 0xA0 + v, p & 0xFF);
    out(PORT_LEFT, 0xB0 + v, p >> 8);
    int detune = (c->key_detune & 31) + 5;
    if (detune > 0x1F) {
        detune -= 0x20;
        if (note < 0x5F)
            note++;
    }
    x->pitch_right = A.octave_pitch[note / 12] | A.note_pitch[detune][note % 12];
    const int r = x->pitch_right | x->status;
    out(PORT_RIGHT, 0xA0 + v, r & 0xFF);
    out(PORT_RIGHT, 0xB0 + v, r >> 8);
}

/* AL_GetVoice: the channel's voice playing the key, or -1. */
static int get_voice(int ch, int key)
{
    for (int v = A.channel[ch].voices.first; v >= 0; v = A.voice[v].next)
        if (A.voice[v].key == key)
            return v;
    return -1;
}

/* AL_NoteOff: the key bit off; the voice back to the free list. */
static void note_off(int ch, int key)
{
    const int v = get_voice(ch, key);
    if (v < 0)
        return;
    voice_t *x = &A.voice[v];
    x->status = 0;
    if (A.stereo) {
        out(PORT_LEFT, 0xB0 + v, x->pitch >> 8);
        out(PORT_RIGHT, 0xB0 + v, x->pitch_right >> 8);
    } else {
        out(PORT_ADLIB, 0xB0 + v, x->pitch >> 8);
    }
    list_remove(&A.channel[ch].voices, v);
    list_add(&A.free_voices, v);
}

/* AL_AllocVoice: the first free voice, or -1. */
static int alloc_voice(void)
{
    const int v = A.free_voices.first;
    if (v >= 0)
        list_remove(&A.free_voices, v);
    return v;
}

/* AL_NoteOn: a free voice (else a drum's), the instrument, volume and pitch. */
static void note_on(int ch, int key, int velocity)
{
    if (velocity == 0) {
        note_off(ch, key);
        return;
    }
    int v = alloc_voice();
    if (v < 0 && A.channel[DRUMS].voices.first >= 0) {
        note_off(DRUMS, A.voice[A.channel[DRUMS].voices.first].key);
        v = alloc_voice();
    }
    if (v < 0)
        return;
    voice_t *x = &A.voice[v];
    x->key = key;
    x->channel = ch;
    x->velocity = velocity;
    x->status = KEY_ON;
    list_add(&A.channel[ch].voices, v);
    set_voice_timbre(v);
    set_voice_volume(v);
    set_voice_pitch(v);
}

/* AL_AllNotesOff (controller 123) */
static void all_notes_off(int ch)
{
    while (A.channel[ch].voices.first >= 0)
        note_off(ch, A.voice[A.channel[ch].voices.first].key);
}

/* AL_ResetVoices: every voice free (the chip isn't told), every channel to its start. */
static void reset_voices(void)
{
    A.free_voices.first = A.free_voices.last = -1;
    for (int v = 0; v < VOICES; v++) {
        voice_t *x = &A.voice[v];
        x->key = x->velocity = 0;
        x->channel = -1;
        x->timbre = -1;
        x->status = 0;
        list_add(&A.free_voices, v);
    }
    for (int c = 0; c < CHANNELS; c++) {
        channel_t *ch = &A.channel[c];
        memset(ch, 0, sizeof *ch);
        ch->voices.first = ch->voices.last = -1;
        ch->volume = 0x5A;
    }
}

/* AL_SetChannelVolume */
static void set_channel_volume(int ch, int volume)
{
    volume = volume < 0 ? 0 : volume > 0x7F ? 0x7F : volume;
    A.channel[ch].volume = volume;
    for (int v = A.channel[ch].voices.first; v >= 0; v = A.voice[v].next)
        set_voice_volume(v);
}

/* AL_ControlChange: 7 volume, 10 pan, 94 detune (both kept for the stereo chip), 121 reset,
 * 123 all notes off. */
static void control_change(int ch, int type, int value)
{
    switch (type) {
    case 7: set_channel_volume(ch, value); break;
    case 10: A.channel[ch].pan = value; break;
    case 0x5E: A.channel[ch].detune = value; break;
    case 0x7B: all_notes_off(ch); break;
    case 0x79:
        reset_voices();
        set_channel_volume(ch, 0x5A);
        A.channel[ch].pan = 0;
        A.channel[ch].detune = 0;
        break;
    default: break;
    }
}

/* AL_SetPitchBend: lsb + (msb << 8) (not << 7), times the range, in 1/32 semitones from 2
 * below. */
static void pitch_bend(int ch, int lsb, int msb)
{
    channel_t *c = &A.channel[ch];
    c->pitchbend = lsb + (msb << 8);
    const uint32_t t = (uint16_t)(c->pitchbend * A.bend_range) / 0x200u;
    c->key_offset = (int)(t >> 5) - A.bend_range;
    c->key_detune = (int)(t & 0x1F);
    for (int v = c->voices.first; v >= 0; v = A.voice[v].next)
        set_voice_pitch(v);
}

/* AL_FlushCard(port): every voice silent, its waveforms back to the sine. */
static void flush_card(int port)
{
    for (int v = 0; v < VOICES; v++) {
        out(port, 0xA0 + v, 0);
        out(port, 0xB0 + v, 0);
        out(port, 0xE0 + A.offset_slot[A.slot_voice[v][0]], 0);
        out(port, 0xE0 + A.offset_slot[A.slot_voice[v][1]], 0);
    }
}

/* AL_Init / AL_Reset: waveforms on, note select and rhythm off (through 388h), every voice
 * silent on each chip. */
void al_init(void)
{
    opl_reset();
    A.bend_range = 2;
    out(PORT_ADLIB, 0x01, 0x20);
    out(PORT_ADLIB, 0x08, 0);
    out(PORT_ADLIB, 0xBD, 0);
    if (A.stereo) {
        flush_card(PORT_LEFT);
        flush_card(PORT_RIGHT);
    } else {
        flush_card(PORT_ADLIB);
    }
    reset_voices();
    for (int c = 0; c < CHANNELS; c++)
        A.midi_volume[c] = 0x5A;
}

/* ------------------------------------------------------------------------------------------ */
/* MIDI.C                                                                                      */

/* MIDI_SetChannelVolume: the song's volume, scaled by the music volume (the AdLib driver has no
 * volume of its own). */
static void midi_channel_volume(int ch, int volume)
{
    A.midi_volume[ch] = volume;
    control_change(ch, 7, volume * A.total_volume / 255);
}

void midi_set_volume(int volume)
{
    A.total_volume = volume < 0 ? 0 : volume > 255 ? 255 : volume;
    for (int c = 0; c < CHANNELS; c++)
        midi_channel_volume(c, A.midi_volume[c]);
}

void midi_all_notes_off(void)
{
    if (!A.loaded)
        return;
    for (int c = 0; c < CHANNELS; c++)
        control_change(c, 0x7B, 0); /* (and 64 / 120, which the AdLib ignores) */
}

void midi_reset(void)
{
    if (!A.loaded)
        return;
    midi_all_notes_off();
    for (int c = 0; c < CHANNELS; c++) {
        control_change(c, 0x79, 0); /* (then RPN 0: a bend range of 2, which it ignores) */
        A.midi_volume[c] = 0x5A;
    }
    midi_set_volume(A.total_volume);
}

void midi_event(uint8_t status, uint8_t d1, uint8_t d2)
{
    if (!A.loaded)
        return;
    const int ch = status & 0x0F;
    switch (status & 0xF0) {
    case 0x80: note_off(ch, d1); break;
    case 0x90: note_on(ch, d1, d2); break;
    case 0xB0:
        if (d1 == 7)
            midi_channel_volume(ch, d2);
        else
            control_change(ch, d1, d2);
        break;
    case 0xC0: A.channel[ch].timbre = d1; break; /* AL_ProgramChange */
    case 0xE0: pitch_bend(ch, d1, d2); break;
    default: break; /* (aftertouch: the AdLib driver has none) */
    }
}
