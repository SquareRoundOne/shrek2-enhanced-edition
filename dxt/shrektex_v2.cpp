// ShrekTex: extract world textures of Shrek 2 (UE2, package v129) to PNG for AI upscaling,
// and pack the upscaled PNGs back into texture packages (new mips, DXT1/DXT5/RGBA8).
//
//   ShrekTex extract <TexturesDir> <WorkDir> <pkg> [pkg...]
//       WorkDir/lo/<pkg>__<tex>.png        colour, wrap-padded by PAD texels on each side
//       WorkDir/lo/<pkg>__<tex>__a.png     alpha as grey (only when the texture has alpha)
//       WorkDir/manifest.txt
//   ShrekTex pack <TexturesDir> <WorkDir> <OutDir>
//       reads WorkDir/hi/<same names>.png (4x of lo), writes OutDir/<pkg>.utx
//
// Only USize/VSize/UBits/VBits/UClamp/VClamp/Format change in each texture; names and
// references stay the same, so maps and meshes keep working unchanged.
#include "bc7enc_rdo/rgbcx.h"
#include "bc7enc_rdo/lodepng.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <algorithm>
#include <filesystem>
#include <cctype>

using std::string; using std::vector;
typedef vector<uint8_t> Bytes;

static bool readfile(const string& fn, Bytes& d) {
    FILE* f = fopen(fn.c_str(), "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    d.resize(n); bool ok = fread(d.data(), 1, n, f) == (size_t)n; fclose(f); return ok;
}
static bool writefile(const string& fn, const Bytes& d) {
    FILE* f = fopen(fn.c_str(), "wb"); if (!f) return false;
    bool ok = fwrite(d.data(), 1, d.size(), f) == d.size(); fclose(f); return ok;
}

// ------------------------------------------------------------------ package
struct Rd {
    const Bytes& d; size_t p;
    Rd(const Bytes& d, size_t p) : d(d), p(p) {}
    uint8_t u8() { return d.at(p++); }
    uint16_t u16() { uint16_t v; memcpy(&v, &d.at(p), 2); p += 2; return v; }
    uint32_t u32() { uint32_t v; memcpy(&v, &d.at(p), 4); p += 4; return v; }
    int32_t i32() { return (int32_t)u32(); }
    int32_t ci() {
        uint8_t b0 = u8(); int32_t v = b0 & 0x3f;
        if (b0 & 0x40) { int sh = 6; for (int i = 0; i < 4; i++) { uint8_t x = u8(); v |= (x & 0x7f) << sh; sh += 7; if (!(x & 0x80)) break; } }
        return (b0 & 0x80) ? -v : v;
    }
};
static void put_ci(Bytes& o, int32_t v) {
    bool neg = v < 0; uint32_t a = neg ? -v : v;
    uint8_t b0 = (a & 0x3f) | (neg ? 0x80 : 0); a >>= 6; if (a) b0 |= 0x40; o.push_back(b0);
    while (a) { uint8_t b = a & 0x7f; a >>= 7; if (a) b |= 0x80; o.push_back(b); }
}
static void put_i32(Bytes& o, int32_t v) { uint8_t b[4]; memcpy(b, &v, 4); o.insert(o.end(), b, b + 4); }

struct Imp { int32_t cpkg, cname, pkg, name; };
struct Exp { int32_t cls, super, pkg, name; uint32_t flags; int32_t size, off; int idx; };
struct Pkg {
    Bytes d; vector<string> names; vector<Imp> imps; vector<Exp> exps;
    uint32_t nameCount, nameOff, expCount, expOff, impCount, impOff;
    bool load(const string& fn) {
        if (!readfile(fn, d) || d.size() < 64) return false;
        Rd r(d, 0);
        if (r.u32() != 0x9E2A83C1) return false;
        r.u16(); r.u16(); r.u32();
        nameCount = r.u32(); nameOff = r.u32(); expCount = r.u32(); expOff = r.u32(); impCount = r.u32(); impOff = r.u32();
        r.p = nameOff;
        for (uint32_t i = 0; i < nameCount; i++) { int n = r.ci(); string s((const char*)&d[r.p], n > 0 ? n - 1 : 0); r.p += n; r.u32(); names.push_back(s); }
        r.p = impOff;
        for (uint32_t i = 0; i < impCount; i++) { Imp m; m.cpkg = r.ci(); m.cname = r.ci(); m.pkg = r.i32(); m.name = r.ci(); imps.push_back(m); }
        r.p = expOff;
        for (uint32_t i = 0; i < expCount; i++) {
            Exp e; e.cls = r.ci(); e.super = r.ci(); e.pkg = r.i32(); e.name = r.ci(); e.flags = r.u32(); e.size = r.ci(); e.off = e.size > 0 ? r.ci() : 0; e.idx = i + 1;
            exps.push_back(e);
        }
        return true;
    }
    string objname(int32_t ref) const { if (ref > 0) return names[exps[ref - 1].name]; if (ref < 0) return names[imps[-ref - 1].name]; return "None"; }
    string classname(const Exp& e) const { return e.cls ? objname(e.cls) : "Class"; }
    int nameidx(const string& s) const { for (size_t i = 0; i < names.size(); i++) if (names[i] == s) return (int)i; return -1; }
};

// ------------------------------------------------------------------ texture
enum { F_P8 = 0, F_DXT1 = 3, F_RGBA8 = 5, F_DXT3 = 7, F_DXT5 = 8 };
struct Mip { size_t off; int size, w, h; };
struct Tex {
    std::map<string, int> ival; std::set<string> flags; int palette = 0;
    size_t props_end; vector<Mip> mips; bool tail = false;
};
static bool read_tex(const Pkg& P, const Exp& e, Tex& t) {
    Rd r(P.d, e.off);
    while (true) {
        int ni = r.ci(); if (ni < 0 || ni >= (int)P.names.size()) return false;
        const string& nm = P.names[ni];
        if (nm == "None") break;
        uint8_t info = r.u8(); int typ = info & 15, sz = (info >> 4) & 7; bool arr = info & 0x80;
        if (typ == 10) r.ci();
        int size = sz == 0 ? 1 : sz == 1 ? 2 : sz == 2 ? 4 : sz == 3 ? 12 : sz == 4 ? 16 : 0;
        if (sz == 5) size = r.u8(); else if (sz == 6) size = r.u16(); else if (sz == 7) size = r.u32();
        if (arr && typ != 3) { uint8_t b = r.u8(); if (b & 0x80) { if (b & 0x40) { r.u8(); r.u16(); } else r.u8(); } }
        size_t vp = r.p; r.p += size;
        if (typ == 3) { if (arr) t.flags.insert(nm); }
        else if (typ == 1) t.ival[nm] = P.d[vp];
        else if (typ == 2) { int32_t v; memcpy(&v, &P.d[vp], 4); t.ival[nm] = v; }
        else if (typ == 5 && nm == "Palette") { Rd q(P.d, vp); t.palette = q.ci(); }
    }
    t.props_end = r.p;
    int n = r.ci();
    for (int i = 0; i < n; i++) {
        r.i32(); Mip m; m.size = r.ci(); m.off = r.p; r.p += m.size; m.w = r.i32(); m.h = r.i32(); r.u8(); r.u8();
        t.mips.push_back(m);
    }
    t.tail = r.p != (size_t)(e.off + e.size);
    return true;
}
static bool decode_tex(const Pkg& P, const Tex& t, vector<uint8_t>& rgba, int& w, int& h) {
    int fmt = t.ival.count("Format") ? t.ival.at("Format") : 0;
    const Mip& m = t.mips[0]; w = m.w; h = m.h; rgba.assign((size_t)w * h * 4, 255);
    const uint8_t* src = &P.d[m.off];
    if (fmt == F_DXT1 || fmt == F_DXT3 || fmt == F_DXT5) {
        int bw = (w + 3) / 4, bh = (h + 3) / 4; uint8_t px[64];
        for (int by = 0; by < bh; by++) for (int bx = 0; bx < bw; bx++) {
            if (fmt == F_DXT1) { rgbcx::unpack_bc1(src, px, true); src += 8; }
            else if (fmt == F_DXT5) { rgbcx::unpack_bc3(src, px); src += 16; }
            else { rgbcx::unpack_bc1(src + 8, px, true); for (int i = 0; i < 16; i++) px[i * 4 + 3] = (uint8_t)(((src[i / 2] >> ((i & 1) * 4)) & 15) * 17); src += 16; }
            for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
                int sx = bx * 4 + x, sy = by * 4 + y; if (sx < w && sy < h) memcpy(&rgba[((size_t)sy * w + sx) * 4], px + (y * 4 + x) * 4, 4);
            }
        }
        return true;
    }
    if (fmt == F_RGBA8) {
        for (size_t i = 0; i < (size_t)w * h; i++) { rgba[i * 4] = src[i * 4 + 2]; rgba[i * 4 + 1] = src[i * 4 + 1]; rgba[i * 4 + 2] = src[i * 4]; rgba[i * 4 + 3] = src[i * 4 + 3]; }
        return true;
    }
    if (fmt == F_P8 && t.palette > 0) {
        const Exp& pe = P.exps[t.palette - 1]; Rd r(P.d, pe.off);
        if (P.names[r.ci()] != "None") return false;
        int n = r.ci(); uint8_t pal[256][4] = {};
        for (int i = 0; i < n && i < 256; i++) { pal[i][2] = r.u8(); pal[i][1] = r.u8(); pal[i][0] = r.u8(); pal[i][3] = r.u8(); }
        bool masked = t.flags.count("bMasked");
        for (size_t i = 0; i < (size_t)w * h; i++) { memcpy(&rgba[i * 4], pal[src[i]], 4); if (masked && src[i] == 0) rgba[i * 4 + 3] = 0; }
        return true;
    }
    return false;
}

static bool pow2(int v) { return v > 0 && !(v & (v - 1)); }
static bool eligible(const Pkg& P, const Exp& e, Tex& t) {
    if (P.classname(e) != "Texture" || e.size <= 0) return false;
    if (!read_tex(P, e, t) || t.mips.empty() || t.tail) return false;
    int fmt = t.ival.count("Format") ? t.ival.at("Format") : 0;
    if (!(fmt == F_DXT1 || fmt == F_DXT3 || fmt == F_DXT5 || fmt == F_RGBA8 || (fmt == F_P8 && t.palette > 0))) return false;
    int w = t.mips[0].w, h = t.mips[0].h;
    return std::min(w, h) >= 16 && pow2(w) && pow2(h) && std::max(w, h) <= 1024;
}

static int pad_for(int w, int h) { return std::max(4, std::min(16, std::min(w, h) / 4)); }

// ------------------------------------------------------------------ cubemaps
// A UE2 Cubemap points to six ordinary textures (Faces[0..5] = +X,-X,+Y,-Y,+Z,-Z, Direct3D layout).
// Faces of one cube must share size and format, and a dedicated face set needs padding taken from
// the neighbouring faces (not wrapped from its own opposite edge) or the reflections get seams.
struct Cube { string pkg, name; string fpkg[6], ftex[6]; };
typedef std::pair<string, string> TexKey;   // (package, texture)

static string lower(string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

static vector<Cube> scan_cubes(const string& texdir, const vector<string>& pkgs) {
    vector<Cube> out;
    for (const string& pn : pkgs) {
        Pkg P; if (!P.load(texdir + "/" + pn + ".utx")) continue;
        for (const Exp& e : P.exps) {
            if (e.size <= 0 || P.classname(e) != "Cubemap") continue;
            Cube c; c.pkg = pn; c.name = P.names[e.name]; int found = 0;
            Rd r(P.d, e.off);
            try {
                while (true) {
                    const string& nm = P.names.at(r.ci());
                    if (nm == "None") break;
                    uint8_t info = r.u8(); int typ = info & 15, sz = (info >> 4) & 7; bool arr = info & 0x80;
                    if (typ == 10) r.ci();
                    int size = sz == 0 ? 1 : sz == 1 ? 2 : sz == 2 ? 4 : sz == 3 ? 12 : sz == 4 ? 16 : 0;
                    if (sz == 5) size = r.u8(); else if (sz == 6) size = r.u16(); else if (sz == 7) size = r.u32();
                    int idx = 0;
                    if (arr && typ != 3) {
                        uint8_t b = r.u8();
                        if (b & 0x80) { if (b & 0x40) { idx = ((b & 0x3f) << 24) | (r.u8() << 16); idx |= r.u16(); } else idx = ((b & 0x7f) << 8) | r.u8(); }
                        else idx = b;
                    }
                    size_t vp = r.p; r.p += size;
                    if (nm == "Faces" && idx >= 0 && idx < 6) {
                        Rd q(P.d, vp); int32_t ref = q.ci(); if (!ref) continue;
                        string fp = pn;
                        if (ref < 0) {   // imported: walk the outers up to the package
                            int32_t o = P.imps[-ref - 1].pkg;
                            while (o) { fp = P.objname(o); o = o < 0 ? P.imps[-o - 1].pkg : P.exps[o - 1].pkg; }
                        }
                        c.fpkg[idx] = fp; c.ftex[idx] = P.objname(ref); found++;
                    }
                }
            } catch (...) { continue; }
            if (found == 6) out.push_back(c);
        }
    }
    return out;
}

// Direct3D cube mapping
static void face_dir(int f, float s, float t, float d[3]) {
    switch (f) {
    case 0: d[0] = 1; d[1] = -t; d[2] = -s; break;
    case 1: d[0] = -1; d[1] = -t; d[2] = s; break;
    case 2: d[0] = s; d[1] = 1; d[2] = t; break;
    case 3: d[0] = s; d[1] = -1; d[2] = -t; break;
    case 4: d[0] = s; d[1] = -t; d[2] = 1; break;
    default: d[0] = -s; d[1] = -t; d[2] = -1; break;
    }
}
static void dir_face(const float d[3], int& f, float& s, float& t) {
    float ax = fabsf(d[0]), ay = fabsf(d[1]), az = fabsf(d[2]), sc, tc, ma;
    if (ax >= ay && ax >= az) { ma = ax; if (d[0] > 0) { f = 0; sc = -d[2]; tc = -d[1]; } else { f = 1; sc = d[2]; tc = -d[1]; } }
    else if (ay >= az) { ma = ay; if (d[1] > 0) { f = 2; sc = d[0]; tc = d[2]; } else { f = 3; sc = d[0]; tc = -d[2]; } }
    else { ma = az; if (d[2] > 0) { f = 4; sc = d[0]; tc = -d[1]; } else { f = 5; sc = -d[0]; tc = -d[1]; } }
    s = (sc / ma + 1) * 0.5f; t = (tc / ma + 1) * 0.5f;
}

static bool same_file(const string& fn, const vector<uint8_t>& png) { Bytes d; return readfile(fn, d) && d == png; }

static int cmd_extract(const string& texdir, const string& work, const vector<string>& pkgs) {
    string lo = work + "/lo"; FILE* man = fopen((work + "/manifest.txt").c_str(), "w");
    // dedicated cube face sets (six different textures): pad from the neighbouring faces
    vector<Cube> cubes = scan_cubes(texdir, pkgs);
    std::map<TexKey, std::pair<int, int>> cubeFace;   // face -> (cube index, face number)
    for (size_t i = 0; i < cubes.size(); i++) {
        std::set<TexKey> u; for (int f = 0; f < 6; f++) u.insert({ lower(cubes[i].fpkg[f]), lower(cubes[i].ftex[f]) });
        if (u.size() == 6) for (int f = 0; f < 6; f++) cubeFace[{ lower(cubes[i].fpkg[f]), lower(cubes[i].ftex[f]) }] = { (int)i, f };
    }
    std::map<int, vector<vector<uint8_t>>> cubePix; std::map<int, int> cubeSize;
    for (auto& kv : cubeFace) {
        int ci = kv.second.first; if (cubePix.count(ci)) continue;
        vector<vector<uint8_t>> faces(6); int N = -1; bool ok = true;
        for (int f = 0; f < 6 && ok; f++) {
            Pkg P; ok = P.load(texdir + "/" + cubes[ci].fpkg[f] + ".utx"); if (!ok) break; ok = false;
            for (const Exp& e : P.exps) {
                if (lower(P.names[e.name]) != lower(cubes[ci].ftex[f])) continue;
                Tex t; if (!eligible(P, e, t)) continue;
                int w, h; if (!decode_tex(P, t, faces[f], w, h) || w != h || (N >= 0 && N != w)) break;
                N = w; ok = true; break;
            }
        }
        if (ok) { cubePix[ci] = faces; cubeSize[ci] = N; printf("cubemap %s.%s: six faces padded across edges\n", cubes[ci].pkg.c_str(), cubes[ci].name.c_str()); }
    }
    string redo = work + "/lo_redo"; int redone = 0;
    std::error_code mkec; std::filesystem::create_directories(redo, mkec);
    if (!man) { fprintf(stderr, "cannot write %s/manifest.txt (does %s/lo exist?)\n", work.c_str(), work.c_str()); return 1; }
    int total = 0;
    for (const string& pn : pkgs) {
        Pkg P; if (!P.load(texdir + "/" + pn + ".utx")) { fprintf(stderr, "skip %s (not found)\n", pn.c_str()); continue; }
        int n = 0;
        for (const Exp& e : P.exps) {
            Tex t; if (!eligible(P, e, t)) continue;
            vector<uint8_t> rgba; int w, h; if (!decode_tex(P, t, rgba, w, h)) continue;
            bool alpha = false; for (size_t i = 0; i < (size_t)w * h; i++) if (rgba[i * 4 + 3] < 255) { alpha = true; break; }
            int pad = pad_for(w, h), W = w + 2 * pad, H = h + 2 * pad;
            // Cut-out cards (trees on the horizon, foliage, signs) have a transparent border. Wrapping
            // them for the upscaler put the opposite edge (a tree trunk's bottom) next to the top
            // edge, and the upscale smeared opaque bits into the clear border: thin stripes above the
            // trees in game. Such an edge is extended (clamped) instead; tiling textures still wrap.
            auto clearRun = [&](int x0, int y0, int dx, int dy, int n) {
                int clear = 0; for (int i = 0; i < n; i++) if (rgba[((size_t)(y0 + dy * i) * w + (x0 + dx * i)) * 4 + 3] < 16) clear++;
                return alpha && clear * 10 >= n * 9;
            };
            bool clL = clearRun(0, 0, 0, 1, h), clR = clearRun(w - 1, 0, 0, 1, h), clT = clearRun(0, 0, 1, 0, w), clB = clearRun(0, h - 1, 1, 0, w);
            vector<uint8_t> c((size_t)W * H * 3), a((size_t)W * H);
            auto cf = cubeFace.find({ lower(pn), lower(P.names[e.name]) });
            bool cube = cf != cubeFace.end() && cubePix.count(cf->second.first) && cubeSize[cf->second.first] == w;
            for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
                const uint8_t* s;
                if (cube && (x < pad || y < pad || x >= w + pad || y >= h + pad)) {
                    // direction through this padded texel, looked up in the neighbouring face
                    float d[3]; face_dir(cf->second.second, ((x - pad) + 0.5f) / w * 2 - 1, ((y - pad) + 0.5f) / h * 2 - 1, d);
                    int nf; float ns, nt; dir_face(d, nf, ns, nt);
                    int px = std::min(w - 1, std::max(0, (int)(ns * w))), py = std::min(h - 1, std::max(0, (int)(nt * h)));
                    s = &cubePix[cf->second.first][nf][((size_t)py * w + px) * 4];
                } else {
                    int ux = x - pad, uy = y - pad;
                    int sx = (ux < 0 && clL) ? 0 : (ux >= w && clR) ? w - 1 : ((ux % w) + w) % w;
                    int sy = (uy < 0 && clT) ? 0 : (uy >= h && clB) ? h - 1 : ((uy % h) + h) % h;
                    s = &rgba[((size_t)sy * w + sx) * 4];
                }
                memcpy(&c[((size_t)y * W + x) * 3], s, 3); a[(size_t)y * W + x] = s[3];
            }
            string base = pn + "__" + P.names[e.name];
            vector<uint8_t> png, apng; lodepng::encode(png, c, W, H, LCT_RGB, 8);
            if (alpha) lodepng::encode(apng, a, W, H, LCT_GREY, 8);
            string fn = lo + "/" + base + ".png", afn = lo + "/" + base + "__a.png";
            bool cOld = !same_file(fn, png), aOld = alpha && !same_file(afn, apng);
            if (cOld || aOld) {
                // an older extract wrote different pixels: both upscales of this texture are stale
                Bytes old; bool existed = readfile(fn, old);
                writefile(fn, png); if (alpha) writefile(afn, apng);
                if (existed) {
                    remove((work + "/hi/" + base + ".png").c_str()); remove((work + "/hi/" + base + "__a.png").c_str());
                    writefile(redo + "/" + base + ".png", png); if (alpha) writefile(redo + "/" + base + "__a.png", apng);
                    redone++;
                }
            }
            fprintf(man, "%s %s %d %d %d %d\n", pn.c_str(), P.names[e.name].c_str(), w, h, pad, alpha ? 1 : 0);
            n++;
        }
        printf("%-26s %4d textures\n", pn.c_str(), n); fflush(stdout); total += n;
    }
    fclose(man);
    printf("extracted %d textures\n", total);
    if (redone) printf("%d images changed since the last extract; their old upscales were removed.\n"
                       "Upscale them again (they are also copied to lo_redo) and put the results into hi.\n", redone);
    return 0;
}

// ------------------------------------------------------------------ pack
static inline float s2l(float v) { return powf(v, 2.2f); }
static inline float l2s(float v) { return powf(v < 0 ? 0 : v > 1 ? 1 : v, 1.f / 2.2f); }

static bool load_png(const string& fn, vector<uint8_t>& px, unsigned& w, unsigned& h, LodePNGColorType ct) {
    return lodepng::decode(px, w, h, fn, ct, 8) == 0;
}

struct Built { Bytes props; vector<Bytes> mips; vector<std::pair<int, int>> sizes; };

static Bytes patch_props(const Pkg& P, const Exp& e, const std::map<string, std::pair<int, int>>& nv /* name -> (isbyte, value) */) {
    Rd r(P.d, e.off); Bytes out; std::set<string> seen;
    while (true) {
        size_t st = r.p; int ni = r.ci(); const string& nm = P.names[ni];
        if (nm == "None") {
            for (auto& kv : nv) {
                if (seen.count(kv.first)) continue;
                int idx = P.nameidx(kv.first); if (idx < 0) { fprintf(stderr, "name %s missing\n", kv.first.c_str()); exit(2); }
                put_ci(out, idx);
                if (kv.second.first) { out.push_back(0x01); out.push_back((uint8_t)kv.second.second); }
                else { out.push_back(0x02); put_i32(out, kv.second.second); }
            }
            out.insert(out.end(), P.d.begin() + st, P.d.begin() + r.p);
            return out;
        }
        uint8_t info = r.u8(); int typ = info & 15, sz = (info >> 4) & 7; bool arr = info & 0x80;
        if (typ == 10) r.ci();
        int size = sz == 0 ? 1 : sz == 1 ? 2 : sz == 2 ? 4 : sz == 3 ? 12 : sz == 4 ? 16 : 0;
        if (sz == 5) size = r.u8(); else if (sz == 6) size = r.u16(); else if (sz == 7) size = r.u32();
        if (arr && typ != 3) { uint8_t b = r.u8(); if (b & 0x80) { if (b & 0x40) { r.u8(); r.u16(); } else r.u8(); } }
        out.insert(out.end(), P.d.begin() + st, P.d.begin() + r.p);
        auto it = nv.find(nm);
        if (it != nv.end() && (typ == 1 || typ == 2)) {
            seen.insert(nm);
            if (typ == 1) out.push_back((uint8_t)it->second.second); else put_i32(out, it->second.second);
        } else out.insert(out.end(), P.d.begin() + r.p, P.d.begin() + r.p + size);
        r.p += size;
    }
}

static float coverage(const vector<float>& a, float thr) { size_t c = 0; for (float v : a) if (v > thr) c++; return a.empty() ? 0 : (float)c / a.size(); }

struct Force { int factor = 0; int fmt = -1; };   // per cube face: shared factor, original format kept

static bool build_tex(const Pkg& P, const Exp& e, const Tex& t, const string& work, const string& pn,
                      int w, int h, int pad, bool alpha, Built& B, const Force* force = nullptr) {
    string base = work + "/hi/" + pn + "__" + P.names[e.name];
    vector<uint8_t> c, a; unsigned cw, ch, aw = 0, ah = 0;
    if (!load_png(base + ".png", c, cw, ch, LCT_RGB)) { fprintf(stderr, "  missing %s.png\n", base.c_str()); return false; }
    int s = (int)cw / (w + 2 * pad);
    if (s < 2 || (int)cw != s * (w + 2 * pad) || (int)ch != s * (h + 2 * pad)) { fprintf(stderr, "  unexpected size %s\n", base.c_str()); return false; }
    if (alpha) {
        vector<uint8_t> a3;
        if (!load_png(base + "__a.png", a3, aw, ah, LCT_RGB)) { fprintf(stderr, "  missing alpha %s\n", base.c_str()); return false; }
        a.resize((size_t)aw * ah);
        for (size_t i = 0; i < a.size(); i++) a[i] = (uint8_t)((a3[i * 3] + a3[i * 3 + 1] + a3[i * 3 + 2] + 1) / 3);
    }
    if (alpha && (aw != cw || ah != ch)) { fprintf(stderr, "  alpha size mismatch %s\n", base.c_str()); return false; }
    int fmt = t.ival.count("Format") ? t.ival.at("Format") : 0;
    // target factor: x4, or x2 for textures >= 512 (cap 2048)
    int factor = std::max(w, h) >= 512 ? 2 : 4; if (factor > s) factor = s;
    while (std::max(w, h) * factor > 2048) factor /= 2;
    if (force && force->factor) { if (force->factor > s) { fprintf(stderr, "  cube face %s upscaled less than its neighbours\n", base.c_str()); return false; } factor = force->factor; }
    int W = w * factor, H = h * factor, k = s / factor;   // box-downsample k x k from the network output
    vector<float> img((size_t)W * H * 4);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float acc[4] = {0, 0, 0, 0};
        for (int j = 0; j < k; j++) for (int i = 0; i < k; i++) {
            size_t sx = (size_t)(pad * s + x * k + i), sy = (size_t)(pad * s + y * k + j);
            const uint8_t* p = &c[(sy * cw + sx) * 3];
            acc[0] += s2l(p[0] / 255.f); acc[1] += s2l(p[1] / 255.f); acc[2] += s2l(p[2] / 255.f);
            acc[3] += alpha ? a[sy * cw + sx] / 255.f : 1.f;
        }
        float* o = &img[((size_t)y * W + x) * 4]; float inv = 1.f / (k * k);
        for (int q = 0; q < 4; q++) o[q] = acc[q] * inv;   // linear colour, linear alpha
    }
    bool masked = t.flags.count("bMasked") && alpha;
    int nf = fmt == F_RGBA8 ? F_RGBA8 : ((alpha || t.flags.count("bAlphaTexture") || t.flags.count("bMasked")) ? F_DXT5 : F_DXT1);
    if (force && force->fmt >= 0) nf = force->fmt;   // all faces of a cube keep one format
    float cov0 = 0; if (masked) { vector<float> al((size_t)W * H); for (size_t i = 0; i < al.size(); i++) al[i] = img[i * 4 + 3]; cov0 = coverage(al, 0.5f); }
    int mw = W, mh = H; vector<float> cur = img;
    while (true) {
        vector<float> al((size_t)mw * mh); for (size_t i = 0; i < al.size(); i++) al[i] = cur[i * 4 + 3];
        float scale = 1.f;
        if (masked && cov0 > 0 && !B.mips.empty()) {
            float lo = 0, hi = 4;
            for (int it = 0; it < 16; it++) { float m = (lo + hi) / 2; vector<float> t2(al); for (float& v : t2) v = std::min(1.f, v * m); if (coverage(t2, 0.5f) < cov0) lo = m; else hi = m; }
            scale = hi;
        }
        vector<uint8_t> px((size_t)mw * mh * 4);
        for (size_t i = 0; i < (size_t)mw * mh; i++) {
            for (int q = 0; q < 3; q++) px[i * 4 + q] = (uint8_t)std::lround(l2s(cur[i * 4 + q]) * 255.f);
            px[i * 4 + 3] = (uint8_t)std::lround(std::min(1.f, std::max(0.f, al[i] * scale)) * 255.f);
        }
        Bytes enc;
        if (nf == F_RGBA8) { enc.resize(px.size()); for (size_t i = 0; i < (size_t)mw * mh; i++) { enc[i * 4] = px[i * 4 + 2]; enc[i * 4 + 1] = px[i * 4 + 1]; enc[i * 4 + 2] = px[i * 4]; enc[i * 4 + 3] = px[i * 4 + 3]; } }
        else {
            int bw = (mw + 3) / 4, bh = (mh + 3) / 4, bs = nf == F_DXT1 ? 8 : 16; enc.resize((size_t)bw * bh * bs);
            uint8_t blk[64];
            for (int by = 0; by < bh; by++) for (int bx = 0; bx < bw; bx++) {
                for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
                    int sx = std::min(bx * 4 + x, mw - 1), sy = std::min(by * 4 + y, mh - 1);
                    memcpy(blk + (y * 4 + x) * 4, &px[((size_t)sy * mw + sx) * 4], 4);
                }
                uint8_t* dst = &enc[((size_t)by * bw + bx) * bs];
                if (nf == F_DXT1) rgbcx::encode_bc1(12, dst, blk, false, false); else rgbcx::encode_bc3_hq(12, dst, blk);
            }
        }
        B.mips.push_back(enc); B.sizes.push_back({mw, mh});
        if (mw == 1 && mh == 1) break;
        int nw = std::max(1, mw / 2), nh = std::max(1, mh / 2), fx = mw / nw, fy = mh / nh;
        vector<float> nx((size_t)nw * nh * 4, 0.f);
        for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) for (int j = 0; j < fy; j++) for (int i = 0; i < fx; i++)
            for (int q = 0; q < 4; q++) nx[((size_t)y * nw + x) * 4 + q] += cur[((size_t)(y * fy + j) * mw + (x * fx + i)) * 4 + q] / (fx * fy);
        cur.swap(nx); mw = nw; mh = nh;
    }
    auto bits = [](int v) { int b = 0; while ((1 << b) < v) b++; return b; };
    std::map<string, std::pair<int, int>> nv = {
        {"USize", {0, W}}, {"VSize", {0, H}}, {"UClamp", {0, w}}, {"VClamp", {0, h}},   // UClamp/VClamp keep the authored size: the patched Engine.dll uses it as MaterialUSize/VSize (BSP, terrain and canvas UVs)
        {"UBits", {1, bits(W)}}, {"VBits", {1, bits(H)}}, {"Format", {1, nf}}};
    B.props = patch_props(P, e, nv);
    return true;
}

static Bytes texture_body(const Built& B, size_t file_off) {
    Bytes b = B.props; put_ci(b, (int)B.mips.size());
    for (size_t i = 0; i < B.mips.size(); i++) {
        Bytes cnt; put_ci(cnt, (int)B.mips[i].size());
        int32_t skip = (int32_t)(file_off + b.size() + 4 + cnt.size() + B.mips[i].size());
        put_i32(b, skip); b.insert(b.end(), cnt.begin(), cnt.end()); b.insert(b.end(), B.mips[i].begin(), B.mips[i].end());
        put_i32(b, B.sizes[i].first); put_i32(b, B.sizes[i].second);
        auto bl = [](int v) { int r = 0; while (v > 1) { v >>= 1; r++; } return r; };
        b.push_back((uint8_t)bl(B.sizes[i].first)); b.push_back((uint8_t)bl(B.sizes[i].second));
    }
    return b;
}

// The original bytes stay where they are; only the new texture bodies are appended, followed by
// copies of the import and export tables (the header points at the copies). Nothing that is not a
// texture moves, so static meshes and other objects with absolute offsets inside (lazy arrays)
// load exactly as before. (Earlier versions repacked the whole file and had to guess those offsets;
// a missed one broke e.g. Beanstalk.StaticEnviroment.fence_1: "Bad import index".)
static Bytes rebuild(const Pkg& P, const std::map<int, Built>& nb) {
    vector<Exp> ex = P.exps;
    Bytes out(P.d.begin(), P.d.end());
    for (auto& e : ex) {
        auto it = nb.find(e.idx);
        if (it == nb.end()) continue;
        Bytes body = texture_body(it->second, out.size());
        e.off = (int32_t)out.size(); e.size = (int32_t)body.size();
        out.insert(out.end(), body.begin(), body.end());
    }
    uint32_t io = (uint32_t)out.size();
    for (const Imp& m : P.imps) { put_ci(out, m.cpkg); put_ci(out, m.cname); put_i32(out, m.pkg); put_ci(out, m.name); }
    uint32_t eo = (uint32_t)out.size();
    for (const Exp& e : ex) {
        put_ci(out, e.cls); put_ci(out, e.super); put_i32(out, e.pkg); put_ci(out, e.name);
        put_i32(out, (int32_t)e.flags); put_ci(out, e.size); if (e.size > 0) put_ci(out, e.off);
    }
    // header: export offset (word 7) and import offset (word 9)
    memcpy(&out[24], &eo, 4); memcpy(&out[32], &io, 4);
    return out;
}

static int cmd_pack(const string& texdir, const string& work, const string& outdir) {
    FILE* man = fopen((work + "/manifest.txt").c_str(), "r"); if (!man) { fprintf(stderr, "no manifest\n"); return 1; }
    std::map<string, vector<std::tuple<string, int, int, int, int>>> todo; vector<string> order;
    char pn[256], tn[256]; int w, h, pad, al;
    while (fscanf(man, "%255s %255s %d %d %d %d", pn, tn, &w, &h, &pad, &al) == 6) {
        if (!todo.count(pn)) order.push_back(pn);
        todo[pn].push_back(std::make_tuple(string(tn), w, h, pad, al));
    }
    fclose(man);
    // cube faces: every face of a cube gets the same factor and keeps its original format family;
    // if any face of a cube has no usable upscale, the whole cube keeps its original faces
    std::map<TexKey, Force> force; std::set<TexKey> keep;
    {
        vector<Cube> cubes = scan_cubes(texdir, order);
        std::map<TexKey, std::tuple<int, int, int>> man_of;   // (w, h, pad)
        for (auto& p : order) for (auto& tt : todo[p]) man_of[{ lower(p), lower(std::get<0>(tt)) }] = std::make_tuple(std::get<1>(tt), std::get<2>(tt), std::get<3>(tt));
        for (auto& c : cubes) {
            int fac = 1 << 30; bool ok = true; int fmtv = -1;
            for (int f = 0; f < 6; f++) {
                TexKey k{ lower(c.fpkg[f]), lower(c.ftex[f]) };
                auto m = man_of.find(k);
                if (m == man_of.end()) { ok = false; break; }
                int w = std::get<0>(m->second), h = std::get<1>(m->second), pad = std::get<2>(m->second);
                unsigned cw = 0, ch = 0; vector<uint8_t> tmp;
                string fn = work + "/hi/" + c.fpkg[f] + "__" + c.ftex[f] + ".png";
                // exact case from the manifest
                for (auto& p : order) if (lower(p) == k.first) for (auto& tt : todo[p]) if (lower(std::get<0>(tt)) == k.second) fn = work + "/hi/" + p + "__" + std::get<0>(tt) + ".png";
                if (!load_png(fn, tmp, cw, ch, LCT_RGB)) { ok = false; break; }
                int sc = (int)cw / (w + 2 * pad); if (sc < 2) { ok = false; break; }
                int target = std::max(w, h) >= 512 ? 2 : 4; while (std::max(w, h) * target > 2048) target /= 2;
                fac = std::min(fac, std::min(sc, target));
                // original format of the face
                Pkg P; if (!P.load(texdir + "/" + c.fpkg[f] + ".utx")) { ok = false; break; }
                for (const Exp& e : P.exps) if (lower(P.names[e.name]) == k.second) { Tex t; if (eligible(P, e, t)) {
                    int of = t.ival.count("Format") ? t.ival.at("Format") : 0;
                    int ff = of == F_RGBA8 ? F_RGBA8 : of == F_DXT1 ? F_DXT1 : F_DXT5;
                    if (fmtv < 0) fmtv = ff; else if (fmtv != ff) fmtv = std::max(fmtv, ff); } break; }
            }
            for (int f = 0; f < 6; f++) {
                TexKey k{ lower(c.fpkg[f]), lower(c.ftex[f]) };
                if (!ok) { keep.insert(k); continue; }
                Force& fo = force[k];
                fo.factor = fo.factor ? std::min(fo.factor, fac) : fac;
                fo.fmt = fo.fmt < 0 ? fmtv : std::max(fo.fmt, fmtv);
            }
            printf("cubemap %s.%s: %s\n", c.pkg.c_str(), c.name.c_str(), ok ? "faces packed with one size and format" : "kept original (a face has no upscale)");
        }
        for (auto& k : keep) force.erase(k);
    }
    int bad = 0;
    for (const string& p : order) {
        Pkg P; if (!P.load(texdir + "/" + p + ".utx")) { fprintf(stderr, "cannot read %s\n", p.c_str()); bad++; continue; }
        std::map<int, Built> nb; int n = 0;
        for (auto& tt : todo[p]) {
            const string& name = std::get<0>(tt);
            for (const Exp& e : P.exps) {
                if (P.names[e.name] != name) continue;
                Tex t; if (!eligible(P, e, t)) continue;
                if (t.mips[0].w != std::get<1>(tt) || t.mips[0].h != std::get<2>(tt)) continue;
                TexKey key{ lower(p), lower(name) };
                if (keep.count(key)) break;
                Built B; auto fo = force.find(key);
                if (build_tex(P, e, t, work, p, std::get<1>(tt), std::get<2>(tt), std::get<3>(tt), std::get<4>(tt) != 0, B, fo == force.end() ? nullptr : &fo->second)) { nb[e.idx] = std::move(B); n++; }
                else bad++;
                break;
            }
        }
        Bytes out = rebuild(P, nb);
        if (!writefile(outdir + "/" + p + ".utx", out)) { fprintf(stderr, "cannot write %s\n", p.c_str()); return 1; }
        printf("%-26s %4d textures  %6.1f -> %6.1f MB\n", p.c_str(), n, P.d.size() / 1e6, out.size() / 1e6); fflush(stdout);
    }
    if (bad) { printf("%d textures were skipped (see messages above)\n", bad); }
    return 0;
}

// list every texture of the packages: format, size, whether it is upscaled and why not
static int cmd_list(const string& texdir, const vector<string>& pkgs) {
    for (const string& pn : pkgs) {
        Pkg P; if (!P.load(texdir + "/" + pn + ".utx")) { printf("%s: not found\n", pn.c_str()); continue; }
        for (const Exp& e : P.exps) {
            if (P.classname(e) != "Texture" || e.size <= 0) continue;
            Tex t; bool rd = read_tex(P, e, t);
            int fmt = rd && t.ival.count("Format") ? t.ival.at("Format") : -1;
            int w = rd && !t.mips.empty() ? t.mips[0].w : 0, h = rd && !t.mips.empty() ? t.mips[0].h : 0;
            Tex t2; bool el = eligible(P, e, t2);
            printf("%s\t%s\t%d\t%d\t%d\t%s%s\n", pn.c_str(), P.names[e.name].c_str(), fmt, w, h, el ? "yes" : "no",
                   !rd ? " (unreadable)" : t.tail ? " (tail)" : "");
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
    if (argc >= 4 && !strcmp(argv[1], "list")) {
        vector<string> pk; for (int i = 3; i < argc; i++) pk.push_back(argv[i]);
        return cmd_list(argv[2], pk);
    }
    if (argc >= 5 && !strcmp(argv[1], "extract")) {
        vector<string> pk; for (int i = 4; i < argc; i++) pk.push_back(argv[i]);
        return cmd_extract(argv[2], argv[3], pk);
    }
    if (argc == 5 && !strcmp(argv[1], "pack")) return cmd_pack(argv[2], argv[3], argv[4]);
    fprintf(stderr, "usage:\n  ShrekTex extract <TexturesDir> <WorkDir> <pkg>...\n  ShrekTex pack <TexturesDir> <WorkDir> <OutDir>\n");
    return 1;
}
