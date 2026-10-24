// Bink audio (the DCT variant of the 2004 Bink files inside Shrek 2's sound packages) -> 16-bit PCM.
// The bitstream decoding follows FFmpeg's libavcodec/binkaudio.c (LGPL 2.1 or later, Copyright (c)
// 2007-2011 Peter Ross, 2009 Daniel Verkamp); this file is under the same licence.
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <complex>
#include <algorithm>

namespace binkaudio {

struct Bits {   // little-endian bit reader (LSB first)
    const uint8_t* d; size_t n, pos = 0;
    Bits(const uint8_t* p, size_t sz) : d(p), n(sz * 8) {}
    unsigned bit() { if (pos >= n) { pos++; return 0; } unsigned b = (d[pos >> 3] >> (pos & 7)) & 1; pos++; return b; }
    unsigned get(int k) { unsigned v = 0; for (int i = 0; i < k; i++) v |= bit() << i; return v; }
    float flt() { int power = (int)get(5); float f = ldexpf((float)get(23), power - 23); if (bit()) f = -f; return f; }
    void align32() { pos = (pos + 31) & ~(size_t)31; }
    bool left(size_t k) const { return pos + k <= n; }
};

// DCT-III of length N (power of two) through one complex FFT of length N:
// x[n] = X[0]/2 + sum_{k>=1} X[k] cos(pi k (2n+1) / 2N)  (FFmpeg's DCT_III, before its own scaling)
struct Dct3 {     // as a zero-padded inverse FFT of length 2N: x[n] = Re sum_k c_k X[k] e^{i pi k (2n+1) / 2N}
    int N = 0, M = 0; std::vector<std::complex<double>> tw, w; std::vector<int> rev;
    void init(int n) {
        N = n; M = 2 * n; tw.resize(N); w.resize(M / 2); rev.resize(M);
        for (int k = 0; k < N; k++) tw[k] = std::polar(1.0, 3.14159265358979323846 * k / (2.0 * N));
        for (int k = 0; k < M / 2; k++) w[k] = std::polar(1.0, 2 * 3.14159265358979323846 * k / M);
        int bits = 0; while ((1 << bits) < M) bits++;
        for (int i = 0; i < M; i++) { int r = 0; for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[i] = r; }
    }
    void run(float* x) const {
        std::vector<std::complex<double>> a(M);
        for (int k = 0; k < N; k++) a[k] = tw[k] * (k ? (double)x[k] : 0.5 * x[0]);
        for (int i = 0; i < M; i++) if (i < rev[i]) std::swap(a[i], a[rev[i]]);
        for (int len = 2; len <= M; len <<= 1) {
            int step = M / len;
            for (int i = 0; i < M; i += len)
                for (int j = 0; j < len / 2; j++) {
                    std::complex<double> u = a[i + j], v = a[i + j + len / 2] * w[j * step];
                    a[i + j] = u + v; a[i + j + len / 2] = u - v;
                }
        }
        for (int n = 0; n < N; n++) x[n] = (float)a[n].real();
    }
};

static const uint16_t CRIT[25] = { 100, 200, 300, 400, 510, 630, 770, 920, 1080, 1270, 1480, 1720, 2000, 2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500, 24500 };
static const uint8_t RLE[16] = { 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64 };

struct Decoder {
    int rate = 0, channels = 1, frameLen = 0, overlap = 0, numBands = 0; bool dct = true, first = true;
    float root = 0, gain = 32768.f; float quantTable[96]; int bands[26];
    std::vector<float> prev[2], out[2]; Dct3 tr;
    bool init(int sampleRate, int ch, bool useDct) {
        if (!useDct || ch < 1 || ch > 2) return false;     // the RDFT variant is not used by the game's sounds
        rate = sampleRate; channels = ch; dct = useDct;
        int bits = rate < 22050 ? 9 : rate < 44100 ? 10 : 11;
        frameLen = 1 << bits; overlap = frameLen / 16;
        int half = (rate + 1) / 2;
        root = (float)(frameLen / (std::sqrt((double)frameLen) * 32768.0));
        for (int i = 0; i < 96; i++) quantTable[i] = expf(i * 0.15289164787221953823f) * root;
        for (numBands = 1; numBands < 25; numBands++) if (half <= CRIT[numBands - 1]) break;
        bands[0] = 2;
        for (int i = 1; i < numBands; i++) bands[i] = (CRIT[i - 1] * frameLen / half) & ~1;
        bands[numBands] = frameLen;
        for (int c = 0; c < channels; c++) { prev[c].assign(overlap, 0); out[c].assign(frameLen, 0); }
        tr.init(frameLen); first = true;
        gain = 32768.f * 2.f / frameLen;      // FFmpeg's DCT-III carries a factor 2/N
        return true;
    }
    // one block: frameLen - overlap samples per channel, appended interleaved to pcm
    void block(Bits& g, std::vector<int16_t>& pcm) {
        float quant[25];
        g.get(2);
        for (int ch = 0; ch < channels; ch++) {
            float* c = out[ch].data();
            c[0] = g.flt() * root; c[1] = g.flt() * root;
            for (int i = 0; i < numBands; i++) { int v = (int)g.get(8); quant[i] = quantTable[std::min(v, 95)]; }
            int k = 0; float q = quant[0]; int i = 2;
            while (i < frameLen) {
                int j;
                if (g.bit()) j = i + RLE[g.get(4)] * 8; else j = i + 8;
                j = std::min(j, frameLen);
                int width = (int)g.get(4);
                if (!width) {
                    std::fill(c + i, c + j, 0.f); i = j;
                    while (bands[k] < i) q = quant[k++];
                } else {
                    while (i < j) {
                        if (bands[k] == i) q = quant[k++];
                        int coeff = (int)g.get(width);
                        if (coeff) c[i] = g.bit() ? -q * coeff : q * coeff; else c[i] = 0.f;
                        i++;
                    }
                }
            }
            c[0] /= 0.5f;
            tr.run(c);
        }
        for (int ch = 0; ch < channels; ch++) {
            int count = overlap * channels;
            if (!first) {
                int j = ch;
                for (int i = 0; i < overlap; i++, j += channels)
                    out[ch][i] = (prev[ch][i] * (count - j) + out[ch][i] * j) / count;
            }
            std::copy(out[ch].begin() + (frameLen - overlap), out[ch].end(), prev[ch].begin());
        }
        first = false;
        int n = frameLen - overlap;
        for (int i = 0; i < n; i++)
            for (int ch = 0; ch < channels; ch++) {
                float v = out[ch][i] * gain;
                pcm.push_back((int16_t)std::max(-32768.f, std::min(32767.f, std::round(v))));
            }
    }
    // one audio packet of a Bink frame (after its 4-byte length): reported size, then blocks
    void packet(const uint8_t* p, size_t n, std::vector<int16_t>& pcm) {
        if (n < 4) return;
        uint32_t want; memcpy(&want, p, 4);     // decoded bytes in this packet
        Bits g(p + 4, n - 4);
        size_t start = pcm.size();
        size_t wantSamples = want / 2;
        while (g.left(64) && pcm.size() - start < wantSamples) { block(g, pcm); g.align32(); }
        if (pcm.size() - start > wantSamples) pcm.resize(start + wantSamples);
    }
};

// a whole Bink file with one audio track -> PCM; returns false if it is not one
inline bool decodeFile(const uint8_t* b, size_t n, std::vector<int16_t>& pcm, int& rate, int& channels) {
    auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= n) memcpy(&v, b + o, 4); return v; };
    if (n < 48 || memcmp(b, "BIK", 3)) return false;
    uint32_t frames = u32(8), tracks = u32(40);
    if (tracks < 1 || tracks > 4 || frames == 0 || frames > 1000000) return false;
    size_t p = 44 + 4 * tracks;
    uint16_t r, flags; memcpy(&r, b + p, 2); memcpy(&flags, b + p + 2, 2);
    p += 4 * tracks;   // rate + flags of every track
    p += 4 * tracks;   // track ids
    rate = r; channels = (flags & 0x2000) ? 2 : 1;
    Decoder d; if (!d.init(rate, channels, (flags & 0x1000) != 0)) return false;
    std::vector<uint32_t> idx(frames + 1);
    for (uint32_t i = 0; i <= frames; i++) idx[i] = u32(p + 4 * i) & ~1u;
    if (idx[frames] == 0 || idx[frames] > n) idx[frames] = (uint32_t)n;
    for (uint32_t f = 0; f < frames; f++) {
        size_t q = idx[f], end = idx[f + 1];
        if (q + 4 > n || end > n || end <= q) break;
        uint32_t asz = u32(q);           // first track's audio packet
        if (asz >= 4 && q + 4 + asz <= end) d.packet(b + q + 4, asz, pcm);
    }
    return true;
}

}  // namespace binkaudio
