// subs.cpp - subtitles of our own for every voice line. The game plays each line as a sound of the
// AllDialog package named after its line ID (pc_shk_SwampIntro_3...); ALAudio's PlaySound is hooked
// (rumble.cpp) and hands the name here. The text comes from the game's own dialogue files of the
// active language (System\HpDialog.<lang>, BumpDialog.<lang>, the same the game shows), the length of
// each line from ShrekRemaster\subs_dur.txt (read from the Bink headers of the voice packs), the
// speaker from the ID. Drawn at Present over everything, without a box, with a dark outline:
// "Name: text", colours from EnhancedEdition.ini [Subtitles]. ShrekPad hides the game's own
// subtitle text (and, as chosen, the black bars) - see dinput8.cpp.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cwchar>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <thread>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

void shr_log(const char* f, ...);
extern "C" const uint8_t shr_ttf[];

namespace {
struct Cfg { int mode = 1; bool on = true, storyOnly = false, names = true, nameChar = true; DWORD text = 0xFFFFFF, name = 0xFFD54A; float size = 1.f; bool en = false, voiceEn = false; } g_cfg;
bool g_ready = false; volatile bool g_hidden = false;   // photo mode
// 0.9.73: the English game's subtitles are set in a Palatino-like serif (SHMedInkFont in SH_Fonts.utx, Latin only);
// Windows' Palatino Linotype (pala.ttf, with Cyrillic) is the same face, so both languages take it. Other choices:
// (EnhancedEdition.ini [Subtitles] Font=palab.ttf (bold), tahomabd.ttf (the Russian game's own Tahoma), a file in Windows\Fonts or a full path; "builtin": the
// font in shrz.dll), or the font in shrz.dll when it is not there
std::vector<uint8_t> g_fontFile; const uint8_t* g_fontData = shr_ttf; float g_fontScale = 1.f;   // a serif face sits smaller on the line
void loadFont(const wchar_t* name) {
    g_fontData = shr_ttf; g_fontScale = 1.f;
    if (!name || !*name || !_wcsicmp(name, L"builtin")) { shr_log("subtitles: the built-in font"); return; }
    std::wstring path = name;
    if (!wcschr(name, L'\\') && !wcschr(name, L'/')) { wchar_t w[MAX_PATH] = L""; GetWindowsDirectoryW(w, MAX_PATH); path = std::wstring(w) + L"\\Fonts\\" + name; }
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (f) { fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); if (n > 0 && n < 64 << 20) { g_fontFile.resize(n); if (fread(g_fontFile.data(), 1, n, f) != (size_t)n) g_fontFile.clear(); } fclose(f); }
    stbtt_fontinfo t;
    if (!g_fontFile.empty() && stbtt_InitFont(&t, g_fontFile.data(), stbtt_GetFontOffsetForIndex(g_fontFile.data(), 0)) && stbtt_FindGlyphIndex(&t, 0x416)) {
        g_fontData = g_fontFile.data(); g_fontScale = _wcsnicmp(name, L"pala", 4) ? 1.f : 1.12f; shr_log("subtitles: font %ls", path.c_str());
    } else { g_fontFile.clear(); shr_log("subtitles: font %ls not usable, the built-in one instead", path.c_str()); }
}
volatile float g_sideDim = 0;   // a page in the 4:3 box (the potion shop): how dark the strips at the sides of the screen go
volatile float g_bars = 0;   // the black bars of a scene (ShrekPad): their height, a fraction of the screen
std::wstring g_dir;
std::unordered_map<std::string, std::wstring> g_text;       // line id (lower case) -> text
std::unordered_map<std::string, std::pair<float, float>> g_len;   // id -> seconds (Russian, English voice)

struct Sub { std::string id; std::wstring name, text; double start, end; DWORD nameCol = 0xFFD54A; };
std::vector<Sub> g_subs; std::mutex g_mu;
LARGE_INTEGER g_qf;
double now() { LARGE_INTEGER q; QueryPerformanceCounter(&q); if (!g_qf.QuadPart) QueryPerformanceFrequency(&g_qf); return double(q.QuadPart) / g_qf.QuadPart; }

std::string lower8(const wchar_t* w) { std::string s; for (; *w; w++) s += (char)towlower(*w < 128 ? *w : L'?'); return s; }

// UTF-16 ini with a [All] section: id=[Mood]text
void loadDialog(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb"); if (!f) return;
    std::vector<uint8_t> b; { uint8_t buf[65536]; size_t n; while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n); }
    fclose(f);
    std::wstring s;
    if (b.size() >= 2 && b[0] == 0xFF && b[1] == 0xFE) s.assign((const wchar_t*)(b.data() + 2), (b.size() - 2) / 2);
    else { int n = MultiByteToWideChar(CP_UTF8, 0, (const char*)b.data(), (int)b.size(), nullptr, 0); s.resize(n); MultiByteToWideChar(CP_UTF8, 0, (const char*)b.data(), (int)b.size(), &s[0], n); }
    size_t p = 0; int n = 0;
    while (p < s.size()) {
        size_t e = s.find(L'\n', p); if (e == std::wstring::npos) e = s.size();
        std::wstring ln = s.substr(p, e - p); p = e + 1;
        while (!ln.empty() && (ln.back() == L'\r' || ln.back() == L' ')) ln.pop_back();
        size_t eq = ln.find(L'=');
        if (eq == std::wstring::npos || ln[0] == L'[' || ln[0] == L';') continue;
        std::wstring v = ln.substr(eq + 1);
        while (!v.empty() && v[0] == L'[') { size_t c = v.find(L']'); if (c == std::wstring::npos) break; v.erase(0, c + 1); }   // [Mood] tags
        while (!v.empty() && v[0] == L' ') v.erase(0, 1);
        if (v.empty()) continue;
        std::string id = lower8(ln.substr(0, eq).c_str());
        g_text[id] = v; n++;
    }
    shr_log("subtitles: %d lines from %ls", n, path.c_str() + path.rfind(L'\\') + 1);
}

// ---- control hints: the game's lines that name keys, reworded for the player's buttons
// (ShrekRemaster\hints.txt: "[pad ]<lang> <id>=text with {Fire}..."; hints_keys.txt from the launcher:
// "K|P.<lang>.<name>=button"). With a gamepad in use (ShrekPad) the pad's buttons; on the keyboard
// only when its layout is not the game's classic one (the original words are right for that).
struct Hint { std::wstring all, pad; };
std::unordered_map<std::string, Hint> g_hint;                  // "<lang>|<id>"
std::unordered_map<std::string, std::wstring> g_hkey;          // "K.ru.Fire" -> words
bool g_kbDefault = true;
std::wstring utf8(const std::string& a) { int n = MultiByteToWideChar(CP_UTF8, 0, a.data(), (int)a.size(), nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, a.data(), (int)a.size(), &w[0], n); return w; }
void loadHints() {
    auto lines = [](const std::wstring& path, auto fn) {
        FILE* f = _wfopen(path.c_str(), L"rb"); if (!f) return false;
        char ln[2048];
        while (fgets(ln, sizeof ln, f)) {
            std::string l = ln; while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            if (l.size() >= 3 && (uint8_t)l[0] == 0xEF) l.erase(0, 3);
            if (l.empty() || l[0] == '#') continue;
            fn(l);
        }
        fclose(f); return true;
    };
    lines(g_dir + L"ShrekRemaster\\hints.txt", [](std::string l) {
        bool pad = !l.compare(0, 4, "pad "); if (pad) l.erase(0, 4);
        size_t sp = l.find(' '), eq = l.find('='); if (sp == std::string::npos || eq == std::string::npos || eq < sp) return;
        std::string lang = l.substr(0, sp), id = l.substr(sp + 1, eq - sp - 1);
        for (auto& c : id) c = (char)tolower((unsigned char)c);
        Hint& h = g_hint[lang + "|" + id]; (pad ? h.pad : h.all) = utf8(l.substr(eq + 1));
    });
    lines(g_dir + L"ShrekRemaster\\hints_keys.txt", [](std::string l) {
        size_t eq = l.find('='); if (eq == std::string::npos) return;
        std::string k = l.substr(0, eq);
        if (k == "K.Default") g_kbDefault = l.substr(eq + 1) == "1";
        else g_hkey[k] = utf8(l.substr(eq + 1));
    });
    shr_log("subtitles: %u control hints, keyboard %s", (unsigned)g_hint.size(), g_kbDefault ? "classic (hints as written)" : "own layout");
}
bool usingPad() {
    typedef int (__cdecl* Fn)(); static Fn f = nullptr; static bool looked = false;
    if (!looked) { looked = true; HMODULE m = GetModuleHandleW(L"dinput8.dll"); f = m ? (Fn)GetProcAddress(m, "ShrekPadUsingPad") : nullptr; }
    return f && f() != 0;
}
// the text of a line as the player should read it (the original when there is nothing to change)
std::wstring hinted(const std::string& id, const std::wstring& orig) {
    if (g_hint.empty()) return orig;
    std::string lang = g_cfg.en ? "en" : "ru";
    auto it = g_hint.find(lang + "|" + id); if (it == g_hint.end()) return orig;
    bool pad = usingPad();
    if (!pad && g_kbDefault) return orig;
    const std::wstring& t = pad && !it->second.pad.empty() ? it->second.pad : it->second.all;
    if (t.empty()) return orig;
    std::string pre = std::string(pad ? "P." : "K.") + lang + ".";
    std::wstring out;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] != L'{') { out += t[i]; continue; }
        size_t e = t.find(L'}', i); if (e == std::wstring::npos) return orig;
        std::string name; for (size_t k = i + 1; k < e; k++) name += (char)t[k];
        auto v = g_hkey.find(pre + name); if (v == g_hkey.end() || v->second.empty()) return orig;   // that button is not bound: keep the words of the game
        std::wstring w = v->second;
        bool start = out.empty() || (out.size() >= 2 && (out[out.size() - 2] == L'.' || out[out.size() - 2] == L'!' || out[out.size() - 2] == L'?'));
        if (start && !w.empty()) CharUpperBuffW(&w[0], 1);
        out += w; i = e;
    }
    return out;
}

// the speaker of a line (pc_<who>_...) and the colour that suits the character
std::wstring speaker(const std::string& id, DWORD* col = nullptr) {
    if (col) *col = 0xF2E6C8;
    if (id.size() < 7 || id.compare(0, 3, "pc_")) return L"";
    std::string w = id.substr(3, 3);
    struct N { const char* k; const wchar_t* ru; const wchar_t* en; DWORD c; };
    static const N T[] = {
        { "shk", L"Шрек", L"Shrek", 0x9BD34A }, { "dnk", L"Осёл", L"Donkey", 0xBDBDD6 }, { "pib", L"Кот в сапогах", L"Puss in Boots", 0xFFA24D },
        { "fin", L"Фиона", L"Fiona", 0x7FE0B0 }, { "fgm", L"Крёстная Фея", L"Fairy Godmother", 0x8CC4FF }, { "kng", L"Король", L"The King", 0xF2D16B },
        { "nar", L"Рассказчик", L"Narrator", 0xE8DDB8 }, { "gbm", L"Пряник", L"Gingy", 0xD9A066 }, { "ggm", L"Монго", L"Mongo", 0xC98A4E },
        { "knt", L"Рыцарь", L"Knight", 0xB4C6DE }, { "bnd", L"Разбойник", L"Bandit", 0xE0896A }, { "elf", L"Эльф", L"Elf", 0xA6E3A1 },
        { "pst", L"Крестьянин", L"Peasant", 0xD9C49C }, { "fem", L"Горожанка", L"Townswoman", 0xEDB6A6 }, { "psw", L"Крестьянка", L"Peasant woman", 0xEDB6A6 },
        { "psb", L"Мальчишка", L"Peasant boy", 0xF0C98A }, { "psg", L"Девчонка", L"Peasant girl", 0xF5B8D0 }, { "psc", L"Узник", L"Prisoner", 0xB0B0B0 },
        { "psp", L"Узник", L"Prisoner", 0xB0B0B0 }, { "guy", L"Продавец", L"Clerk", 0xFFD27F }, { "bgy", L"Продавец", L"Clerk", 0xFFD27F },
        { "sgy", L"Продавец", L"Clerk", 0xFFD27F }, { "tgy", L"Продавец", L"Clerk", 0xFFD27F }, { "roy", L"Рой", L"Roy", 0xFFF07A },
        { "tra", L"Экскурсовод", L"Tour guide", 0xF2B880 }, { "bbs", L"Голос из уборной", L"Voice from the outhouse", 0xC9BBFF },
        { "lep", L"Голос из уборной", L"Voice from the outhouse", 0xC9BBFF },
    };
    for (auto& n : T) if (w == n.k) { if (col) *col = n.c; return g_cfg.en ? n.en : n.ru; }
    return L"";
}

DWORD hexColor(const wchar_t* s, DWORD def) { if (!s || !*s) return def; wchar_t* e; unsigned long v = wcstoul(s, &e, 16); return *e ? def : (DWORD)(v & 0xFFFFFF); }

// ------------------------------------------------------------------ glyph atlas (fill + outline)
struct Glyph { float u0, v0, u1, v1; int w, h, ox, oy; float adv; };
struct Atlas {
    IDirect3DDevice9* dev = nullptr; IDirect3DTexture9* fill = nullptr; IDirect3DTexture9* line = nullptr; IDirect3DStateBlock9* sb = nullptr;
    int px = 0, pad = 0, W = 2048, H = 0; float ascent = 0, lineH = 0, scale = 0;
    std::unordered_map<uint32_t, Glyph> g; stbtt_fontinfo font;
} A;

void release() {
    if (A.fill) A.fill->Release(); if (A.line) A.line->Release(); if (A.sb) A.sb->Release();
    A.fill = A.line = nullptr; A.sb = nullptr; A.dev = nullptr; A.px = 0; A.g.clear();
}

std::vector<uint32_t> charset() {
    std::vector<uint32_t> c;
    for (uint32_t i = 32; i < 127; i++) c.push_back(i);
    for (uint32_t i = 0xA0; i < 0x100; i++) c.push_back(i);
    for (uint32_t i = 0x400; i < 0x460; i++) c.push_back(i);
    for (uint32_t u : { 0x2013u, 0x2014u, 0x2018u, 0x2019u, 0x201Cu, 0x201Du, 0x201Eu, 0x2026u, 0x2116u }) c.push_back(u);
    return c;
}

bool build(IDirect3DDevice9* d, int screenH) {
    int px = std::max(14, (int)std::lround(screenH * 0.0323f * g_cfg.size * g_fontScale));
    if (A.dev == d && A.px == px && A.fill) return true;
    release();
    if (!stbtt_InitFont(&A.font, g_fontData, stbtt_GetFontOffsetForIndex(g_fontData, 0))) return false;
    A.px = px; A.pad = std::max(1, (int)std::lround(px * 0.04f)) + 1;
    A.scale = stbtt_ScaleForPixelHeight(&A.font, (float)px);
    int asc, desc, gap; stbtt_GetFontVMetrics(&A.font, &asc, &desc, &gap);
    A.ascent = asc * A.scale; A.lineH = (asc - desc + gap) * A.scale * 1.08f;
    struct Bm { uint32_t cp; int w, h, ox, oy; float adv; std::vector<uint8_t> px; };
    std::vector<Bm> bms;
    for (uint32_t cp : charset()) {
        if (!stbtt_FindGlyphIndex(&A.font, (int)cp) && cp != 32) continue;
        Bm b; b.cp = cp; int adv, lsb; stbtt_GetCodepointHMetrics(&A.font, (int)cp, &adv, &lsb); b.adv = adv * A.scale;
        int x0, y0, x1, y1; stbtt_GetCodepointBitmapBox(&A.font, (int)cp, A.scale, A.scale, &x0, &y0, &x1, &y1);
        b.w = std::max(0, x1 - x0); b.h = std::max(0, y1 - y0); b.ox = x0; b.oy = y0;
        b.px.assign((size_t)b.w * b.h, 0);
        if (b.w && b.h) stbtt_MakeCodepointBitmap(&A.font, b.px.data(), b.w, b.h, b.w, A.scale, A.scale, (int)cp);
        bms.push_back(std::move(b));
    }
    // shelf packing, each glyph with the outline's room around it
    int P = A.pad, x = 1, y = 1, rowH = 0;
    struct Pos { int x, y; }; std::vector<Pos> pos;
    for (auto& b : bms) {
        int w = b.w + 2 * P + 1, h = b.h + 2 * P + 1;
        if (x + w >= A.W) { x = 1; y += rowH; rowH = 0; }
        pos.push_back({ x, y }); x += w; rowH = std::max(rowH, h);
    }
    int H = 64; while (H < y + rowH + 1) H *= 2;
    A.H = H;
    std::vector<uint8_t> F((size_t)A.W * H, 0), O((size_t)A.W * H, 0);
    // the outline: the glyph widened by a disc of radius r (soft edge)
    float r = (float)(P - 1);
    std::vector<std::pair<int, int>> disc; std::vector<float> dw;
    for (int dy = -P; dy <= P; dy++) for (int dx = -P; dx <= P; dx++) {
        float dd = std::sqrt(float(dx * dx + dy * dy));
        if (dd <= r + 1) { disc.push_back({ dx, dy }); dw.push_back(std::min(1.f, r + 1 - dd)); }
    }
    for (size_t i = 0; i < bms.size(); i++) {
        auto& b = bms[i]; int bx = pos[i].x + P, by = pos[i].y + P;
        for (int yy = 0; yy < b.h; yy++) for (int xx = 0; xx < b.w; xx++) {
            uint8_t v = b.px[(size_t)yy * b.w + xx]; if (!v) continue;
            F[(size_t)(by + yy) * A.W + bx + xx] = v;
            for (size_t k = 0; k < disc.size(); k++) {
                uint8_t& o = O[(size_t)(by + yy + disc[k].second) * A.W + bx + xx + disc[k].first];
                uint8_t c = (uint8_t)(v * dw[k]);
                if (c > o) o = c;
            }
        }
        Glyph gl; gl.w = b.w + 2 * P; gl.h = b.h + 2 * P; gl.ox = b.ox - P; gl.oy = b.oy - P; gl.adv = b.adv;
        gl.u0 = (float)pos[i].x / A.W; gl.v0 = (float)pos[i].y / H; gl.u1 = (float)(pos[i].x + gl.w) / A.W; gl.v1 = (float)(pos[i].y + gl.h) / H;
        A.g[b.cp] = gl;
    }
    auto make = [&](std::vector<uint8_t>& src, IDirect3DTexture9** t) {
        if (FAILED(d->CreateTexture(A.W, H, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, t, nullptr))) return false;
        D3DLOCKED_RECT lr; if (FAILED((*t)->LockRect(0, &lr, nullptr, 0))) return false;
        for (int yy = 0; yy < H; yy++) {
            DWORD* row = (DWORD*)((BYTE*)lr.pBits + yy * lr.Pitch);
            for (int xx = 0; xx < A.W; xx++) row[xx] = ((DWORD)src[(size_t)yy * A.W + xx] << 24) | 0xFFFFFF;
        }
        (*t)->UnlockRect(0); return true;
    };
    if (!make(F, &A.fill) || !make(O, &A.line) || FAILED(d->CreateStateBlock(D3DSBT_ALL, &A.sb))) { shr_log("subtitles: no textures"); release(); return false; }
    A.dev = d;
    shr_log("subtitles: font %d px, atlas %dx%d", px, A.W, H);
    return true;
}

struct V { float x, y, z, w; DWORD c; float u, v; };
float advance(uint32_t a, uint32_t b) {
    auto it = A.g.find(a); float w = it != A.g.end() ? it->second.adv : A.px * 0.3f;
    if (b) w += stbtt_GetCodepointKernAdvance(&A.font, (int)a, (int)b) * A.scale;
    return w;
}
// words wrapped into lines no wider than maxW; the name only before the first line
struct Run { std::wstring s; DWORD col; };
std::vector<std::vector<Run>> wrap(const std::wstring& name, const std::wstring& text, float maxW, DWORD nameCol, DWORD textCol) {
    auto width = [&](const std::wstring& t) { float w = 0; for (size_t i = 0; i < t.size(); i++) w += advance(t[i], i + 1 < t.size() ? t[i + 1] : 0); return w; };
    std::vector<std::vector<Run>> lines(1); float lw = 0; bool words = false;
    if (!name.empty()) { std::wstring n = name + L": "; lines[0].push_back({ n, nameCol }); lw = width(n); }
    float sp = width(L" ");
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find(L' ', p); if (e == std::wstring::npos) e = text.size();
        std::wstring w = text.substr(p, e - p); p = e + 1;
        if (w.empty()) continue;
        float ww = width(w), add = (words ? sp : 0) + ww;
        if (lw > 0 && lw + add > maxW) { lines.push_back({}); lw = 0; words = false; add = ww; }
        std::wstring t = (words ? L" " : L"") + w;
        auto& L = lines.back();
        if (words && !L.empty() && L.back().col == textCol) L.back().s += t; else L.push_back({ t, textCol });
        lw += add; words = true;
    }
    return lines;
}
} // namespace

// ------------------------------------------------------------------ interface (shrz.cpp, rumble.cpp, pp9.cpp)
void subs_init(const wchar_t* dir) {
    g_dir = dir;
    std::wstring ini = g_dir + L"EnhancedEdition.ini";
    g_cfg.mode = (int)GetPrivateProfileIntW(L"Subtitles", L"Mode", 1, ini.c_str());
    g_cfg.names = GetPrivateProfileIntW(L"Subtitles", L"Names", 1, ini.c_str()) != 0;
    g_cfg.on = GetPrivateProfileIntW(L"Subtitles", L"Enabled", 1, ini.c_str()) != 0;
    g_cfg.storyOnly = GetPrivateProfileIntW(L"Subtitles", L"StoryOnly", 0, ini.c_str()) != 0;   // only the lines of scenes, not the talk around
    g_cfg.size = std::min(2.f, std::max(0.5f, GetPrivateProfileIntW(L"Subtitles", L"Size", 100, ini.c_str()) / 100.f));
    wchar_t b[32];
    GetPrivateProfileStringW(L"Subtitles", L"TextColor", L"FFFFFF", b, 32, ini.c_str()); g_cfg.text = hexColor(b, 0xFFFFFF);
    GetPrivateProfileStringW(L"Subtitles", L"NameColor", L"char", b, 32, ini.c_str());
    g_cfg.nameChar = !_wcsicmp(b, L"char");                 // each character's own colour (the default)
    g_cfg.name = hexColor(b, 0xFFD54A);
    GetPrivateProfileStringW(L"Engine.Engine", L"Language", L"Rus", b, 32, (g_dir + L"Default.ini").c_str());
    g_cfg.en = !_wcsicmp(b, L"int");
    GetPrivateProfileStringW(L"Lang", L"Voice", L"ru", b, 32, (g_dir + L"ShrekRemaster\\lang\\state.ini").c_str());
    g_cfg.voiceEn = !_wcsicmp(b, L"en");
    { wchar_t fn[MAX_PATH]; GetPrivateProfileStringW(L"Subtitles", L"Font", L"builtin", fn, MAX_PATH, ini.c_str()); loadFont(fn); }
    if (!g_cfg.on) { shr_log("subtitles: off"); return; }
    if (g_cfg.mode <= 0) { shr_log("subtitles: the game's own"); return; }
    std::wstring ext = g_cfg.en ? L"int" : L"Rus";
    loadDialog(g_dir + L"HpDialog." + ext);
    loadDialog(g_dir + L"BumpDialog." + ext);
    loadHints();
    if (FILE* f = _wfopen((g_dir + L"ShrekRemaster\\subs_dur.txt").c_str(), L"r")) {
        char ln[256];
        while (fgets(ln, sizeof ln, f)) { char id[160]; float a, c; if (ln[0] != '#' && sscanf(ln, "%159s %f %f", id, &a, &c) == 3) g_len[id] = { a, c }; }
        fclose(f);
    }
    g_ready = !g_text.empty();
    shr_log("subtitles: mode %d, %s text, %s voice, %u line lengths", g_cfg.mode, g_cfg.en ? "English" : "Russian", g_cfg.voiceEn ? "English" : "Russian", (unsigned)g_len.size());
}
int subs_mode() { return g_cfg.mode; }
bool subs_english() { return g_cfg.en; }

// a sound starts: a voice line?
void subs_onSound(const wchar_t* name) {
    if (!g_ready || !name || towlower(name[0]) != L'p' || towlower(name[1]) != L'c' || name[2] != L'_') return;
    std::string id = lower8(name);
    if (g_cfg.storyOnly && id.find("_bumpline_") != std::string::npos) return;   // the talk around (bump sets), not a scene
    auto t = g_text.find(id); if (t == g_text.end()) return;
    float len = 0;
    auto l = g_len.find(id);
    if (l != g_len.end()) len = g_cfg.voiceEn ? (l->second.second > 0 ? l->second.second : l->second.first) : (l->second.first > 0 ? l->second.first : l->second.second);
    if (len <= 0) len = std::max(2.5f, t->second.size() * 0.06f);
    double n = now();
    std::lock_guard<std::mutex> g(g_mu);
    for (auto& s : g_subs) if (s.id == id && n - s.start < 0.3) return;     // the same line again at once (two slots)
    Sub s; s.id = id; s.text = hinted(id, t->second); DWORD c = g_cfg.name; s.name = g_cfg.names ? speaker(id, &c) : L""; s.start = n; s.end = n + len + 0.35;
    s.nameCol = g_cfg.nameChar ? c : g_cfg.name;
    g_subs.push_back(s);
    if (g_subs.size() > 3) g_subs.erase(g_subs.begin());
}
// a sound is stopped (a skipped scene): its line goes
void subs_onStop(const wchar_t* name) {
    if (!g_ready || !name) return;
    std::string id = lower8(name);
    double n = now();
    std::lock_guard<std::mutex> g(g_mu);
    for (auto& s : g_subs) if (s.id == id && s.end > n + 0.25) s.end = n + 0.25;
}
// text the game itself shows (ShrekPad reads it before hiding the game's subtitles): only used for
// lines without a voice, which no sound announces
extern "C" __declspec(dllexport) void __cdecl ShrSubsGameText(const wchar_t* text) {
    if (!g_ready || !text || !*text) return;
    std::wstring t = text;
    while (!t.empty() && t[0] == L'[') { size_t c = t.find(L']'); if (c == std::wstring::npos) break; t.erase(0, c + 1); }
    while (!t.empty() && (t.back() == L' ' || t.back() == L'\n')) t.pop_back();
    if (t.empty()) return;
    double n = now();
    std::lock_guard<std::mutex> g(g_mu);
    for (auto& s : g_subs) {
        if (n - s.start > 2.0 && s.end < n) continue;
        size_t k = std::min<size_t>(16, std::min(s.text.size(), t.size()));
        if (!s.text.compare(0, k, t, 0, k)) return;                     // already shown from its sound
    }
    Sub s; s.text = t; s.start = n; s.end = n + std::max(2.5, t.size() * 0.065);
    g_subs.push_back(s);
    if (g_subs.size() > 3) g_subs.erase(g_subs.begin());
}

void subs_reset(bool rel) {   // before a Reset / when the device goes (rel = false: another device took over, its objects are left)
    if (rel) { if (A.sb) A.sb->Release(); if (A.fill) A.fill->Release(); if (A.line) A.line->Release(); }
    A.sb = nullptr; A.fill = A.line = nullptr; A.dev = nullptr; A.px = 0; A.g.clear();
}

extern "C" __declspec(dllexport) void __cdecl ShrSubsHide(int hide) { g_hidden = hide != 0; }
extern "C" __declspec(dllexport) void __cdecl ShrSideDim(float a) { g_sideDim = a > 0 ? (a < 0.9f ? a : 0.9f) : 0; }
extern "C" __declspec(dllexport) void __cdecl ShrSubsBars(float h) { g_bars = h > 0 && h < 0.3f ? h : 0; }

// the speedrun timer (shrz.cpp): two lines at the top right corner, empty = none
std::wstring g_timer[2];
void subs_setTimer(const std::wstring& a, const std::wstring& b) { std::lock_guard<std::mutex> g(g_mu); g_timer[0] = a; g_timer[1] = b; }
// a short notice at the top (a bug mark saved): shown from a moment later, so the screenshot is without it
std::wstring g_note; double g_noteFrom = 0, g_noteTo = 0;
extern "C" __declspec(dllexport) void __cdecl ShrNotify(const wchar_t* t) { std::lock_guard<std::mutex> g(g_mu); g_note = t ? t : L""; g_noteFrom = now() + 0.2; g_noteTo = g_noteFrom + 2.5; }

void subs_draw(void* dv) {
    if (!dv || g_hidden) return;
    double n = now();
    std::vector<Sub> act; std::wstring tm[2], note;
    {
        std::lock_guard<std::mutex> g(g_mu);
        g_subs.erase(std::remove_if(g_subs.begin(), g_subs.end(), [&](const Sub& s) { return s.end < n; }), g_subs.end());
        if (g_ready) act = g_subs;
        tm[0] = g_timer[0]; tm[1] = g_timer[1];
        if (n >= g_noteFrom && n < g_noteTo) note = g_note;
    }
    static float dimNow = 0; static double dimAt = 0;
    { float tgt = g_sideDim; double dt = std::min(0.1, n - dimAt); dimAt = n; dimNow += (tgt - dimNow) * (float)std::min(1.0, dt * 8); if (tgt <= 0 && dimNow < 0.01f) dimNow = 0; }
    if (act.empty() && tm[0].empty() && note.empty() && dimNow <= 0) return;
    auto d = (IDirect3DDevice9*)dv;
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    D3DSURFACE_DESC bd{}; bb->GetDesc(&bd);
    if (!build(d, (int)bd.Height)) { bb->Release(); return; }
    float SW = (float)bd.Width, SH = (float)bd.Height;
    float maxW = std::min(SW * 0.8f, SH * 16.f / 9.f * 0.72f);
    // the lines, newest at the bottom; in a scene with black bars, in the middle of the bottom bar
    std::vector<V> outl, fill;
    float y = SH * 0.935f;
    float bars = g_bars;
    if (bars > 0.03f) {
        float total = 0;
        for (auto& s : act) total += A.lineH * wrap(s.name, s.text, maxW, s.nameCol, g_cfg.text).size() + A.lineH * 0.25f;
        total -= A.lineH * 0.25f;
        float barTop = SH * (1 - bars), barH = SH * bars;
        y = total <= barH ? barTop + (barH + total) * 0.5f : SH - A.lineH * 0.3f;   // more than the bar holds: from its bottom up
    }
    for (int i = (int)act.size() - 1; i >= 0; i--) {
        const Sub& s = act[i];
        float a = (float)std::min({ 1.0, (n - s.start) / 0.12, (s.end - n) / 0.25 });
        if (a <= 0) continue;
        auto lines = wrap(s.name, s.text, maxW, s.nameCol, g_cfg.text);
        y -= A.lineH * lines.size();
        float ly = y;
        for (auto& L : lines) {
            float w = 0; std::wstring all; for (auto& r : L) all += r.s;
            for (size_t k = 0; k < all.size(); k++) w += advance(all[k], k + 1 < all.size() ? all[k + 1] : 0);
            float x = std::floor((SW - w) * 0.5f), base = std::floor(ly + A.ascent);
            size_t k = 0;
            for (auto& r : L) {
                DWORD al = (DWORD)(a * 255) << 24;
                for (size_t q = 0; q < r.s.size(); q++, k++) {
                    uint32_t cp = r.s[q];
                    auto it = A.g.find(cp);
                    if (it != A.g.end() && it->second.w > 0 && cp != 32) {
                        const Glyph& g = it->second;
                        float x0 = x + g.ox - 0.5f, y0 = base + g.oy - 0.5f, x1 = x0 + g.w, y1 = y0 + g.h;
                        auto quad = [&](std::vector<V>& o, DWORD c) {
                            V v0{ x0, y0, 0, 1, c, g.u0, g.v0 }, v1{ x1, y0, 0, 1, c, g.u1, g.v0 }, v2{ x0, y1, 0, 1, c, g.u0, g.v1 }, v3{ x1, y1, 0, 1, c, g.u1, g.v1 };
                            o.push_back(v0); o.push_back(v1); o.push_back(v2); o.push_back(v2); o.push_back(v1); o.push_back(v3);
                        };
                        quad(outl, (DWORD)(a * 235) << 24);
                        quad(fill, al | r.col);
                    }
                    x += advance(cp, k + 1 < all.size() ? all[k + 1] : 0);
                }
            }
            ly += A.lineH;
        }
        y -= A.lineH * 0.25f;
    }
    // the timer: smaller glyphs (the same atlas, scaled), right-aligned at the top right; a notice at the top middle
    auto small = [&](const std::wstring& t, float sc, float x, float ty, DWORD col) {
        float base = std::floor(ty + A.ascent * sc);
        for (size_t k = 0; k < t.size(); k++) {
            uint32_t cp = t[k];
            auto it = A.g.find(cp);
            if (it != A.g.end() && it->second.w > 0 && cp != 32) {
                const Glyph& g = it->second;
                float x0 = x + g.ox * sc - 0.5f, y0 = base + g.oy * sc - 0.5f, x1 = x0 + g.w * sc, y1 = y0 + g.h * sc;
                auto quad = [&](std::vector<V>& o, DWORD c) {
                    V v0{ x0, y0, 0, 1, c, g.u0, g.v0 }, v1{ x1, y0, 0, 1, c, g.u1, g.v0 }, v2{ x0, y1, 0, 1, c, g.u0, g.v1 }, v3{ x1, y1, 0, 1, c, g.u1, g.v1 };
                    o.push_back(v0); o.push_back(v1); o.push_back(v2); o.push_back(v2); o.push_back(v1); o.push_back(v3);
                };
                quad(outl, 0xEB000000); quad(fill, col);
            }
            x += advance(cp, k + 1 < t.size() ? t[k + 1] : 0) * sc;
        }
    };
    if (!note.empty()) {
        float w = 0; for (size_t k = 0; k < note.size(); k++) w += advance(note[k], k + 1 < note.size() ? note[k + 1] : 0) * 0.8f;
        small(note, 0.8f, std::floor((SW - w) * 0.5f), SH * 0.04f, 0xFFFFD54A);
    }
    if (!tm[0].empty()) {
        const float sc = 0.72f;
        float ty = SH * 0.025f;
        for (int li = 0; li < 2; li++) {
            const std::wstring& t = tm[li]; if (t.empty()) continue;
            float w = 0; for (size_t k = 0; k < t.size(); k++) w += advance(t[k], k + 1 < t.size() ? t[k + 1] : 0) * sc;
            float x = std::floor(SW - SH * 0.03f - w), base = std::floor(ty + A.ascent * sc);
            for (size_t k = 0; k < t.size(); k++) {
                uint32_t cp = t[k];
                auto it = A.g.find(cp);
                if (it != A.g.end() && it->second.w > 0 && cp != 32) {
                    const Glyph& g = it->second;
                    float x0 = x + g.ox * sc - 0.5f, y0 = base + g.oy * sc - 0.5f, x1 = x0 + g.w * sc, y1 = y0 + g.h * sc;
                    auto quad = [&](std::vector<V>& o, DWORD c) {
                        V v0{ x0, y0, 0, 1, c, g.u0, g.v0 }, v1{ x1, y0, 0, 1, c, g.u1, g.v0 }, v2{ x0, y1, 0, 1, c, g.u0, g.v1 }, v3{ x1, y1, 0, 1, c, g.u1, g.v1 };
                        o.push_back(v0); o.push_back(v1); o.push_back(v2); o.push_back(v2); o.push_back(v1); o.push_back(v3);
                    };
                    quad(outl, 0xEB000000);
                    quad(fill, li == 0 ? 0xFFFFFFFF : 0xFFFFD54A);
                }
                x += advance(cp, k + 1 < t.size() ? t[k + 1] : 0) * sc;
            }
            ty += A.lineH * sc;
        }
    }
    std::vector<V> dimq;
    if (dimNow > 0) {   // the strips left and right of the centred 4:3 box
        float box = std::min(SW, SH * 4.f / 3.f), sw = std::floor((SW - box) * 0.5f); DWORD c = (DWORD)(dimNow * 255) << 24;
        if (sw >= 1) for (int k = 0; k < 2; k++) {
            float x0 = k ? SW - sw : 0, x1 = k ? SW : sw;
            V v0{ x0, 0, 0, 1, c, 0, 0 }, v1{ x1, 0, 0, 1, c, 0, 0 }, v2{ x0, SH, 0, 1, c, 0, 0 }, v3{ x1, SH, 0, 1, c, 0, 0 };
            dimq.push_back(v0); dimq.push_back(v1); dimq.push_back(v2); dimq.push_back(v2); dimq.push_back(v1); dimq.push_back(v3);
        }
    }
    if (fill.empty() && dimq.empty()) { bb->Release(); return; }
    A.sb->Capture();
    IDirect3DSurface9* rt = nullptr; d->GetRenderTarget(0, &rt);
    d->SetRenderTarget(0, bb);
    D3DVIEWPORT9 vp = { 0, 0, bd.Width, bd.Height, 0, 1 }; d->SetViewport(&vp);
    const DWORD rs[][2] = { { D3DRS_ZENABLE, FALSE }, { D3DRS_ZWRITEENABLE, FALSE }, { D3DRS_ALPHABLENDENABLE, TRUE },
        { D3DRS_SRCBLEND, D3DBLEND_SRCALPHA }, { D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA }, { D3DRS_BLENDOP, D3DBLENDOP_ADD },
        { D3DRS_SEPARATEALPHABLENDENABLE, FALSE }, { D3DRS_ALPHATESTENABLE, FALSE }, { D3DRS_CULLMODE, D3DCULL_NONE },
        { D3DRS_FOGENABLE, FALSE }, { D3DRS_STENCILENABLE, FALSE }, { D3DRS_COLORWRITEENABLE, 0xF }, { D3DRS_SCISSORTESTENABLE, FALSE },
        { D3DRS_SRGBWRITEENABLE, FALSE }, { D3DRS_FILLMODE, D3DFILL_SOLID }, { D3DRS_LIGHTING, FALSE }, { D3DRS_CLIPPLANEENABLE, 0 },
        { D3DRS_TEXTUREFACTOR, 0xFFFFFFFF } };
    for (auto& r : rs) d->SetRenderState((D3DRENDERSTATETYPE)r[0], r[1]);
    d->SetVertexShader(nullptr); d->SetPixelShader(nullptr);
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE); d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE); d->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE); d->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0); d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE); d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR); d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR); d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP); d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    if (!dimq.empty()) {
        d->SetTexture(0, nullptr);
        d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(dimq.size() / 3), &dimq[0], sizeof(V));
        d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE); d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    }
    d->SetTexture(0, A.line);
    for (size_t i = 0; i < outl.size(); i += 3 * 20000) d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)std::min<size_t>(20000, (outl.size() - i) / 3), &outl[i], sizeof(V));
    d->SetTexture(0, A.fill);
    for (size_t i = 0; i < fill.size(); i += 3 * 20000) d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)std::min<size_t>(20000, (fill.size() - i) / 3), &fill[i], sizeof(V));
    if (rt) { d->SetRenderTarget(0, rt); rt->Release(); }
    A.sb->Apply();
    bb->Release();
}

// ---------------------------------------------------------------- bug mark screenshot
// ShrekPad (F12) asks for the frame as it is shown: copied at the next Present (through a plain
// render target: the back buffer may be multisampled), written as PNG on a thread of its own
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include "stb_image_write.h"
static std::mutex g_shotMu; static std::wstring g_shotPath;
extern "C" __declspec(dllexport) void __cdecl ShrBugShot(const wchar_t* path) { std::lock_guard<std::mutex> g(g_shotMu); g_shotPath = path ? path : L""; }
void subs_bugShot(void* dv) {
    auto d = (IDirect3DDevice9*)dv; if (!d) return;
    std::wstring path; { std::lock_guard<std::mutex> g(g_shotMu); if (g_shotPath.empty()) return; path.swap(g_shotPath); }
    IDirect3DSurface9 *bb = nullptr, *rt = nullptr, *sys = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    D3DSURFACE_DESC bd{}; bb->GetDesc(&bd);
    bool ok = (bd.Format == D3DFMT_X8R8G8B8 || bd.Format == D3DFMT_A8R8G8B8) &&
              SUCCEEDED(d->CreateRenderTarget(bd.Width, bd.Height, bd.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr)) &&
              SUCCEEDED(d->StretchRect(bb, nullptr, rt, nullptr, D3DTEXF_NONE)) &&
              SUCCEEDED(d->CreateOffscreenPlainSurface(bd.Width, bd.Height, bd.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr)) &&
              SUCCEEDED(d->GetRenderTargetData(rt, sys));
    std::vector<uint8_t>* rgb = nullptr; UINT W = bd.Width, H = bd.Height;
    D3DLOCKED_RECT lr;
    if (ok && SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
        rgb = new std::vector<uint8_t>((size_t)W * H * 3);
        for (UINT y = 0; y < H; y++) {
            const uint8_t* src = (const uint8_t*)lr.pBits + (size_t)y * lr.Pitch; uint8_t* dst = rgb->data() + (size_t)y * W * 3;
            for (UINT x = 0; x < W; x++) { dst[x * 3] = src[x * 4 + 2]; dst[x * 3 + 1] = src[x * 4 + 1]; dst[x * 3 + 2] = src[x * 4]; }
        }
        sys->UnlockRect();
    }
    if (sys) sys->Release(); if (rt) rt->Release(); bb->Release();
    if (!rgb) { shr_log("bug mark: screenshot failed (format %d)", (int)bd.Format); return; }
    char u8[MAX_PATH * 3]; WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, u8, sizeof u8, nullptr, nullptr);
    std::string file = u8;
    std::thread([rgb, W, H, file]() { stbi_write_png(file.c_str(), (int)W, (int)H, 3, rgb->data(), (int)W * 3); delete rgb; }).detach();
    shr_log("bug mark: screenshot %ux%u", W, H);
}

