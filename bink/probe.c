/* Parse-only Bink 1 'i' decoder: reports per-plane bit sizes to learn the 32-bit packet field.
   usage: probe file.bik [frames] */
#include "bink_common.h"

typedef struct { int vlc; uint8_t syms[16]; } Tree;
typedef struct { int len; Tree tree; int dec, ptr, ended; } Bundle;
static Bundle B[NB_SRC];
static Tree col_high[16];
static int col_last;
static int maxt[NB_SRC];
static int version = 'i';

static int huff(BR *r, Tree *t)
{
    uint32_t code = 0;
    for (int l = 1; l <= 7; l++) {
        code |= br_get(r, 1) << (l - 1);
        for (int i = 0; i < 16; i++)
            if (bink_tree_lens[t->vlc][i] == l && bink_tree_bits[t->vlc][i] == code) return t->syms[i];
    }
    fprintf(stderr, "bad huff\n"); exit(1);
}
static void merge(BR *r, uint8_t *dst, uint8_t *src, int size)
{
    uint8_t *src2 = src + size; int size2 = size;
    do { if (!br_get(r, 1)) { *dst++ = *src++; size--; } else { *dst++ = *src2++; size2--; } } while (size && size2);
    while (size--) *dst++ = *src++;
    while (size2--) *dst++ = *src2++;
}
static void read_tree(BR *r, Tree *t)
{
    uint8_t tmp1[16] = {0}, tmp2[16], *in = tmp1, *out = tmp2;
    t->vlc = br_get(r, 4);
    if (!t->vlc) { for (int i = 0; i < 16; i++) t->syms[i] = i; return; }
    if (br_get(r, 1)) {
        int len = br_get(r, 3);
        for (int i = 0; i <= len; i++) { t->syms[i] = br_get(r, 4); tmp1[t->syms[i]] = 1; }
        for (int i = 0; i < 16 && len < 15; i++) if (!tmp1[i]) t->syms[++len] = i;
    } else {
        int len = br_get(r, 2);
        for (int i = 0; i < 16; i++) in[i] = i;
        for (int i = 0; i <= len; i++) {
            int size = 1 << i;
            for (int t2 = 0; t2 < 16; t2 += size << 1) merge(r, out + t2, in + t2, size);
            uint8_t *x = in; in = out; out = x;
        }
        memcpy(t->syms, in, 16);
    }
}
/* returns count read or 0 */
static int check_read(BR *r, Bundle *b)
{
    if (b->ended || b->dec > b->ptr) return 0;
    int t = br_get(r, b->len);
    if (!t) { b->ended = 1; return 0; }
    b->dec += t;
    if (t > maxt[b - B]) maxt[b - B] = t;
    return t;
}
static int maxt[NB_SRC];
static long stat_chunks[NB_SRC], stat_vals[NB_SRC];
static void read_bundles_row(BR *r)
{
    int t;
    for (int k = 0; k < 2; k++) { /* block types, sub types */
        Bundle *b = &B[k];
        if ((t = check_read(r, b))) {
            stat_chunks[k]++; stat_vals[k] += t;
            if (br_get(r, 1)) br_get(r, 4);
            else { int n = 0; while (n < t) { int v = huff(r, &b->tree); n += v < 12 ? 1 : (int[]){4,8,12,32}[v-12]; } }
        }
    }
    if ((t = check_read(r, &B[SRC_COLORS]))) {
        stat_chunks[2]++; stat_vals[2] += t;
        if (br_get(r, 1)) { col_last = huff(r, &col_high[col_last]); huff(r, &B[SRC_COLORS].tree); }
        else for (int i = 0; i < t; i++) { col_last = huff(r, &col_high[col_last]); huff(r, &B[SRC_COLORS].tree); }
    }
    if ((t = check_read(r, &B[SRC_PATTERN]))) { stat_chunks[3]++; stat_vals[3] += t; for (int i = 0; i < 2 * t; i++) huff(r, &B[SRC_PATTERN].tree); }
    for (int k = SRC_X_OFF; k <= SRC_Y_OFF; k++)
        if ((t = check_read(r, &B[k]))) {
            stat_chunks[k]++; stat_vals[k] += t;
            if (br_get(r, 1)) { if (br_get(r, 4)) br_get(r, 1); }
            else for (int i = 0; i < t; i++) if (huff(r, &B[k].tree)) br_get(r, 1);
        }
    for (int k = SRC_INTRA_DC; k <= SRC_INTER_DC; k++)
        if ((t = check_read(r, &B[k]))) {
            stat_chunks[k]++; stat_vals[k] += t;
            int hs = k == SRC_INTER_DC;
            int v = br_get(r, 11 - hs); if (v && hs) br_get(r, 1);
            int len = t - 1;
            for (int i = 0; i < len; i += 8) {
                int len2 = len - i < 8 ? len - i : 8;
                int bs = br_get(r, 4);
                if (bs) for (int j = 0; j < len2; j++) if (br_get(r, bs)) br_get(r, 1);
            }
        }
    if ((t = check_read(r, &B[SRC_RUN]))) {
        stat_chunks[8]++; stat_vals[8] += t;
        if (br_get(r, 1)) br_get(r, 4); else for (int i = 0; i < t; i++) huff(r, &B[SRC_RUN].tree);
    }
}
/* bundle value storage for consumption */
static uint8_t vals[NB_SRC][1 << 20];
static int stored[NB_SRC];
/* re-read with storage: simpler to keep a second implementation that stores values */
static void store_row(BR *r)
{
    int t;
    for (int k = 0; k < 2; k++) {
        Bundle *b = &B[k];
        if ((t = check_read(r, b))) {
            if (br_get(r, 1)) { int v = br_get(r, 4); for (int i = 0; i < t; i++) vals[k][stored[k]++] = v; }
            else { int n = 0, last = 0; while (n < t) { int v = huff(r, &b->tree);
                if (v < 12) { last = v; vals[k][stored[k]++] = v; n++; }
                else { int run = (int[]){4,8,12,32}[v-12]; for (int i = 0; i < run; i++) vals[k][stored[k]++] = last; n += run; } } }
        }
    }
    if ((t = check_read(r, &B[SRC_COLORS]))) {
        if (br_get(r, 1)) { col_last = huff(r, &col_high[col_last]); int v = col_last << 4 | huff(r, &B[SRC_COLORS].tree); for (int i = 0; i < t; i++) vals[2][stored[2]++] = v; }
        else for (int i = 0; i < t; i++) { col_last = huff(r, &col_high[col_last]); vals[2][stored[2]++] = col_last << 4 | huff(r, &B[SRC_COLORS].tree); }
    }
    if ((t = check_read(r, &B[SRC_PATTERN]))) for (int i = 0; i < t; i++) { int v = huff(r, &B[3].tree); v |= huff(r, &B[3].tree) << 4; vals[3][stored[3]++] = v; }
    for (int k = SRC_X_OFF; k <= SRC_Y_OFF; k++)
        if ((t = check_read(r, &B[k]))) {
            if (br_get(r, 1)) { int v = br_get(r, 4); if (v && br_get(r, 1)) v = -v; for (int i = 0; i < t; i++) vals[k][stored[k]++] = v; }
            else for (int i = 0; i < t; i++) { int v = huff(r, &B[k].tree); if (v && br_get(r, 1)) v = -v; vals[k][stored[k]++] = v; }
        }
    for (int k = SRC_INTRA_DC; k <= SRC_INTER_DC; k++)
        if ((t = check_read(r, &B[k]))) {
            int hs = k == SRC_INTER_DC;
            int v = br_get(r, 11 - hs); if (v && hs && br_get(r, 1)) v = -v;
            stored[k]++;
            int len = t - 1;
            for (int i = 0; i < len; i += 8) {
                int len2 = len - i < 8 ? len - i : 8;
                int bs = br_get(r, 4);
                for (int j = 0; j < len2; j++) { if (bs && br_get(r, bs)) br_get(r, 1); stored[k]++; }
            }
        }
    if ((t = check_read(r, &B[SRC_RUN]))) {
        if (br_get(r, 1)) { int v = br_get(r, 4); for (int i = 0; i < t; i++) vals[8][stored[8]++] = v; }
        else for (int i = 0; i < t; i++) vals[8][stored[8]++] = huff(r, &B[SRC_RUN].tree);
    }
}
static int getv(int k) { if (B[k].ptr >= stored[k]) { fprintf(stderr, "underflow bundle %d\n", k); exit(1); } return vals[k][B[k].ptr++]; }

static void skip_dct(BR *r)
{
    int coef_list[128], mode_list[128], list_start = 64, list_end = 64;
    coef_list[list_end] = 4;  mode_list[list_end++] = 0;
    coef_list[list_end] = 24; mode_list[list_end++] = 0;
    coef_list[list_end] = 44; mode_list[list_end++] = 0;
    coef_list[list_end] = 1;  mode_list[list_end++] = 3;
    coef_list[list_end] = 2;  mode_list[list_end++] = 3;
    coef_list[list_end] = 3;  mode_list[list_end++] = 3;
    for (int bits = (int)br_get(r, 4) - 1; bits >= 0; bits--) {
        int list_pos = list_start;
        while (list_pos < list_end) {
            if (!(mode_list[list_pos] | coef_list[list_pos]) || !br_get(r, 1)) { list_pos++; continue; }
            int ccoef = coef_list[list_pos], mode = mode_list[list_pos];
            switch (mode) {
            case 0: coef_list[list_pos] = ccoef + 4; mode_list[list_pos] = 1; /* fallthrough */
            case 2:
                if (mode == 2) { coef_list[list_pos] = 0; mode_list[list_pos++] = 0; }
                for (int i = 0; i < 4; i++, ccoef++) {
                    if (br_get(r, 1)) { coef_list[--list_start] = ccoef; mode_list[list_start] = 3; }
                    else { if (!bits) br_get(r, 1); else { br_get(r, bits); br_get(r, 1); } }
                }
                break;
            case 1:
                mode_list[list_pos] = 2;
                for (int i = 0; i < 3; i++) { ccoef += 4; coef_list[list_end] = ccoef; mode_list[list_end++] = 2; }
                break;
            case 3:
                if (!bits) br_get(r, 1); else { br_get(r, bits); br_get(r, 1); }
                coef_list[list_pos] = 0; mode_list[list_pos++] = 0;
                break;
            }
        }
    }
    br_get(r, 4);
}
static void skip_residue(BR *r, int masks_count)
{
    int coef_list[128], mode_list[128], list_start = 64, list_end = 64, nzc = 0;
    coef_list[list_end] = 4;  mode_list[list_end++] = 0;
    coef_list[list_end] = 24; mode_list[list_end++] = 0;
    coef_list[list_end] = 44; mode_list[list_end++] = 0;
    coef_list[list_end] = 0;  mode_list[list_end++] = 2;
    for (int mask = 1 << br_get(r, 3); mask; mask >>= 1) {
        for (int i = 0; i < nzc; i++) { if (!br_get(r, 1)) continue; if (--masks_count < 0) return; }
        int list_pos = list_start;
        while (list_pos < list_end) {
            if (!(coef_list[list_pos] | mode_list[list_pos]) || !br_get(r, 1)) { list_pos++; continue; }
            int ccoef = coef_list[list_pos], mode = mode_list[list_pos];
            switch (mode) {
            case 0: coef_list[list_pos] = ccoef + 4; mode_list[list_pos] = 1; /* fallthrough */
            case 2:
                if (mode == 2) { coef_list[list_pos] = 0; mode_list[list_pos++] = 0; }
                for (int i = 0; i < 4; i++, ccoef++) {
                    if (br_get(r, 1)) { coef_list[--list_start] = ccoef; mode_list[list_start] = 3; }
                    else { nzc++; br_get(r, 1); if (--masks_count < 0) return; }
                }
                break;
            case 1:
                mode_list[list_pos] = 2;
                for (int i = 0; i < 3; i++) { ccoef += 4; coef_list[list_end] = ccoef; mode_list[list_end++] = 2; }
                break;
            case 3:
                nzc++; br_get(r, 1); coef_list[list_pos] = 0; mode_list[list_pos++] = 0;
                if (--masks_count < 0) return;
                break;
            }
        }
    }
}
static int hist[10];
static void parse_plane(BR *r, int width, int height, int is_chroma)
{
    int bw = is_chroma ? (width * 2 + 15) >> 4 : (width + 7) >> 3;
    int bh = is_chroma ? (height * 2 + 15) >> 4 : (height + 7) >> 3;
    int len[NB_SRC];
    bundle_lens(width < 8 ? 8 : width, bw, len);
    memset(B, 0, sizeof B); memset(stored, 0, sizeof stored);
    for (int i = 0; i < NB_SRC; i++) {
        B[i].len = len[i];
        if (i == SRC_COLORS) { for (int j = 0; j < 16; j++) read_tree(r, &col_high[j]); col_last = 0; }
        if (i != SRC_INTRA_DC && i != SRC_INTER_DC) read_tree(r, &B[i].tree);
    }
    for (int by = 0; by < bh; by++) {
        store_row(r);
        for (int bx = 0; bx < bw; bx++) {
            int blk = getv(SRC_BLOCK_TYPES);
            if (((by & 1) || (bx & 1)) && blk == SCALED_BLOCK) { bx++; continue; }
            hist[blk]++;
            switch (blk) {
            case SKIP_BLOCK: break;
            case SCALED_BLOCK: {
                int sb = getv(SRC_SUB_BLOCK_TYPES);
                if (sb == RUN_BLOCK) { const uint8_t *scan = bink_patterns[br_get(r, 4)]; (void)scan; int i = 0;
                    do { int run = getv(SRC_RUN) + 1; i += run; if (br_get(r, 1)) getv(SRC_COLORS); else for (int j = 0; j < run; j++) getv(SRC_COLORS); } while (i < 63);
                    if (i == 63) getv(SRC_COLORS); }
                else if (sb == INTRA_BLOCK) { B[SRC_INTRA_DC].ptr++; skip_dct(r); }
                else if (sb == FILL_BLOCK) getv(SRC_COLORS);
                else if (sb == PATTERN_BLOCK) { getv(2); getv(2); for (int j = 0; j < 8; j++) getv(3); }
                else if (sb == RAW_BLOCK) for (int j = 0; j < 64; j++) getv(2);
                else { fprintf(stderr, "bad sub %d\n", sb); exit(1); }
                bx++;
                break; }
            case MOTION_BLOCK: getv(4); getv(5); break;
            case RUN_BLOCK: { br_get(r, 4); int i = 0;
                do { int run = getv(SRC_RUN) + 1; i += run; if (br_get(r, 1)) getv(SRC_COLORS); else for (int j = 0; j < run; j++) getv(SRC_COLORS); } while (i < 63);
                if (i == 63) getv(SRC_COLORS); break; }
            case RESIDUE_BLOCK: getv(4); getv(5); skip_residue(r, br_get(r, 7)); break;
            case INTRA_BLOCK: B[SRC_INTRA_DC].ptr++; skip_dct(r); break;
            case FILL_BLOCK: getv(2); break;
            case INTER_BLOCK: getv(4); getv(5); B[SRC_INTER_DC].ptr++; skip_dct(r); break;
            case PATTERN_BLOCK: getv(2); getv(2); for (int j = 0; j < 8; j++) getv(3); break;
            case RAW_BLOCK: for (int j = 0; j < 64; j++) getv(2); break;
            default: fprintf(stderr, "bad blk %d\n", blk); exit(1);
            }
        }
    }
    if (r->pos & 31) r->pos += 32 - (r->pos & 31);
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(sz); fread(d, 1, sz, f); fclose(f);
    uint32_t *h = (uint32_t *)d;
    int nf = h[2], w = h[5], ht = h[6], nt = h[10];
    int maxf = argc > 2 ? atoi(argv[2]) : nf;
    uint32_t *idx = h + 11 + 3 * nt;
    for (int i = 0; i < nf && i < maxf; i++) {
        uint32_t o = idx[i] & ~1u, e = (i == nf - 1) ? (uint32_t)sz : (idx[i + 1] & ~1u);
        const uint8_t *p = d + o;
        for (int t = 0; t < nt; t++) { uint32_t n = *(uint32_t *)p; p += 4 + n; }
        BR r = { p, 0, (d + e) - p };
        uint32_t f32 = br_get(&r, 32);
        parse_plane(&r, w, ht, 0);
        uint64_t yend = r.pos >> 3;
        parse_plane(&r, w / 2, ht / 2, 1);
        uint64_t c1 = r.pos >> 3;
        parse_plane(&r, w / 2, ht / 2, 1);
        printf("frame %d size %llu field %u  Yend %llu C1end %llu C2end %llu\n", i, (unsigned long long)r.size, f32,
               (unsigned long long)yend, (unsigned long long)c1, (unsigned long long)(r.pos >> 3));
    }
    printf("max chunk:"); for (int i = 0; i < NB_SRC; i++) printf(" %d", maxt[i]); printf("\n");
    printf("block hist:"); for (int i = 0; i < 10; i++) printf(" %d", hist[i]); printf("\n");
    return 0;
}
