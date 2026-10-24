// File helpers that work with non-ASCII (Cyrillic) paths on Windows and Linux alike.
#pragma once
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <system_error>

namespace osio {
namespace fs = std::filesystem;
typedef fs::path Path;

inline FILE* open(const Path& p, const char* mode) {
#ifdef _WIN32
    std::wstring m(mode, mode + strlen(mode));
    return _wfopen(p.wstring().c_str(), m.c_str());
#else
    return fopen(p.string().c_str(), mode);
#endif
}
inline bool readFile(const Path& p, std::vector<uint8_t>& d) {
    FILE* f = open(p, "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    d.resize((size_t)n); bool ok = n == 0 || fread(d.data(), 1, (size_t)n, f) == (size_t)n; fclose(f); return ok;
}
inline bool writeFile(const Path& p, const std::vector<uint8_t>& d) {
    FILE* f = open(p, "wb"); if (!f) return false;
    bool ok = d.empty() || fwrite(d.data(), 1, d.size(), f) == d.size();
    ok = (fclose(f) == 0) && ok; return ok;
}
inline bool exists(const Path& p) { std::error_code ec; return fs::exists(p, ec); }
inline bool isDir(const Path& p) { std::error_code ec; return fs::is_directory(p, ec); }
inline int64_t size(const Path& p) { std::error_code ec; auto s = fs::file_size(p, ec); return ec ? -1 : (int64_t)s; }
inline bool mkdirs(const Path& p) { std::error_code ec; fs::create_directories(p, ec); return !ec; }
// files copied from a CD can carry the read-only attribute: Windows then refuses to replace them
inline void makeWritable(const Path& p) {
    std::error_code ec;
    if (fs::exists(p, ec)) fs::permissions(p, fs::perms::owner_write, fs::perm_options::add, ec);
}
inline bool remove(const Path& p) { makeWritable(p); std::error_code ec; return fs::remove(p, ec); }
inline bool copyFile(const Path& a, const Path& b) {
    makeWritable(b);
    std::error_code ec; fs::copy_file(a, b, fs::copy_options::overwrite_existing, ec); return !ec;
}
inline bool hardlinkOrCopy(const Path& a, const Path& b) {
    std::error_code ec; fs::create_hard_link(a, b, ec); if (!ec) return true;
    return copyFile(a, b);
}
// UTF-8 <-> path
inline Path u8path(const std::string& s) {
#if defined(__cpp_char8_t)
    return Path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}
inline std::string u8str(const Path& p) {
    auto s = p.u8string(); return std::string(s.begin(), s.end());
}

}  // namespace osio
