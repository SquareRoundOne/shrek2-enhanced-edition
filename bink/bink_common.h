/* Shared Bink 1 ('BIKi') definitions for the encoder and the parse-only probe. */
#ifndef BINK_COMMON_H
#define BINK_COMMON_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "binkdata.h"

enum { SRC_BLOCK_TYPES = 0, SRC_SUB_BLOCK_TYPES, SRC_COLORS, SRC_PATTERN, SRC_X_OFF, SRC_Y_OFF,
       SRC_INTRA_DC, SRC_INTER_DC, SRC_RUN, NB_SRC };
enum { SKIP_BLOCK = 0, SCALED_BLOCK, MOTION_BLOCK, RUN_BLOCK, RESIDUE_BLOCK, INTRA_BLOCK,
       FILL_BLOCK, INTER_BLOCK, PATTERN_BLOCK, RAW_BLOCK };

static int ilog2(unsigned v) { int r = -1; while (v) { r++; v >>= 1; } return r; }

/* bundle length fields, as init_lengths() */
static void bundle_lens(int width, int bw, int len[NB_SRC])
{
    width = (width + 7) & ~7;
    len[SRC_BLOCK_TYPES] = ilog2((width >> 3) + 511) + 1;
    len[SRC_SUB_BLOCK_TYPES] = ilog2((width >> 4) + 511) + 1;
    len[SRC_COLORS] = ilog2(bw * 64 + 511) + 1;
    len[SRC_INTRA_DC] = len[SRC_INTER_DC] = len[SRC_X_OFF] = len[SRC_Y_OFF] = ilog2((width >> 3) + 511) + 1;
    len[SRC_PATTERN] = ilog2((bw << 3) + 511) + 1;
    len[SRC_RUN] = ilog2(bw * 48 + 511) + 1;
}

/* ---------------- exact IDCT (binkdsp.c) */
#define A1  2896
#define A2  2217
#define A3  3784
#define A4 -5352
#define MUL(X,Y) ((int)((unsigned)(X) * (Y)) >> 11)
#define IDCT_TRANSFORM(dest,s0,s1,s2,s3,s4,s5,s6,s7,d0,d1,d2,d3,d4,d5,d6,d7,munge,src) {\
    const int a0 = (src)[s0] + (src)[s4]; \
    const int a1 = (src)[s0] - (src)[s4]; \
    const int a2 = (src)[s2] + (src)[s6]; \
    const int a3 = MUL(A1, (src)[s2] - (src)[s6]); \
    const int a4 = (src)[s5] + (src)[s3]; \
    const int a5 = (src)[s5] - (src)[s3]; \
    const int a6 = (src)[s1] + (src)[s7]; \
    const int a7 = (src)[s1] - (src)[s7]; \
    const int b0 = a4 + a6; \
    const int b1 = MUL(A3, a5 + a7); \
    const int b2 = MUL(A4, a5) - b0 + b1; \
    const int b3 = MUL(A1, a6 - a4) - b2; \
    const int b4 = MUL(A2, a7) + b3 - b1; \
    (dest)[d0] = munge(a0+a2   +b0); \
    (dest)[d1] = munge(a1+a3-a2+b2); \
    (dest)[d2] = munge(a1-a3+a2+b3); \
    (dest)[d3] = munge(a0-a2   -b4); \
    (dest)[d4] = munge(a0-a2   +b4); \
    (dest)[d5] = munge(a1-a3+a2-b3); \
    (dest)[d6] = munge(a1+a3-a2-b2); \
    (dest)[d7] = munge(a0+a2   -b0); \
}
#define MUNGE_NONE(x) (x)
#define MUNGE_ROW(x) (((x) + 0x7F)>>8)
static void idct_col(int *dest, const int32_t *src)
{
    if ((src[8]|src[16]|src[24]|src[32]|src[40]|src[48]|src[56]) == 0) {
        dest[0] = dest[8] = dest[16] = dest[24] = dest[32] = dest[40] = dest[48] = dest[56] = src[0];
    } else {
        IDCT_TRANSFORM(dest,0,8,16,24,32,40,48,56,0,8,16,24,32,40,48,56,MUNGE_NONE,src);
    }
}
/* out[64] = untruncated pixel values (put) or residual (add) */
static void idct_int(const int32_t *block, int *out)
{
    int temp[64];
    for (int i = 0; i < 8; i++) idct_col(&temp[i], &block[i]);
    for (int i = 0; i < 8; i++) IDCT_TRANSFORM((&out[8*i]),0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7,MUNGE_ROW,(&temp[8*i]));
}

/* ---------------- bit writer (little endian, LSB first) */
typedef struct { uint8_t *buf; size_t cap; uint64_t nbits; } BW;
static void bw_init(BW *w) { w->cap = 1 << 16; w->buf = calloc(w->cap, 1); w->nbits = 0; }
static void bw_reset(BW *w) { memset(w->buf, 0, (w->nbits + 7) >> 3); w->nbits = 0; }
static void bw_free(BW *w) { free(w->buf); w->buf = NULL; }
static void bw_put(BW *w, uint32_t v, int n)
{
    if (!n) return;
    if (((w->nbits + n) >> 3) + 8 >= w->cap) {
        size_t nc = w->cap * 2; w->buf = realloc(w->buf, nc); memset(w->buf + w->cap, 0, nc - w->cap); w->cap = nc;
    }
    v &= (n == 32) ? 0xFFFFFFFFu : ((1u << n) - 1);
    uint64_t p = w->nbits;
    uint64_t x = (uint64_t)v << (p & 7);
    uint8_t *b = w->buf + (p >> 3);
    for (int i = 0; x; i++, x >>= 8) b[i] |= (uint8_t)x;
    w->nbits += n;
}
static void bw_append(BW *w, const BW *s)
{
    uint64_t n = s->nbits, i = 0;
    for (; i + 32 <= n; i += 32) {
        uint32_t v; memcpy(&v, s->buf + (i >> 3), 4); bw_put(w, v, 32);
    }
    for (; i < n; i++) bw_put(w, (s->buf[i >> 3] >> (i & 7)) & 1, 1);
}
static void bw_align32(BW *w) { if (w->nbits & 31) bw_put(w, 0, 32 - (w->nbits & 31)); }

/* ---------------- bit reader */
typedef struct { const uint8_t *buf; uint64_t pos, size; } BR;
static uint32_t br_get(BR *r, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++, r->pos++) {
        if ((r->pos >> 3) < r->size) v |= (uint32_t)((r->buf[r->pos >> 3] >> (r->pos & 7)) & 1) << i;
    }
    return v;
}
#endif
