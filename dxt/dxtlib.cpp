#include "bc7enc_rdo/rgbcx.h"
#include <cstring>
#include <cstdint>
static bool inited = false;
static void ini() { if (!inited) { rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal); inited = true; } }
// fmt: 1 = DXT1, 5 = DXT5 ; rgba w*h*4, w,h multiples of 4 (or <4 padded by caller)
extern "C" void dxt_encode(const uint8_t* rgba, int w, int h, int fmt, int level, uint8_t* out) {
    ini();
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    uint8_t blk[64];
    for (int by = 0; by < bh; by++) for (int bx = 0; bx < bw; bx++) {
        for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
            int sx = bx * 4 + x; if (sx >= w) sx = w - 1;
            int sy = by * 4 + y; if (sy >= h) sy = h - 1;
            memcpy(blk + (y * 4 + x) * 4, rgba + (sy * w + sx) * 4, 4);
        }
        if (fmt == 1) { rgbcx::encode_bc1(level, out, blk, false, false); out += 8; }
        else { rgbcx::encode_bc3_hq(level, out, blk); out += 16; }
    }
}
// fmt: 1 DXT1, 3 DXT3, 5 DXT5 -> rgba
extern "C" void dxt_decode(const uint8_t* in, int w, int h, int fmt, uint8_t* rgba) {
    ini();
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    uint8_t px[64];
    for (int by = 0; by < bh; by++) for (int bx = 0; bx < bw; bx++) {
        if (fmt == 1) { rgbcx::unpack_bc1(in, px, true); in += 8; }
        else if (fmt == 5) { rgbcx::unpack_bc3(in, px); in += 16; }
        else { rgbcx::unpack_bc1(in + 8, px, true); for (int i = 0; i < 16; i++) { int a = (in[i / 2] >> ((i & 1) * 4)) & 15; px[i * 4 + 3] = (uint8_t)(a * 17); } in += 16; }
        for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
            int sx = bx * 4 + x, sy = by * 4 + y; if (sx >= w || sy >= h) continue;
            memcpy(rgba + (sy * w + sx) * 4, px + (y * 4 + x) * 4, 4);
        }
    }
}
