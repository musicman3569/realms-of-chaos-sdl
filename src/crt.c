/* The CRT look (see crt.h). */
#include "crt.h"

#include <math.h>
#include <string.h>

/* The shader's texture reads, taken at quarter-pixel offsets from each output pixel's centre
 * with nearest filtering: an output pixel o's centre is at (2o + 1) / 4 of a source pixel, so
 * an offset of q quarters reads source pixel floor((2o + 1 + q) / 4), held inside the picture. */
enum { Q_M3, Q_M1, Q_0, Q_P1, Q_P3, QS };

static void offsets(int (*tab)[QS], int n, int size)
{
    static const int q[QS] = {-3, -1, 0, 1, 3};
    for (int o = 0; o < n; o++)
        for (int k = 0; k < QS; k++) {
            const int v = 2 * o + 1 + q[k];
            const int s = v < 0 ? 0 : v / 4;
            tab[o][k] = s >= size ? size - 1 : s;
        }
}

static uint32_t pack(float r, float g, float b)
{
    const float c[3] = {r, g, b};
    uint32_t v = 0xFF000000u;
    for (int k = 0; k < 3; k++) {
        const float x = c[k] < 0 ? 0 : c[k] > 1 ? 1 : c[k];
        v |= (uint32_t)(x * 255.0f + 0.5f) << (16 - 8 * k);
    }
    return v;
}

/* The 13 texture reads, by their names in the shader. */
enum { C11, S00, S20, S22, S02, H00, H20, H22, H02, V00, V20, V22, V02, TAPS };
static const int tap_q[TAPS][2] = {
    {Q_0, Q_0},
    {Q_M1, Q_M1}, {Q_P1, Q_M1}, {Q_P1, Q_P1}, {Q_M1, Q_P1},
    {Q_M3, Q_M1}, {Q_P3, Q_M1}, {Q_P3, Q_P1}, {Q_M3, Q_P1},
    {Q_M1, Q_M3}, {Q_P1, Q_M3}, {Q_P1, Q_P3}, {Q_M1, Q_P3},
};

/* 1 / (the colours' distance): the weight of a pair, larger the more alike. */
#define W(a, b) (1.0f / (fabsf(t[a][0][x] - t[b][0][x]) + fabsf(t[a][1][x] - t[b][1][x]) + \
                         fabsf(t[a][2][x] - t[b][2][x]) + 0.00001f))
#define PAIR(a, b) (t[a][c][x] + t[b][c][x])

/* 2xsal-level2.slang's fragment shader, per output pixel: three averages across the diagonals
 * (the near ones, and ones stretched horizontally and vertically), each weighted towards the
 * pairs that are alike, then blended by how close each is to the centre pixel. The reads are
 * gathered first, so the maths runs down plain arrays and the compiler can do several pixels at
 * once: each source row a tap reads is spread out to the output's width at its five horizontal
 * offsets once (the rows an output row reads span at most three source rows; four are kept). */
void crt_2xsal(const uint32_t *in, uint32_t *out)
{
    static float spread[4][QS][3][CRT_W];
    static int spread_row[4];
    static int xs[CRT_W][QS], ys[CRT_H][QS];
    static int ready;
    if (!ready) {
        offsets(xs, CRT_W, SCREEN_W);
        offsets(ys, CRT_H, SCREEN_H);
        ready = 1;
    }
    for (int i = 0; i < 4; i++)
        spread_row[i] = -1;
    for (int oy = 0; oy < CRT_H; oy++) {
        static float t[TAPS][3][CRT_W];
        for (int k = 0; k < TAPS; k++) {
            const int y = ys[oy][tap_q[k][1]], slot = y & 3;
            if (spread_row[slot] != y) {
                for (int q = 0; q < QS; q++)
                    for (int x = 0; x < CRT_W; x++) {
                        const uint32_t p = in[y * SCREEN_W + xs[x][q]];
                        for (int c = 0; c < 3; c++)
                            spread[slot][q][c][x] = (float)(p >> (16 - 8 * c) & 0xFF) / 255.0f;
                    }
                spread_row[slot] = y;
            }
            memcpy(t[k], spread[slot][tap_q[k][0]], sizeof t[k]);
        }
        float res[3][CRT_W];
        for (int x = 0; x < CRT_W; x++) {
            const float m1 = W(S00, S22), m2 = W(S02, S20);
            const float h1 = W(S00, H22), h2 = W(S02, H20), h3 = W(H00, S22), h4 = W(H02, S20);
            const float v1 = W(S00, V22), v2 = W(S02, V20), v3 = W(V00, S22), v4 = W(V02, S20);
            const float m = 0.5f / (m1 + m2), h = 0.5f / (h1 + h2 + h3 + h4);
            const float v = 0.5f / (v1 + v2 + v3 + v4);
            float t1[3], t2[3], t3[3];
            for (int c = 0; c < 3; c++) {
                t1[c] = (m1 * PAIR(S00, S22) + m2 * PAIR(S02, S20)) * m;
                t2[c] = (h1 * PAIR(S00, H22) + h2 * PAIR(S02, H20) + h3 * PAIR(H00, S22) +
                         h4 * PAIR(H02, S20)) * h;
                t3[c] = (v1 * PAIR(S00, V22) + v2 * PAIR(S02, V20) + v3 * PAIR(V00, S22) +
                         v4 * PAIR(V02, S20)) * v;
            }
            float d1 = 0.00001f, d2 = 0.00001f, d3 = 0.00001f;
            for (int c = 0; c < 3; c++) {
                d1 += fabsf(t1[c] - t[C11][c][x]);
                d2 += fabsf(t2[c] - t[C11][c][x]);
                d3 += fabsf(t3[c] - t[C11][c][x]);
            }
            const float k1 = 1.0f / d1, k2 = 1.0f / d2, k3 = 1.0f / d3;
            const float k = 1.0f / (k1 + k2 + k3);
            for (int c = 0; c < 3; c++)
                res[c][x] = (k1 * t1[c] + k2 * t2[c] + k3 * t3[c]) * k;
        }
        for (int x = 0; x < CRT_W; x++)
            out[oy * CRT_W + x] = pack(res[0][x], res[1][x], res[2][x]);
    }
}
#undef W
#undef PAIR

/* dotmask.slang's Mask() for style 3 at pos = floor(texcoord * size + 0.5), which for pixel i's
 * centre is i + 1: the channels in pairs of columns, shifted three columns a row. */
int crt_mask_channel(int x, int y)
{
    return ((x + 1) + 3 * (y + 1)) % 6 / 2;
}

void crt_render(const uint32_t *in, uint32_t *out)
{
    crt_2xsal(in, out);
    for (int y = 0; y < CRT_H; y++)
        for (int x = 0; x < CRT_W; x++) {
            uint32_t *p = &out[y * CRT_W + x];
            const int lit = crt_mask_channel(x, y);
            float c[3];
            for (int k = 0; k < 3; k++)
                c[k] = (float)(*p >> (16 - 8 * k) & 0xFF) / 255.0f *
                       (k == lit ? CRT_MASK_LIGHT : CRT_MASK_DARK);
            *p = pack(c[0], c[1], c[2]);
        }
}
