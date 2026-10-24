// A bug report for the developer: the logs, settings and marked screenshots in one .zip (stored,
// no compression - logs and PNGs; nothing to install to open it).
#pragma once
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include "osio.h"

namespace bugreport {

inline uint32_t crc32(const uint8_t* d, size_t n) {
    static uint32_t T[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; T[i] = c; } init = true; }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = T[(c ^ d[i]) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

struct Zip {
    std::vector<uint8_t> out, cd; int count = 0;
    static void u16(std::vector<uint8_t>& o, uint16_t v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); }
    static void u32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; i++) o.push_back((uint8_t)(v >> (8 * i))); }
    void add(const std::string& name, const std::vector<uint8_t>& data) {
        uint32_t crc = crc32(data.data(), data.size()), off = (uint32_t)out.size();
        time_t t = time(nullptr); tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &t);
#else
        localtime_r(&t, &lt);
#endif
        uint16_t dtime = (uint16_t)((lt.tm_hour << 11) | (lt.tm_min << 5) | (lt.tm_sec / 2));
        uint16_t ddate = (uint16_t)(((lt.tm_year - 80) << 9) | ((lt.tm_mon + 1) << 5) | lt.tm_mday);
        u32(out, 0x04034b50); u16(out, 20); u16(out, 0x0800); u16(out, 0); u16(out, dtime); u16(out, ddate);
        u32(out, crc); u32(out, (uint32_t)data.size()); u32(out, (uint32_t)data.size()); u16(out, (uint16_t)name.size()); u16(out, 0);
        out.insert(out.end(), name.begin(), name.end()); out.insert(out.end(), data.begin(), data.end());
        u32(cd, 0x02014b50); u16(cd, 20); u16(cd, 20); u16(cd, 0x0800); u16(cd, 0); u16(cd, dtime); u16(cd, ddate);
        u32(cd, crc); u32(cd, (uint32_t)data.size()); u32(cd, (uint32_t)data.size()); u16(cd, (uint16_t)name.size());
        u16(cd, 0); u16(cd, 0); u16(cd, 0); u16(cd, 0); u32(cd, 0); u32(cd, off);
        cd.insert(cd.end(), name.begin(), name.end());
        count++;
    }
    std::vector<uint8_t> finish() {
        std::vector<uint8_t> r = out; uint32_t cdOff = (uint32_t)r.size();
        r.insert(r.end(), cd.begin(), cd.end());
        u32(r, 0x06054b50); u16(r, 0); u16(r, 0); u16(r, (uint16_t)count); u16(r, (uint16_t)count);
        u32(r, (uint32_t)cd.size()); u32(r, cdOff); u16(r, 0);
        return r;
    }
};

}  // namespace bugreport
