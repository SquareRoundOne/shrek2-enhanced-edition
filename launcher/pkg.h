// Read textures straight from the game's own packages (Unreal Engine 2, package v129).
// The launcher ships no game art: it decodes the menu textures from the player's Textures folder.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include "rgbcx.h"
#include "osio.h"

namespace upkg {

typedef std::vector<uint8_t> Bytes;

struct Rd {
    const Bytes& d; size_t p;
    Rd(const Bytes& d, size_t p) : d(d), p(p) {}
    uint8_t u8() { return d.at(p++); }
    uint16_t u16() { uint16_t v; memcpy(&v, &d.at(p + 1) - 1, 2); p += 2; return v; }
    uint32_t u32() { if (p + 4 > d.size()) throw 1; uint32_t v; memcpy(&v, &d[p], 4); p += 4; return v; }
    int32_t i32() { return (int32_t)u32(); }
    int32_t ci() {
        uint8_t b0 = u8(); int32_t v = b0 & 0x3f;
        if (b0 & 0x40) { int sh = 6; for (int i = 0; i < 4; i++) { uint8_t x = u8(); v |= (x & 0x7f) << sh; sh += 7; if (!(x & 0x80)) break; } }
        return (b0 & 0x80) ? -v : v;
    }
};

struct Exp { int32_t cls, super, pkg, name; uint32_t flags; int32_t size, off; };
struct Imp { int32_t cpkg, cname, pkg, name; };

struct Pkg {
    Bytes d; std::vector<std::string> names; std::vector<Imp> imps; std::vector<Exp> exps;
    bool load(const osio::Path& fn) {
        if (!osio::readFile(fn, d)) return false;
        return parse();
    }
    bool parse() {
        try {
            names.clear(); imps.clear(); exps.clear();
            if (d.size() < 64) return false;
            Rd r(d, 0);
            if (r.u32() != 0x9E2A83C1) return false;
            r.u16(); r.u16(); r.u32();
            uint32_t nc = r.u32(), no = r.u32(), ec = r.u32(), eo = r.u32(), ic = r.u32(), io = r.u32();
            r.p = no;
            for (uint32_t i = 0; i < nc; i++) { int n = r.ci(); std::string s((const char*)&d.at(r.p), n > 0 ? n - 1 : 0); r.p += n; r.u32(); names.push_back(s); }
            r.p = io;
            for (uint32_t i = 0; i < ic; i++) { Imp m; m.cpkg = r.ci(); m.cname = r.ci(); m.pkg = r.i32(); m.name = r.ci(); imps.push_back(m); }
            r.p = eo;
            for (uint32_t i = 0; i < ec; i++) {
                Exp e; e.cls = r.ci(); e.super = r.ci(); e.pkg = r.i32(); e.name = r.ci(); e.flags = r.u32(); e.size = r.ci(); e.off = e.size > 0 ? r.ci() : 0;
                exps.push_back(e);
            }
            return true;
        } catch (...) { return false; }
    }
    std::string objname(int32_t ref) const {
        if (ref > 0) return names.at(exps.at(ref - 1).name);
        if (ref < 0) return names.at(imps.at(-ref - 1).name);
        return "None";
    }
    std::string classname(const Exp& e) const { return e.cls ? objname(e.cls) : "Class"; }
};

enum { F_P8 = 0, F_DXT1 = 3, F_RGBA8 = 5, F_DXT3 = 7, F_DXT5 = 8 };

// DXT1/3/5 or BGRA8 pixels -> RGBA8
inline bool decodePixels(int fmt, const uint8_t* src, size_t msize, int w, int h, std::vector<uint8_t>& rgba) {
    rgba.assign((size_t)w * h * 4, 255);
    if (fmt == F_DXT1 || fmt == F_DXT3 || fmt == F_DXT5) {
        int bw = (w + 3) / 4, bh = (h + 3) / 4; uint8_t px[64];
        size_t need = (size_t)bw * bh * (fmt == F_DXT1 ? 8 : 16); if (need > msize) return false;
        for (int by = 0; by < bh; by++) for (int bx = 0; bx < bw; bx++) {
            if (fmt == F_DXT1) { rgbcx::unpack_bc1(src, px, true); src += 8; }
            else if (fmt == F_DXT5) { rgbcx::unpack_bc3(src, px); src += 16; }
            else { rgbcx::unpack_bc1(src + 8, px, true); for (int i = 0; i < 16; i++) px[i * 4 + 3] = (uint8_t)(((src[i / 2] >> ((i & 1) * 4)) & 15) * 17); src += 16; }
            for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
                int sx = bx * 4 + x, sy = by * 4 + y;
                if (sx < w && sy < h) memcpy(&rgba[((size_t)sy * w + sx) * 4], px + (y * 4 + x) * 4, 4);
            }
        }
        return true;
    }
    if (fmt == F_RGBA8) {
        if (msize < (size_t)w * h * 4) return false;
        for (size_t i = 0; i < (size_t)w * h; i++) { rgba[i * 4] = src[i * 4 + 2]; rgba[i * 4 + 1] = src[i * 4 + 1]; rgba[i * 4 + 2] = src[i * 4]; rgba[i * 4 + 3] = src[i * 4 + 3]; }
        return true;
    }
    return false;
}

// decode the top mip of texture `name` to RGBA8 (straight alpha)
// authW/authH: the authored size (UClamp/VClamp); an HD texture from the texture kit is larger
inline bool decodeTexture(const Pkg& P, const std::string& name, std::vector<uint8_t>& rgba, int& w, int& h, int* authW = nullptr, int* authH = nullptr) {
    try {
        for (const Exp& e : P.exps) {
            if (e.size <= 0 || P.names.at(e.name) != name || P.classname(e) != "Texture") continue;
            Rd r(P.d, e.off);
            std::map<std::string, int> ival; std::set<std::string> flags; int palette = 0;
            while (true) {
                int ni = r.ci(); const std::string& nm = P.names.at(ni);
                if (nm == "None") break;
                uint8_t info = r.u8(); int typ = info & 15, sz = (info >> 4) & 7; bool arr = info & 0x80;
                if (typ == 10) r.ci();
                int size = sz == 0 ? 1 : sz == 1 ? 2 : sz == 2 ? 4 : sz == 3 ? 12 : sz == 4 ? 16 : 0;
                if (sz == 5) size = r.u8(); else if (sz == 6) size = r.u16(); else if (sz == 7) size = r.u32();
                if (arr && typ != 3) { uint8_t b = r.u8(); if (b & 0x80) { if (b & 0x40) { r.u8(); r.u16(); } else r.u8(); } }
                size_t vp = r.p; r.p += size;
                if (typ == 3) { if (arr) flags.insert(nm); }
                else if (typ == 1) ival[nm] = P.d.at(vp);
                else if (typ == 2) { int32_t v; memcpy(&v, &P.d.at(vp), 4); ival[nm] = v; }
                else if (typ == 5 && nm == "Palette") { Rd q(P.d, vp); palette = q.ci(); }
            }
            int n = r.ci(); if (n <= 0) return false;
            r.i32(); int msize = r.ci(); size_t moff = r.p; r.p += msize; w = r.i32(); h = r.i32();
            if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || moff + msize > P.d.size()) return false;
            if (authW) *authW = ival.count("UClamp") ? ival["UClamp"] : w;
            if (authH) *authH = ival.count("VClamp") ? ival["VClamp"] : h;
            int fmt = ival.count("Format") ? ival["Format"] : 0;
            if (fmt != F_P8) return decodePixels(fmt, &P.d[moff], (size_t)msize, w, h, rgba);
            rgba.assign((size_t)w * h * 4, 255);
            const uint8_t* src = &P.d[moff];
            if (fmt == F_P8 && palette > 0) {
                if ((size_t)msize < (size_t)w * h) return false;
                const Exp& pe = P.exps.at(palette - 1); Rd q(P.d, pe.off);
                if (P.names.at(q.ci()) != "None") return false;
                int pn = q.ci(); uint8_t pal[256][4] = {};
                for (int i = 0; i < pn && i < 256; i++) { pal[i][2] = q.u8(); pal[i][1] = q.u8(); pal[i][0] = q.u8(); pal[i][3] = q.u8(); }
                bool masked = flags.count("bMasked") > 0;
                for (size_t i = 0; i < (size_t)w * h; i++) { memcpy(&rgba[i * 4], pal[src[i]], 4); if (masked && src[i] == 0) rgba[i * 4 + 3] = 0; }
                return true;
            }
            return false;
        }
    } catch (...) {}
    return false;
}

}  // namespace upkg
