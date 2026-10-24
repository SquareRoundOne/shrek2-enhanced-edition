// ShrekSnd: sound effects of Shrek 2 (UE2 .uax packages) to WAV for AI upscaling, and the upscaled
// WAVs back into the packages.
//
//   ShrekSnd extract <SoundsDir> <WorkDir> <pkg.uax>...
//       WorkDir/lo/<pkg>__<sound>.wav   16-bit PCM at the original rate (Bink sounds are decoded)
//       WorkDir/manifest.txt            <pkg> <sound> <rate> <channels> <samples>
//   ShrekSnd pack <SoundsDir> <WorkDir> <OutDir>
//       every WorkDir/hi/<pkg>__<sound>.wav (any rate; 16/24/32-bit PCM or 32-bit float) becomes that
//       sound as 16-bit PCM WAV; sounds without one keep their original data. Writes OutDir/<pkg>.uax.
//
// Packages are rewritten append-only: the original bytes stay, changed sounds get new bodies at the
// end, and the name / import / export tables are written again after them.
#include "binkaudio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <filesystem>
#include <cmath>

using std::string; using std::vector;
typedef vector<uint8_t> Bytes;

static bool readfile(const string& fn, Bytes& d) {
    FILE* f = fopen(fn.c_str(), "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    d.resize(n > 0 ? n : 0); bool ok = n <= 0 || fread(d.data(), 1, n, f) == (size_t)n; fclose(f); return ok;
}
static bool writefile(const string& fn, const Bytes& d) {
    FILE* f = fopen(fn.c_str(), "wb"); if (!f) return false;
    bool ok = d.empty() || fwrite(d.data(), 1, d.size(), f) == d.size(); return fclose(f) == 0 && ok;
}

struct Rd {
    const Bytes& d; size_t p;
    Rd(const Bytes& b, size_t at) : d(b), p(at) {}
    uint8_t u8() { if (p >= d.size()) throw 1; return d[p++]; }
    uint16_t u16() { uint16_t v = u8(); v |= (uint16_t)u8() << 8; return v; }
    uint32_t u32() { uint32_t v = u16(); v |= (uint32_t)u16() << 16; return v; }
    int32_t i32() { return (int32_t)u32(); }
    int32_t ci() {
        uint8_t b0 = u8(); int32_t v = b0 & 0x3F;
        if (b0 & 0x40) { int sh = 6; for (int i = 0; i < 4; i++) { uint8_t x = u8(); v |= (x & 0x7F) << sh; sh += 7; if (!(x & 0x80)) break; } }
        return (b0 & 0x80) ? -v : v;
    }
};
static void put_ci(Bytes& o, int32_t v) {
    bool neg = v < 0; uint32_t a = neg ? -v : v;
    uint8_t b0 = (a & 0x3F) | (neg ? 0x80 : 0); a >>= 6;
    if (a) b0 |= 0x40;
    o.push_back(b0);
    while (a) { uint8_t b = a & 0x7F; a >>= 7; if (a) b |= 0x80; o.push_back(b); }
}
static void put_u32(Bytes& o, uint32_t v) { for (int i = 0; i < 4; i++) o.push_back((v >> (8 * i)) & 0xFF); }
static void put_u16(Bytes& o, uint16_t v) { o.push_back(v & 0xFF); o.push_back(v >> 8); }

static string lowerS(string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
struct Exp { int32_t cls, super, pkg, name; uint32_t flags; int32_t size, off; };
struct Imp { int32_t cpkg, cname, pkg, name; };
struct Pkg {
    Bytes d; uint16_t ver = 0; vector<string> names; vector<uint32_t> nflags; vector<Imp> imps; vector<Exp> exps;
    bool load(const string& fn) {
        if (!readfile(fn, d) || d.size() < 64) return false;
        try {
            Rd r(d, 0);
            if (r.u32() != 0x9E2A83C1) return false;
            ver = r.u16(); r.u16(); r.u32();
            uint32_t nc = r.u32(), no = r.u32(), ec = r.u32(), eo = r.u32(), ic = r.u32(), io = r.u32();
            Rd n(d, no);
            for (uint32_t i = 0; i < nc; i++) {
                int len = n.ci(); string s;
                if (len > 0) { s.assign((const char*)&d.at(n.p), len - 1); n.p += len; }
                names.push_back(s); nflags.push_back(n.u32());
            }
            Rd im(d, io);
            for (uint32_t i = 0; i < ic; i++) { Imp m; m.cpkg = im.ci(); m.cname = im.ci(); m.pkg = im.i32(); m.name = im.ci(); imps.push_back(m); }
            Rd ex(d, eo);
            for (uint32_t i = 0; i < ec; i++) {
                Exp e; e.cls = ex.ci(); e.super = ex.ci(); e.pkg = ex.i32(); e.name = ex.ci(); e.flags = ex.u32(); e.size = ex.ci(); e.off = e.size > 0 ? ex.ci() : 0;
                exps.push_back(e);
            }
        } catch (...) { return false; }
        return true;
    }
    string classname(const Exp& e) const {
        if (e.cls < 0) return names.at(imps.at(-e.cls - 1).name);
        if (e.cls > 0) return names.at(exps.at(e.cls - 1).name);
        return "Class";
    }
    int name(const string& s) const { string l = lowerS(s); for (size_t i = 0; i < names.size(); i++) if (lowerS(names[i]) == l) return (int)i; return -1; }
};

// a Sound export: [tagged properties ... None] FileType [i32 SkipPos][ci count][data] tail
struct SoundBody { size_t propsEnd, dataOff, dataLen, tailOff; int fileType; };
static bool parseSound(const Pkg& P, const Exp& e, SoundBody& s) {
    try {
        Rd r(P.d, e.off);
        while (true) {
            int ni = r.ci();
            if (P.names.at(ni) == "None") break;
            uint8_t info = r.u8(); int typ = info & 15, sz = (info >> 4) & 7; bool arr = info & 0x80;
            if (typ == 10) r.ci();
            int size = sz == 0 ? 1 : sz == 1 ? 2 : sz == 2 ? 4 : sz == 3 ? 12 : sz == 4 ? 16 : 0;
            if (sz == 5) size = r.u8(); else if (sz == 6) size = r.u16(); else if (sz == 7) size = r.i32();
            if (arr && typ != 3) { uint8_t b = r.u8(); if (b & 0x80) { r.u8(); if (b & 0x40) r.u16(); } }
            r.p += size;
        }
        s.propsEnd = r.p;
        s.fileType = r.ci();
        int32_t skip = r.i32(); int32_t cnt = r.ci();
        s.dataOff = r.p; s.dataLen = cnt; s.tailOff = r.p + cnt;
        if (cnt < 0 || s.tailOff > (size_t)(e.off + e.size) || skip != (int32_t)s.tailOff) return false;
        return true;
    } catch (...) { return false; }
}

// ---------------------------------------------------------------- WAV
struct Pcm { int rate = 0, ch = 0; vector<int16_t> s; Bytes smpl; };   // interleaved; smpl: loop points chunk body
static Bytes wavBytes(const Pcm& p) {
    Bytes o; uint32_t data = (uint32_t)(p.s.size() * 2);
    uint32_t extra = p.smpl.empty() ? 0 : (uint32_t)(8 + p.smpl.size() + (p.smpl.size() & 1));
    o.insert(o.end(), { 'R', 'I', 'F', 'F' }); put_u32(o, 36 + data + extra); o.insert(o.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    put_u32(o, 16); put_u16(o, 1); put_u16(o, (uint16_t)p.ch); put_u32(o, p.rate); put_u32(o, p.rate * p.ch * 2); put_u16(o, (uint16_t)(p.ch * 2)); put_u16(o, 16);
    o.insert(o.end(), { 'd', 'a', 't', 'a' }); put_u32(o, data);
    size_t at = o.size(); o.resize(at + data); memcpy(&o[at], p.s.data(), data);
    if (!p.smpl.empty()) {   // loop points (the engine loops these sounds between them)
        o.insert(o.end(), { 's', 'm', 'p', 'l' }); put_u32(o, (uint32_t)p.smpl.size());
        o.insert(o.end(), p.smpl.begin(), p.smpl.end()); if (p.smpl.size() & 1) o.push_back(0);
    }
    return o;
}
static bool parseWav(const Bytes& d, Pcm& out) {
    if (d.size() < 12 || memcmp(d.data(), "RIFF", 4) || memcmp(d.data() + 8, "WAVE", 4)) return false;
    size_t p = 12; int fmt = 0, ch = 0, bits = 0; uint32_t rate = 0; const uint8_t* data = nullptr; size_t dlen = 0;
    out.smpl.clear();
    while (p + 8 <= d.size()) {
        uint32_t sz; memcpy(&sz, &d[p + 4], 4);
        const uint8_t* c = &d[p + 8]; size_t avail = std::min<size_t>(sz, d.size() - (p + 8));
        if (!memcmp(&d[p], "fmt ", 4) && avail >= 16) {
            uint16_t f, cc, b; memcpy(&f, c, 2); memcpy(&cc, c + 2, 2); memcpy(&rate, c + 4, 4); memcpy(&b, c + 14, 2);
            fmt = f; ch = cc; bits = b;
            if (f == 0xFFFE && avail >= 26) { uint16_t sub; memcpy(&sub, c + 24, 2); fmt = sub; }
        } else if (!memcmp(&d[p], "data", 4)) { data = c; dlen = avail; }
        else if (!memcmp(&d[p], "smpl", 4) && avail >= 36) out.smpl.assign(c, c + avail);
        p += 8 + sz + (sz & 1);
    }
    if (!data || ch < 1 || ch > 8 || rate < 4000) return false;
    out.rate = rate; out.ch = ch; out.s.clear();
    size_t bps = bits / 8; if (!bps) return false;
    size_t n = dlen / bps;
    out.s.reserve(n);
    for (size_t i = 0; i < n; i++) {
        const uint8_t* q = data + i * bps; float v;
        if (fmt == 1 && bits == 16) { int16_t x; memcpy(&x, q, 2); out.s.push_back(x); continue; }
        if (fmt == 1 && bits == 8) v = (q[0] - 128) / 128.f;
        else if (fmt == 1 && bits == 24) { int32_t x = (q[0] << 8) | (q[1] << 16) | (q[2] << 24); v = x / 2147483648.f; }
        else if (fmt == 1 && bits == 32) { int32_t x; memcpy(&x, q, 4); v = x / 2147483648.f; }
        else if (fmt == 3 && bits == 32) { memcpy(&v, q, 4); }
        else if (fmt == 3 && bits == 64) { double x; memcpy(&x, q, 8); v = (float)x; }
        else return false;
        out.s.push_back((int16_t)std::max(-32768.f, std::min(32767.f, std::round(v * 32768.f))));
    }
    return true;
}
// the channel count of the original sound (an upscaler may return stereo from mono)
static void toChannels(Pcm& p, int ch) {
    if (p.ch == ch) return;
    size_t frames = p.s.size() / p.ch; vector<int16_t> o(frames * ch);
    for (size_t f = 0; f < frames; f++) {
        if (ch == 1) { int sum = 0; for (int c = 0; c < p.ch; c++) sum += p.s[f * p.ch + c]; o[f] = (int16_t)(sum / p.ch); }
        else for (int c = 0; c < ch; c++) o[f * ch + c] = p.s[f * p.ch + std::min(c, p.ch - 1)];
    }
    p.s.swap(o); p.ch = ch;
}

static string lower(string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static string stem(const string& f) { string b = std::filesystem::path(f).filename().string(); size_t d = b.rfind('.'); return d == string::npos ? b : b.substr(0, d); }

static int cmd_extract(const string& dir, const string& work, const vector<string>& pkgs) {
    std::error_code ec; std::filesystem::create_directories(work + "/lo", ec);
    FILE* man = fopen((work + "/manifest.txt").c_str(), "w");
    if (!man) { fprintf(stderr, "cannot write %s/manifest.txt\n", work.c_str()); return 1; }
    int total = 0, bad = 0;
    for (const string& pf : pkgs) {
        Pkg P; if (!P.load(dir + "/" + pf)) { fprintf(stderr, "skip %s (not readable)\n", pf.c_str()); continue; }
        string pn = stem(pf); int n = 0;
        for (const Exp& e : P.exps) {
            if (e.size <= 0 || P.classname(e) != "Sound") continue;
            SoundBody sb; if (!parseSound(P, e, sb)) { bad++; continue; }
            string ft = lower(P.names.at(sb.fileType));
            Pcm pcm;
            if (ft == "wav") {
                Bytes w(P.d.begin() + sb.dataOff, P.d.begin() + sb.dataOff + sb.dataLen);
                if (!parseWav(w, pcm)) { fprintf(stderr, "%s.%s: unsupported WAV\n", pn.c_str(), P.names[e.name].c_str()); bad++; continue; }
            } else if (ft == "bik") {
                if (!binkaudio::decodeFile(&P.d[sb.dataOff], sb.dataLen, pcm.s, pcm.rate, pcm.ch) || pcm.s.empty()) {
                    fprintf(stderr, "%s.%s: Bink audio not decoded\n", pn.c_str(), P.names[e.name].c_str()); bad++; continue;
                }
            } else { bad++; continue; }
            string base = pn + "__" + P.names[e.name];
            if (!writefile(work + "/lo/" + base + ".wav", wavBytes(pcm))) { fprintf(stderr, "cannot write %s\n", base.c_str()); return 1; }
            fprintf(man, "%s %s %d %d %zu\n", pf.c_str(), P.names[e.name].c_str(), pcm.rate, pcm.ch, pcm.s.size() / pcm.ch);
            n++;
        }
        printf("%-24s %4d sounds\n", pf.c_str(), n); fflush(stdout); total += n;
    }
    fclose(man);
    printf("extracted %d sounds%s\n", total, bad ? (" (" + std::to_string(bad) + " skipped)").c_str() : "");
    return 0;
}

static Bytes rebuild(const Pkg& P, const std::map<int, Bytes>& bodies, vector<string> names) {
    vector<Exp> ex = P.exps;
    Bytes out(P.d.begin(), P.d.end());
    for (size_t i = 0; i < ex.size(); i++) {
        auto it = bodies.find((int)i); if (it == bodies.end()) continue;
        ex[i].off = (int32_t)out.size(); ex[i].size = (int32_t)it->second.size();
        out.insert(out.end(), it->second.begin(), it->second.end());
    }
    // the body was built for this offset: SkipPos is absolute (patched below in pack)
    uint32_t no = (uint32_t)out.size();
    for (size_t i = 0; i < names.size(); i++) {
        put_ci(out, (int32_t)names[i].size() + 1); out.insert(out.end(), names[i].begin(), names[i].end()); out.push_back(0);
        put_u32(out, i < P.nflags.size() ? P.nflags[i] : 0x00070010);
    }
    uint32_t io = (uint32_t)out.size();
    for (const Imp& m : P.imps) { put_ci(out, m.cpkg); put_ci(out, m.cname); put_u32(out, (uint32_t)m.pkg); put_ci(out, m.name); }
    uint32_t eo = (uint32_t)out.size();
    for (const Exp& e : ex) {
        put_ci(out, e.cls); put_ci(out, e.super); put_u32(out, (uint32_t)e.pkg); put_ci(out, e.name);
        put_u32(out, e.flags); put_ci(out, e.size); if (e.size > 0) put_ci(out, e.off);
    }
    uint32_t nc = (uint32_t)names.size(), ecn = (uint32_t)ex.size();
    memcpy(&out[12], &nc, 4); memcpy(&out[16], &no, 4); memcpy(&out[24], &eo, 4); memcpy(&out[32], &io, 4);
    // generations (after the GUID): the last one carries the export and name counts
    int32_t gc; memcpy(&gc, &out[52], 4);
    if (gc > 0 && gc < 64) { memcpy(&out[56 + 8 * (gc - 1)], &ecn, 4); memcpy(&out[60 + 8 * (gc - 1)], &nc, 4); }
    return out;
}

static int cmd_pack(const string& dir, const string& work, const string& outdir) {
    FILE* man = fopen((work + "/manifest.txt").c_str(), "r"); if (!man) { fprintf(stderr, "no manifest\n"); return 1; }
    std::map<string, std::set<string>> todo; vector<string> order;
    std::map<string, std::pair<int, size_t>> origLen;    // "<pkg> <sound>" -> (rate, frames) of the original
    char pf[512], sn[512]; int rate, ch; size_t smp;
    while (fscanf(man, "%511s %511s %d %d %zu", pf, sn, &rate, &ch, &smp) == 5) {
        if (!todo.count(pf)) order.push_back(pf);
        todo[pf].insert(sn);
        origLen[string(pf) + " " + sn] = { rate, smp };
    }
    fclose(man);
    std::error_code ec; std::filesystem::create_directories(outdir, ec);
    int bad = 0;
    for (const string& p : order) {
        Pkg P; if (!P.load(dir + "/" + p)) { fprintf(stderr, "cannot read %s\n", p.c_str()); bad++; continue; }
        string pn = stem(p);
        vector<string> names = P.names;
        int wavName = P.name("WAV"); if (wavName < 0) { names.push_back("WAV"); wavName = (int)names.size() - 1; }
        // where the new bodies will start: after the original data (append-only)
        size_t at = P.d.size();
        std::map<int, Bytes> bodies; int n = 0; double secs = 0;
        for (size_t i = 0; i < P.exps.size(); i++) {
            const Exp& e = P.exps[i];
            if (e.size <= 0 || P.classname(e) != "Sound" || !todo[p].count(P.names[e.name])) continue;
            Bytes w; if (!readfile(work + "/hi/" + pn + "__" + P.names[e.name] + ".wav", w)) continue;
            SoundBody sb; if (!parseSound(P, e, sb)) continue;
            Pcm pcm; if (!parseWav(w, pcm)) { fprintf(stderr, "%s__%s.wav: unsupported WAV format\n", pn.c_str(), P.names[e.name].c_str()); bad++; continue; }
            // the original: channel count and loudness (AudioSR normalises every sound to half scale;
            // the game's mix needs the original levels back)
            int origCh = 1; int origPeak = 0;
            {
                string ft = lower(P.names.at(sb.fileType)); Pcm o; bool ok = false;
                if (ft == "wav") { Bytes ow(P.d.begin() + sb.dataOff, P.d.begin() + sb.dataOff + sb.dataLen); ok = parseWav(ow, o); }
                else if (ft == "bik") ok = binkaudio::decodeFile(&P.d[sb.dataOff], sb.dataLen, o.s, o.rate, o.ch);
                if (ok && o.ch > 0) { origCh = o.ch; for (int16_t v : o.s) origPeak = std::max(origPeak, std::abs((int)v)); }
            }
            toChannels(pcm, origCh);
            pcm.smpl.clear();
            if (getenv("SHRSND_KEEPLEVEL") == nullptr) {   // the level of the original
                int pk = 0; for (int16_t v : pcm.s) pk = std::max(pk, std::abs((int)v));
                if (pk > 0 && origPeak > 0) {
                    double g = (double)origPeak / pk;
                    for (auto& v : pcm.s) v = (int16_t)std::max(-32768.0, std::min(32767.0, std::round(v * g)));
                }
            }
            {   // the original duration: upscalers pad short sounds (and may cut a few samples)
                auto ol = origLen.find(p + " " + P.names[e.name]);
                if (ol != origLen.end() && ol->second.first > 0) {
                    size_t want = (size_t)std::llround((double)ol->second.second * pcm.rate / ol->second.first);
                    pcm.s.resize(want * pcm.ch, 0);
                }
            }
            {   // loop points of the original, moved to the new sample rate
                string ft = lower(P.names.at(sb.fileType)); Pcm o;
                if (ft == "wav") {
                    Bytes ow(P.d.begin() + sb.dataOff, P.d.begin() + sb.dataOff + sb.dataLen);
                    if (parseWav(ow, o) && o.smpl.size() >= 36 && o.rate > 0) {
                        Bytes sm = o.smpl; double k = (double)pcm.rate / o.rate; uint32_t frames = (uint32_t)(pcm.s.size() / pcm.ch);
                        uint32_t period = (uint32_t)(1e9 / pcm.rate + 0.5); memcpy(&sm[8], &period, 4);
                        uint32_t nl; memcpy(&nl, &sm[28], 4);
                        for (uint32_t l = 0; l < nl && 36 + 24 * (l + 1) <= sm.size(); l++) {
                            uint8_t* L = &sm[36 + 24 * l];
                            uint32_t st, en; memcpy(&st, L + 8, 4); memcpy(&en, L + 12, 4);
                            st = (uint32_t)std::min<double>(frames ? frames - 1 : 0, std::round(st * k));
                            en = (uint32_t)std::min<double>(frames ? frames - 1 : 0, std::round((en + 1) * k) - 1);
                            memcpy(L + 8, &st, 4); memcpy(L + 12, &en, 4);
                        }
                        pcm.smpl = sm;
                    }
                }
            }
            Bytes data = wavBytes(pcm);
            Bytes b(P.d.begin() + e.off, P.d.begin() + sb.propsEnd);     // properties as they were
            put_ci(b, wavName);
            size_t skipAt = b.size(); put_u32(b, 0);
            put_ci(b, (int32_t)data.size());
            b.insert(b.end(), data.begin(), data.end());
            uint32_t skip = (uint32_t)(at + b.size());
            memcpy(&b[skipAt], &skip, 4);
            size_t tailLen = e.off + e.size - sb.tailOff;
            put_u32(b, 0); if (tailLen >= 8) put_u32(b, 0xFFFFFFFFu);
            at += b.size();
            bodies[(int)i] = std::move(b); n++; secs += (double)pcm.s.size() / pcm.ch / pcm.rate;
        }
        Bytes out = rebuild(P, bodies, names);
        if (!writefile(outdir + "/" + p, out)) { fprintf(stderr, "cannot write %s\n", p.c_str()); return 1; }
        printf("%-24s %4d sounds (%.1f min)  %6.1f -> %6.1f MB\n", p.c_str(), n, secs / 60, P.d.size() / 1e6, out.size() / 1e6); fflush(stdout);
    }
    if (bad) printf("%d sounds were skipped (see messages above)\n", bad);
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 5 && !strcmp(argv[1], "extract")) { vector<string> p; for (int i = 4; i < argc; i++) p.push_back(argv[i]); return cmd_extract(argv[2], argv[3], p); }
    if (argc == 5 && !strcmp(argv[1], "pack")) return cmd_pack(argv[2], argv[3], argv[4]);
    fprintf(stderr, "usage:\n  ShrekSnd extract <SoundsDir> <WorkDir> <pkg.uax>...\n  ShrekSnd pack <SoundsDir> <WorkDir> <OutDir>\n");
    return 2;
}
