/* A YM3812 (OPL2) emulator (see opl.h).
 *
 * How the chip makes a sample: each operator ("slot") has a phase counter that a channel's
 * frequency number (F-number << block) times the slot's multiplier advances every sample. The
 * phase picks an entry of a quarter-sine table stored as -log2 (logsin), the attenuation
 * (envelope + total level + key scaling + tremolo, in 0.1875 dB steps, 8 table units each) is
 * added in that log domain, and an exponent table turns the sum back into a linear 13-bit value.
 * In FM mode (connection 0) the first slot's output shifts the second slot's phase; in additive
 * mode (1) both are heard. The first slot can feed its own output back into its phase. */
#include "opl.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

enum { EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE };
enum { EG_MAX = 511 };

typedef struct {
    /* registers */
    bool am, vib, egt, ksr;      /* 20h: tremolo, vibrato, sustained tone, key scale rate */
    int mult;                    /*      multiplier */
    int ksl, tl;                 /* 40h: key scale level, total level (0.75 dB steps) */
    int ar, dr, sl, rr;          /* 60h / 80h: attack, decay, sustain level, release */
    int ws;                      /* E0h: waveform */
    /* state */
    uint32_t phase;              /* 19 bits a cycle */
    int env;                     /* attenuation, 0 (loudest) - 511 */
    int state;
    int out, prev;               /* the last two outputs (feedback) */
} slot_t;

typedef struct {
    int fnum, block;
    bool key;
    int fb, cnt;                 /* C0h: feedback, connection */
    slot_t *op[2];
} chan_t;

typedef struct {
    slot_t slot[18];
    chan_t chan[9];
    bool wse;                    /* 01h bit 5: the waveforms enabled */
    bool nts;                    /* 08h bit 6: note select (key scale rate's bit) */
    bool dam, dvb;               /* BDh: deep tremolo, deep vibrato */
    uint32_t eg_cnt;             /* the envelopes' clock: one a sample */
    int trem_pos, trem_step;     /* tremolo: a triangle of 210 steps, one each 64 samples */
    int vib_pos, vib_step;       /* vibrato: 8 steps, one each 1024 samples */
} chip_t;

/* the chips (OPL_CHIPS: an SB Pro has two OPL2s, one for each side) and the chip at work */
static chip_t chips[OPL_CHIPS];
static chip_t *O = &chips[0];

/* the ROM tables, shared */
static struct {
    uint16_t logsin[256];
    uint16_t exprom[256];
    bool made;
} T;

/* slot number for each register offset (-1: none) */
static const int8_t slot_of[0x20] = {0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1,
                                     12, 13, 14, 15, 16, 17, -1, -1, -1, -1, -1, -1, -1, -1,
                                     -1, -1};
/* the multiplier, doubled */
static const uint8_t mult2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
/* key scale level: attenuation by F-number's top 4 bits (at block 8) */
static const uint8_t ksl_rom[16] = {0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64};
static const uint8_t ksl_shift[4] = {8, 1, 2, 0}; /* off, 3, 1.5, 6 dB an octave */

/* The envelope steps: for each rate group, 8 cycles of increments. */
static const uint8_t eg_inc[13][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 2, 1, 1, 1, 2},
    {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2}, {2, 2, 2, 2, 2, 2, 2, 2},
    {2, 2, 2, 4, 2, 2, 2, 4}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
    {4, 4, 4, 4, 4, 4, 4, 4},
};

static void make_tables(void)
{
    for (int i = 0; i < 256; i++) {
        const double s = sin((i + 0.5) * 3.14159265358979323846 / 512.0);
        T.logsin[i] = (uint16_t)lround(-log2(s) * 256.0);
        T.exprom[i] = (uint16_t)lround((pow(2.0, i / 256.0) - 1.0) * 1024.0);
    }
    T.made = true;
}

static void reset_chip(void)
{
    memset(O->slot, 0, sizeof O->slot);
    memset(O->chan, 0, sizeof O->chan);
    for (int s = 0; s < 18; s++) {
        O->slot[s].env = EG_MAX;
        O->slot[s].state = EG_RELEASE;
    }
    /* channel c: slots c % 3 + (c / 3) * 6 and 3 more */
    for (int c = 0; c < 9; c++) {
        const int s = c % 3 + c / 3 * 6;
        O->chan[c].op[0] = &O->slot[s];
        O->chan[c].op[1] = &O->slot[s + 3];
    }
    O->wse = O->nts = O->dam = O->dvb = false;
    O->eg_cnt = 0;
    O->trem_pos = O->trem_step = O->vib_pos = O->vib_step = 0;
}

void opl_reset(void)
{
    if (!T.made)
        make_tables();
    for (int i = 0; i < OPL_CHIPS; i++) {
        O = &chips[i];
        reset_chip();
    }
}

static void key_on(chan_t *c)
{
    for (int k = 0; k < 2; k++) {
        c->op[k]->state = EG_ATTACK;
        c->op[k]->phase = 0;
    }
}

static void key_off(chan_t *c)
{
    for (int k = 0; k < 2; k++)
        c->op[k]->state = EG_RELEASE;
}

static void write_chip(int reg, int v)
{
    reg &= 0xFF;
    v &= 0xFF;
    if (reg == 0x01) {
        O->wse = (v & 0x20) != 0;
        return;
    }
    if (reg == 0x08) {
        O->nts = (v & 0x40) != 0;
        return;
    }
    if (reg == 0xBD) {
        O->dam = (v & 0x80) != 0;
        O->dvb = (v & 0x40) != 0;
        return; /* (rhythm mode isn't emulated) */
    }
    const int group = reg & 0xE0;
    if (group == 0x20 || group == 0x40 || group == 0x60 || group == 0x80 || group == 0xE0) {
        const int n = slot_of[reg & 0x1F];
        if (n < 0)
            return;
        slot_t *s = &O->slot[n];
        switch (group) {
        case 0x20:
            s->am = v & 0x80;
            s->vib = v & 0x40;
            s->egt = v & 0x20;
            s->ksr = v & 0x10;
            s->mult = v & 0x0F;
            break;
        case 0x40:
            s->ksl = v >> 6;
            s->tl = v & 0x3F;
            break;
        case 0x60:
            s->ar = v >> 4;
            s->dr = v & 0x0F;
            break;
        case 0x80:
            s->sl = v >> 4;
            s->rr = v & 0x0F;
            break;
        default:
            s->ws = v & 3;
            break;
        }
        return;
    }
    const int c = reg & 0x0F;
    if (c > 8)
        return;
    chan_t *ch = &O->chan[c];
    if (group == 0xA0 && (reg & 0xF0) == 0xA0) {
        ch->fnum = (ch->fnum & 0x300) | v;
    } else if ((reg & 0xF0) == 0xB0) {
        ch->fnum = (ch->fnum & 0xFF) | (v & 3) << 8;
        ch->block = v >> 2 & 7;
        const bool key = v & 0x20;
        if (key && !ch->key)
            key_on(ch);
        else if (!key && ch->key)
            key_off(ch);
        ch->key = key;
    } else if ((reg & 0xF0) == 0xC0) {
        ch->fb = v >> 1 & 7;
        ch->cnt = v & 1;
    }
}

/* One envelope step for a rate (0-15) scaled by the key (rks). */
static int eg_step(int rate, int rks, bool *instant)
{
    *instant = false;
    if (rate == 0)
        return 0;
    int eff = rate * 4 + rks;
    if (eff > 63)
        eff = 63;
    if (eff >= 60)
        *instant = true;
    const int shift = eff < 48 ? 12 - (eff >> 2) : 0;
    const int sel = eff < 48 ? (eff & 3) : eff < 52 ? 4 + (eff & 3) : eff < 56 ? 8 + (eff & 3) : 12;
    if (O->eg_cnt & ((1u << shift) - 1))
        return 0;
    return eg_inc[sel][(O->eg_cnt >> shift) & 7];
}

static void envelope(slot_t *s, const chan_t *c)
{
    int rks = c->block << 1 | (c->fnum >> (O->nts ? 8 : 9) & 1);
    if (!s->ksr)
        rks >>= 2;
    bool instant;
    switch (s->state) {
    case EG_ATTACK: {
        const int inc = eg_step(s->ar, rks, &instant);
        if (instant && s->ar)
            s->env = 0;
        else if (inc)
            s->env += (~s->env * inc) >> 3;
        if (s->env <= 0) {
            s->env = 0;
            s->state = EG_DECAY;
        }
        break;
    }
    case EG_DECAY: {
        const int sl = s->sl == 15 ? 31 << 4 : s->sl << 4;
        s->env += eg_step(s->dr, rks, &instant);
        if (s->env >= sl) {
            s->env = sl;
            s->state = EG_SUSTAIN;
        }
        break;
    }
    default:
        /* a sustained tone (EGT) is held while the key is down; a percussive one, and every
         * released one, fade at the release rate */
        if (s->state == EG_SUSTAIN && s->egt)
            break;
        s->env += eg_step(s->rr, rks, &instant);
        if (s->env > EG_MAX)
            s->env = EG_MAX;
        break;
    }
}

/* A slot's output for phase index p (10 bits) and attenuation att. */
static int wave(const slot_t *s, int p, int att)
{
    p &= 0x3FF;
    int idx;
    bool neg = false;
    switch (O->wse ? s->ws : 0) {
    case 0:
        idx = p & 0x100 ? ~p & 0xFF : p & 0xFF;
        neg = p & 0x200;
        break;
    case 1:
        if (p & 0x200)
            return 0;
        idx = p & 0x100 ? ~p & 0xFF : p & 0xFF;
        break;
    case 2:
        idx = p & 0x100 ? ~p & 0xFF : p & 0xFF;
        break;
    default:
        if (p & 0x100)
            return 0;
        idx = p & 0xFF;
        break;
    }
    const int level = T.logsin[idx] + (att << 3);
    if (level >= 0x1FFF)
        return 0;
    const int v = ((T.exprom[(level & 0xFF) ^ 0xFF] | 0x400) << 1) >> (level >> 8);
    return neg ? -v : v;
}

/* The slot's total attenuation now. */
static int attenuation(const slot_t *s, const chan_t *c)
{
    int ksl = (ksl_rom[c->fnum >> 6] << 2) - ((8 - c->block) << 5);
    if (ksl < 0)
        ksl = 0;
    int att = s->env + (s->tl << 2) + (ksl >> ksl_shift[s->ksl]);
    if (s->am) {
        const int t = O->trem_pos < 105 ? O->trem_pos : 209 - O->trem_pos;
        att += t >> (O->dam ? 2 : 4);
    }
    return att > EG_MAX ? EG_MAX : att;
}

static void advance_phase(slot_t *s, const chan_t *c)
{
    int fnum = c->fnum;
    if (s->vib) {
        /* vibrato: a fraction of the F-number's top bits, up and down in 8 steps */
        int range = fnum >> 7 & 7;
        if (!(O->vib_pos & 3))
            range = 0;
        else if (O->vib_pos & 1)
            range >>= 1;
        if (!O->dvb)
            range >>= 1;
        fnum += O->vib_pos & 4 ? -range : range;
    }
    const uint32_t inc = (uint32_t)(((fnum << c->block) >> 1) * mult2[s->mult]) >> 1;
    s->phase = (s->phase + inc) & 0x7FFFF;
}

void opl_write(int chip, int reg, int v)
{
    O = &chips[chip < 0 || chip >= OPL_CHIPS ? 0 : chip];
    write_chip(reg, v);
}

static int chip_sample(void)
{
    int mix = 0;
    for (int n = 0; n < 9; n++) {
        chan_t *c = &O->chan[n];
        slot_t *m = c->op[0], *k = c->op[1];
        envelope(m, c);
        envelope(k, c);
        /* the modulator, fed back on itself */
        const int fb = c->fb ? (m->out + m->prev) >> (9 - c->fb) : 0;
        m->prev = m->out;
        m->out = wave(m, (int)(m->phase >> 9) + fb, attenuation(m, c));
        /* the carrier, its phase moved by the modulator in FM mode */
        const int mod = c->cnt ? 0 : m->out;
        k->prev = k->out;
        k->out = wave(k, (int)(k->phase >> 9) + mod, attenuation(k, c));
        mix += c->cnt ? m->out + k->out : k->out;
        advance_phase(m, c);
        advance_phase(k, c);
    }
    O->eg_cnt++;
    if (++O->trem_step == 64) {
        O->trem_step = 0;
        if (++O->trem_pos == 210)
            O->trem_pos = 0;
    }
    if (++O->vib_step == 1024) {
        O->vib_step = 0;
        O->vib_pos = (O->vib_pos + 1) & 7;
    }
    return mix;
}

void opl_sample(int out[OPL_CHIPS])
{
    for (int i = 0; i < OPL_CHIPS; i++) {
        O = &chips[i];
        out[i] = chip_sample();
    }
}
