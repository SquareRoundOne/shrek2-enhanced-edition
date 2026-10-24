// Language assets: subtitles (original 2005 translation / new translation / English), voice packs
// (Russian / English), the localized textures (signs, posters, HUD) and the FGM commercial.
//
// Everything is switched on the player's own files and can be reverted:
//  * text: System\HpDialog.Rus / BumpDialog.Rus are copied from lang\text_orig or lang\text_new,
//    Default.ini Language=Rus|int;
//  * voices: the original Sounds\AllDialog.Rus_uax is moved to lang\voice_ru_orig.uax; the engine
//    loads AllDialog.Rus_uax (Language=Rus) or AllDialog.uax (int), so both names become hard links
//    to the chosen pack (lang\voice_ru.uax = the original plus the one line it lacks, or voice_en.uax);
//  * textures: the English bodies of the localized Texture exports are appended to the package and
//    a new export table is written after them. The original bytes stay untouched in front, so
//    switching back is "truncate to the old length and restore the 64-byte header";
//  * Movies\FGMcommercial*.bik follow the voice (the commercial has its own sound track).
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include "osio.h"
#include "ini.h"

namespace lang {

typedef std::vector<uint8_t> Bytes;
using osio::Path;

inline std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// ------------------------------------------------------------------ binary helpers
inline void putCI(Bytes& o, int32_t v) {
    uint32_t a = v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
    uint8_t b0 = (uint8_t)((v < 0 ? 0x80 : 0) | (a & 0x3f)); a >>= 6;
    if (a) b0 |= 0x40;
    o.push_back(b0);
    while (a) { uint8_t b = a & 0x7f; a >>= 7; if (a) b |= 0x80; o.push_back(b); }
}
inline void putU32(Bytes& o, uint32_t v) { for (int i = 0; i < 4; i++) o.push_back((uint8_t)(v >> (i * 8))); }
inline void setU32(Bytes& o, size_t p, uint32_t v) { for (int i = 0; i < 4; i++) o.at(p + i) = (uint8_t)(v >> (i * 8)); }
inline uint32_t getU32(const Bytes& o, size_t p) { if (p + 4 > o.size()) throw 1; uint32_t v; memcpy(&v, &o[p], 4); return v; }

struct Rd {
    const Bytes& d; size_t p;
    Rd(const Bytes& d, size_t p) : d(d), p(p) {}
    uint8_t u8() { if (p >= d.size()) throw 1; return d[p++]; }
    uint16_t u16() { uint16_t v = u8(); v |= (uint16_t)u8() << 8; return v; }
    uint32_t u32() { uint32_t v = getU32(d, p); p += 4; return v; }
    int32_t i32() { return (int32_t)u32(); }
    int32_t ci() {
        uint8_t b0 = u8(); int32_t v = b0 & 0x3f;
        if (b0 & 0x40) { int sh = 6; for (int i = 0; i < 4; i++) { uint8_t x = u8(); v |= (int32_t)(x & 0x7f) << sh; sh += 7; if (!(x & 0x80)) break; } }
        return (b0 & 0x80) ? -v : v;
    }
    std::string str() { uint16_t n = u16(); if (p + n > d.size()) throw 1; std::string s((const char*)&d[p], n); p += n; return s; }
    void bytes(Bytes& out, size_t n) { if (p + n > d.size()) throw 1; out.assign(d.begin() + p, d.begin() + p + n); p += n; }
};

// ------------------------------------------------------------------ package tables
struct Package {
    struct Imp { int32_t cpkg, cname, pkg, name; };
    struct Exp { int32_t cls, super, pkg, name; uint32_t flags; int32_t size, off; };
    Bytes d;
    std::vector<std::string> names; std::vector<uint32_t> nflags;
    std::vector<Imp> imps; std::vector<Exp> exps;
    bool namesDirty = false;
    std::map<std::string, int> nameIdx;

    bool parse() {
        try {
            if (d.size() < 64 || getU32(d, 0) != 0x9E2A83C1u) return false;
            Rd r(d, 12);
            uint32_t nc = r.u32(), no = r.u32(), ec = r.u32(), eo = r.u32(), ic = r.u32(), io = r.u32();
            names.clear(); nflags.clear(); imps.clear(); exps.clear(); nameIdx.clear();
            r.p = no;
            for (uint32_t i = 0; i < nc; i++) {
                int n = r.ci(); if (n < 0 || r.p + n > d.size()) return false;
                std::string s((const char*)&d[r.p], n > 0 ? n - 1 : 0); r.p += n;
                nflags.push_back(r.u32()); names.push_back(s);
                nameIdx.emplace(lower(s), (int)i);
            }
            r.p = io;
            for (uint32_t i = 0; i < ic; i++) { Imp m; m.cpkg = r.ci(); m.cname = r.ci(); m.pkg = r.i32(); m.name = r.ci(); imps.push_back(m); }
            r.p = eo;
            for (uint32_t i = 0; i < ec; i++) {
                Exp e; e.cls = r.ci(); e.super = r.ci(); e.pkg = r.i32(); e.name = r.ci(); e.flags = r.u32();
                e.size = r.ci(); e.off = e.size > 0 ? r.ci() : 0; exps.push_back(e);
            }
            return true;
        } catch (...) { return false; }
    }
    int findName(const std::string& s) const { auto it = nameIdx.find(lower(s)); return it == nameIdx.end() ? -1 : it->second; }
    int name(const std::string& s) {
        int i = findName(s); if (i >= 0) return i;
        names.push_back(s); nflags.push_back(nflags.empty() ? 0x00070010u : nflags.back());
        nameIdx.emplace(lower(s), (int)names.size() - 1); namesDirty = true;
        return (int)names.size() - 1;
    }
    std::string objName(int32_t ref) const {
        if (ref > 0 && ref <= (int)exps.size()) return names.at(exps[ref - 1].name);
        if (ref < 0 && -ref <= (int)imps.size()) return names.at(imps[-ref - 1].name);
        return "None";
    }
    std::string fullName(int32_t ref) const {
        std::string s; int guard = 0;
        while (ref && guard++ < 64) {
            std::string n = objName(ref); s = s.empty() ? n : n + "." + s;
            ref = ref > 0 ? exps[ref - 1].pkg : imps[-ref - 1].pkg;
        }
        return s;
    }
    std::string className(const Exp& e) const { return e.cls ? objName(e.cls) : "Class"; }
    int32_t findExport(const std::string& full, const std::string& cls) const {
        std::string lf = lower(full), lc = lower(cls);
        for (size_t i = 0; i < exps.size(); i++)
            if (lower(className(exps[i])) == lc && lower(fullName((int32_t)i + 1)) == lf) return (int32_t)i + 1;
        return 0;
    }
    int32_t findImport(const std::string& nm) const {
        for (size_t i = 0; i < imps.size(); i++) if (lower(names.at(imps[i].name)) == lower(nm)) return -(int32_t)i - 1;
        return 0;
    }
    // append the (changed) tables after everything else and point the header at them
    void writeTables() {
        if (namesDirty) {
            setU32(d, 16, (uint32_t)d.size()); setU32(d, 12, (uint32_t)names.size());
            for (size_t i = 0; i < names.size(); i++) {
                putCI(d, (int32_t)names[i].size() + 1);
                d.insert(d.end(), names[i].begin(), names[i].end()); d.push_back(0);
                putU32(d, nflags[i]);
            }
            namesDirty = false;
        }
        setU32(d, 24, (uint32_t)d.size()); setU32(d, 20, (uint32_t)exps.size());
        for (auto& e : exps) {
            putCI(d, e.cls); putCI(d, e.super); putU32(d, (uint32_t)e.pkg); putCI(d, e.name); putU32(d, e.flags);
            putCI(d, e.size); if (e.size > 0) putCI(d, e.off);
        }
        uint32_t gc = getU32(d, 52);
        if (gc > 0 && gc < 1000 && 56 + 8 * gc <= getU32(d, 16)) {
            setU32(d, 56 + 8 * (gc - 1), (uint32_t)exps.size());
            setU32(d, 56 + 8 * (gc - 1) + 4, (uint32_t)names.size());
        }
    }
};

inline bool writeAtomic(const Path& p, const Bytes& d) {
    Path tmp = p; tmp += ".shrtmp";
    if (!osio::writeFile(tmp, d)) { osio::remove(tmp); return false; }
    osio::makeWritable(p);
    std::error_code ec; osio::fs::rename(tmp, p, ec);
    if (ec) { osio::remove(tmp); return false; }
    return true;
}
inline bool moveFile(const Path& a, const Path& b) {
    std::error_code ec; osio::fs::rename(a, b, ec); if (!ec) return true;
    if (!osio::copyFile(a, b)) return false;
    osio::remove(a); return true;
}
inline bool sameFile(const Path& a, const Path& b) {
    std::error_code ec; if (osio::fs::equivalent(a, b, ec) && !ec) return true;
    int64_t sa = osio::size(a), sb = osio::size(b);
    if (sa < 0 || sa != sb) return false;
    Bytes x, y; return osio::readFile(a, x) && osio::readFile(b, y) && x == y;
}

// ------------------------------------------------------------------ texture overlay
struct TexEnt {
    struct Prop { std::string name, structName; uint8_t info = 0; Bytes extra; uint8_t kind = 0; Bytes raw; std::string ref; };
    std::string full; std::vector<Prop> props; Bytes blob; std::vector<std::pair<uint32_t, uint32_t>> skips;
};
struct TexPatch {
    std::vector<std::pair<std::string, std::vector<TexEnt>>> pkgs;   // relative path ("Textures/X.utx")
    bool load(const Path& fn) {
        Bytes d; if (!osio::readFile(fn, d) || d.size() < 12 || memcmp(d.data(), "SHRLTEX1", 8)) return false;
        try {
            Rd r(d, 8); uint32_t np = r.u32();
            for (uint32_t i = 0; i < np; i++) {
                std::pair<std::string, std::vector<TexEnt>> pk; pk.first = r.str();
                uint32_t ne = r.u32();
                for (uint32_t j = 0; j < ne; j++) {
                    TexEnt e; e.full = r.str(); uint32_t npr = r.u32();
                    for (uint32_t k = 0; k < npr; k++) {
                        TexEnt::Prop p; p.name = r.str(); p.info = r.u8(); p.structName = r.str();
                        uint16_t nx = r.u16(); r.bytes(p.extra, nx);
                        p.kind = r.u8();
                        if (p.kind == 0) { uint32_t n = r.u32(); r.bytes(p.raw, n); } else p.ref = r.str();
                        e.props.push_back(p);
                    }
                    uint32_t bl = r.u32(); r.bytes(e.blob, bl);
                    uint32_t ns = r.u32();
                    for (uint32_t k = 0; k < ns; k++) { uint32_t a = r.u32(), b = r.u32(); e.skips.push_back({ a, b }); }
                    pk.second.push_back(std::move(e));
                }
                pkgs.push_back(std::move(pk));
            }
            return true;
        } catch (...) { pkgs.clear(); return false; }
    }
};

// skip the tagged properties of an object body; returns the position after "None"
inline size_t skipProps(const Package& P, size_t p) {
    Rd r(P.d, p);
    for (int guard = 0; guard < 4096; guard++) {
        int ni = r.ci(); if (ni < 0 || ni >= (int)P.names.size()) throw 2;
        if (lower(P.names[ni]) == "none") return r.p;
        uint8_t info = r.u8(); int typ = info & 15, sz = (info >> 4) & 7; bool arr = info & 0x80;
        if (typ == 10) r.ci();
        uint32_t size = sz == 0 ? 1 : sz == 1 ? 2 : sz == 2 ? 4 : sz == 3 ? 12 : sz == 4 ? 16 : 0;
        if (sz == 5) size = r.u8(); else if (sz == 6) size = r.u16(); else if (sz == 7) size = r.u32();
        if (typ == 3) continue;
        if (arr) { uint8_t b = r.u8(); if (b & 0x80) { if (b & 0x40) { r.u8(); r.u16(); } else r.u8(); } }
        r.p += size;
    }
    throw 3;
}
// the mip section with the absolute lazy-array offsets zeroed (so two bodies can be compared)
inline Bytes normalizedMips(const Package& P, size_t p) {
    Rd r(P.d, p); int n = r.ci(); std::vector<size_t> sk;
    if (n < 0 || n > 20) throw 4;
    for (int i = 0; i < n; i++) {
        sk.push_back(r.p); r.i32(); int cnt = r.ci(); if (cnt < 0) throw 5; r.p += cnt;
        r.i32(); r.i32(); r.u8(); r.u8();
    }
    if (r.p > P.d.size()) throw 6;
    Bytes b(P.d.begin() + p, P.d.begin() + r.p);
    for (size_t s : sk) memset(&b[s - p], 0, 4);
    return b;
}

struct Overlay {
    // state of the packages we appended to: rel -> "base size header-hex"
    struct Rec { uint64_t base = 0, size = 0; Bytes hdr; };
    static bool parseRec(const std::string& v, Rec& r) {
        unsigned long long b = 0, s = 0; char hex[200] = {};
        if (sscanf(v.c_str(), "%llu %llu %199s", &b, &s, hex) != 3 || strlen(hex) != 128) return false;
        r.base = b; r.size = s; r.hdr.clear();
        for (int i = 0; i < 64; i++) { unsigned x; if (sscanf(hex + i * 2, "%2x", &x) != 1) return false; r.hdr.push_back((uint8_t)x); }
        return true;
    }
    static std::string fmtRec(const Rec& r) {
        char b[64]; snprintf(b, sizeof b, "%llu %llu ", (unsigned long long)r.base, (unsigned long long)r.size);
        std::string s = b; for (uint8_t x : r.hdr) { char h[3]; snprintf(h, 3, "%02x", x); s += h; }
        return s;
    }

    // Bring one package to "original" (ents == nullptr) or "original + these textures".
    // Returns false with err on failure; the file is either fully updated or untouched.
    static bool apply(const Path& root, const std::string& rel, const std::vector<TexEnt>* ents, ini::File& st, std::string& err, int* applied = nullptr, bool force = false) {
        Path f = root / osio::u8path(rel);
        if (applied) *applied = 0;
        if (!osio::exists(f)) { st.remove("Overlay", rel); return true; }
        {   // cheap checks first: most calls find everything as it should be
            Rec q; std::string qv = st.get("Overlay", rel, "");
            bool valid = !qv.empty() && parseRec(qv, q) && (int64_t)q.size == osio::size(f);
            if (qv.empty() && (!ents || ents->empty())) return true;
            if (valid && ents && !ents->empty() && !force) return true;
        }
        Package P; if (!osio::readFile(f, P.d)) { err = "не читается " + rel; return false; }
        bool reverted = false;
        Rec rec; std::string rv = st.get("Overlay", rel, "");
        if (!rv.empty()) {
            if (parseRec(rv, rec) && rec.size == P.d.size() && rec.base < P.d.size() && rec.base >= 64) {
                P.d.resize((size_t)rec.base); memcpy(P.d.data(), rec.hdr.data(), 64); reverted = true;
            }
            st.remove("Overlay", rel);      // stale (the file was replaced) or reverted now
        }
        auto finishRevert = [&]() -> bool {
            if (reverted && !writeAtomic(f, P.d)) { err = "не записывается " + rel; return false; }
            return true;
        };
        if (!ents || ents->empty()) return finishRevert();
        if (!P.parse()) { err = "не разбирается " + rel; return false; }
        Rec nr; nr.base = P.d.size(); nr.hdr.assign(P.d.begin(), P.d.begin() + 64);
        int changed = 0;
        try {
            for (const TexEnt& e : *ents) {
                int32_t idx = P.findExport(e.full, "Texture");
                if (!idx) continue;
                Package::Exp& x = P.exps[idx - 1];
                if (x.size <= 0) continue;
                try {
                    size_t mp = skipProps(P, (size_t)x.off);
                    if (normalizedMips(P, mp) == e.blob) continue;          // already this picture
                } catch (...) {}
                Bytes body;
                for (const auto& pr : e.props) {
                    putCI(body, P.name(pr.name)); body.push_back(pr.info);
                    if ((pr.info & 15) == 10) putCI(body, P.name(pr.structName));
                    body.insert(body.end(), pr.extra.begin(), pr.extra.end());
                    if (pr.kind == 0) body.insert(body.end(), pr.raw.begin(), pr.raw.end());
                    else throw 7;    // the patch builder only emits plain values
                }
                putCI(body, P.name("None"));
                size_t ms = body.size(), abs0 = P.d.size();
                body.insert(body.end(), e.blob.begin(), e.blob.end());
                for (auto& s : e.skips) setU32(body, ms + s.first, (uint32_t)(abs0 + ms + s.second));
                P.d.insert(P.d.end(), body.begin(), body.end());
                x.off = (int32_t)abs0; x.size = (int32_t)body.size();
                changed++;
            }
        } catch (...) { err = "ошибка в данных текстур для " + rel; return false; }
        if (applied) *applied = changed;
        if (!changed) {
            P.d.resize((size_t)nr.base);
            return finishRevert();
        }
        P.writeTables();
        if (!writeAtomic(f, P.d)) { err = "не записывается " + rel; return false; }
        nr.size = P.d.size();
        st.set("Overlay", rel, fmtRec(nr));
        return true;
    }
};

// ------------------------------------------------------------------ voice packs with the missing lines
// Lines the game asks for that one voice pack lacks and the other has: the recording is appended as
// a new export. Russian lacks Shrek's "Whoa" when the giant gingerbread man appears and two cries of
// the peasants ("Run!", "An ogre!" - their Russian text exists); English lacks two other peasant lines.
static const char* RU_ADD[] = { "pc_shk_ShrekEscPrison_2", "pc_pst_bumpline_1172", "pc_pst_bumpline_264" };
static const char* EN_ADD[] = { "pc_pst_bumpline_1087", "pc_pst_bumpline_289" };
// appends to R every line of `list` that R lacks and E has; returns how many, -1 on an error
inline int appendSounds(Package& R, Package& E, const char* const* list, size_t n) {
    int added = 0;
    try {
        for (size_t i = 0; i < n; i++) {
            std::string line = list[i];
            if (R.findExport(line, "Sound")) continue;
            int32_t ei = E.findExport(line, "Sound"); if (!ei) continue;
            const Package::Exp& x = E.exps[ei - 1];
            Rd r(E.d, (size_t)x.off);
            if (lower(E.names.at(r.ci())) != "none") throw 1;
            std::string ft = E.names.at(r.ci());
            r.i32(); int cnt = r.ci(); if (cnt < 0) throw 2;
            size_t dataPos = r.p, end = (size_t)x.off + (size_t)x.size;
            if (dataPos + cnt > end || end > E.d.size()) throw 3;
            int32_t snd = R.findImport("Sound"); if (!snd) throw 4;
            Bytes body; putCI(body, R.name("None")); putCI(body, R.name(ft));
            size_t skipAt = body.size(); putU32(body, 0); putCI(body, cnt);
            body.insert(body.end(), E.d.begin() + dataPos, E.d.begin() + dataPos + cnt);
            size_t abs0 = R.d.size();
            setU32(body, skipAt, (uint32_t)(abs0 + body.size()));
            body.insert(body.end(), E.d.begin() + dataPos + cnt, E.d.begin() + end);
            Package::Exp ne{ snd, 0, 0, R.name(line), x.flags, (int32_t)body.size(), (int32_t)abs0 };
            R.d.insert(R.d.end(), body.begin(), body.end());
            R.exps.push_back(ne);
            added++;
        }
        if (added) R.writeTables();
    } catch (...) { return -1; }
    return added;
}
inline bool buildRuVoice(const Path& orig, const Path& en, const Path& out, std::string& err) {
    Package R, E;
    if (!osio::readFile(orig, R.d) || !R.parse()) { err = "не читается русская озвучка"; return false; }
    if (!osio::readFile(en, E.d) || !E.parse()) { err = "не читается английская озвучка"; return false; }
    if (appendSounds(R, E, RU_ADD, sizeof RU_ADD / sizeof *RU_ADD) < 0) { err = "не удалось добавить реплику в озвучку"; return false; }
    Bytes().swap(E.d);
    if (!writeAtomic(out, R.d)) { err = "не записывается " + osio::u8str(out.filename()); return false; }
    return true;
}
// a pack already in place (also an HD one) gets the lines it lacks; false only on an error
inline bool addMissingLines(const Path& target, const Path& src, const char* const* list, size_t n, std::string& err) {
    if (!osio::exists(target) || !osio::exists(src)) return true;
    Package R, E;
    if (!osio::readFile(target, R.d) || !R.parse()) { err = "не читается " + osio::u8str(target.filename()); return false; }
    bool need = false;
    for (size_t i = 0; i < n; i++) if (!R.findExport(list[i], "Sound")) need = true;
    if (!need) return true;
    if (!osio::readFile(src, E.d) || !E.parse()) { err = "не читается " + osio::u8str(src.filename()); return false; }
    int a = appendSounds(R, E, list, n);
    if (a < 0) { err = "не удалось добавить реплики в " + osio::u8str(target.filename()); return false; }
    if (a == 0) return true;
    Bytes().swap(E.d);
    if (!writeAtomic(target, R.d)) { err = "не записывается " + osio::u8str(target.filename()); return false; }
    return true;
}

// ------------------------------------------------------------------ the whole switch
enum Text { TEXT_RU = 0, TEXT_RU_NEW = 1, TEXT_EN = 2 };
enum Voice { VOICE_RU = 0, VOICE_EN = 1 };

static const char* TEXT_FILES[] = { "HpDialog.Rus", "BumpDialog.Rus" };
static const char* MOVIES[] = { "FGMcommercial.bik", "FGMcommercial1024.bik", "FGMcommercial512.bik", "FGMcommercial640.bik" };

struct Manager {
    Path root, sys, L;
    std::function<bool(bool english)> setEngineLanguage;   // Default.ini etc.: Language=int|Rus

    void init(const Path& r) { root = r; sys = r / "System"; L = sys / "ShrekRemaster" / "lang"; }
    Path statePath() const { return L / "state.ini"; }
    bool configured() const { return osio::exists(statePath()); }
    bool load(ini::File& st) const {
        if (!st.load(statePath())) { st = ini::File(); st.path = statePath(); st.loaded = true; st.crlf = true; }
        return true;
    }
    int text(int def) const { ini::File st; load(st); std::string v = st.get("Lang", "Text", ""); return v == "ru_new" ? TEXT_RU_NEW : v == "en" ? TEXT_EN : v == "ru" ? TEXT_RU : def; }
    int voice() const { ini::File st; load(st); return st.get("Lang", "Voice", "ru") == "en" ? VOICE_EN : VOICE_RU; }

    bool haveEnglishVoice() const { return osio::exists(L / "voice_en.uax"); }
    bool haveNewText() const { return osio::exists(L / "text_new" / TEXT_FILES[0]); }
    bool haveEnglishTextures() const { return osio::exists(L / "tex_en.bin"); }

    // ---- text
    bool stashText(std::string& err) {
        osio::mkdirs(L / "text_orig");
        for (auto f : TEXT_FILES) {
            Path o = L / "text_orig" / f, s = sys / f, n = L / "text_new" / f;
            if (osio::exists(o) || !osio::exists(s)) continue;
            if (osio::exists(n) && sameFile(s, n)) continue;     // can't tell the original any more
            if (!osio::copyFile(s, o)) { err = "не удалось сохранить System\\" + std::string(f); return false; }
        }
        return true;
    }
    bool applyText(int t, std::string& err) {
        if (!stashText(err)) return false;
        for (auto f : TEXT_FILES) {
            Path src = (t == TEXT_RU_NEW ? L / "text_new" : L / "text_orig") / f, dst = sys / f;
            if (!osio::exists(src)) { if (t == TEXT_RU_NEW) { err = "нет файлов нового перевода"; return false; } continue; }
            if (osio::exists(dst) && osio::size(dst) == osio::size(src) && sameFile(dst, src)) continue;
            if (!osio::copyFile(src, dst)) { err = "не записывается System\\" + std::string(f); return false; }
        }
        return true;
    }

    // ---- voices
    Path sRu() const { return root / "Sounds" / "AllDialog.Rus_uax"; }
    Path sInt() const { return root / "Sounds" / "AllDialog.uax"; }
    bool setupVoices(std::string& err, ini::File* stIn = nullptr) {
        osio::mkdirs(L);
        Path orig = L / "voice_ru_orig.uax";
        if (!osio::exists(orig)) {
            Path oldMark = sys / "ShrekRemaster" / "voice_link.txt";   // launcher 0.9.x linked RU voices as AllDialog.uax
            if (osio::exists(oldMark)) { osio::remove(sInt()); osio::remove(oldMark); }
            if (!osio::exists(sRu())) { err = "нет Sounds\\AllDialog.Rus_uax"; return false; }
            if (!moveFile(sRu(), orig)) { err = "не удалось перенести Sounds\\AllDialog.Rus_uax"; return false; }
            if (osio::exists(sInt()) && !osio::exists(L / "AllDialog.user.uax")) moveFile(sInt(), L / "AllDialog.user.uax");
        }
        Path ru = L / "voice_ru.uax";
        if (!osio::exists(ru)) {
            std::string e2;
            Path line = haveEnglishVoice() ? L / "voice_en.uax" : L / "lines_en.uax";   // lines_en: just the missing line
            if (!(osio::exists(line) && buildRuVoice(orig, line, ru, e2)))
                if (!osio::hardlinkOrCopy(orig, ru)) { err = e2.empty() ? "не удалось подготовить русскую озвучку" : e2; return false; }
        }
        // packs made by earlier versions (or by the HD sound kit) get the lines added later; done once
        // per pack size, the packs are big
        ini::File own; if (!stIn) load(own);
        ini::File& st = stIn ? *stIn : own;
        std::string stamp = "2:" + std::to_string(osio::size(ru)) + ":" + std::to_string(osio::size(L / "voice_en.uax"));
        if (st.get("Lang", "MissingLines", "") != stamp) {
            std::string e3;
            Path srcRu = haveEnglishVoice() ? L / "voice_en.uax" : L / "lines_en.uax";
            bool ok = addMissingLines(ru, srcRu, RU_ADD, sizeof RU_ADD / sizeof *RU_ADD, e3);
            if (ok && haveEnglishVoice()) ok = addMissingLines(L / "voice_en.uax", orig, EN_ADD, sizeof EN_ADD / sizeof *EN_ADD, e3);
            if (ok) {
                st.set("Lang", "MissingLines", "2:" + std::to_string(osio::size(ru)) + ":" + std::to_string(osio::size(L / "voice_en.uax")));
                st.dirty = true; if (!stIn) st.save();
            }
        }
        return true;
    }
    bool linkVoice(int v, std::string& err) {
        Path src = v == VOICE_EN ? L / "voice_en.uax" : L / "voice_ru.uax";
        if (!osio::exists(src)) { err = v == VOICE_EN ? "нет английской озвучки (lang\\voice_en.uax)" : "нет русской озвучки"; return false; }
        for (Path dst : { sRu(), sInt() }) {
            std::error_code ec;
            if (osio::exists(dst) && ((osio::fs::equivalent(dst, src, ec) && !ec) || osio::size(dst) == osio::size(src))) continue;
            osio::remove(dst);
            if (!osio::hardlinkOrCopy(src, dst)) { err = "не записывается Sounds\\" + osio::u8str(dst.filename()); return false; }
        }
        return true;
    }

    // ---- the commercial (picture with its own voice track)
    bool applyMovies(int v, std::string& err) {
        Path mv = root / "Movies", en = L / "movies_en", ru = L / "movies_ru";
        for (auto f : MOVIES) {
            Path m = mv / f, e = en / f, r = ru / f;
            if (!osio::exists(e)) continue;
            bool isEn = osio::exists(m) && osio::size(m) == osio::size(e);
            if (v == VOICE_EN) {
                if (isEn) continue;
                if (osio::exists(m)) { osio::mkdirs(ru); osio::remove(r); if (!moveFile(m, r)) { err = "не удалось сохранить Movies\\" + std::string(f); return false; } }
                if (!osio::copyFile(e, m)) { err = "не записывается Movies\\" + std::string(f); return false; }
            } else {
                if (!osio::exists(r)) continue;
                if (osio::exists(m) && !isEn) { osio::remove(r); continue; }     // something newer was installed
                osio::remove(m);
                if (!moveFile(r, m)) { err = "не удалось вернуть Movies\\" + std::string(f); return false; }
            }
        }
        return true;
    }

    // ---- textures
    bool applyTextures(bool english, ini::File& st, std::string& err, int* count = nullptr) {
        TexPatch tp;
        // the HD texture kit makes HD versions of the English textures (tex_en_hd.bin, same format)
        Path pf = L / "tex_en_hd.bin";
        bool hd = osio::exists(pf) && (!english || tp.load(pf));
        if (!hd) { pf = L / "tex_en.bin"; tp.pkgs.clear(); if (english) tp.load(pf); }
        // a newer patch file (update of the remaster, HD kit run or removed) re-applies everything once
        std::string stamp = (hd ? "hd" : "") + std::to_string(osio::size(pf));
        bool force = english && st.get("Lang", "TexPatch", "") != stamp;
        int total = 0;
        std::vector<std::string> done;
        for (auto& pk : tp.pkgs) {
            int n = 0;
            if (!Overlay::apply(root, pk.first, &pk.second, st, err, &n, force)) return false;
            total += n; done.push_back(lower(pk.first));
        }
        // anything still recorded (Russian now, or a package no longer in the patch) goes back
        size_t a, b;
        if (st.sectionRange("Overlay", a, b)) {
            std::vector<std::string> keys; std::string k, v;
            for (size_t i = a; i < b; i++) if (ini::File::keyOf(st.lines[i], k, v)) keys.push_back(k);
            for (auto& key : keys) {
                bool keep = false; for (auto& d : done) if (d == lower(key)) keep = true;
                if (!keep && !Overlay::apply(root, key, nullptr, st, err)) return false;
            }
        }
        if (english) st.set("Lang", "TexPatch", stamp); else st.remove("Lang", "TexPatch");
        if (count) *count = total;
        return true;
    }

    // ---- everything
    bool apply(int t, int v, std::string& err) {
        ini::File st; load(st);
        osio::mkdirs(L);
        if (!applyText(t, err)) return false;
        if (setEngineLanguage && !setEngineLanguage(t == TEXT_EN)) { err = "не удалось записать Language в Default.ini"; return false; }
        if (!setupVoices(err, &st) || !linkVoice(v, err)) return false;
        if (!applyMovies(v, err)) return false;
        if (!applyTextures(t == TEXT_EN, st, err)) { st.dirty = true; st.save(); return false; }
        st.set("Lang", "Text", t == TEXT_RU_NEW ? "ru_new" : t == TEXT_EN ? "en" : "ru");
        st.set("Lang", "Voice", v == VOICE_EN ? "en" : "ru");
        st.dirty = true;
        if (!st.save()) { err = "не записывается lang\\state.ini"; return false; }
        return true;
    }
    // re-check after other tools touched the files (HD textures, HD videos, reinstall)
    bool ensure(std::string& err) {
        if (!configured()) return true;
        return apply(text(TEXT_RU), voice(), err);
    }
    // back to the untouched game (uninstall)
    bool reset(std::string& err) {
        bool ok = true; std::string e;
        ini::File st; load(st);
        if (!applyTextures(false, st, e)) { ok = false; err = e; }
        st.dirty = true; st.save();
        for (auto f : TEXT_FILES) { Path o = L / "text_orig" / f; if (osio::exists(o) && !osio::copyFile(o, sys / f)) { ok = false; err = "System\\" + std::string(f); } }
        if (setEngineLanguage) setEngineLanguage(false);
        if (!applyMovies(VOICE_RU, e)) { ok = false; err = e; }
        Path orig = L / "voice_ru_orig.uax";
        if (osio::exists(orig)) {
            osio::remove(sRu()); osio::remove(sInt());
            if (!moveFile(orig, sRu())) { ok = false; err = "Sounds\\AllDialog.Rus_uax"; }
            if (osio::exists(L / "AllDialog.user.uax")) moveFile(L / "AllDialog.user.uax", sInt());
            osio::remove(L / "voice_ru.uax");
        }
        osio::remove(statePath());
        return ok;
    }
};

}  // namespace lang
