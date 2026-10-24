/* Bink 1 ('BIKi', the revision understood by binkw32 1.5) video encoder.
 *
 * usage: binkenc in.yuv W H audio_src.bik out.bik recon.yuv [mse_luma mse_chroma]
 *   in.yuv     raw yuv420p frames (BT.601, limited range), frame count must match audio_src
 *   audio_src  original .bik: its audio packets, frame rate and audio track headers are kept as is
 *   recon.yuv  encoder-side reconstruction (to verify against an independent decoder)
 *
 * Block types used: SKIP, FILL, INTRA (DCT), INTER (DCT residual, zero motion), RAW (fallback).
 * The decoder model (bundles, Huffman trees, coefficient coding, integer IDCT) follows
 * libavcodec/bink.c, which was verified bit-exact against the game's own files by probe.c.
 */
#include "bink_common.h"
#include <math.h>

/* ---------------------------------------------------------------- transforms */
static double Tf[8][8], Ti[8][8];   /* 1-D float model of the IDCT and its inverse */

static void idct1d_f(const double *s, double *d)
{
    double a0 = s[0] + s[4], a1 = s[0] - s[4], a2 = s[2] + s[6], a3 = A1 / 2048.0 * (s[2] - s[6]);
    double a4 = s[5] + s[3], a5 = s[5] - s[3], a6 = s[1] + s[7], a7 = s[1] - s[7];
    double b0 = a4 + a6, b1 = A3 / 2048.0 * (a5 + a7), b2 = A4 / 2048.0 * a5 - b0 + b1;
    double b3 = A1 / 2048.0 * (a6 - a4) - b2, b4 = A2 / 2048.0 * a7 + b3 - b1;
    d[0] = a0 + a2 + b0; d[1] = a1 + a3 - a2 + b2; d[2] = a1 - a3 + a2 + b3; d[3] = a0 - a2 - b4;
    d[4] = a0 - a2 + b4; d[5] = a1 - a3 + a2 - b3; d[6] = a1 + a3 - a2 - b2; d[7] = a0 + a2 - b0;
}
static void init_transform(void)
{
    for (int k = 0; k < 8; k++) {
        double e[8] = {0}, o[8]; e[k] = 1; idct1d_f(e, o);
        for (int n = 0; n < 8; n++) Tf[n][k] = o[n];
    }
    /* invert Tf (Gauss-Jordan) */
    double a[8][16];
    for (int i = 0; i < 8; i++) for (int j = 0; j < 16; j++) a[i][j] = j < 8 ? Tf[i][j] : (j - 8 == i);
    for (int c = 0; c < 8; c++) {
        int p = c; for (int r = c + 1; r < 8; r++) if (fabs(a[r][c]) > fabs(a[p][c])) p = r;
        for (int j = 0; j < 16; j++) { double t = a[c][j]; a[c][j] = a[p][j]; a[p][j] = t; }
        double v = a[c][c]; for (int j = 0; j < 16; j++) a[c][j] /= v;
        for (int r = 0; r < 8; r++) if (r != c) { double f = a[r][c]; for (int j = 0; j < 16; j++) a[r][j] -= f * a[c][j]; }
    }
    for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) Ti[i][j] = a[i][j + 8];
}
/* pixels (64, row-major) -> dequantized-domain coefficients, natural order:
   P = T C T' / 256  =>  C = 256 Ti P Ti' */
static void fdct(const double *p, double *c)
{
    double t[64];
    for (int r = 0; r < 8; r++) for (int j = 0; j < 8; j++) { double s = 0; for (int k = 0; k < 8; k++) s += Ti[r][k] * p[k * 8 + j]; t[r * 8 + j] = s; }
    for (int r = 0; r < 8; r++) for (int j = 0; j < 8; j++) { double s = 0; for (int k = 0; k < 8; k++) s += t[r * 8 + k] * Ti[j][k]; c[r * 8 + j] = 256.0 * s; }
}

/* ---------------------------------------------------------------- coefficient coding */
static int levelbits(int v) { v = abs(v); return v ? ilog2(v) : -1; }

/* writes (w != NULL) or counts bits of read_dct_coeffs() for AC levels lv[1..63] (scan order) */
static long put_coeffs(BW *w, const int *lv, int q)
{
    long bits = 0;
    int L[64], done[64] = {0}, maxl = -1;
    for (int i = 1; i < 64; i++) { L[i] = levelbits(lv[i]); if (L[i] > maxl) maxl = L[i]; }
#define PUT(v, n) do { if (w) bw_put(w, (v), (n)); bits += (n); } while (0)
    PUT(maxl + 1, 4);
    int coef[128], mode[128], ls = 64, le = 64;
    coef[le] = 4; mode[le++] = 0; coef[le] = 24; mode[le++] = 0; coef[le] = 44; mode[le++] = 0;
    coef[le] = 1; mode[le++] = 3; coef[le] = 2; mode[le++] = 3; coef[le] = 3; mode[le++] = 3;
    for (int b = maxl; b >= 0; b--) {
        int pos = ls;
        while (pos < le) {
            if (!(mode[pos] | coef[pos])) { pos++; continue; }
            int c = coef[pos], m = mode[pos];
            int span = m == 0 ? 20 : m == 1 ? 16 : m == 2 ? 4 : 1;
            int sig = 0;
            for (int k = c; k < c + span && k < 64; k++) if (!done[k] && L[k] == b) { sig = 1; break; }
            PUT(sig, 1);
            if (!sig) { pos++; continue; }
            switch (m) {
            case 0: coef[pos] = c + 4; mode[pos] = 1; /* fallthrough */
            case 2:
                if (m == 2) { coef[pos] = 0; mode[pos++] = 0; }
                for (int i = 0; i < 4; i++, c++) {
                    if (!(L[c] == b && !done[c])) { PUT(1, 1); coef[--ls] = c; mode[ls] = 3; }
                    else {
                        PUT(0, 1);
                        if (!b) PUT(lv[c] < 0, 1);
                        else { PUT(abs(lv[c]) & ((1 << b) - 1), b); PUT(lv[c] < 0, 1); }
                        done[c] = 1;
                    }
                }
                break;
            case 1:
                mode[pos] = 2;
                for (int i = 0; i < 3; i++) { c += 4; coef[le] = c; mode[le++] = 2; }
                break;
            case 3:
                if (!b) PUT(lv[c] < 0, 1);
                else { PUT(abs(lv[c]) & ((1 << b) - 1), b); PUT(lv[c] < 0, 1); }
                done[c] = 1; coef[pos] = 0; mode[pos++] = 0;
                break;
            }
        }
    }
    PUT(q, 4);
#undef PUT
    return bits;
}

typedef struct { int ok; int q; int lv[64]; int rec[64]; long sse; long bits; } DctRes;

/* src: target values (pixels for intra, residual for inter); base: prediction (NULL for intra) */
static void dct_try(const int *src, const uint8_t *base, const int32_t (*quant)[64], int q, int intra, const int *target, DctRes *r)
{
    double p[64], c[64];
    for (int i = 0; i < 64; i++) p[i] = src[i];
    fdct(p, c);
    int32_t blk[64] = {0};
    r->ok = 1; r->q = q;
    for (int i = 0; i < 64; i++) {
        int pos = bink_scan[i];
        double v = c[pos] * 2048.0 / quant[q][i];
        int l = (int)lrint(v);
        if (i == 0) {
            if (intra) { if (l < 0) l = 0; if (l > 2047) l = 2047; }
            else { if (l < -1023) l = -1023; if (l > 1023) l = 1023; }
        } else {
            if (l > 32767) l = 32767; if (l < -32767) l = -32767;
        }
        int64_t prod = (int64_t)l * quant[q][i];
        if (prod > 0x7FFFFFFFLL || prod < -0x80000000LL) { r->ok = 0; return; }
        r->lv[i] = l;
        blk[pos] = (int32_t)((uint32_t)l * (uint32_t)quant[q][i]) >> 11;
    }
    int out[64];
    idct_int(blk, out);
    long sse = 0;
    for (int i = 0; i < 64; i++) {
        int v = base ? base[i] + out[i] : out[i];
        if (v < 0 || v > 255) { r->ok = 0; return; }
        r->rec[i] = v;
        int d = v - target[i]; sse += d * d;
    }
    r->sse = sse;
    r->bits = put_coeffs(NULL, r->lv, q);
}

/* largest quantizer meeting the distortion budget (falls back to the smallest error) */
static void dct_best(const int *src, const uint8_t *base, const int32_t (*quant)[64], int intra, const int *target, long budget, DctRes *best)
{
    DctRes r;
    best->ok = 0;
    int lo = 0, hi = 15;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        dct_try(src, base, quant, m, intra, target, &r);
        if (r.ok && r.sse <= budget) { *best = r; lo = m + 1; }
        else hi = m - 1;
    }
    if (best->ok) return;
    for (int q = 0; q < 16; q++) {
        dct_try(src, base, quant, q, intra, target, &r);
        if (r.ok && (!best->ok || r.sse < best->sse)) *best = r;
        if (r.ok) break;
    }
}

/* ---------------------------------------------------------------- Huffman tree choice */
typedef struct { int vlc, m; uint8_t syms[16], pos[16]; } TreeSel;

static void tree_identity(TreeSel *t) { t->vlc = 0; t->m = 0; for (int i = 0; i < 16; i++) t->syms[i] = t->pos[i] = i; }

static void tree_choose(const long *h, TreeSel *out)
{
    long total = 0; for (int i = 0; i < 16; i++) total += h[i];
    tree_identity(out);
    long best = 4 + 4 * total;
    int order[16]; for (int i = 0; i < 16; i++) order[i] = i;
    for (int i = 0; i < 16; i++) for (int j = i + 1; j < 16; j++)
        if (h[order[j]] > h[order[i]] || (h[order[j]] == h[order[i]] && order[j] < order[i])) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int m = 1; m <= 8; m++) {
        uint8_t syms[16], used[16] = {0};
        for (int i = 0; i < m; i++) { syms[i] = order[i]; used[order[i]] = 1; }
        int n = m; for (int s = 0; s < 16; s++) if (!used[s]) syms[n++] = s;
        for (int v = 1; v < 16; v++) {
            long cost = 4 + 1 + 3 + 4 * m;
            for (int i = 0; i < 16; i++) cost += h[syms[i]] * bink_tree_lens[v][i];
            if (cost < best) {
                best = cost; out->vlc = v; out->m = m; memcpy(out->syms, syms, 16);
                for (int i = 0; i < 16; i++) out->pos[syms[i]] = i;
            }
        }
    }
}
static void tree_write(BW *w, const TreeSel *t)
{
    bw_put(w, t->vlc, 4);
    if (!t->vlc) return;
    bw_put(w, 1, 1); bw_put(w, t->m - 1, 3);
    for (int i = 0; i < t->m; i++) bw_put(w, t->syms[i], 4);
}

/* ---------------------------------------------------------------- plane encoder */
typedef struct {
    int w, h;            /* real plane size */
    int bw, bh, stride;  /* blocks, padded stride = bw*8 */
    uint8_t *rec, *prev; /* reconstructions, padded (bh*8 rows) */
} Plane;

typedef struct { int16_t *v; int n, cap; int *rowstart; } Vals;

static void vals_push(Vals *a, int v)
{
    if (a->n == a->cap) { a->cap = a->cap ? a->cap * 2 : 4096; a->v = realloc(a->v, a->cap * sizeof *a->v); }
    a->v[a->n++] = (int16_t)v;
}

/* per-plane encoding context */
typedef struct {
    Vals b[NB_SRC];
    BW *rowbits;      /* inline bits per block row */
    int len[NB_SRC];
    TreeSel tree[NB_SRC], colhi[16];
    long hist[NB_SRC][16], histhi[16][16];
    int collast;
} Ctx;

static long g_hist[10];
static double g_budget_mse;

static void put_huff(BW *w, const TreeSel *t, int sym) { int p = t->pos[sym]; bw_put(w, bink_tree_bits[t->vlc][p], bink_tree_lens[t->vlc][p]); }

/* chunk writer (w == NULL: gather statistics into ctx histograms) */
static void write_chunk(BW *w, Ctx *c, int k, int start, int t)
{
    Vals *a = &c->b[k];
    if (w) bw_put(w, t, c->len[k]);
    if (!t) return;
    const int16_t *v = a->v + start;
    int same = 1; for (int i = 1; i < t; i++) if (v[i] != v[0]) { same = 0; break; }
    switch (k) {
    case SRC_BLOCK_TYPES: case SRC_SUB_BLOCK_TYPES: case SRC_RUN:
        if (same) { if (w) { bw_put(w, 1, 1); bw_put(w, v[0], 4); } return; }
        if (w) bw_put(w, 0, 1);
        if (k == SRC_RUN) { for (int i = 0; i < t; i++) { if (w) put_huff(w, &c->tree[k], v[i]); else c->hist[k][v[i]]++; } return; }
        {
            int last = 0, i = 0;
            static const int rl[4] = { 32, 12, 8, 4 }, rs[4] = { 15, 14, 13, 12 };
            while (i < t) {
                int run = 0; while (i + run < t && v[i + run] == last) run++;
                int s = -1;
                for (int j = 0; j < 4; j++) if (run >= rl[j]) { s = j; break; }
                if (s >= 0) { if (w) put_huff(w, &c->tree[k], rs[s]); else c->hist[k][rs[s]]++; i += rl[s]; continue; }
                if (w) put_huff(w, &c->tree[k], v[i]); else c->hist[k][v[i]]++;
                last = v[i]; i++;
            }
        }
        return;
    case SRC_COLORS: {
        int n = same ? 1 : t;
        if (w) bw_put(w, same, 1);
        for (int i = 0; i < n; i++) {
            int hi = (v[i] >> 4) & 15, lo = v[i] & 15;
            if (w) { put_huff(w, &c->colhi[c->collast], hi); put_huff(w, &c->tree[k], lo); }
            else { c->histhi[c->collast][hi]++; c->hist[k][lo]++; }
            c->collast = hi;
        }
        return; }
    case SRC_PATTERN:
        for (int i = 0; i < t; i++) {
            if (w) { put_huff(w, &c->tree[k], v[i] & 15); put_huff(w, &c->tree[k], (v[i] >> 4) & 15); }
            else { c->hist[k][v[i] & 15]++; c->hist[k][(v[i] >> 4) & 15]++; }
        }
        return;
    case SRC_X_OFF: case SRC_Y_OFF:
        if (same) { if (w) { bw_put(w, 1, 1); bw_put(w, abs(v[0]), 4); if (v[0]) bw_put(w, v[0] < 0, 1); } return; }
        if (w) bw_put(w, 0, 1);
        for (int i = 0; i < t; i++) {
            if (w) { put_huff(w, &c->tree[k], abs(v[i])); if (v[i]) bw_put(w, v[i] < 0, 1); }
            else c->hist[k][abs(v[i])]++;
        }
        return;
    case SRC_INTRA_DC: case SRC_INTER_DC: {
        if (!w) return;
        int hs = k == SRC_INTER_DC;
        bw_put(w, abs(v[0]), 11 - hs);
        if (hs && v[0]) bw_put(w, v[0] < 0, 1);
        int prev = v[0];
        for (int i = 1; i < t; i += 8) {
            int n = t - i < 8 ? t - i : 8, mx = 0;
            for (int j = 0; j < n; j++) { int d = abs(v[i + j] - (j ? v[i + j - 1] : prev)); if (d > mx) mx = d; }
            int bs = mx ? ilog2(mx) + 1 : 0;
            bw_put(w, bs, 4);
            for (int j = 0; j < n; j++) {
                int d = v[i + j] - (j ? v[i + j - 1] : prev);
                if (bs) { bw_put(w, abs(d), bs); if (d) bw_put(w, d < 0, 1); }
            }
            prev = v[i + n - 1];
        }
        return; }
    }
}

/* walk the decoder's read schedule; emits (or counts) bundle chunks before each block row */
static void emit_plane(BW *w, Ctx *c, int bh)
{
    int ended[NB_SRC] = {0}, pend[NB_SRC] = {0}, next[NB_SRC] = {0};
    c->collast = 0;
    if (w) {
        for (int k = 0; k < NB_SRC; k++) {
            if (k == SRC_COLORS) for (int j = 0; j < 16; j++) tree_write(w, &c->colhi[j]);
            if (k != SRC_INTRA_DC && k != SRC_INTER_DC) tree_write(w, &c->tree[k]);
        }
    }
    for (int r = 0; r < bh; r++) {
        for (int k = 0; k < NB_SRC; k++) {
            Vals *a = &c->b[k];
            int need = a->rowstart[r + 1] - a->rowstart[r];
            if (!ended[k] && pend[k] == 0) {
                /* decoder reads a chunk now: this row's values, or the next row that has any */
                int rr = r;
                while (rr < bh && a->rowstart[rr + 1] == a->rowstart[rr]) rr++;
                int t = rr < bh ? a->rowstart[rr + 1] - a->rowstart[rr] : 0;
                if (t >= (1 << c->len[k])) { fprintf(stderr, "chunk too large: bundle %d t=%d\n", k, t); exit(1); }
                write_chunk(w, c, k, next[k], t);
                if (!t) ended[k] = 1;
                next[k] += t; pend[k] += t;
            }
            pend[k] -= need;
            if (pend[k] < 0) { fprintf(stderr, "schedule underflow bundle %d row %d\n", k, r); exit(1); }
        }
        if (w) bw_append(w, &c->rowbits[r]);
    }
    if (w) bw_align32(w);
}

static void encode_plane(BW *out, Plane *p, const uint8_t *src, int sstride, int key, double mse)
{
    Ctx c; memset(&c, 0, sizeof c);
    bundle_lens(p->w < 8 ? 8 : p->w, p->bw, c.len);
    for (int k = 0; k < NB_SRC; k++) c.b[k].rowstart = calloc(p->bh + 1, sizeof(int));
    c.rowbits = calloc(p->bh, sizeof(BW));
    long budget = (long)(mse * 64);
    int S[64], R[64];
    uint8_t P[64];
    for (int by = 0; by < p->bh; by++) {
        bw_init(&c.rowbits[by]);
        for (int k = 0; k < NB_SRC; k++) c.b[k].rowstart[by] = c.b[k].n;
        for (int bx = 0; bx < p->bw; bx++) {
            /* gather source (edge-replicated) and prediction */
            for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
                int sy = by * 8 + y, sx = bx * 8 + x;
                if (sy >= p->h) sy = p->h - 1;
                if (sx >= p->w) sx = p->w - 1;
                S[y * 8 + x] = src[sy * sstride + sx];
                P[y * 8 + x] = p->prev[(by * 8 + y) * p->stride + bx * 8 + x];
            }
            uint8_t *dst = p->rec + by * 8 * p->stride + bx * 8;
            int type = -1, rec[64];
            long sum = 0; for (int i = 0; i < 64; i++) sum += S[i];
            int mean = (int)((sum + 32) >> 6);
            long sse_fill = 0; for (int i = 0; i < 64; i++) sse_fill += (S[i] - mean) * (S[i] - mean);
            if (!key) {
                long sse = 0; int mx = 0;
                for (int i = 0; i < 64; i++) { int d = abs(S[i] - P[i]); sse += d * d; if (d > mx) mx = d; }
                if (sse <= budget / 2 && mx <= 6) {
                    type = SKIP_BLOCK;
                    for (int i = 0; i < 64; i++) rec[i] = P[i];
                }
            }
            if (type < 0 && sse_fill <= budget / 2) {
                type = FILL_BLOCK;
                vals_push(&c.b[SRC_COLORS], mean);
                for (int i = 0; i < 64; i++) rec[i] = mean;
            }
            if (type < 0) {
                DctRes ri, rn; rn.ok = 0;
                dct_best(S, NULL, bink_intra_quant, 1, S, budget, &ri);
                if (!key) {
                    for (int i = 0; i < 64; i++) R[i] = S[i] - P[i];
                    dct_best(R, P, bink_inter_quant, 0, S, budget, &rn);
                }
                /* rough bundle costs: intra DC ~ 6 bits, inter DC ~ 5 bits + 2 motion values */
                long ci = ri.ok ? ri.bits + 6 + (ri.sse > budget) * 100000 : 1L << 40;
                long cn = rn.ok ? rn.bits + 5 + (rn.sse > budget) * 100000 : 1L << 40;
                DctRes *b = NULL;
                if (ri.ok && (!rn.ok || ci <= cn)) { type = INTRA_BLOCK; b = &ri; }
                else if (rn.ok) { type = INTER_BLOCK; b = &rn; }
                if (b) {
                    if (type == INTRA_BLOCK) vals_push(&c.b[SRC_INTRA_DC], b->lv[0]);
                    else { vals_push(&c.b[SRC_X_OFF], 0); vals_push(&c.b[SRC_Y_OFF], 0); vals_push(&c.b[SRC_INTER_DC], b->lv[0]); }
                    put_coeffs(&c.rowbits[by], b->lv, b->q);
                    for (int i = 0; i < 64; i++) rec[i] = b->rec[i];
                } else {
                    type = RAW_BLOCK;
                    for (int i = 0; i < 64; i++) { vals_push(&c.b[SRC_COLORS], S[i]); rec[i] = S[i]; }
                }
            }
            vals_push(&c.b[SRC_BLOCK_TYPES], type);
            g_hist[type]++;
            for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) dst[y * p->stride + x] = (uint8_t)rec[y * 8 + x];
        }
    }
    for (int k = 0; k < NB_SRC; k++) c.b[k].rowstart[p->bh] = c.b[k].n;
    /* pass 1: statistics, choose trees */
    emit_plane(NULL, &c, p->bh);
    for (int k = 0; k < NB_SRC; k++) tree_choose(c.hist[k], &c.tree[k]);
    for (int j = 0; j < 16; j++) tree_choose(c.histhi[j], &c.colhi[j]);
    /* pass 2: write */
    emit_plane(out, &c, p->bh);
    for (int k = 0; k < NB_SRC; k++) { free(c.b[k].v); free(c.b[k].rowstart); }
    for (int r = 0; r < p->bh; r++) bw_free(&c.rowbits[r]);
    free(c.rowbits);
}

/* ---------------------------------------------------------------- container */
static uint8_t *readfile(const char *fn, long *sz)
{
    FILE *f = fopen(fn, "rb"); if (!f) { perror(fn); exit(1); }
    fseek(f, 0, SEEK_END); *sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(*sz); if (fread(d, 1, *sz, f) != (size_t)*sz) exit(1); fclose(f); return d;
}

int main(int argc, char **argv)
{
    if (argc < 7) { fprintf(stderr, "usage: binkenc in.yuv W H audio_src.bik out.bik recon.yuv [mseY mseC]\n"); return 1; }
    int W = atoi(argv[2]), H = atoi(argv[3]);
    double mseY = argc > 7 ? atof(argv[7]) : 3.0, mseC = argc > 8 ? atof(argv[8]) : 3.0;
    init_transform();
    long asz; uint8_t *ad = readfile(argv[4], &asz);
    uint32_t *ah = (uint32_t *)ad;
    int nf = ah[2], fpsn = ah[7], fpsd = ah[8], nt = ah[10];
    uint32_t *aidx = ah + 11 + 3 * nt;
    FILE *fi = fopen(argv[1], "rb"); if (!fi) { perror(argv[1]); return 1; }
    fseek(fi, 0, SEEK_END); long ysz = ftell(fi); fseek(fi, 0, SEEK_SET);
    long fsz = (long)W * H * 3 / 2;
    if (ysz / fsz != nf) { fprintf(stderr, "frame count mismatch: yuv %ld, bik %d\n", ysz / fsz, nf); return 1; }
    FILE *fr = fopen(argv[6], "wb");

    Plane pl[3];
    for (int i = 0; i < 3; i++) {
        Plane *p = &pl[i];
        p->w = i ? W / 2 : W; p->h = i ? H / 2 : H;
        p->bw = i ? (W + 15) >> 4 : (W + 7) >> 3;
        p->bh = i ? (H + 15) >> 4 : (H + 7) >> 3;
        p->stride = p->bw * 8;
        p->rec = calloc(p->stride * p->bh * 8, 1);
        p->prev = calloc(p->stride * p->bh * 8, 1);
    }
    uint8_t *frame = malloc(fsz);
    /* packets kept in memory */
    uint8_t **pk = calloc(nf, sizeof *pk); uint32_t *pks = calloc(nf, 4);
    BW vw; bw_init(&vw);
    double t0 = 0; long totbytes = 0;
    for (int f = 0; f < nf; f++) {
        if (fread(frame, 1, fsz, fi) != (size_t)fsz) { fprintf(stderr, "short read\n"); return 1; }
        const uint8_t *src[3] = { frame, frame + W * H, frame + W * H + (W / 2) * (H / 2) };
        bw_reset(&vw);
        bw_put(&vw, 0, 32);                     /* patched: byte offset of the end of the Y plane */
        /* bitstream plane order for 'h'+: Y, V, U */
        int order[3] = { 0, 2, 1 };
        uint32_t yend = 0;
        for (int j = 0; j < 3; j++) {
            int i = order[j];
            encode_plane(&vw, &pl[i], src[i], pl[i].w, f == 0, i ? mseC : mseY);
            if (j == 0) yend = (uint32_t)(vw.nbits >> 3);
        }
        memcpy(vw.buf, &yend, 4);
        uint32_t vbytes = (uint32_t)(vw.nbits >> 3);
        /* audio packets of the original frame */
        uint32_t o = aidx[f] & ~1u, e = (f == nf - 1) ? (uint32_t)asz : (aidx[f + 1] & ~1u);
        const uint8_t *p = ad + o; uint32_t abytes = 0;
        for (int t = 0; t < nt; t++) { uint32_t n; memcpy(&n, p + abytes, 4); abytes += 4 + n; }
        (void)e;
        pks[f] = abytes + vbytes;
        pk[f] = malloc(pks[f]);
        memcpy(pk[f], p, abytes); memcpy(pk[f] + abytes, vw.buf, vbytes);
        totbytes += pks[f];
        /* recon out (unpadded) and swap */
        for (int i = 0; i < 3; i++) {
            Plane *q = &pl[i];
            for (int y = 0; y < q->h; y++) fwrite(q->rec + y * q->stride, 1, q->w, fr);
            uint8_t *t = q->prev; q->prev = q->rec; q->rec = t;
        }
        if (f % 25 == 0 || f == nf - 1) { fprintf(stderr, "\rframe %d/%d  avg %.1f KB/frame", f + 1, nf, totbytes / 1024.0 / (f + 1)); fflush(stderr); }
        (void)t0;
    }
    fprintf(stderr, "\nblocks: skip %ld fill %ld intra %ld inter %ld raw %ld\n", g_hist[0], g_hist[6], g_hist[5], g_hist[7], g_hist[9]);
    fclose(fr); fclose(fi);

    /* write container */
    uint32_t hdr = 44 + 12 * nt + 4 * (nf + 1);
    uint32_t pos = hdr, maxf = 0;
    uint32_t *idx = calloc(nf + 1, 4);
    for (int f = 0; f < nf; f++) {
        idx[f] = pos | (f == 0);
        uint32_t s = (pks[f] + 3) & ~3u;
        if (s > maxf) maxf = s;
        pos += s;
    }
    idx[nf] = pos;
    FILE *fo = fopen(argv[5], "wb");
    uint32_t h[11] = { 0x694B4942 /* BIKi */, pos - 8, nf, maxf, nf, W, H, fpsn, fpsd, 0, nt };
    fwrite(h, 4, 11, fo);
    fwrite(ah + 11, 4, 3 * nt, fo);            /* audio max sizes, rate/flags, ids */
    fwrite(idx, 4, nf, fo);
    fwrite(&idx[nf], 4, 0, fo);
    /* the index has nf+1 entries: last one is the end offset */
    { uint32_t endo = pos; fseek(fo, 44 + 12 * nt + 4 * nf, SEEK_SET); fwrite(&endo, 4, 1, fo); }
    for (int f = 0; f < nf; f++) {
        fwrite(pk[f], 1, pks[f], fo);
        static const uint8_t z[4] = {0};
        fwrite(z, 1, ((pks[f] + 3) & ~3u) - pks[f], fo);
    }
    fclose(fo);
    fprintf(stderr, "written %s: %u bytes, %d frames, largest frame %u\n", argv[5], pos, nf, maxf);
    return 0;
}
