// Runtime font package for the menus and HUD: glyphs rasterised at the exact pixel size of the
// current screen height (no pre-baked size buckets, no scaling), 8-bit anti-aliased coverage,
// stored uncompressed (no DXT block artefacts on letter edges).
//
// Output is an Unreal package (v129) with fonts S<base>_<H>: base = the SAPFont sizes the game asks
// for at 768 lines, H = screen height. GUI.dll looks these names up before the native GDI font.
// Platform independent (tested on Linux); shrz.dll writes Textures\SHHDFonts.utx at start-up.
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "stb_truetype.h"

namespace fontgen {

static const int BASES[] = { 8, 10, 12, 14, 16, 18, 20, 24, 26, 30 };
static const int PAGE_W = 1024, MAX_PAGE_H = 2048;
static const char* VERSION_TAG = "SHRFONTS4";

inline std::vector<uint32_t> charset() {
    std::vector<uint32_t> c;
    for (uint32_t i = 32; i < 127; i++) c.push_back(i);
    for (uint32_t i = 0xA0; i < 0x100; i++) c.push_back(i);
    for (uint32_t i = 0x400; i < 0x460; i++) c.push_back(i);
    for (uint32_t u : { 0x2013u, 0x2014u, 0x2018u, 0x2019u, 0x201Au, 0x201Cu, 0x201Du, 0x201Eu, 0x2026u, 0x2022u, 0x2116u, 0x2122u, 0x20ACu })
        c.push_back(u);
    return c;
}

// ------------------------------------------------------------------ package writer
struct Writer {
    std::vector<std::string> names; std::map<std::string, int> nidx;
    struct Imp { int cpkg, cname, pkg, name; };
    struct Exp { int cls, pkg, name; uint32_t flags; std::vector<uint8_t> body; uint32_t off = 0; };
    std::vector<Imp> imps; std::vector<Exp> exps;
    int name(const std::string& s) { auto it = nidx.find(s); if (it != nidx.end()) return it->second; nidx[s] = (int)names.size(); names.push_back(s); return (int)names.size() - 1; }
    int imp(const char* cp, const char* cn, int pkg, const char* n) { imps.push_back({ name(cp), name(cn), pkg, name(n) }); return -(int)imps.size(); }
};
inline void ci(std::vector<uint8_t>& o, int32_t v) {                 // Unreal compact index
    uint32_t a = v < 0 ? (uint32_t)-v : (uint32_t)v;
    uint8_t b = (v < 0 ? 0x80 : 0) | (a & 0x3F); a >>= 6;
    if (a) b |= 0x40;
    o.push_back(b);
    while (a) { uint8_t c = a & 0x7F; a >>= 7; if (a) c |= 0x80; o.push_back(c); }
}
inline void u32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; i++) o.push_back((v >> (8 * i)) & 0xFF); }
inline void u16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(v & 0xFF); o.push_back(v >> 8); }

struct Glyph { int x, y, w, h, page; };
struct Font { std::string name; std::vector<Glyph> glyphs; std::vector<std::vector<uint8_t>> pages; std::vector<int> pageH; int line = 0; };

// rasterise one font: cells are advance x line height, the glyph drawn on the baseline
inline Font build(const stbtt_fontinfo& fi, int px, const std::vector<uint32_t>& cs, const std::string& name) {
    Font f; f.name = name;
    float sc = stbtt_ScaleForMappingEmToPixels(&fi, (float)px);
    int asc, desc, gap; stbtt_GetFontVMetrics(&fi, &asc, &desc, &gap);
    int ascent = (int)std::ceil(asc * sc), descent = (int)std::ceil(-desc * sc);
    f.line = ascent + descent;
    int x = 1, y = 1;
    auto newPage = [&]() { f.pages.emplace_back((size_t)PAGE_W * MAX_PAGE_H, 0); x = y = 1; };
    newPage();
    for (uint32_t ch : cs) {
        int g = stbtt_FindGlyphIndex(&fi, (int)ch);
        int adv, lsb; stbtt_GetGlyphHMetrics(&fi, g, &adv, &lsb);
        int w = (int)std::lround(adv * sc); if (w <= 0) w = std::max(1, px / 4);
        if (x + w + 1 > PAGE_W) { x = 1; y += f.line + 2; }
        if (y + f.line + 1 > MAX_PAGE_H) newPage();
        int x0, y0, x1, y1; stbtt_GetGlyphBitmapBox(&fi, g, sc, sc, &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;
        if (g && gw > 0 && gh > 0) {
            std::vector<uint8_t> bm((size_t)gw * gh);
            stbtt_MakeGlyphBitmap(&fi, bm.data(), gw, gh, gw, sc, sc, g);
            auto& pg = f.pages.back();
            for (int yy = 0; yy < gh; yy++) for (int xx = 0; xx < gw; xx++) {
                int dx = x + x0 + xx, dy = y + ascent + y0 + yy;
                if (dx < x || dx >= x + w + 1 || dy < y || dy >= y + f.line || dx >= PAGE_W) continue;   // stay in the cell (+1 px)
                uint8_t& p = pg[(size_t)dy * PAGE_W + dx]; p = std::max(p, bm[(size_t)yy * gw + xx]);
            }
        }
        f.glyphs.push_back({ x, y, w, f.line, (int)f.pages.size() - 1 });
        x += w + 2;
    }
    // trim every page to the smallest power of two that holds its glyphs
    for (size_t i = 0; i < f.pages.size(); i++) {
        int used = 1; for (auto& gl : f.glyphs) if (gl.page == (int)i) used = std::max(used, gl.y + gl.h + 1);
        int h = MAX_PAGE_H; while (h / 2 >= used) h /= 2;
        f.pages[i].resize((size_t)PAGE_W * h); f.pageH.push_back(h);
    }
    return f;
}

// texture export body: BGRA8 white + coverage alpha, full mip chain (box filter on coverage)
inline std::vector<uint8_t> textureBody(Writer& W, const std::vector<uint8_t>& a8, int w, int h, uint32_t fileOff) {
    std::vector<uint8_t> b;
    auto tagByte = [&](const char* n, uint8_t v) { ci(b, W.name(n)); b.push_back(0x01); b.push_back(v); };
    auto tagInt = [&](const char* n, int32_t v) { ci(b, W.name(n)); b.push_back(0x22); u32(b, (uint32_t)v); };
    auto tagBool = [&](const char* n, bool v) { ci(b, W.name(n)); b.push_back(v ? 0x83 : 0x03); };
    int ub = 0; while ((1 << ub) < w) ub++; int vb = 0; while ((1 << vb) < h) vb++;
    tagBool("bMasked", true);
    tagByte("Format", 5);                       // TEXF_RGBA8
    tagByte("UBits", (uint8_t)ub); tagByte("VBits", (uint8_t)vb);
    tagInt("USize", w); tagInt("VSize", h); tagInt("UClamp", w); tagInt("VClamp", h);
    ci(b, W.name("None"));
    std::vector<std::vector<uint8_t>> mips; std::vector<std::pair<int, int>> dims;
    std::vector<float> cur(a8.begin(), a8.end()); int cw = w, chh = h;
    while (true) {
        std::vector<uint8_t> px((size_t)cw * chh * 4);
        for (size_t i = 0; i < (size_t)cw * chh; i++) { px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = 255; px[i * 4 + 3] = (uint8_t)std::lround(std::min(255.f, cur[i])); }
        mips.push_back(std::move(px)); dims.push_back({ cw, chh });
        if (cw == 1 && chh == 1) break;
        int nw = std::max(1, cw / 2), nh = std::max(1, chh / 2);
        std::vector<float> nx((size_t)nw * nh);
        int sx = cw / nw, sy = chh / nh;
        for (int yy = 0; yy < nh; yy++) for (int xx = 0; xx < nw; xx++) {
            float s = 0; for (int j = 0; j < sy; j++) for (int i = 0; i < sx; i++) s += cur[(size_t)(yy * sy + j) * cw + xx * sx + i];
            nx[(size_t)yy * nw + xx] = s / (sx * sy);
        }
        cur.swap(nx); cw = nw; chh = nh;
    }
    ci(b, (int)mips.size());
    for (size_t m = 0; m < mips.size(); m++) {
        std::vector<uint8_t> cnt; ci(cnt, (int)mips[m].size());
        uint32_t skip = fileOff + (uint32_t)b.size() + 4 + (uint32_t)cnt.size() + (uint32_t)mips[m].size();
        u32(b, skip); b.insert(b.end(), cnt.begin(), cnt.end()); b.insert(b.end(), mips[m].begin(), mips[m].end());
        int mw = dims[m].first, mh = dims[m].second;
        u32(b, mw); u32(b, mh);
        int mub = 0; while ((1 << (mub + 1)) <= mw) mub++; int mvb = 0; while ((1 << (mvb + 1)) <= mh) mvb++;
        b.push_back((uint8_t)mub); b.push_back((uint8_t)mvb);
    }
    return b;
}

inline std::vector<uint8_t> fontBody(Writer& W, const Font& f, const std::vector<int>& texRefs, const std::vector<uint32_t>& cs) {
    std::vector<uint8_t> b; ci(b, W.name("None"));
    ci(b, (int)f.glyphs.size());
    for (auto& g : f.glyphs) { u32(b, g.x); u32(b, g.y); u32(b, g.w); u32(b, g.h); b.push_back((uint8_t)g.page); }
    ci(b, (int)texRefs.size()); for (int r : texRefs) ci(b, r);
    u32(b, 0);                                   // Kerning
    ci(b, (int)cs.size());                       // CharRemap: unicode -> glyph index
    for (size_t i = 0; i < cs.size(); i++) { u16(b, (uint16_t)cs[i]); u16(b, (uint16_t)i); }
    u32(b, 1);                                   // IsRemapped
    return b;
}

// the whole package for one screen height
inline std::vector<uint8_t> makePackage(const uint8_t* ttf, const std::vector<int>& heights) {
    stbtt_fontinfo fi; if (!stbtt_InitFont(&fi, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) return {};
    auto cs = charset();
    Writer W;
    for (const char* n : { "None", "bMasked", "Format", "UBits", "VBits", "USize", "VSize", "UClamp", "VClamp", VERSION_TAG }) W.name(n);
    for (int screenH : heights) W.name(std::string("SHRH") + std::to_string(screenH));
    W.imp("Core", "Package", 0, "Core");
    int eng = W.imp("Core", "Package", 0, "Engine");
    int clsFont = W.imp("Core", "Class", eng, "Font");
    int clsTex = W.imp("Core", "Class", eng, "Texture");
    struct Pending { int exportIdx; const Font* font; int page; std::vector<int> refs; };
    std::vector<Font> fonts;
    fonts.reserve(heights.size() * (sizeof(BASES) / sizeof(BASES[0])));
    for (int screenH : heights) for (int base : BASES) {
        int px = std::max(8, (int)std::lround(base * screenH / 768.0));
        fonts.push_back(build(fi, px, cs, "S" + std::to_string(base) + "_" + std::to_string(screenH)));
    }
    // export table: font, then its pages (pages are inside the font object)
    std::vector<std::vector<uint8_t>> bodies; std::vector<std::pair<int, int>> fontOfPage;   // (font idx, page) or (-1)
    struct E { int cls, pkg, name; uint32_t flags; int font, page; };
    std::vector<E> ex;
    for (size_t i = 0; i < fonts.size(); i++) {
        int fidx = (int)ex.size() + 1;
        ex.push_back({ clsFont, 0, W.name(fonts[i].name), 0x000F0004, (int)i, -1 });
        for (size_t p = 0; p < fonts[i].pages.size(); p++)
            ex.push_back({ clsTex, fidx, W.name(fonts[i].name + "_Page" + char('A' + p)), 0x00070000, (int)i, (int)p });
    }
    // layout: header, names, export bodies, imports, exports
    std::vector<uint8_t> out(64, 0);
    uint32_t nameOff = (uint32_t)out.size();
    for (auto& s : W.names) { ci(out, (int)s.size() + 1); out.insert(out.end(), s.begin(), s.end()); out.push_back(0); u32(out, 0x00070010); }
    std::vector<uint32_t> offs(ex.size()), sizes(ex.size());
    for (size_t i = 0; i < ex.size(); i++) {
        offs[i] = (uint32_t)out.size();
        std::vector<uint8_t> body;
        if (ex[i].page < 0) {
            std::vector<int> refs; for (size_t p = 0; p < fonts[ex[i].font].pages.size(); p++) refs.push_back((int)i + 2 + (int)p);
            body = fontBody(W, fonts[ex[i].font], refs, cs);
        } else {
            auto& f = fonts[ex[i].font];
            body = textureBody(W, f.pages[ex[i].page], PAGE_W, f.pageH[ex[i].page], offs[i]);
        }
        sizes[i] = (uint32_t)body.size();
        out.insert(out.end(), body.begin(), body.end());
    }
    // names may have grown while writing bodies: they were written already, so all names must exist up front
    uint32_t impOff = (uint32_t)out.size();
    for (auto& im : W.imps) { ci(out, im.cpkg); ci(out, im.cname); u32(out, (uint32_t)im.pkg); ci(out, im.name); }
    uint32_t expOff = (uint32_t)out.size();
    for (size_t i = 0; i < ex.size(); i++) {
        ci(out, ex[i].cls); ci(out, 0); u32(out, (uint32_t)ex[i].pkg); ci(out, ex[i].name);
        u32(out, ex[i].flags); ci(out, (int)sizes[i]); if (sizes[i]) ci(out, (int)offs[i]);
    }
    auto put = [&](size_t at, uint32_t v) { for (int k = 0; k < 4; k++) out[at + k] = (v >> (8 * k)) & 0xFF; };
    put(0, 0x9E2A83C1); out[4] = 129; out[5] = 0; out[6] = 0; out[7] = 0; put(8, 1);
    put(12, (uint32_t)W.names.size()); put(16, nameOff); put(20, (uint32_t)ex.size()); put(24, expOff);
    put(28, (uint32_t)W.imps.size()); put(32, impOff);
    const uint8_t guid[16] = { 'S','H','R','F','O','N','T','S','R','U','N','T','I','M','E','1' };
    memcpy(&out[36], guid, 16); put(52, 1); put(56, (uint32_t)ex.size()); put(60, (uint32_t)W.names.size());
    return out;
}

}  // namespace fontgen
