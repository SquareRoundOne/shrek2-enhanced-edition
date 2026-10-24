// Small software 2D renderer for the launcher: premultiplied BGRA canvas, image drawing with
// mip-mapped downscaling / bicubic upscaling, anti-aliased rounded rectangles, stb_truetype text.
// Portable on purpose: the same code renders the Windows window and the Linux test screenshots.
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include "stb_truetype.h"

namespace gfx {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    Rect() {}
    Rect(float x, float y, float w, float h) : x(x), y(y), w(w), h(h) {}
    float r() const { return x + w; }
    float b() const { return y + h; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect inset(float d) const { return Rect(x + d, y + d, w - 2 * d, h - 2 * d); }
    Rect inset(float dx, float dy) const { return Rect(x + dx, y + dy, w - 2 * dx, h - 2 * dy); }
};

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    Color() {}
    Color(float r, float g, float b, float a = 1) : r(r), g(g), b(b), a(a) {}
    static Color hex(uint32_t rgb, float a = 1) {
        return Color(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a);
    }
    Color alpha(float k) const { return Color(r, g, b, a * k); }
};

// premultiplied RGBA float pixel helpers
struct PF { float r, g, b, a; };
static inline PF unpack(uint32_t p) {
    return { ((p >> 16) & 255) / 255.f, ((p >> 8) & 255) / 255.f, (p & 255) / 255.f, (p >> 24) / 255.f };
}
static inline uint32_t pack(PF c) {
    auto q = [](float v) { int i = (int)(v * 255.f + 0.5f); return (uint32_t)(i < 0 ? 0 : i > 255 ? 255 : i); };
    return (q(c.a) << 24) | (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
}

// Image: premultiplied 0xAARRGGBB with a lazily built mip chain.
struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    std::vector<std::shared_ptr<Image>> mips;   // mips[0] = half size
    bool ok() const { return w > 0 && h > 0; }
    static std::shared_ptr<Image> fromRGBA(const uint8_t* rgba, int w, int h) {
        auto im = std::make_shared<Image>(); im->w = w; im->h = h; im->px.resize((size_t)w * h);
        for (size_t i = 0; i < (size_t)w * h; i++) {
            float a = rgba[i * 4 + 3] / 255.f;
            PF c{ rgba[i * 4] / 255.f * a, rgba[i * 4 + 1] / 255.f * a, rgba[i * 4 + 2] / 255.f * a, a };
            im->px[i] = pack(c);
        }
        return im;
    }
    std::shared_ptr<Image> crop(int x, int y, int cw, int ch) const {
        auto im = std::make_shared<Image>(); im->w = cw; im->h = ch; im->px.resize((size_t)cw * ch);
        for (int j = 0; j < ch; j++) for (int i = 0; i < cw; i++) {
            int sx = std::clamp(x + i, 0, w - 1), sy = std::clamp(y + j, 0, h - 1);
            im->px[(size_t)j * cw + i] = px[(size_t)sy * w + sx];
        }
        return im;
    }
    const Image* level(int l) {
        if (l <= 0) return this;
        while ((int)mips.size() < l) {
            const Image* s = mips.empty() ? this : mips.back().get();
            if (s->w <= 1 && s->h <= 1) break;
            auto m = std::make_shared<Image>(); m->w = std::max(1, s->w / 2); m->h = std::max(1, s->h / 2);
            m->px.resize((size_t)m->w * m->h);
            for (int y = 0; y < m->h; y++) for (int x = 0; x < m->w; x++) {
                PF acc{ 0, 0, 0, 0 };
                for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) {
                    int sx = std::min(s->w - 1, x * 2 + dx), sy = std::min(s->h - 1, y * 2 + dy);
                    PF c = unpack(s->px[(size_t)sy * s->w + sx]);
                    acc.r += c.r; acc.g += c.g; acc.b += c.b; acc.a += c.a;
                }
                m->px[(size_t)y * m->w + x] = pack({ acc.r / 4, acc.g / 4, acc.b / 4, acc.a / 4 });
            }
            mips.push_back(m);
        }
        return mips.empty() ? this : mips[std::min((int)mips.size(), l) - 1].get();
    }
    PF at(int x, int y) const {
        x = std::clamp(x, 0, w - 1); y = std::clamp(y, 0, h - 1);
        return unpack(px[(size_t)y * w + x]);
    }
};
typedef std::shared_ptr<Image> ImageP;

// ------------------------------------------------------------------ fonts
struct Font {
    std::vector<uint8_t> data;
    stbtt_fontinfo info{};
    float xscale = 1.f;              // < 1 = condensed
    bool ok = false;
    struct Glyph { int x0, y0, w, h; float adv; std::vector<uint8_t> bm; };
    std::map<uint64_t, Glyph> cache;
    bool load(const uint8_t* d, size_t n, float xs = 1.f) {
        data.assign(d, d + n); xscale = xs;
        ok = stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0)) != 0;
        return ok;
    }
    const Glyph& glyph(int cp, float px) {
        uint64_t key = ((uint64_t)cp << 32) | (uint32_t)(px * 16.f);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        Glyph g{};
        float s = stbtt_ScaleForPixelHeight(&info, px);
        int adv, lsb; stbtt_GetCodepointHMetrics(&info, cp, &adv, &lsb);
        g.adv = adv * s * xscale;
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&info, cp, s * xscale, s, &x0, &y0, &x1, &y1);
        g.x0 = x0; g.y0 = y0; g.w = x1 - x0; g.h = y1 - y0;
        if (g.w > 0 && g.h > 0) {
            g.bm.resize((size_t)g.w * g.h);
            stbtt_MakeCodepointBitmap(&info, g.bm.data(), g.w, g.h, g.w, s * xscale, s, cp);
        }
        return cache.emplace(key, std::move(g)).first->second;
    }
    float ascent(float px) {
        int a, d, l; stbtt_GetFontVMetrics(&info, &a, &d, &l);
        return a * stbtt_ScaleForPixelHeight(&info, px);
    }
    float kern(int a, int b, float px) {
        return stbtt_GetCodepointKernAdvance(&info, a, b) * stbtt_ScaleForPixelHeight(&info, px) * xscale;
    }
};

static inline std::vector<int> utf8(const std::string& s) {
    std::vector<int> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        int cp, n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 31; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 15; n = 3; }
        else { cp = c & 7; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 63);
        out.push_back(cp); i += n;
    }
    return out;
}

// ------------------------------------------------------------------ canvas
enum Align { LEFT = 0, CENTER = 1, RIGHT = 2 };

struct Canvas {
    int w = 0, h = 0;
    float scale = 1.f;                    // logical -> pixels
    std::vector<uint32_t> px;             // premultiplied ARGB, opaque in the end
    Rect clip;                            // in pixels

    void resize(int W, int H) { w = W; h = H; px.assign((size_t)w * h, 0xFF000000); clip = Rect(0, 0, (float)w, (float)h); }
    // intersects the current clip with lr (logical); returns the previous clip for restoring
    Rect setClip(Rect lr) {
        Rect old = clip, p = toPx(lr);
        float x0 = std::max(clip.x, p.x), y0 = std::max(clip.y, p.y), x1 = std::min(clip.r(), p.r()), y1 = std::min(clip.b(), p.b());
        clip = Rect(x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0));
        return old;
    }
    void resetClip() { clip = Rect(0, 0, (float)w, (float)h); }
    Rect toPx(Rect r) const { return Rect(r.x * scale, r.y * scale, r.w * scale, r.h * scale); }

    inline void blend(int x, int y, PF s) {
        if (s.a <= 0) return;
        uint32_t& d = px[(size_t)y * w + x];
        if (s.a >= 1.f) { d = pack(s); return; }
        PF dc = unpack(d); float k = 1.f - s.a;
        d = pack({ s.r + dc.r * k, s.g + dc.g * k, s.b + dc.b * k, s.a + dc.a * k });
    }
    void pixelBounds(const Rect& p, int& x0, int& y0, int& x1, int& y1) const {
        x0 = std::max((int)std::floor(p.x), (int)clip.x); y0 = std::max((int)std::floor(p.y), (int)clip.y);
        x1 = std::min((int)std::ceil(p.r()), (int)clip.r()); y1 = std::min((int)std::ceil(p.b()), (int)clip.b());
    }

    // anti-aliased rounded rect (logical coords); optional vertical gradient to c2
    void roundRect(Rect lr, float radius, Color c, Color c2 = Color(-1, 0, 0, 0)) {
        Rect p = toPx(lr); float rad = radius * scale;
        int x0, y0, x1, y1; pixelBounds(p, x0, y0, x1, y1);
        bool grad = c2.r >= 0;
        for (int y = y0; y < y1; y++) {
            float fy = y + 0.5f;
            float t = grad ? std::clamp((fy - p.y) / p.h, 0.f, 1.f) : 0.f;
            Color cc = grad ? Color(c.r + (c2.r - c.r) * t, c.g + (c2.g - c.g) * t, c.b + (c2.b - c.b) * t, c.a + (c2.a - c.a) * t) : c;
            for (int x = x0; x < x1; x++) {
                float fx = x + 0.5f;
                float cov = coverageRR(fx, fy, p, rad);
                if (cov <= 0) continue;
                float a = cc.a * cov;
                blend(x, y, { cc.r * a, cc.g * a, cc.b * a, a });
            }
        }
    }
    static float coverageRR(float fx, float fy, const Rect& p, float rad) {
        float cx = std::clamp(fx, p.x + rad, p.r() - rad), cy = std::clamp(fy, p.y + rad, p.b() - rad);
        float dx = fx - cx, dy = fy - cy;
        float d = (rad > 0 ? std::sqrt(dx * dx + dy * dy) - rad : std::max(std::fabs(dx), std::fabs(dy)));
        // edges of a plain rect
        float ex = std::min(fx - p.x, p.r() - fx), ey = std::min(fy - p.y, p.b() - fy);
        float e = std::min(ex, ey);
        float cov = std::clamp(0.5f - d, 0.f, 1.f);
        return std::min(cov, std::clamp(e + 0.5f, 0.f, 1.f));
    }
    void fill(Rect lr, Color c) { roundRect(lr, 0, c); }
    // anti-aliased convex polygon (logical coords), 4x4 supersampling
    void polygon(const std::vector<std::pair<float, float>>& pts, Color c) {
        if (pts.size() < 3) return;
        float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
        std::vector<std::pair<float, float>> p;
        for (auto& q : pts) { p.push_back({ q.first * scale, q.second * scale }); minx = std::min(minx, p.back().first); maxx = std::max(maxx, p.back().first); miny = std::min(miny, p.back().second); maxy = std::max(maxy, p.back().second); }
        int x0, y0, x1, y1; pixelBounds(Rect(minx, miny, maxx - minx, maxy - miny), x0, y0, x1, y1);
        // orientation
        float area = 0; for (size_t i = 0; i < p.size(); i++) { auto& a = p[i]; auto& b = p[(i + 1) % p.size()]; area += a.first * b.second - b.first * a.second; }
        float sgn = area > 0 ? 1.f : -1.f;
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
            int n = 0;
            for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) {
                float fx = x + (sx + 0.5f) / 4, fy = y + (sy + 0.5f) / 4; bool in = true;
                for (size_t i = 0; i < p.size() && in; i++) {
                    auto& a = p[i]; auto& b = p[(i + 1) % p.size()];
                    if (sgn * ((b.first - a.first) * (fy - a.second) - (b.second - a.second) * (fx - a.first)) < 0) in = false;
                }
                n += in;
            }
            if (n) { float a = c.a * n / 16.f; blend(x, y, { c.r * a, c.g * a, c.b * a, a }); }
        }
    }
    // soft shadow / glow: alpha falls off over `blur` outside the rect
    void shadow(Rect lr, float radius, float blur, Color c) {
        Rect p = toPx(lr); float rad = radius * scale, bl = std::max(1.f, blur * scale);
        Rect q(p.x - bl, p.y - bl, p.w + 2 * bl, p.h + 2 * bl);
        int x0, y0, x1, y1; pixelBounds(q, x0, y0, x1, y1);
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
            float fx = x + 0.5f, fy = y + 0.5f;
            float cx = std::clamp(fx, p.x + rad, p.r() - rad), cy = std::clamp(fy, p.y + rad, p.b() - rad);
            float d = std::sqrt((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy)) - rad;
            float t = std::clamp(1.f - d / bl, 0.f, 1.f); t = t * t * (3 - 2 * t);
            float a = c.a * t; blend(x, y, { c.r * a, c.g * a, c.b * a, a });
        }
    }

    // draw image region src (image px) into dst (logical). opacity, grey (0..1 desaturate), bright mul
    void image(Image& im, Rect src, Rect dst, float opacity = 1.f, float grey = 0.f, float bright = 1.f, bool flipx = false) {
        if (!im.ok()) return;
        Rect p = toPx(dst);
        int x0, y0, x1, y1; pixelBounds(p, x0, y0, x1, y1);
        if (x1 <= x0 || y1 <= y0) return;
        float sx = src.w / p.w, sy = src.h / p.h;      // source px per dest px
        float minS = std::min(sx, sy);
        int lvl = 0; while (minS >= 2.f && lvl < 12) { minS /= 2; lvl++; }
        const Image* L = im.level(lvl);
        float fx = (float)L->w / im.w, fy = (float)L->h / im.h;
        bool bicubic = std::max(sx, sy) < 1.f;
        for (int y = y0; y < y1; y++) {
            float v = (src.y + ((y + 0.5f) - p.y) * sy) * fy - 0.5f;
            for (int x = x0; x < x1; x++) {
                float lx = ((x + 0.5f) - p.x);
                if (flipx) lx = p.w - lx;
                float u = (src.x + lx * sx) * fx - 0.5f;
                PF c = bicubic ? sampleCubic(*L, u, v) : sampleLinear(*L, u, v);
                // edge anti-aliasing of the destination rect
                float cov = std::clamp(std::min({ x + 1 - p.x, p.r() - x, y + 1 - p.y, p.b() - y }), 0.f, 1.f);
                float k = opacity * cov;
                if (grey > 0) { float l = c.r * 0.3f + c.g * 0.59f + c.b * 0.11f; c.r += (l - c.r) * grey; c.g += (l - c.g) * grey; c.b += (l - c.b) * grey; }
                c.r = std::min(c.a, c.r * bright); c.g = std::min(c.a, c.g * bright); c.b = std::min(c.a, c.b * bright);
                blend(x, y, { c.r * k, c.g * k, c.b * k, c.a * k });
            }
        }
    }
    void image(Image& im, Rect dst, float opacity = 1.f) { image(im, Rect(0, 0, (float)im.w, (float)im.h), dst, opacity); }
    // tile an image (logical tile size)
    void tile(Image& im, Rect dst, float tw, float th, float bright = 1.f) {
        for (float y = dst.y; y < dst.b(); y += th) for (float x = dst.x; x < dst.r(); x += tw) {
            float w = std::min(tw, dst.r() - x), h = std::min(th, dst.b() - y);
            image(im, Rect(0, 0, im.w * w / tw, im.h * h / th), Rect(x, y, w, h), 1.f, 0.f, bright);
        }
    }
    // 9-slice: border sizes in source px (l,t,r,b) and logical (dl,dt,dr,db)
    void nine(Image& im, Rect dst, float l, float t, float r, float b, float dl, float dt, float dr, float db, float opacity = 1.f, float grey = 0.f, float bright = 1.f) {
        float sx[4] = { 0, l, im.w - r, (float)im.w }, sy[4] = { 0, t, im.h - b, (float)im.h };
        float dx[4] = { dst.x, dst.x + dl, dst.r() - dr, dst.r() }, dy[4] = { dst.y, dst.y + dt, dst.b() - db, dst.b() };
        for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++)
            image(im, Rect(sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j]), Rect(dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j]), opacity, grey, bright);
    }

    static PF sampleLinear(const Image& im, float u, float v) {
        int x = (int)std::floor(u), y = (int)std::floor(v); float ax = u - x, ay = v - y;
        PF a = im.at(x, y), b = im.at(x + 1, y), c = im.at(x, y + 1), d = im.at(x + 1, y + 1);
        auto L = [&](float p, float q, float r, float s) { return (p * (1 - ax) + q * ax) * (1 - ay) + (r * (1 - ax) + s * ax) * ay; };
        return { L(a.r, b.r, c.r, d.r), L(a.g, b.g, c.g, d.g), L(a.b, b.b, c.b, d.b), L(a.a, b.a, c.a, d.a) };
    }
    static PF sampleCubic(const Image& im, float u, float v) {
        int x = (int)std::floor(u), y = (int)std::floor(v); float tx = u - x, ty = v - y;
        auto w4 = [](float t, float* w) {   // Catmull-Rom
            float t2 = t * t, t3 = t2 * t;
            w[0] = -0.5f * t3 + t2 - 0.5f * t; w[1] = 1.5f * t3 - 2.5f * t2 + 1;
            w[2] = -1.5f * t3 + 2 * t2 + 0.5f * t; w[3] = 0.5f * t3 - 0.5f * t2;
        };
        float wx[4], wy[4]; w4(tx, wx); w4(ty, wy);
        PF acc{ 0, 0, 0, 0 };
        for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) {
            PF c = im.at(x - 1 + i, y - 1 + j); float k = wx[i] * wy[j];
            acc.r += c.r * k; acc.g += c.g * k; acc.b += c.b * k; acc.a += c.a * k;
        }
        acc.a = std::clamp(acc.a, 0.f, 1.f);
        acc.r = std::clamp(acc.r, 0.f, acc.a); acc.g = std::clamp(acc.g, 0.f, acc.a); acc.b = std::clamp(acc.b, 0.f, acc.a);
        return acc;
    }

    // ---- text (size = logical pixel height of the em box)
    float textWidth(Font& f, const std::string& s, float size) {
        float px = size * scale, x = 0; auto cps = utf8(s);
        for (size_t i = 0; i < cps.size(); i++) { x += f.glyph(cps[i], px).adv; if (i + 1 < cps.size()) x += f.kern(cps[i], cps[i + 1], px); }
        return x / scale;
    }
    // draws text with its top-left at (x,y) logical; returns width
    float text(Font& f, const std::string& s, float x, float y, float size, Color c, Align al = LEFT, float maxw = 0) {
        float wdt = textWidth(f, s, size);
        if (al == CENTER) x -= (maxw > 0 ? -maxw / 2 + wdt / 2 : wdt / 2);
        else if (al == RIGHT) x -= (maxw > 0 ? -maxw + wdt : wdt);
        float px = size * scale, pen = x * scale, base = y * scale + f.ascent(px);
        auto cps = utf8(s);
        for (size_t i = 0; i < cps.size(); i++) {
            const Font::Glyph& g = f.glyph(cps[i], px);
            int gx = (int)std::lround(pen) + g.x0, gy = (int)std::lround(base) + g.y0;
            for (int yy = 0; yy < g.h; yy++) {
                int Y = gy + yy; if (Y < clip.y || Y >= clip.b()) continue;
                for (int xx = 0; xx < g.w; xx++) {
                    int X = gx + xx; if (X < clip.x || X >= clip.r()) continue;
                    float a = g.bm[(size_t)yy * g.w + xx] / 255.f * c.a;
                    if (a > 0) blend(X, Y, { c.r * a, c.g * a, c.b * a, a });
                }
            }
            pen += g.adv; if (i + 1 < cps.size()) pen += f.kern(cps[i], cps[i + 1], px);
        }
        return wdt;
    }
    // word-wrapped paragraph; returns height used
    float paragraph(Font& f, const std::string& s, float x, float y, float width, float size, Color c, float lineh = 1.3f, bool draw = true) {
        std::vector<std::string> words; std::string cur;
        for (char ch : s) { if (ch == ' ') { words.push_back(cur); cur.clear(); } else if (ch == '\n') { words.push_back(cur); words.push_back("\n"); cur.clear(); } else cur += ch; }
        words.push_back(cur);
        std::string line; float yy = y;
        auto flush = [&]() { if (draw) text(f, line, x, yy, size, c); yy += size * lineh; line.clear(); };
        for (auto& wd : words) {
            if (wd == "\n") { flush(); continue; }
            std::string t = line.empty() ? wd : line + " " + wd;
            if (!line.empty() && textWidth(f, t, size) > width) { flush(); line = wd; }
            else line = t;
        }
        if (!line.empty()) flush();
        return yy - y;
    }
};

}  // namespace gfx
