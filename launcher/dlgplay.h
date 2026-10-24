// One voice line out of the game's dialogue package (Sounds\AllDialog.uax, the voice pack the
// launcher's language switch linked in) as a WAV in memory - for "Скрытый контент". Only the
// package's tables and the one sound are read (the package is 130 MB, with HD sound much more).
// Original sounds are Bink audio (decoded by binkaudio.h), the HD kit's are WAV already.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include "osio.h"
#include "binkaudio.h"

namespace dlgplay {

struct File {
    FILE* f = nullptr;
    ~File() { if (f) fclose(f); }
    bool seek(uint64_t o) {
#ifdef _WIN32
        return _fseeki64(f, (long long)o, SEEK_SET) == 0;
#else
        return fseeko(f, (off_t)o, SEEK_SET) == 0;
#endif
    }
    bool read(uint64_t o, size_t n, std::vector<uint8_t>& out) {
        out.resize(n); if (!seek(o)) return false;
        size_t got = fread(out.data(), 1, n, f); out.resize(got); return got > 0;
    }
};
struct Rd {
    const std::vector<uint8_t>& d; size_t p = 0;
    explicit Rd(const std::vector<uint8_t>& d) : d(d) {}
    bool ok(size_t n) const { return p + n <= d.size(); }
    uint8_t u8() { if (!ok(1)) throw 1; return d[p++]; }
    uint16_t u16() { if (!ok(2)) throw 1; uint16_t v; memcpy(&v, &d[p], 2); p += 2; return v; }
    uint32_t u32() { if (!ok(4)) throw 1; uint32_t v; memcpy(&v, &d[p], 4); p += 4; return v; }
    int32_t ci() {
        uint8_t b0 = u8(); int32_t v = b0 & 0x3f;
        if (b0 & 0x40) { int sh = 6; for (int i = 0; i < 4; i++) { uint8_t x = u8(); v |= (x & 0x7f) << sh; sh += 7; if (!(x & 0x80)) break; } }
        return (b0 & 0x80) ? -v : v;
    }
};
inline std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

inline void putU32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; i++) o.push_back((uint8_t)(v >> (8 * i))); }
inline void putU16(std::vector<uint8_t>& o, uint16_t v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); }
inline std::vector<uint8_t> wav(const std::vector<int16_t>& s, int rate, int ch) {
    std::vector<uint8_t> o; uint32_t data = (uint32_t)(s.size() * 2);
    o.insert(o.end(), { 'R', 'I', 'F', 'F' }); putU32(o, 36 + data); o.insert(o.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    putU32(o, 16); putU16(o, 1); putU16(o, (uint16_t)ch); putU32(o, (uint32_t)rate); putU32(o, (uint32_t)(rate * ch * 2)); putU16(o, (uint16_t)(ch * 2)); putU16(o, 16);
    o.insert(o.end(), { 'd', 'a', 't', 'a' }); putU32(o, data);
    size_t at = o.size(); o.resize(at + data); memcpy(&o[at], s.data(), data);
    return o;
}

// the sound named `name` (case does not matter) -> WAV bytes; err says what went wrong
inline bool lineWav(const osio::Path& pkg, const std::string& name, std::vector<uint8_t>& out, std::string& err) {
    File F; F.f = osio::open(pkg, "rb");
    if (!F.f) { err = "нет файла Sounds\\AllDialog.uax"; return false; }
    std::vector<uint8_t> h;
    if (!F.read(0, 64, h) || h.size() < 40) { err = "файл озвучки не читается"; return false; }
    try {
        Rd r(h);
        if (r.u32() != 0x9E2A83C1u) { err = "файл озвучки не того формата"; return false; }
        r.u16(); r.u16(); r.u32();
        uint32_t nc = r.u32(), no = r.u32(), ec = r.u32(), eo = r.u32(), ic = r.u32(), io = r.u32();
        if (nc > 2000000 || ec > 2000000 || ic > 2000000) { err = "файл озвучки повреждён"; return false; }
        // names
        std::vector<uint8_t> b; std::vector<std::string> names; names.reserve(nc);
        if (!F.read(no, std::min<size_t>((size_t)nc * 80 + 4096, 64u << 20), b)) { err = "таблица имён не читается"; return false; }
        { Rd n(b); for (uint32_t i = 0; i < nc; i++) { int len = n.ci(); if (len < 0 || !n.ok((size_t)len + 4)) throw 1; names.emplace_back((const char*)&b[n.p], len > 0 ? len - 1 : 0); n.p += len; n.u32(); } }
        // imports: the class "Sound"
        if (!F.read(io, std::min<size_t>((size_t)ic * 24 + 4096, 16u << 20), b)) { err = "таблица импорта не читается"; return false; }
        std::vector<int> impName;
        { Rd m(b); for (uint32_t i = 0; i < ic; i++) { m.ci(); m.ci(); m.u32(); impName.push_back(m.ci()); } }
        std::string want = lower(name);
        if (!F.read(eo, std::min<size_t>((size_t)ec * 40 + 4096, 64u << 20), b)) { err = "таблица экспорта не читается"; return false; }
        Rd e(b);
        for (uint32_t i = 0; i < ec; i++) {
            int32_t cls = e.ci(); e.ci(); e.u32(); int32_t nm = e.ci(); e.u32(); int32_t size = e.ci(); int32_t off = size > 0 ? e.ci() : 0;
            if (nm < 0 || nm >= (int)names.size() || lower(names[nm]) != want) continue;
            if (cls >= 0 || -cls - 1 >= (int)impName.size() || names.at(impName[-cls - 1]) != "Sound") continue;
            std::vector<uint8_t> body;
            if (size <= 0 || !F.read((uint64_t)(uint32_t)off, (size_t)size, body)) { err = "звук не читается"; return false; }
            Rd s(body);
            for (;;) {     // tagged properties up to None
                int ni = s.ci(); if (ni < 0 || ni >= (int)names.size()) throw 1;
                if (names[ni] == "None") break;
                uint8_t info = s.u8(); int typ = info & 15, sz = (info >> 4) & 7; bool arr = info & 0x80;
                if (typ == 10) s.ci();
                int n = sz == 0 ? 1 : sz == 1 ? 2 : sz == 2 ? 4 : sz == 3 ? 12 : sz == 4 ? 16 : 0;
                if (sz == 5) n = s.u8(); else if (sz == 6) n = s.u16(); else if (sz == 7) n = (int)s.u32();
                if (arr && typ != 3) { uint8_t a = s.u8(); if (a & 0x80) { s.u8(); if (a & 0x40) s.u16(); } }
                s.p += n;
            }
            s.ci(); s.u32(); int32_t cnt = s.ci();
            if (cnt <= 0 || !s.ok((size_t)cnt)) { err = "звук пустой"; return false; }
            const uint8_t* d = &body[s.p];
            if (cnt > 12 && !memcmp(d, "RIFF", 4)) { out.assign(d, d + cnt); return true; }
            if (cnt > 12 && !memcmp(d, "BIK", 3)) {
                std::vector<int16_t> pcm; int rate = 0, chs = 0;
                if (!binkaudio::decodeFile(d, (size_t)cnt, pcm, rate, chs) || pcm.empty()) { err = "звук не декодируется"; return false; }
                out = wav(pcm, rate, chs); return true;
            }
            err = "незнакомый формат звука"; return false;
        }
        err = "реплики нет в установленной озвучке"; return false;
    } catch (...) { err = "файл озвучки не читается"; return false; }
}

}  // namespace dlgplay
