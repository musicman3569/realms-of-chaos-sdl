#include "archive.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------ */
/* -lh5- decoder: LZSS with an 8 KB window, literals/lengths and distances Huffman coded in    */
/* blocks (ar002's decode() / huf.c)                                                          */

enum {
    DICBIT = 13,
    NC = 256 + 256 - 3 + 2, /* literals, then match lengths 3..256 */
    NP = DICBIT + 1,        /* distance codes */
    NT = 19,                /* code-length codes */
    TBIT = 5, CBIT = 9, PBIT = 4,
    THRESHOLD = 3,
    NO_SYMBOL = 0xFFFF,
};

typedef struct {
    const uint8_t *in;
    size_t len, pos;
    uint32_t buf; /* unread bits, most significant first */
    int count;    /* how many */
} bits_t;

static unsigned peek(bits_t *b, int n)
{
    while (b->count < n) {
        b->buf = b->buf << 8 | (b->pos < b->len ? b->in[b->pos] : 0);
        b->pos++;
        b->count += 8;
    }
    return (b->buf >> (b->count - n)) & ((1u << n) - 1);
}

static void skip(bits_t *b, int n)
{
    peek(b, n);
    b->count -= n;
    b->buf &= (1u << b->count) - 1;
}

static unsigned get(bits_t *b, int n)
{
    const unsigned v = peek(b, n);
    skip(b, n);
    return v;
}

/* Canonical Huffman code with a 16-bit lookup table (codes are at most 16 bits). */
typedef struct {
    int single;             /* >= 0: every code is this symbol, using no bits */
    uint16_t sym[1 << 16];
    uint8_t len[1 << 16];
} huff_t;

static bool huff_build(huff_t *h, const uint8_t *lengths, int n)
{
    h->single = -1;
    memset(h->sym, 0xFF, sizeof h->sym);
    unsigned code = 0;
    for (int l = 1; l <= 16; l++) {
        for (int s = 0; s < n; s++) {
            if (lengths[s] != l)
                continue;
            const unsigned start = code << (16 - l), count = 1u << (16 - l);
            if (start + count > 1u << 16)
                return false;
            for (unsigned i = 0; i < count; i++) {
                h->sym[start + i] = (uint16_t)s;
                h->len[start + i] = (uint8_t)l;
            }
            code++;
        }
        code <<= 1;
    }
    return true;
}

static int huff_decode(const huff_t *h, bits_t *b)
{
    if (h->single >= 0)
        return h->single;
    const unsigned i = peek(b, 16);
    if (h->sym[i] == NO_SYMBOL)
        return -1;
    skip(b, h->len[i]);
    return h->sym[i];
}

/* read_pt_len: the code-length code (nn = NT) or the distance code (nn = NP). */
static bool read_pt_len(huff_t *h, bits_t *b, int nn, int nbit, int special)
{
    const int n = (int)get(b, nbit);
    if (n == 0) {
        h->single = (int)get(b, nbit);
        return true;
    }
    if (n > nn)
        return false;
    uint8_t lengths[NT > NP ? NT : NP] = {0};
    int i = 0;
    while (i < n) {
        int c = (int)get(b, 3);
        if (c == 7)
            while (get(b, 1) && c < 16)
                c++;
        lengths[i++] = (uint8_t)c;
        if (i == special) {
            for (int k = (int)get(b, 2); k > 0 && i < nn; k--)
                lengths[i++] = 0;
        }
    }
    return huff_build(h, lengths, nn);
}

static bool read_c_len(huff_t *h, const huff_t *pt, bits_t *b)
{
    const int n = (int)get(b, CBIT);
    if (n == 0) {
        h->single = (int)get(b, CBIT);
        return true;
    }
    if (n > NC)
        return false;
    uint8_t lengths[NC] = {0};
    int i = 0;
    while (i < n) {
        const int c = huff_decode(pt, b);
        if (c < 0)
            return false;
        if (c <= 2) {
            const int run = c == 0 ? 1 : c == 1 ? (int)get(b, 4) + 3 : (int)get(b, CBIT) + 20;
            i += run;
        } else {
            lengths[i++] = (uint8_t)(c - 2);
        }
    }
    return huff_build(h, lengths, NC);
}

static bool lh5_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t size)
{
    huff_t *t = malloc(3 * sizeof *t);
    if (!t)
        return false;
    huff_t *pt = &t[0], *c_tab = &t[1], *p_tab = &t[2];
    bits_t b = {in, in_len, 0, 0, 0};
    size_t pos = 0;
    unsigned block = 0;
    bool ok = true;
    while (ok && pos < size) {
        if (block == 0) {
            block = get(&b, 16);
            ok = read_pt_len(pt, &b, NT, TBIT, 3) && read_c_len(c_tab, pt, &b) &&
                 read_pt_len(p_tab, &b, NP, PBIT, -1);
            if (!ok)
                break;
        }
        block--;
        const int c = huff_decode(c_tab, &b);
        if (c < 0) {
            ok = false;
        } else if (c < 256) {
            out[pos++] = (uint8_t)c;
        } else {
            size_t length = (size_t)(c - 256 + THRESHOLD);
            int p = huff_decode(p_tab, &b);
            if (p < 0) {
                ok = false;
                break;
            }
            if (p > 1)
                p = (1 << (p - 1)) + (int)get(&b, p - 1);
            if ((size_t)p + 1 > pos) {
                ok = false;
                break;
            }
            const size_t src = pos - (size_t)p - 1;
            for (size_t k = 0; k < length && pos < size; k++)
                out[pos++] = out[src + k];
        }
    }
    free(t);
    return ok;
}

static uint16_t crc16(const uint8_t *p, size_t n)
{
    static uint16_t table[256];
    if (!table[1])
        for (unsigned i = 0; i < 256; i++) {
            unsigned c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? (c >> 1) ^ 0xA001 : c >> 1;
            table[i] = (uint16_t)c;
        }
    uint16_t crc = 0;
    while (n--)
        crc = (uint16_t)((crc >> 8) ^ table[(crc ^ *p++) & 0xFF]);
    return crc;
}

/* ------------------------------------------------------------------------------------------ */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }

bool arc_open(archive_t *a, const char *path, char *err, size_t errlen)
{
    memset(a, 0, sizeof *a);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errlen, "can't open %s", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    a->data = n > 0 ? malloc((size_t)n) : NULL;
    if (!a->data || fread(a->data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        snprintf(err, errlen, "can't read %s", path);
        arc_close(a);
        return false;
    }
    fclose(f);
    a->len = (size_t)n;

    int cap = 0;
    size_t p = 0;
    while (p + 24 < a->len && a->data[p]) {
        const uint8_t *h = a->data + p;
        const size_t hsize = h[0], nlen = h[21];
        if (memcmp(h + 2, "-lh", 3) != 0 || (h[5] != '5' && h[5] != '0') || h[6] != '-' ||
            h[20] != 1 || p + hsize + 2 > a->len || nlen >= sizeof a->entries[0].name) {
            snprintf(err, errlen, "%s: not an LHA archive I understand (at byte %zu)", path, p);
            arc_close(a);
            return false;
        }
        uint32_t packed = rd32(h + 7);
        size_t q = p + hsize + 2;
        /* Level 1: extended headers follow, and count towards the packed size. */
        for (unsigned ext = rd16(a->data + q - 2); ext; ) {
            if (q + ext > a->len || ext > packed) {
                snprintf(err, errlen, "%s: bad extended header at byte %zu", path, q);
                arc_close(a);
                return false;
            }
            packed -= ext;
            const unsigned next = rd16(a->data + q + ext - 2);
            q += ext;
            ext = next;
        }
        if (q + packed > a->len) {
            snprintf(err, errlen, "%s: truncated", path);
            arc_close(a);
            return false;
        }
        if (a->count == cap) {
            cap = cap ? cap * 2 : 512;
            arc_entry_t *e = realloc(a->entries, (size_t)cap * sizeof *e);
            if (!e) {
                snprintf(err, errlen, "out of memory");
                arc_close(a);
                return false;
            }
            a->entries = e;
        }
        arc_entry_t *e = &a->entries[a->count++];
        memcpy(e->name, h + 22, nlen);
        e->name[nlen] = 0;
        e->stored = h[5] == '0';
        e->offset = (uint32_t)q;
        e->packed = packed;
        e->size = rd32(h + 11);
        e->crc = rd16(h + 22 + nlen);
        p = q + packed;
    }
    return true;
}

void arc_close(archive_t *a)
{
    free(a->data);
    free(a->entries);
    memset(a, 0, sizeof *a);
}

const arc_entry_t *arc_find(const archive_t *a, const char *name)
{
    for (int i = 0; i < a->count; i++) {
        const char *x = a->entries[i].name, *y = name;
        while (*x && tolower((unsigned char)*x) == tolower((unsigned char)*y))
            x++, y++;
        if (!*x && !*y)
            return &a->entries[i];
    }
    return NULL;
}

uint8_t *arc_load(const archive_t *a, const char *name, size_t *len)
{
    const arc_entry_t *e = arc_find(a, name);
    if (!e)
        return NULL;
    uint8_t *out = malloc(e->size ? e->size : 1);
    if (!out)
        return NULL;
    const uint8_t *in = a->data + e->offset;
    bool ok;
    if (e->stored) {
        ok = e->packed == e->size;
        if (ok)
            memcpy(out, in, e->size);
    } else {
        ok = lh5_decode(in, e->packed, out, e->size);
    }
    if (!ok || crc16(out, e->size) != e->crc) {
        free(out);
        return NULL;
    }
    if (len)
        *len = e->size;
    return out;
}
