// Minimal, format-preserving INI editor for Unreal Engine 2 config files.
// Keeps the file's encoding (ANSI bytes or UTF-16LE with BOM), line endings, comments and order;
// only the touched keys change.
#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include "osio.h"

namespace ini {

inline std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

struct File {
    osio::Path path;
    std::vector<std::string> lines;
    bool utf16 = false, crlf = true, loaded = false, dirty = false, finalEol = true;

    bool load(const osio::Path& p) {
        path = p; lines.clear(); loaded = false; dirty = false;
        std::vector<uint8_t> d;
        if (!osio::readFile(p, d)) return false;
        std::string text;
        if (d.size() >= 2 && d[0] == 0xFF && d[1] == 0xFE) {
            utf16 = true;
            for (size_t i = 2; i + 1 < d.size(); i += 2) {
                uint32_t c = d[i] | (d[i + 1] << 8);
                if (c >= 0xD800 && c < 0xDC00 && i + 3 < d.size()) { uint32_t c2 = d[i + 2] | (d[i + 3] << 8); c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00); i += 2; }
                if (c < 0x80) text += (char)c;
                else if (c < 0x800) { text += (char)(0xC0 | (c >> 6)); text += (char)(0x80 | (c & 63)); }
                else if (c < 0x10000) { text += (char)(0xE0 | (c >> 12)); text += (char)(0x80 | ((c >> 6) & 63)); text += (char)(0x80 | (c & 63)); }
                else { text += (char)(0xF0 | (c >> 18)); text += (char)(0x80 | ((c >> 12) & 63)); text += (char)(0x80 | ((c >> 6) & 63)); text += (char)(0x80 | (c & 63)); }
            }
        } else text.assign(d.begin(), d.end());
        crlf = text.find("\r\n") != std::string::npos || text.find('\n') == std::string::npos;
        finalEol = !text.empty() && text.back() == '\n';
        size_t s = 0;
        while (s <= text.size()) {
            size_t e = text.find('\n', s);
            if (e == std::string::npos) { if (s < text.size()) lines.push_back(text.substr(s)); break; }
            std::string l = text.substr(s, e - s);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l); s = e + 1;
        }
        loaded = true; return true;
    }
    bool save() {
        if (!dirty) return true;
        std::string text;
        for (size_t i = 0; i < lines.size(); i++) { text += lines[i]; if (i + 1 < lines.size() || finalEol) text += crlf ? "\r\n" : "\n"; }
        std::vector<uint8_t> d;
        if (utf16) {
            d.push_back(0xFF); d.push_back(0xFE);
            for (size_t i = 0; i < text.size();) {
                unsigned char c = text[i]; uint32_t cp; int n;
                if (c < 0x80) { cp = c; n = 1; } else if ((c >> 5) == 6) { cp = c & 31; n = 2; } else if ((c >> 4) == 14) { cp = c & 15; n = 3; } else { cp = c & 7; n = 4; }
                for (int k = 1; k < n && i + k < text.size(); k++) cp = (cp << 6) | (text[i + k] & 63);
                i += n;
                if (cp >= 0x10000) { cp -= 0x10000; uint16_t a = 0xD800 + (cp >> 10), b = 0xDC00 + (cp & 0x3FF); d.push_back(a & 255); d.push_back(a >> 8); d.push_back(b & 255); d.push_back(b >> 8); }
                else { d.push_back(cp & 255); d.push_back(cp >> 8); }
            }
        } else d.assign(text.begin(), text.end());
        if (!osio::writeFile(path, d)) return false;
        dirty = false; return true;
    }
    // [first, end) line range of a section body; returns false if absent
    bool sectionRange(const std::string& sec, size_t& a, size_t& b) const {
        std::string want = "[" + lower(sec) + "]";
        for (size_t i = 0; i < lines.size(); i++) {
            if (lower(trim(lines[i])) == want) {
                a = i + 1; b = lines.size();
                for (size_t j = a; j < lines.size(); j++) { std::string t = trim(lines[j]); if (!t.empty() && t[0] == '[') { b = j; break; } }
                return true;
            }
        }
        return false;
    }
    static bool keyOf(const std::string& line, std::string& k, std::string& v) {
        std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '[') return false;
        size_t e = t.find('='); if (e == std::string::npos) return false;
        k = trim(t.substr(0, e)); v = t.substr(e + 1);
        if (!v.empty() && v.back() == '\r') v.pop_back();
        return true;
    }
    bool get(const std::string& sec, const std::string& key, std::string& out) const {
        size_t a, b; if (!sectionRange(sec, a, b)) return false;
        std::string lk = lower(key), k, v;
        for (size_t i = a; i < b; i++) if (keyOf(lines[i], k, v) && lower(k) == lk) { out = trim(v); return true; }
        return false;
    }
    std::string get(const std::string& sec, const std::string& key, const std::string& def = "") const {
        std::string v; return get(sec, key, v) ? v : def;
    }
    void set(const std::string& sec, const std::string& key, const std::string& val) {
        size_t a, b;
        if (!sectionRange(sec, a, b)) {
            if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
            lines.push_back("[" + sec + "]"); lines.push_back(key + "=" + val); dirty = true; return;
        }
        std::string lk = lower(key), k, v; bool found = false;
        for (size_t i = a; i < b; i++) if (keyOf(lines[i], k, v) && lower(k) == lk) {
            std::string nl = k + "=" + val;
            if (lines[i] != nl) { lines[i] = nl; dirty = true; }
            found = true;
        }
        if (!found) {
            size_t at = b; while (at > a && trim(lines[at - 1]).empty()) at--;
            lines.insert(lines.begin() + at, key + "=" + val); dirty = true;
        }
    }
    void remove(const std::string& sec, const std::string& key) {
        size_t a, b; if (!sectionRange(sec, a, b)) return;
        std::string lk = lower(key), k, v;
        for (size_t i = b; i-- > a;) if (keyOf(lines[i], k, v) && lower(k) == lk) { lines.erase(lines.begin() + i); dirty = true; }
    }
};

}  // namespace ini
