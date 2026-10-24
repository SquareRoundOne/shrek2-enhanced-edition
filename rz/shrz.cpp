// shrz.dll - reversed floating-point depth buffer for Shrek 2 (D3DDrv.dll imports Direct3DCreate8
// from here instead of d3d8.dll).
//
// The game renders with a 24-bit integer depth buffer and an ordinary projection: almost all of
// its precision sits right in front of the camera, so surfaces that lie close together further
// away (shore and water, stones on the ground, decals, bush roots) fight for the same depth value
// and shimmer when the camera moves. With DXVK the device can get a 32-bit float depth buffer;
// storing depth reversed (1 at the near plane, 0 at infinity) matches the float's distribution to
// the perspective one and gives near-uniform relative precision at every distance.
//
// What has to be reversed so that the game does not notice:
//   projection (fixed function)   column z' = column w - column z
//   vertex shaders                oPos.z' = oPos.w - oPos.z   (bytecode rewritten on creation)
//   depth test                    LESS <-> GREATER, LESSEQUAL <-> GREATEREQUAL
//   depth clear                   Z' = 1 - Z;   viewport MinZ/MaxZ' = 1 - MaxZ/MinZ
//   depth bias (ZBIAS)            applied as a relative depth scale instead of the D3D8 offset
//   table fog                     DXVK derives it from the window depth -> the game is told the
//                                 device has no table fog and uses vertex (range) fog instead
// Getters return what the game set. Without DXVK (or if the float format is refused) the wrapper
// is a plain pass-through.
#define INITGUID
#include <windows.h>
#include <thread>
#include <mutex>
#include <string>
#include <vector>
#include <mmsystem.h>
#include <d3d8.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "fontgen.h"

static FILE* g_log;
static wchar_t g_dir[MAX_PATH];
static void logf(const char* f, ...) {
    if (!g_log) return;
    va_list a; va_start(a, f); vfprintf(g_log, f, a); va_end(a); fputc('\n', g_log); fflush(g_log);
}
void shr_log(const char* f, ...) {
    if (!g_log) return;
    va_list a; va_start(a, f); vfprintf(g_log, f, a); va_end(a); fputc('\n', g_log); fflush(g_log);
}
// per-pixel lighting (pp9.cpp, d3d9 side)
void pp_installHook(HMODULE d3d8);
void* pp_takeDevice();
void pp_deviceGone();
bool pp_begin(void* dev9, unsigned genK, unsigned genC, DWORD gameFog);
void pp_unbind(void* dev9, DWORD gameFog);
void pp_setEnabled(bool on);
bool pp_enabled();
void pp_setDebug(bool on);
bool pp_debug();
void pp_setBump(float depth, float ao, int level);
int pp_bumpLevel();
void pp_setSharpen(float s);
float pp_sharpen();
void pp_reset();
void pp_setBumpExtra(float chars, float blur, bool world);
void pp_setLightmapFilter(bool on);
void pp_setDetail(float a);
void pp_setAerial(float a);
void pp_alphaToCoverage(void* dev9, bool alphaTest);
static float g_bumpChar = 0.35f, g_bumpBlur = 2.f; static bool g_bumpWorld = true, g_lmFilterIni = true; static float g_detailIni = 0.35f, g_aerialIni = 0.35f; bool g_a2cIni = false;
void pp_present(void* dev9);
void pp_gpuEnable(bool on); void pp_gpuFrameStart(void* dv); void pp_gpuBeforePresent(void* dv); bool pp_gpuReport(char* b, int n, unsigned frames);
static float g_bumpDepth = 1.f, g_bumpAO = 1.5f, g_sharpenIni = 0.f; static int g_bumpIni = 2;
unsigned pp_drawCount();
// ambient occlusion, bloom, character shadows (pp9.cpp)
void pp_rtChanged(void* dev9);
void pp_stateApplied(void* dev9);
bool pp_postPending();
void pp_post(void* dev9);
void pp_castShadow(void* dev9, int kind, int prim, UINT a, UINT b, UINT c, UINT e, INT base, const void* idx, int ifmt, const void* data, UINT stride);
void pp_setEffects(float ssao, float radius, float shadow, float bloom);
void pp_setExtra(float worldSh, float water, float rays);
int pp_fxView();
void pp_setFxView(int v);
unsigned pp_shadowCasts();
void pp_frameEnd(void* dev9);
void pp_zCleared(void* dev9);
void pp_info(bool used, char* b, int n);
void pp_auxPass(void* dev9, int kind, int prim, UINT a, UINT b, UINT c, UINT e, INT base);
bool pp_waterPre(void* dev9);
void pp_waterPost(void* dev9, int kind, int prim, UINT a, UINT b, UINT c, UINT e, INT base);
void pp_setMapLight(bool known, bool sun, float pitchDeg, float yawDeg, float chars, float shadows);
void pp_setNight(bool n); void pp_setBlobOff(bool b); bool pp_isBlob(void* dv);
// night levels: lit by the moon
static bool isNight(const char* m) { return !strcmp(m, "3_the_hunt_part1") || !strcmp(m, "beanstalk_bonus_knight") || !strcmp(m, "2_carriage_hijack") || !strcmp(m, "10_castle_siege") || !strcmp(m, "11_fgm_battle"); }
void pp_worldShadowReset();
void pp_setBloomLevel(float scale, float threshold);
void pp_setBook(bool b);

// ---------------------------------------------------------------- current level
// The level is known from the map file the game opens (Core.dll's CreateFile): the light of each
// level (sun or moon direction for the shadows, brightness of characters) comes from a table made
// from the level files.
#include "maplight.inc"
static char g_mapName[64] = ""; static volatile LONG g_mapSeq = 0;
typedef HANDLE (WINAPI* CreateFileWFn)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI* CreateFileAFn)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileWFn g_cfW; static CreateFileAFn g_cfA;
static void noteFile(const wchar_t* w, const char* a) {
    char n[MAX_PATH] = "";
    if (w) { for (int i = 0; i < MAX_PATH - 1 && w[i]; i++) { n[i] = (char)(w[i] < 128 ? w[i] : '_'); n[i + 1] = 0; } }
    else if (a) { strncpy(n, a, MAX_PATH - 1); n[MAX_PATH - 1] = 0; }
    size_t l = strlen(n); if (l < 5 || _stricmp(n + l - 4, ".unr")) return;
    const char* b = n + l; while (b > n && b[-1] != '\\' && b[-1] != '/') b--;
    char m[64]; size_t k = 0; for (; b[k] && b[k] != '.' && k < 63; k++) m[k] = (char)tolower((unsigned char)b[k]); m[k] = 0;
    if (strcmp(m, g_mapName)) { strcpy(g_mapName, m); InterlockedIncrement(&g_mapSeq); }
}
// only files that really open: the package search also tries Maps\<every package>.unr
static HANDLE WINAPI hkCreateFileW(LPCWSTR f, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD fl, HANDLE t) {
    HANDLE h = g_cfW(f, a, s, sa, c, fl, t);
    if (h != INVALID_HANDLE_VALUE && f && !(a & GENERIC_WRITE)) { DWORD e = GetLastError(); noteFile(f, nullptr); SetLastError(e); }
    return h;
}
static HANDLE WINAPI hkCreateFileA(LPCSTR f, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD fl, HANDLE t) {
    HANDLE h = g_cfA(f, a, s, sa, c, fl, t);
    if (h != INVALID_HANDLE_VALUE && f && !(a & GENERIC_WRITE)) { DWORD e = GetLastError(); noteFile(nullptr, f); SetLastError(e); }
    return h;
}
static int hookIAT(HMODULE mod, const char* dll, const char* fn, void* hook, void** orig) {
    if (!mod) return 0;
    auto base = (BYTE*)mod; auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    FARPROC real = GetProcAddress(GetModuleHandleA(dll), fn); if (!real) return 0;
    int n = 0;
    for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++)
        for (auto th = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk); th->u1.Function; th++) {
            if ((FARPROC)th->u1.Function != real) continue;
            DWORD old; if (!VirtualProtect(&th->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) continue;
            if (!*orig) *orig = (void*)real;
            th->u1.Function = (ULONG_PTR)hook; n++;
            VirtualProtect(&th->u1.Function, sizeof(void*), old, &old);
        }
    return n;
}
static void hookMapFiles() {
    int n = 0;
    for (const char* m : { "Core.dll", "Engine.dll", "Game.exe", "msvcrt.dll", "msvcr70.dll", "msvcr71.dll" }) {
        HMODULE h = GetModuleHandleA(m);
        n += hookIAT(h, "kernel32.dll", "CreateFileW", (void*)hkCreateFileW, (void**)&g_cfW);
        n += hookIAT(h, "kernel32.dll", "CreateFileA", (void*)hkCreateFileA, (void**)&g_cfA);
    }
    logf("levels: watching map files (%d imports)", n);
}
// the sun of the level actually loaded (ShrekPad reads its Sunlight actor): a saved game opens as
// Save<N>, not under its map's name, and the table above would not know it
static volatile LONG g_sunSeq = 0; static float g_sunPitch = 0, g_sunYaw = 0;
extern "C" __declspec(dllexport) void __cdecl ShrSetSun(float pitchDeg, float yawDeg) { g_sunPitch = pitchDeg; g_sunYaw = yawDeg; InterlockedIncrement(&g_sunSeq); }
// ShrekPad: the level's visible lights (torches, lamps, fires) for their shadows - x, y, z, radius each
void pp_setTorches(const float* v, int n); void pp_setTorchShadows(float v);
extern "C" __declspec(dllexport) void __cdecl ShrSetTorches(const float* v, int n) { pp_setTorches(v, n); }
static void sunUpdate() {
    static LONG seen = 0;
    LONG q = g_sunSeq; if (q == seen) return;
    seen = q;
    for (const MapLight& m : MAP_LIGHT) if (!strcmp(m.map, g_mapName)) return;      // a map of the table: its own values
    const MapLight* best = nullptr;
    for (const MapLight& m : MAP_LIGHT) if (m.sun && fabsf(m.pitch - g_sunPitch) < 1.5f && fabsf(fmodf(m.yaw - g_sunYaw + 540.f, 360.f) - 180.f) < 1.5f) best = &m;
    // only a sun the table knows (interiors and night levels keep their light from above)
    if (!best) { logf("level %s: its sun (pitch %.1f, yaw %.1f) is not one of the table's, light from above", g_mapName, g_sunPitch, g_sunYaw); return; }
    pp_setMapLight(true, best->sun, best->pitch, best->yaw, best->chars, best->shadows); pp_setNight(isNight(best->map));
    pp_worldShadowReset();
    logf("level %s: the sun of %s (pitch %.1f, yaw %.1f) - a saved game of that level", g_mapName, best->map, g_sunPitch, g_sunYaw);
}
static volatile LONG g_isBook = 0;
extern "C" __declspec(dllexport) int __cdecl ShrIsBook() { return g_isBook ? 1 : 0; }
void pp_setExposure(float e);
static void mapLightUpdate() {
    sunUpdate();
    static LONG seen = -1;
    LONG q = g_mapSeq; if (q == seen) return;
    seen = q;
    pp_worldShadowReset();
    // the storybook (menu book, story scenes): white parchment under strong light - bloom there would
    // wash it out, so it is much weaker
    bool book = !strncmp(g_mapName, "book_", 5) || !strcmp(g_mapName, "sh2_preamble") || !strcmp(g_mapName, "credits");
    pp_setBloomLevel(book ? 0.f : 1.f, book ? 0.95f : 0.62f);
    g_isBook = book; pp_setExposure(book ? 0.74f : 1.f);
    pp_setBook(book);
    pp_setNight(isNight(g_mapName));
    for (const MapLight& m : MAP_LIGHT) if (!strcmp(m.map, g_mapName)) {
        pp_setMapLight(true, m.sun, m.pitch, m.yaw, m.chars, m.shadows);
        logf("level %s: %s, characters %.0f%%, shadows %.0f%%", g_mapName, m.sun ? "light from the level's sun / moon" : "light from above", m.chars * 100, m.shadows * 100);
        return;
    }
    pp_setMapLight(false, false, 0, 0, 1, 1);
    logf("level %s: light from above", g_mapName);
}
void subs_bugShot(void* dev9);

// ---------------------------------------------------------------- level warm-up
// After a level has loaded the screen stays black (ShrekPad holds the game paused meanwhile) until the
// shaders the level needs are compiled and the level shadow map has been filled from the first view:
// no pop-in of shaders, no shadows drawing themselves in at the start. 40 smooth frames at least, the
// compile queue empty, at most 4 s.
unsigned pp_compileBusy();
static volatile LONG g_warm = 0;
extern "C" __declspec(dllexport) int __cdecl ShrWarmupBusy() { return g_warm ? 1 : 0; }
void pp_fillBlack(void* dev9);
// ShrekPad keeps the screen black after the warm-up until the level's opening scene has started (or 0.8 s):
// the first frames of a level show the heroes standing about before the scene's camera takes over
static volatile LONG g_holdBlack = 0;
extern "C" __declspec(dllexport) void __cdecl ShrHoldBlack(int on) { g_holdBlack = on; }
static void warmTick(void* d) {
    static LONG seen = -1; static int frames = 0; static LARGE_INTEGER f{}, last{}, start{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    double dt = last.QuadPart ? double(q.QuadPart - last.QuadPart) / f.QuadPart : 1; last = q;
    LONG sq = g_mapSeq;
    if (sq != seen) {
        seen = sq;
        bool menu = !*g_mapName || !strcmp(g_mapName, "entry") || !strncmp(g_mapName, "book_", 5) || !strcmp(g_mapName, "credits");
        if (!menu && pp_enabled()) { g_warm = 1; frames = 0; start.QuadPart = 0; logf("level %s: warming up", g_mapName); }
    }
    if (!g_warm) { if (g_holdBlack) pp_fillBlack(d); return; }
    if (dt < 0.1) { if (!start.QuadPart) start = q; frames++; }
    double t = start.QuadPart ? double(q.QuadPart - start.QuadPart) / f.QuadPart : 0;
    unsigned busy = pp_compileBusy();
    if ((frames >= 40 && !busy && t > 0.6) || t > 4.0) { g_warm = 0; logf("level %s: warm-up done in %.1f s (%d frames, %u shaders left)", g_mapName, t, frames, busy); return; }
    if (!start.QuadPart) return;   // the game's own loading picture stays
    pp_fillBlack(d);
}

// ---------------------------------------------------------------- speedrun timer
// EnhancedEdition.ini [Game] SpeedrunTimer=1: the time of this level and of the whole run, at the top
// right. Loads do not count (a frame that took over 0.25 s is a load), nor the time a menu is open
// (ShrekPad: the pause menu shows the cursor), nor the main menu book. The run starts again with a
// new game (the main menu, then the opening of chapter 1).
void subs_setTimer(const std::wstring& a, const std::wstring& b);
bool subs_english();
static int g_timerOn = -1;
static void timerTick() {
    if (g_timerOn < 0) { wchar_t p[MAX_PATH]; swprintf(p, MAX_PATH, L"%lsEnhancedEdition.ini", g_dir); g_timerOn = GetPrivateProfileIntW(L"Game", L"SpeedrunTimer", 0, p) != 0; if (g_timerOn) logf("speedrun timer on"); }
    if (!g_timerOn) return;
    static LARGE_INTEGER f{}, last{}; if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    double dt = last.QuadPart ? double(q.QuadPart - last.QuadPart) / f.QuadPart : 0; last = q;
    if (dt > 0.25 || dt < 0) dt = 0;
    static LONG seen = -1; static char prev[64] = ""; static double lvl = 0, total = 0;
    auto menuMap = [](const char* m) { return !strcmp(m, "book_frontend") || !strcmp(m, "entry") || !*m; };
    LONG sq = g_mapSeq;
    if (sq != seen) {
        seen = sq;
        if (menuMap(prev) && (!strcmp(g_mapName, "sh2_preamble") || !strcmp(g_mapName, "book_story_1") || !strcmp(g_mapName, "1_shreks_swamp"))) { total = 0; logf("speedrun timer: a new run"); }
        else if (!menuMap(prev)) logf("speedrun timer: %s %.2f s, run %.2f s", prev, lvl, total);
        lvl = 0; strncpy(prev, g_mapName, 63);
    }
    typedef int (__cdecl* MenuFn)(); static MenuFn inMenu = nullptr; static bool looked = false;
    if (!looked) { looked = true; HMODULE m = GetModuleHandleW(L"dinput8.dll"); inMenu = m ? (MenuFn)GetProcAddress(m, "ShrekPadInMenu") : nullptr; }
    bool menu = menuMap(g_mapName) || (inMenu && inMenu()) || g_warm;
    if (!menu) { lvl += dt; total += dt; }
    auto fmtT = [](double t, bool cs) {
        int h = (int)(t / 3600), m = (int)(t / 60) % 60, s = (int)t % 60, c = (int)(t * 100) % 100;
        wchar_t b[32];
        if (h) swprintf(b, 32, L"%d:%02d:%02d", h, m, s); else if (cs) swprintf(b, 32, L"%d:%02d.%02d", m, s, c); else swprintf(b, 32, L"%d:%02d.%d", m, s, c / 10);
        return std::wstring(b);
    };
    if (menuMap(g_mapName) && total == 0) { subs_setTimer(L"", L""); return; }
    bool en = subs_english();
    subs_setTimer((en ? L"Level  " : L"Уровень  ") + fmtT(lvl, true), (en ? L"Total  " : L"Всего  ") + fmtT(total, false));
}
static float g_ssaoIni = 1.f, g_ssaoRadiusIni = 55.f, g_shadowIni = 0.6f, g_bloomIni = 0.f, g_worldShIni = 1.f, g_waterIni = 0.6f, g_raysIni = 0.5f;
static bool g_perPixel = true;
static int g_ppKey = VK_F3, g_ppOnlyKey = VK_F4, g_reliefKey = VK_F5, g_fxKey = VK_F6, g_casKey = VK_F1;

static const D3DFORMAT FMT_D32F = (D3DFORMAT)82;     // D3DFMT_D32F_LOCKABLE: d3d9 only, DXVK's d3d8 refuses it
// The float buffer comes from DXVK itself: with d3d9.useD32forD24 = True in dxvk.conf every 24-bit
// depth format becomes VK_FORMAT_D32_SFLOAT(_S8). The game then just asks for D24X8.
static const D3DFORMAT FMT_DEPTH = D3DFMT_D24X8;
static const DWORD BIAS_REG = 95;                     // vertex shader constant used for the depth scale
static const float BIAS_PER_UNIT = 2e-5f;             // relative depth pull per ZBIAS step
static bool g_wantReverse = true, g_planned = false, g_capture = true;
static int g_capKey = VK_F10;

static DWORD flipCmp(DWORD f) {
    switch (f) { case D3DCMP_LESS: return D3DCMP_GREATER; case D3DCMP_GREATER: return D3DCMP_LESS;
                 case D3DCMP_LESSEQUAL: return D3DCMP_GREATEREQUAL; case D3DCMP_GREATEREQUAL: return D3DCMP_LESSEQUAL; }
    return f;
}



// ---------------------------------------------------------------- engine memcpy
// Every Unreal module carries an inlined appMemcpy that, when the CPU has MMX, copies through MMX
// registers and ends each copy with EMMS + SFENCE - even for 32-byte copies, which the engine makes
// by the hundred thousand per frame (mesh data). On today's CPUs that path is far slower than the
// plain REP MOVSD the same function falls back to without MMX. The test of the MMX flag is turned
// into an unconditional jump to that fallback, in every loaded module.
// The fallback itself (REP MOVSD) is then replaced by a jump to the C runtime's memcpy, which
// copies small blocks with SSE instead of the slow string instruction start-up.
#include <tlhelp32.h>
extern "C" void shr_memcpy_thunk();
asm(".text\n.globl _shr_memcpy_thunk\n_shr_memcpy_thunk:\n"
    "  pushl 16(%ebp)\n  pushl 12(%ebp)\n  pushl 8(%ebp)\n  call _memcpy\n  addl $12, %esp\n"
    "  popl %edi\n  popl %esi\n  popl %ebx\n  popl %ebp\n  ret\n");
static std::vector<BYTE*> g_memcpyDone;
static bool g_memcpyCrt = true;
static void patchMemcpy() {
    static const BYTE tail[] = { 0x8b, 0x4d, 0x10, 0x8b, 0x7d, 0x08, 0x8b, 0x75, 0x0c, 0x8b, 0xd9, 0xfc };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W me{}; me.dwSize = sizeof me;
    int patched = 0;
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
        BYTE* base = me.modBaseAddr;
        if (std::find(g_memcpyDone.begin(), g_memcpyDone.end(), base) != g_memcpyDone.end()) continue;
        g_memcpyDone.push_back(base);
        auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        auto sec = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
            if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            BYTE* p = base + sec->VirtualAddress; size_t n = sec->Misc.VirtualSize;
            for (size_t k = 0; k + 26 < n; k++) {
                // mov eax,[flag]; cmp dword [eax],0; je fallback; mov ecx,[ebp+10h] ...
                if (p[k] != 0xa1 || p[k + 5] != 0x83 || p[k + 6] != 0x38 || p[k + 7] != 0x00 || p[k + 8] != 0x0f || p[k + 9] != 0x84) continue;
                if (memcmp(p + k + 14, tail, sizeof tail)) continue;
                DWORD old;
                // 1. the fallback (not reached yet: the CPU has MMX) jumps to the CRT memcpy
                static const BYTE fb[] = { 0x8b, 0x4d, 0x10, 0x8b, 0x75, 0x0c, 0x8b, 0x7d, 0x08, 0x8b, 0xd9, 0xc1, 0xe9, 0x02, 0x83, 0xe3, 0x03,
                                           0xf3, 0xa5, 0x8b, 0xcb, 0xf3, 0xa4, 0x5f, 0x5e, 0x5b, 0x5d, 0xc3 };
                BYTE* f = p + k + 14 + *(int32_t*)(p + k + 10);
                bool crt = false;
                if (g_memcpyCrt && f > base && f + sizeof fb < base + me.modBaseSize && !memcmp(f, fb, sizeof fb) &&
                    VirtualProtect(f, 5, PAGE_EXECUTE_READWRITE, &old)) {
                    f[0] = 0xe9; *(int32_t*)(f + 1) = (int32_t)((BYTE*)&shr_memcpy_thunk - (f + 5));
                    VirtualProtect(f, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), f, 5);
                    crt = true;
                }
                // 2. the MMX test always takes the fallback
                if (VirtualProtect(p + k + 8, 2, PAGE_EXECUTE_READWRITE, &old)) {
                    BYTE* q = p + k + 8;                          // nop; jmp rel32 (same target)
                    if (!((ULONG_PTR)q & 1)) __atomic_store_n((unsigned short*)q, (unsigned short)0xe990, __ATOMIC_SEQ_CST);   // in one store: another thread may be copying
                    else { q[1] = 0xe9; q[0] = 0x90; }
                    VirtualProtect(p + k + 8, 2, old, &old);
                    FlushInstructionCache(GetCurrentProcess(), p + k + 8, 2);
                    patched++;
                    char nm[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, me.szModule, -1, nm, MAX_PATH, nullptr, nullptr);
                    logf("memcpy: %s uses %s now", nm, crt ? "the C runtime memcpy" : "the plain copy");
                }
            }
        }
    }
    CloseHandle(snap);
}
static bool g_fastMemcpy = true;

// ---------------------------------------------------------------- performance diagnostics
// Every 2 seconds ShrekRender.log gets where the game thread's time per frame went: inside
// Direct3D (DXVK), in the lighting shaders' bookkeeping, waiting in Present, and the rest - the
// game itself (UnrealScript, physics, animation, visibility). On CPUs with performance and
// efficiency cores it also logs how often the game thread ran on an efficiency core, and (by
// default) keeps it on the performance cores.
static LARGE_INTEGER g_qpf;
static LONGLONG g_tD3D, g_tPP, g_tPresent, g_tFrameStart, g_tWindow, g_worst, g_lD3D, g_lPP, g_lPres;
static unsigned g_frames, g_draws, g_ppDraws, g_eSamples, g_blendDraws;
static bool g_perfLog = false, g_pinFast = true, g_profile = false;
void prof_start(const wchar_t* dir);
unsigned pp_takeMade();
void subs_init(const wchar_t* dir);
void subs_draw(void* dv);
void rumble_config(bool on, bool steps);
void rumble_camera(const float* pos);
void rumble_frame();
struct PT { LONGLONG t0; LONGLONG* acc; explicit PT(LONGLONG* a = &g_tD3D) : acc(a) { LARGE_INTEGER q; QueryPerformanceCounter(&q); t0 = q.QuadPart; }
            ~PT() { LARGE_INTEGER q; QueryPerformanceCounter(&q); *acc += q.QuadPart - t0; } };
static std::vector<BYTE> g_effClass;      // per logical processor, higher = faster
static BYTE g_maxClass = 0; static bool g_hybrid = false;
typedef BOOL (WINAPI* GetCpuSetsFn)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
typedef BOOL (WINAPI* SetThreadCpuSetsFn)(HANDLE, const ULONG*, ULONG);
static void cpuSetup() {
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    auto get = (GetCpuSetsFn)GetProcAddress(k, "GetSystemCpuSetInformation");
    auto set = (SetThreadCpuSetsFn)GetProcAddress(k, "SetThreadSelectedCpuSets");
    if (!get) return;
    ULONG len = 0; get(nullptr, 0, &len, GetCurrentProcess(), 0);
    std::vector<BYTE> buf(len);
    if (!len || !get((PSYSTEM_CPU_SET_INFORMATION)buf.data(), len, &len, GetCurrentProcess(), 0)) return;
    std::vector<ULONG> fast; BYTE minClass = 255;
    for (ULONG off = 0; off < len;) {
        auto e = (PSYSTEM_CPU_SET_INFORMATION)(buf.data() + off);
        if (e->Type == CpuSetInformation) {
            BYTE idx = e->CpuSet.LogicalProcessorIndex, cls = e->CpuSet.EfficiencyClass;
            if (g_effClass.size() <= idx) g_effClass.resize(idx + 1, 0);
            g_effClass[idx] = cls;
            if (cls > g_maxClass) { g_maxClass = cls; fast.clear(); }
            if (cls == g_maxClass) fast.push_back(e->CpuSet.Id);
            if (cls < minClass) minClass = cls;
        }
        off += e->Size;
    }
    g_hybrid = minClass != g_maxClass;
    logf("cpu: %u logical processors, %s", (unsigned)g_effClass.size(), g_hybrid ? "performance + efficiency cores" : "all cores alike");
    if (g_hybrid && g_pinFast && set && !fast.empty()) {
        BOOL ok = set(GetCurrentThread(), fast.data(), (ULONG)fast.size());
        logf("cpu: game thread kept on the %u performance threads: %s", (unsigned)fast.size(), ok ? "ok" : "refused");
    }
}
// menus: at most this many frames per second (ShrekPad sets it while a menu has the cursor) - the
// pause menu drew the whole level behind its book as fast as the card could, at full load
static volatile int g_frameCap = 0;
extern "C" __declspec(dllexport) void __cdecl ShrSetFrameCap(int fps) {
    static bool fine = false; if (fps > 0 && !fine) { fine = true; timeBeginPeriod(1); }   // Sleep(1) of 1 ms, not 15
    g_frameCap = fps < 0 ? 0 : fps;
}
static void frameCap() {
    static LARGE_INTEGER last{}, f{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    int cap = g_frameCap;
    if (cap > 0 && last.QuadPart) {
        LONGLONG want = last.QuadPart + f.QuadPart / cap;
        while (now.QuadPart < want) {
            LONGLONG left = (want - now.QuadPart) * 1000 / f.QuadPart;
            Sleep(left > 2 ? 1 : 0);
            QueryPerformanceCounter(&now);
        }
    }
    last = now;
}
static void perfFrame(unsigned ppDraws) {
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    if (!g_qpf.QuadPart) { QueryPerformanceFrequency(&g_qpf); g_tWindow = g_tFrameStart = q.QuadPart; cpuSetup(); if (g_fastMemcpy) patchMemcpy(); if (g_profile) prof_start(g_dir); return; }
    LONGLONG ft = q.QuadPart - g_tFrameStart; g_tFrameStart = q.QuadPart;
    if (ft > g_worst) g_worst = ft;
    {   // a frame over 40 ms: where its time went (ShrekRender.log, first 300 of them)
        static unsigned hitches = 0; static ULONGLONG t0 = GetTickCount64();
        unsigned made = pp_takeMade();
        double f = 1000.0 / g_qpf.QuadPart, ms = ft * f;
        if (ms > 40.0 && hitches < 300 && GetTickCount64() - t0 > 5000) {
            hitches++;
            double d = (g_tD3D - g_lD3D) * f, l = (g_tPP - g_lPP) * f, pr = (g_tPresent - g_lPres) * f;
            logf("hitch: %.0f ms = game %.0f + d3d %.0f + lighting %.0f + present %.0f, shaders made %u", ms, ms - d - l - pr, d, l, pr, made);
        }
        g_lD3D = g_tD3D; g_lPP = g_tPP; g_lPres = g_tPresent;
    }
    g_frames++; g_ppDraws += ppDraws;
    if (g_hybrid) { DWORD c = GetCurrentProcessorNumber(); if (c < g_effClass.size() && g_effClass[c] < g_maxClass) g_eSamples++; }
    double win = double(q.QuadPart - g_tWindow) / g_qpf.QuadPart;
    if (win >= 2.0 && g_frames) {
        double ms = 1000.0 / g_qpf.QuadPart / g_frames, frame = win * 1000.0 / g_frames;
        double d3d = g_tD3D * ms, pp = g_tPP * ms, pres = g_tPresent * ms;
        if (g_perfLog)
            logf("perf: %.0f fps, worst %.1f ms | per frame %.2f ms = game %.2f + d3d %.2f + lighting %.2f + present %.2f | draws %u (%u lit by us, %u blended)%s",
                 g_frames / win, g_worst * 1000.0 / g_qpf.QuadPart, frame, frame - d3d - pp - pres, d3d, pp, pres,
                 g_draws / g_frames, g_ppDraws / g_frames, g_blendDraws / g_frames,
                 g_hybrid ? (std::string(", on efficiency cores ") + std::to_string(g_eSamples * 100 / g_frames) + "%").c_str() : "");
        if (g_perfLog) { char gb[400]; if (pp_gpuReport(gb, sizeof gb, g_frames)) logf("%s", gb); }
        static unsigned memcpyPasses = 0;   // dlls loaded late (the game's own load in the first seconds); then no more module walks
        if (g_fastMemcpy && memcpyPasses < 15) { memcpyPasses++; patchMemcpy(); }
        g_tWindow = q.QuadPart; g_tD3D = g_tPP = g_tPresent = g_worst = 0; g_lD3D = g_lPP = g_lPres = 0; g_frames = g_draws = g_ppDraws = g_eSamples = g_blendDraws = 0;
    }
}

// ---------------------------------------------------------------- vertex shader rewrite
// vs.1.x token stream: instruction token (bit 31 clear), then parameter tokens (bit 31 set);
// register type in bits 28-30, number in bits 0-10, write mask in bits 16-19 (destination).
static bool rewriteVS(const DWORD* src, std::vector<DWORD>& out) {
    if (!src || (src[0] & 0xFFFF0000) != 0xFFFE0000) return false;
    size_t n = 1; int maxTemp = -1; bool writesPos = false;
    while (src[n] != 0x0000FFFF) {
        DWORD t = src[n];
        if ((t & 0xFFFF) == 0xFFFE) { n += 1 + ((t >> 16) & 0x7FFF); continue; }
        DWORD op = t & 0xFFFF; n++;
        if (op == 0x51) { n += 5; continue; }                       // def: dest + 4 literals
        bool first = true;
        while (src[n] & 0x80000000u) {
            DWORD p = src[n++]; DWORD type = (p >> 28) & 7, reg = p & 0x7FF;
            if (type == 0 && (int)reg > maxTemp) maxTemp = reg;
            if (first && type == 4 && reg == 0) writesPos = true;
            first = false;
        }
        if (n > 65536) return false;
    }
    if (!writesPos) return false;
    DWORD rT = maxTemp + 1;
    if (rT > 11) { logf("vertex shader uses all 12 temporaries, left as is"); return false; }
    out.assign(src, src + n);
    // redirect every write to oPos into rT
    for (size_t i = 1; i < out.size();) {
        DWORD t = out[i];
        if ((t & 0xFFFF) == 0xFFFE) { i += 1 + ((t >> 16) & 0x7FFF); continue; }
        DWORD op = t & 0xFFFF; i++;
        if (op == 0x51) { i += 5; continue; }
        if (i < out.size() && (out[i] & 0x80000000u)) {
            DWORD p = out[i];
            if (((p >> 28) & 7) == 4 && (p & 0x7FF) == 0) out[i] = (p & ~(0x70000000u | 0x7FFu)) | rT;
        }
        while (i < out.size() && (out[i] & 0x80000000u)) i++;
    }
    const DWORD PRM = 0x80000000u, TEMP = 0u << 28, RAST = 4u << 28, RCONST = 2u << 28;
    auto dst = [&](DWORD type, DWORD reg, DWORD mask) { return PRM | type | reg | (mask << 16); };
    auto srcp = [&](DWORD type, DWORD reg, DWORD swz, DWORD mod = 0) { return PRM | type | reg | (swz << 16) | (mod << 24); };
    // mov oPos.xyw, rT
    out.push_back(0x01); out.push_back(dst(RAST, 0, 0xB)); out.push_back(srcp(TEMP, rT, 0xE4));
    // add rT.z, rT.w, -rT.z
    out.push_back(0x02); out.push_back(dst(TEMP, rT, 0x4)); out.push_back(srcp(TEMP, rT, 0xFF)); out.push_back(srcp(TEMP, rT, 0xAA, 1));
    // mul oPos.z, rT.z, c95.x
    out.push_back(0x05); out.push_back(dst(RAST, 0, 0x4)); out.push_back(srcp(TEMP, rT, 0xAA)); out.push_back(srcp(RCONST, BIAS_REG, 0x00));
    out.push_back(0x0000FFFF);
    return true;
}

// ---------------------------------------------------------------- frame capture (diagnostics)
// F10 (or Scroll Lock; [Debug] CaptureKey = virtual key code) in game: the next frame's draw calls with their lighting, texture stage and shader
// state go to ShrekRender_frame.txt and the picture to ShrekRender_frame.bmp. Every vertex shader
// the game creates is written to ShrekRender_shaders.txt. This is how the lighting of characters
// is studied before it is changed.
static FILE* g_shaders;
static volatile bool g_captureReq = false;   // ShrekPad's bug mark (F12): the draws of the next frame to ShrekRender_frame.txt
extern "C" __declspec(dllexport) void __cdecl ShrCaptureFrame() { g_captureReq = true; }
struct ShaderRec { DWORD handle; std::vector<DWORD> decl, code; };
static std::vector<ShaderRec> g_shaderList;
static int shaderIndex(DWORD h) { for (size_t i = 0; i < g_shaderList.size(); i++) if (g_shaderList[i].handle == h) return (int)i; return -1; }
static void dumpShader(int idx, const DWORD* decl, const DWORD* code) {
    if (!g_shaders) { wchar_t p[MAX_PATH]; swprintf(p, MAX_PATH, L"%lsShrekRender_shaders.txt", g_dir); g_shaders = _wfopen(p, L"w"); }
    if (!g_shaders) return;
    fprintf(g_shaders, "== shader %d\ndecl:", idx);
    for (size_t i = 0; decl && i < 256; i++) { fprintf(g_shaders, " %08lx", decl[i]); if (decl[i] == 0xFFFFFFFF) break; }
    fprintf(g_shaders, "\ncode:");
    for (size_t i = 0; code && i < 8192; i++) { fprintf(g_shaders, " %08lx", code[i]); if (code[i] == 0x0000FFFF) break; }
    fprintf(g_shaders, "\n"); fflush(g_shaders);
}

// ---------------------------------------------------------------- device
struct DevW;
struct D3D8W : IDirect3D8 {
    IDirect3D8* r; LONG ref = 1;
    explicit D3D8W(IDirect3D8* real) : r(real) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** p) override {
        if (id == IID_IUnknown || id == IID_IDirect3D8) { *p = this; AddRef(); return S_OK; }
        return r->QueryInterface(id, p);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override { LONG c = InterlockedDecrement(&ref); if (!c) { r->Release(); delete this; } return c; }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT a, D3DDEVTYPE t, D3DCAPS8* c) override {
        HRESULT h = r->GetDeviceCaps(a, t, c);
        if (SUCCEEDED(h) && g_planned) c->RasterCaps &= ~D3DPRASTERCAPS_FOGTABLE;
        return h;
    }
    HRESULT STDMETHODCALLTYPE CreateDevice(UINT a, D3DDEVTYPE t, HWND w, DWORD f, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice8** out) override;
#include "fwd_d3d8.inc"
};

struct DevW : IDirect3DDevice8 {
    IDirect3DDevice8* r; D3D8W* parent; LONG ref = 1;
    bool active = false;
    D3DFORMAT gameDepthFmt = D3DFMT_UNKNOWN;
    D3DMATRIX proj; bool haveProj = false;
    DWORD zfunc = D3DCMP_LESSEQUAL, zbias = 0, zenable = D3DZB_TRUE;
    D3DVIEWPORT8 vp{}; bool haveVp = false;
    bool warnedRHW = false, warnedC95 = false;
    void* dev9 = nullptr;                              // DXVK's d3d9 device behind this one (per-pixel lighting)
    // what the game has set, to tell the lighting shaders when something really changed
    unsigned genK = 1, genC = 1;
    DWORD rsShadow[256] = {}, tssShadow[8][32] = {};
    IDirect3DBaseTexture8* texShadow[8];
    void reloadShadow() {
        for (DWORD i = 0; i < 256; i++) { DWORD v = 0; if (SUCCEEDED(r->GetRenderState((D3DRENDERSTATETYPE)i, &v))) rsShadow[i] = v; }
        if (active) { rsShadow[D3DRS_ZFUNC] = zfunc; rsShadow[D3DRS_ZBIAS] = zbias; rsShadow[D3DRS_ZENABLE] = zenable; }
        for (DWORD st = 0; st < 8; st++) for (DWORD t = 0; t < 32; t++) { DWORD v = 0; if (SUCCEEDED(r->GetTextureStageState(st, (D3DTEXTURESTAGESTATETYPE)t, &v))) tssShadow[st][t] = v; }
        for (auto& t : texShadow) t = (IDirect3DBaseTexture8*)(intptr_t)-1;
        genK++; genC++;
    }
    // camera of the frame (for the vibration's distances): the view matrix most draws used
    struct ViewUse { D3DMATRIX m; unsigned draws; };
    ViewUse views[8]; int nViews = 0, curView = -1;
    void noteView(const D3DMATRIX& m) {
        bool ident = m.m[0][0] == 1 && m.m[1][1] == 1 && m.m[2][2] == 1 && m.m[3][0] == 0 && m.m[3][1] == 0 && m.m[3][2] == 0;
        if (ident) { curView = -1; return; }
        for (int i = 0; i < nViews; i++) if (!memcmp(&views[i].m, &m, sizeof m)) { curView = i; return; }
        if (nViews < 8) { views[nViews].m = m; views[nViews].draws = 0; curView = nViews++; } else curView = -1;
    }
    void frameCamera() {
        int best = -1; for (int i = 0; i < nViews; i++) if (best < 0 || views[i].draws > views[best].draws) best = i;
        if (best >= 0 && views[best].draws > 0) {
            const D3DMATRIX& v = views[best].m; float c[3];
            for (int j = 0; j < 3; j++) c[j] = -(v.m[3][0] * v.m[j][0] + v.m[3][1] * v.m[j][1] + v.m[3][2] * v.m[j][2]);
            rumble_camera(c);
        }
        nViews = 0; curView = -1;
        D3DMATRIX m; if (SUCCEEDED(r->GetTransform(D3DTS_VIEW, &m))) noteView(m);   // the view stays set across frames
    }
    void ppUnbind() { if (dev9) pp_unbind(dev9, rsShadow[D3DRS_FOGENABLE]); }
    UINT baseVtx = 0;
    // the first 2D draw (pretransformed vertices, or no depth test with an orthographic projection)
    // ends the 3D scene: ambient occlusion and bloom go in before the HUD and menus
    void fxCheck2D() {
        if (!dev9 || !pp_postPending()) return;
        bool twoD = false;
        bool fvf = !(curVS & 1) && curVS <= 0xFFFF;
        if (fvf && (curVS & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) twoD = true;
        else if (rsShadow[D3DRS_ZENABLE] == D3DZB_FALSE) {
            D3DMATRIX m; if (SUCCEEDED(r->GetTransform(D3DTS_PROJECTION, &m))) twoD = m.m[2][3] == 0 && m.m[3][3] == 1;
        }
        if (!twoD) return;
        ppUnbind();
        PT t(&g_tPP);
        pp_post(dev9);
    }
    bool ppBegin() {
        g_draws++; if (rsShadow[D3DRS_ALPHABLENDENABLE]) g_blendDraws++;
        if (curView >= 0) views[curView].draws++;
        if (!dev9) return false;
        PT t(&g_tPP);
        bool used = pp_begin(dev9, genK, genC, rsShadow[D3DRS_FOGENABLE]);
        if (!used) pp_unbind(dev9, rsShadow[D3DRS_FOGENABLE]);
        if (cf) { char b[512]; pp_info(used, b, sizeof b); fprintf(cf, "%s\n", b); }
        return used;
    }

    DevW(IDirect3DDevice8* real, D3D8W* p) : r(real), parent(p) { parent->AddRef(); }
    float biasScale() const { return 1.f + BIAS_PER_UNIT * (float)zbias; }
    void applyProj() {
        if (!haveProj) return;
        D3DMATRIX m = proj; float s = biasScale();
        for (int i = 0; i < 4; i++) m.m[i][2] = (proj.m[i][3] - proj.m[i][2]) * s;
        r->SetTransform(D3DTS_PROJECTION, &m);
    }
    void applyBiasConst() { float c[4] = { biasScale(), 0, 0, 0 }; r->SetVertexShaderConstant(BIAS_REG, c, 1); }
    void initState() {        // the device defaults, as the game expects them, in reversed form
        if (!active) return;
        zfunc = D3DCMP_LESSEQUAL; zbias = 0; haveProj = false; haveVp = false;
        r->SetRenderState(D3DRS_ZFUNC, flipCmp(zfunc));
        applyBiasConst();
        D3DVIEWPORT8 v; if (SUCCEEDED(r->GetViewport(&v))) { vp = v; haveVp = true; v.MinZ = 1 - vp.MaxZ; v.MaxZ = 1 - vp.MinZ; r->SetViewport(&v); }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** p) override {
        if (id == IID_IUnknown || id == IID_IDirect3DDevice8) { *p = this; AddRef(); return S_OK; }
        return r->QueryInterface(id, p);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG c = InterlockedDecrement(&ref);
        if (!c) {
            if (dev9) { ppUnbind(); pp_deviceGone(); }    // our objects go first, while the device still exists
            r->Release(); parent->Release(); delete this;
        }
        return c;
    }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D8** p) override { parent->AddRef(); *p = parent; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS8* c) override {
        HRESULT h = r->GetDeviceCaps(c);
        if (SUCCEEDED(h) && g_planned) c->RasterCaps &= ~D3DPRASTERCAPS_FOGTABLE;
        return h;
    }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pp) override {
        HRESULT h;
        ppUnbind();
        if (dev9) pp_reset();
        if (active && pp && pp->EnableAutoDepthStencil) {
            D3DFORMAT f = pp->AutoDepthStencilFormat; pp->AutoDepthStencilFormat = FMT_DEPTH;
            h = r->Reset(pp); pp->AutoDepthStencilFormat = f;
            if (FAILED(h)) logf("Reset with the float depth buffer failed: 0x%08lx", h);
        } else h = r->Reset(pp);
        if (SUCCEEDED(h)) initState();
        reloadShadow();
        return h;
    }
    HRESULT STDMETHODCALLTYPE Clear(DWORD n, const D3DRECT* rc, DWORD flags, D3DCOLOR col, float z, DWORD st) override {
        PT pt_;
        HRESULT h = r->Clear(n, rc, flags, col, active ? 1.f - z : z, st);
        if (cf) fprintf(cf, "CLEAR flags=%lx rects=%lu\n", flags, n);
        if (dev9 && (flags & D3DCLEAR_ZBUFFER) && n == 0) pp_zCleared(dev9);
        return h;
    }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE s, const D3DMATRIX* m) override {
        PT pt_;
        genC++;
        if (s == D3DTS_VIEW && m) noteView(*m);
        if (!active || s != D3DTS_PROJECTION || !m) return r->SetTransform(s, m);
        proj = *m; haveProj = true; applyProj(); return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE s, D3DMATRIX* m) override {
        if (active && s == D3DTS_PROJECTION && haveProj && m) { *m = proj; return D3D_OK; }
        return r->GetTransform(s, m);
    }
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE s, const D3DMATRIX* m) override {
        if (!active || s != D3DTS_PROJECTION || !m) return r->MultiplyTransform(s, m);
        D3DMATRIX a = haveProj ? proj : D3DMATRIX{ { { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 } } }, o;
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { float v = 0; for (int k = 0; k < 4; k++) v += a.m[i][k] * m->m[k][j]; o.m[i][j] = v; }
        return SetTransform(s, &o);
    }
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT8* v) override {
        if (!active || !v) return r->SetViewport(v);
        vp = *v; haveVp = true; D3DVIEWPORT8 w = *v; w.MinZ = 1 - v->MaxZ; w.MaxZ = 1 - v->MinZ;
        return r->SetViewport(&w);
    }
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT8* v) override {
        if (active && haveVp && v) { *v = vp; return D3D_OK; }
        return r->GetViewport(v);
    }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE s, DWORD v) override {
        PT pt_;
        if ((DWORD)s >= 256 || rsShadow[s] != v) { if ((DWORD)s < 256) rsShadow[s] = v; genK++; genC++; }
        if (active) switch (s) {
        case D3DRS_ZFUNC: zfunc = v; return r->SetRenderState(s, flipCmp(v));
        case D3DRS_ZBIAS: if (v != zbias) { zbias = v; applyProj(); applyBiasConst(); } return D3D_OK;
        case D3DRS_ZENABLE: zenable = v; return r->SetRenderState(s, v == D3DZB_USEW ? D3DZB_TRUE : v);
        case D3DRS_ALPHATESTENABLE: if (dev9) pp_alphaToCoverage(dev9, v != 0); break;
        default: break;
        }
        return r->SetRenderState(s, v);
    }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE s, DWORD* v) override {
        PT pt_;
        if (s == D3DRS_FOGENABLE && dev9 && v) { *v = rsShadow[s]; return D3D_OK; }
        if (active && v) switch (s) {
        case D3DRS_ZFUNC: *v = zfunc; return D3D_OK;
        case D3DRS_ZBIAS: *v = zbias; return D3D_OK;
        case D3DRS_ZENABLE: *v = zenable; return D3D_OK;
        default: break;
        }
        return r->GetRenderState(s, v);
    }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* decl, const DWORD* code, DWORD* handle, DWORD usage) override {
        HRESULT h = createVS(decl, code, handle, usage);
        if (SUCCEEDED(h) && handle && g_capture) {
            ShaderRec rec; rec.handle = *handle;
            g_shaderList.push_back(rec);
            dumpShader((int)g_shaderList.size() - 1, decl, code);
        }
        return h;
    }
    HRESULT createVS(const DWORD* decl, const DWORD* code, DWORD* handle, DWORD usage) {
        if (active && code) {
            std::vector<DWORD> nc;
            if (rewriteVS(code, nc)) {
                HRESULT h = r->CreateVertexShader(decl, nc.data(), handle, usage);
                if (SUCCEEDED(h)) return h;
                logf("rewritten vertex shader refused (0x%08lx), using the original", h);
            }
        }
        return r->CreateVertexShader(decl, code, handle, usage);
    }
    // ---- capture
    DWORD curVS = 0; float vsc[96][4] = {}; bool vscDirty[96] = {};
    int capFrame = 0; FILE* cf = nullptr; int capDraw = 0;
    void capState(const char* kind, unsigned prims, unsigned verts, UINT stride) {
        if (!cf) return;
        capDraw++;
        DWORD rs[16]; const D3DRENDERSTATETYPE rsl[16] = { D3DRS_LIGHTING, D3DRS_AMBIENT, D3DRS_COLORVERTEX, D3DRS_DIFFUSEMATERIALSOURCE, D3DRS_AMBIENTMATERIALSOURCE,
            D3DRS_EMISSIVEMATERIALSOURCE, D3DRS_SPECULARENABLE, D3DRS_NORMALIZENORMALS, D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
            D3DRS_ALPHATESTENABLE, D3DRS_ZWRITEENABLE, D3DRS_FOGENABLE, D3DRS_CULLMODE, D3DRS_TEXTUREFACTOR };
        for (int i = 0; i < 16; i++) { rs[i] = 0; r->GetRenderState(rsl[i], &rs[i]); }
        int si = (curVS & 1) || curVS > 0xFFFF ? shaderIndex(curVS) : -1;
        DWORD curPS = 0; r->GetPixelShader(&curPS);
        fprintf(cf, "#%d %s prims=%u verts=%u stride=%u ps=%lx vs=", capDraw, kind, prims, verts, stride, curPS);
        if (si >= 0) fprintf(cf, "shader%d", si); else fprintf(cf, "fvf%08lx", curVS);
        fprintf(cf, " light=%lu amb=%08lx colvtx=%lu diffsrc=%lu ambsrc=%lu emissrc=%lu spec=%lu norm=%lu blend=%lu(%lu,%lu) atest=%lu zw=%lu fog=%lu cull=%lu tfac=%08lx\n",
                rs[0], rs[1], rs[2], rs[3], rs[4], rs[5], rs[6], rs[7], rs[8], rs[9], rs[10], rs[11], rs[12], rs[13], rs[14], rs[15]);
        {
            IDirect3DVertexBuffer8* vb = nullptr; UINT st = 0; bool dyn = false;
            if (SUCCEEDED(r->GetStreamSource(0, &vb, &st)) && vb) { D3DVERTEXBUFFER_DESC bd{}; vb->GetDesc(&bd); dyn = (bd.Usage & D3DUSAGE_DYNAMIC) != 0; vb->Release(); }
            D3DMATRIX w{}; r->GetTransform(D3DTS_WORLD, &w);
            fprintf(cf, "   stream0 %s, world pos(%.0f %.0f %.0f) scale %.2f\n", vb ? (dyn ? "dynamic" : "static") : "none", w.m[3][0], w.m[3][1], w.m[3][2], w.m[0][0]);
        }
        if (rs[0]) {
            D3DMATERIAL8 m{}; r->GetMaterial(&m);
            fprintf(cf, "   material D(%.2f %.2f %.2f %.2f) A(%.2f %.2f %.2f) E(%.2f %.2f %.2f) S(%.2f) P%.1f\n", m.Diffuse.r, m.Diffuse.g, m.Diffuse.b, m.Diffuse.a,
                    m.Ambient.r, m.Ambient.g, m.Ambient.b, m.Emissive.r, m.Emissive.g, m.Emissive.b, m.Specular.r, m.Power);
            for (DWORD i = 0; i < 8; i++) {
                BOOL on = FALSE; if (FAILED(r->GetLightEnable(i, &on)) || !on) continue;
                D3DLIGHT8 l{}; r->GetLight(i, &l);
                fprintf(cf, "   light%lu type=%d D(%.2f %.2f %.2f) A(%.2f %.2f %.2f) pos(%.0f %.0f %.0f) dir(%.2f %.2f %.2f) range=%.0f att(%.3g %.3g %.3g)\n", i, (int)l.Type,
                        l.Diffuse.r, l.Diffuse.g, l.Diffuse.b, l.Ambient.r, l.Ambient.g, l.Ambient.b, l.Position.x, l.Position.y, l.Position.z,
                        l.Direction.x, l.Direction.y, l.Direction.z, l.Range, l.Attenuation0, l.Attenuation1, l.Attenuation2);
            }
        }
        for (DWORD st = 0; st < 8; st++) {
            DWORD op = 0, a1 = 0, a2 = 0, aop = 0, tci = 0, ttf = 0; r->GetTextureStageState(st, D3DTSS_COLOROP, &op);
            if (op == D3DTOP_DISABLE) break;
            r->GetTextureStageState(st, D3DTSS_COLORARG1, &a1); r->GetTextureStageState(st, D3DTSS_COLORARG2, &a2);
            r->GetTextureStageState(st, D3DTSS_ALPHAOP, &aop); r->GetTextureStageState(st, D3DTSS_TEXCOORDINDEX, &tci); r->GetTextureStageState(st, D3DTSS_TEXTURETRANSFORMFLAGS, &ttf);
            IDirect3DBaseTexture8* t = nullptr; r->GetTexture(st, &t);
            char td[64] = "none";
            if (t) {
                D3DRESOURCETYPE ty = t->GetType();
                if (ty == D3DRTYPE_TEXTURE) { D3DSURFACE_DESC d{}; ((IDirect3DTexture8*)t)->GetLevelDesc(0, &d); snprintf(td, sizeof td, "%p %ux%u f%d", (void*)t, d.Width, d.Height, (int)d.Format); }
                else snprintf(td, sizeof td, "%p type%d", (void*)t, (int)ty);
                t->Release();
            }
            fprintf(cf, "   stage%lu op=%lu(%lx,%lx) aop=%lu tci=%lx ttf=%lx tex=%s\n", st, op, a1, a2, aop, tci, ttf, td);
        }
        if (si >= 0) {
            fprintf(cf, "   consts:");
            for (int i = 0; i < 96; i++) if (vscDirty[i]) { fprintf(cf, " c%d(%.3g %.3g %.3g %.3g)", i, vsc[i][0], vsc[i][1], vsc[i][2], vsc[i][3]); vscDirty[i] = false; }
            fprintf(cf, "\n");
        }
    }
    void saveFrame() {
        IDirect3DSurface8* bb = nullptr;
        if (FAILED(r->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
        D3DSURFACE_DESC d{}; bb->GetDesc(&d);
        IDirect3DSurface8* sys = nullptr;
        if (SUCCEEDED(r->CreateImageSurface(d.Width, d.Height, d.Format, &sys)) && SUCCEEDED(r->CopyRects(bb, nullptr, 0, sys, nullptr))) {
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                int W = d.Width / 2, H = d.Height / 2; std::vector<uint8_t> px((size_t)W * H * 3);
                for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
                    const uint8_t* s = (const uint8_t*)lr.pBits + (size_t)(y * 2) * lr.Pitch + (size_t)(x * 2) * 4;
                    uint8_t* o = &px[((size_t)(H - 1 - y) * W + x) * 3]; o[0] = s[0]; o[1] = s[1]; o[2] = s[2];
                }
                sys->UnlockRect();
                wchar_t p[MAX_PATH]; swprintf(p, MAX_PATH, L"%lsShrekRender_frame.bmp", g_dir);
                FILE* f = _wfopen(p, L"wb");
                if (f) {
                    int row = W * 3, sz = 54 + row * H; uint8_t h[54] = { 'B', 'M' };
                    memcpy(h + 2, &sz, 4); int off = 54; memcpy(h + 10, &off, 4); int hs = 40; memcpy(h + 14, &hs, 4);
                    memcpy(h + 18, &W, 4); memcpy(h + 22, &H, 4); h[26] = 1; h[28] = 24;
                    fwrite(h, 1, 54, f); fwrite(px.data(), 1, px.size(), f); fclose(f);
                }
            } else logf("capture: back buffer not readable");
        } else logf("capture: back buffer copy failed (format %d)", (int)d.Format);
        if (sys) sys->Release();
        bb->Release();
    }
    UINT curStride = 0;
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE t, UINT sv, UINT pc) override {
        if (cf) capState("DP", pc, 0, curStride);
        fxCheck2D();
        bool pp = ppBegin();
        if (pp) pp_castShadow(dev9, 0, t, sv, pc, 0, 0, 0, nullptr, 0, nullptr, 0);
        if (dev9 && pp_isBlob(dev9)) return D3D_OK;
        bool water = !pp && dev9 && pp_waterPre(dev9);
        HRESULT h; { PT pt_; h = r->DrawPrimitive(t, sv, pc); }
        if (water) pp_waterPost(dev9, 0, t, sv, pc, 0, 0, 0);
        else if (!pp && dev9) pp_auxPass(dev9, 0, t, sv, pc, 0, 0, 0);
        return h;
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t, UINT mi, UINT nv, UINT si, UINT pc) override {
        if (cf) capState("DIP", pc, nv, curStride);
        fxCheck2D();
        bool pp = ppBegin();
        if (pp) pp_castShadow(dev9, 1, t, mi, nv, si, pc, (INT)baseVtx, nullptr, 0, nullptr, 0);
        if (dev9 && pp_isBlob(dev9)) return D3D_OK;
        bool water = !pp && dev9 && pp_waterPre(dev9);
        HRESULT h; { PT pt_; h = r->DrawIndexedPrimitive(t, mi, nv, si, pc); }
        if (water) pp_waterPost(dev9, 1, t, mi, nv, si, pc, (INT)baseVtx);
        else if (!pp && dev9) pp_auxPass(dev9, 1, t, mi, nv, si, pc, (INT)baseVtx);
        return h;
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t, UINT pc, const void* d, UINT stride) override {
        if (cf) capState("DPUP", pc, 0, stride);
        fxCheck2D();
        if (ppBegin()) pp_castShadow(dev9, 2, t, pc, 0, 0, 0, 0, nullptr, 0, d, stride);
        HRESULT h; { PT pt_; h = r->DrawPrimitiveUP(t, pc, d, stride); }
        return h;
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t, UINT mi, UINT nv, UINT pc, const void* id, D3DFORMAT f, const void* d, UINT stride) override {
        if (cf) capState("DIPUP", pc, nv, stride);
        fxCheck2D();
        if (ppBegin()) pp_castShadow(dev9, 3, t, mi, nv, pc, 0, 0, id, f, d, stride);
        HRESULT h; { PT pt_; h = r->DrawIndexedPrimitiveUP(t, mi, nv, pc, id, f, d, stride); }
        return h;
    }
    HRESULT STDMETHODCALLTYPE Present(const RECT* a, const RECT* b, HWND w, const RGNDATA* d) override {
        unsigned ppDraws = pp_drawCount();
        perfFrame(ppDraws);
        frameCamera();
        rumble_frame();
        if (cf) { saveFrame(); fclose(cf); cf = nullptr; logf("capture: %d draws written to ShrekRender_frame.txt, %u with per-pixel lighting", capDraw, ppDraws); }
        // one key per light switch (ShrekRender.ini [Lighting]; defaults F3 light on/off, F4 lighting only, F5 relief,
        // F6 effects, F1 sharpening). A key's own edge: the "pressed since the last call" bit is lost when another
        // part of the process polls the same key.
        auto keyEdge = [](int vk, bool& was) { bool d = vk && (GetAsyncKeyState(vk) & 0x8000) != 0, e = d && !was; was = d; return e; };
        static bool wLight = false, wOnly = false, wRelief = false, wFx = false, wCas = false;
        bool eLight = keyEdge(g_ppKey, wLight), eOnly = keyEdge(g_ppOnlyKey, wOnly), eRelief = keyEdge(g_reliefKey, wRelief), eFx = keyEdge(g_fxKey, wFx), eCas = keyEdge(g_casKey, wCas);
        if (dev9) {
            if (eLight) { pp_setEnabled(!pp_enabled()); logf("per-pixel lighting %s (key)", pp_enabled() ? "on" : "off"); }
            if (eOnly) { pp_setDebug(!pp_debug()); if (pp_debug()) pp_setEnabled(true); logf("lighting-only view %s (key)", pp_debug() ? "on" : "off"); }
            if (eRelief) {
                int lv = (pp_bumpLevel() + 1) % 3; pp_setBump(g_bumpDepth, g_bumpAO, lv);
                static const char* names[] = { "off", "half", "normal", "double" };
                logf("relief %s (key)", names[lv]);
            }
            if (eFx) {
                int v = (pp_fxView() + 1) % 3; pp_setFxView(v);
                static const char* names[] = { "on", "ambient occlusion only", "off" };
                logf("effects %s (key), character shadows: %u draws last frame", names[v], pp_shadowCasts());
            }
            if (eCas) {
                static float last = 0.35f;
                if (pp_sharpen() > 0) { last = pp_sharpen(); pp_setSharpen(0); } else pp_setSharpen(last > 0 ? last : 0.35f);
                logf("sharpening %.2f (key)", pp_sharpen());
            }
        }
        if (g_captureReq || (g_capture && ((GetAsyncKeyState(g_capKey) & 1) | (GetAsyncKeyState(VK_SCROLL) & 1)))) {
            g_captureReq = false;
            wchar_t p[MAX_PATH]; swprintf(p, MAX_PATH, L"%lsShrekRender_frame.txt", g_dir);
            cf = _wfopen(p, L"w"); capDraw = 0;
            for (int i = 0; i < 96; i++) vscDirty[i] = true;
        }
        ppUnbind();
        if (dev9) { PT t(&g_tPP); mapLightUpdate(); pp_frameEnd(dev9); }
        if (dev9) pp_present(dev9);
        if (dev9) warmTick(dev9);
        if (dev9) { timerTick(); subs_draw(dev9); subs_bugShot(dev9); }
        frameCap();
        if (dev9) pp_gpuBeforePresent(dev9);
        HRESULT hr; { PT pt_(&g_tPresent); hr = r->Present(a, b, w, d); }
        if (dev9) pp_gpuFrameStart(dev9);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShader(DWORD h) override {
        PT pt_;
        genK++;
        curVS = h;
        if (active && !warnedRHW && !(h & 1) && (h & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) {
            warnedRHW = true; logf("note: pre-transformed (XYZRHW) geometry drawn, its depth is not reversed");
        }
        return r->SetVertexShader(h);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstant(DWORD reg, const void* data, DWORD count) override {
        PT pt_;
        HRESULT h = r->SetVertexShaderConstant(reg, data, count);
        for (DWORD i = 0; data && i < count && reg + i < 96; i++) { memcpy(vsc[reg + i], (const float*)data + i * 4, 16); vscDirty[reg + i] = true; }
        if (active && reg <= BIAS_REG && reg + count > BIAS_REG) {
            if (!warnedC95) { warnedC95 = true; logf("note: the game writes c%lu, depth scale constant restored", BIAS_REG); }
            applyBiasConst();
        }
        return h;
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE ms, IDirect3DSurface8** s) override {
        HRESULT hr = r->CreateDepthStencilSurface(w, h, f, ms, s);
        if (FAILED(hr) && (f == FMT_D32F || f == FMT_DEPTH) && gameDepthFmt != D3DFMT_UNKNOWN)   // the game copied the format of our buffer
            hr = r->CreateDepthStencilSurface(w, h, gameDepthFmt, ms, s);
        return hr;
    }
#include "fwd_dev.inc"
};

HRESULT STDMETHODCALLTYPE D3D8W::CreateDevice(UINT a, D3DDEVTYPE t, HWND w, DWORD f, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice8** out) {
    IDirect3DDevice8* dev = nullptr; HRESULT h = E_FAIL; bool active = false;
    D3DFORMAT orig = pp ? pp->AutoDepthStencilFormat : D3DFMT_UNKNOWN;
    if (g_planned && pp && pp->EnableAutoDepthStencil) {
        pp->AutoDepthStencilFormat = FMT_DEPTH;
        h = r->CreateDevice(a, t, w, f, pp, &dev);
        pp->AutoDepthStencilFormat = orig;
        if (SUCCEEDED(h)) active = true;
        else logf("D24X8 depth buffer refused (0x%08lx), using format %d with the normal depth range", h, (int)orig);
    }
    if (!active) h = r->CreateDevice(a, t, w, f, pp, &dev);
    if (FAILED(h)) { *out = nullptr; return h; }
    DevW* d = new DevW(dev, this);
    d->active = active; d->gameDepthFmt = orig;
    d->dev9 = g_perPixel ? pp_takeDevice() : nullptr;
    d->reloadShadow();
    pp_setBump(g_bumpDepth, g_bumpAO, std::min(g_bumpIni, 2)); pp_setBumpExtra(g_bumpChar, g_bumpBlur, g_bumpWorld); pp_setLightmapFilter(g_lmFilterIni); pp_setDetail(std::min(std::max(g_detailIni, 0.f), 1.f)); pp_setAerial(std::min(std::max(g_aerialIni, 0.f), 1.f)); pp_setSharpen(g_sharpenIni); pp_setEffects(std::min(g_ssaoIni, 1.f), g_ssaoRadiusIni, std::min(g_shadowIni, 0.6f), std::min(g_bloomIni, 0.5f)); pp_setExtra(std::min(std::max(g_worldShIni, 0.f), 1.f), std::min(std::max(g_waterIni, 0.f), 1.f), std::min(std::max(g_raysIni, 0.f), 1.f));   // the strongest settings are gone (too much)
    if (g_perPixel) logf(d->dev9 ? "per-pixel lighting: on (F3 toggles it; F4 lighting only, F5 relief, F6 effects, F1 sharpening; keys in ShrekRender.ini [Lighting])" : "per-pixel lighting: d3d9 device not found, lighting stays per vertex");
    d->initState();
    logf("device created: %s (game depth format %d, %ux%u, MSAA %d)", active ? "reversed 32-bit float depth" : "pass-through",
         (int)orig, pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0, pp ? (int)pp->MultiSampleType : 0);
    *out = d;
    return h;
}


// ---------------------------------------------------------------- menu/HUD fonts for this screen
// DejaVu Sans Condensed Bold is embedded; the package is rebuilt only when the height changes.
extern "C" const uint8_t shr_ttf[];
asm(".section .rdata,\"dr\"\n.globl _shr_ttf\n.p2align 4\n_shr_ttf:\n.incbin \"DejaVuSansCondensed-Bold.ttf\"\n.text\n");
static BOOL CALLBACK findWnd(HWND h, LPARAM lp) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) { *(HWND*)lp = h; return FALSE; }
    return TRUE;
}
static bool fileHas(const wchar_t* path, const std::vector<std::string>& marks) {
    FILE* f = _wfopen(path, L"rb"); if (!f) return false;
    std::vector<char> b(65536); size_t n = fread(b.data(), 1, b.size(), f); fclose(f);   // names come first
    std::string s(b.data(), n);
    for (auto& m : marks) if (s.find(m + '\0') == std::string::npos) return false;
    return true;
}
static void buildFonts() {
    HWND w = nullptr; EnumWindows(findWnd, (LPARAM)&w);
    RECT rc{}; int h = 0;
    if (w && GetClientRect(w, &rc)) h = rc.bottom - rc.top;
    int scr = GetSystemMetrics(SM_CYSCREEN);
    if (h < 480) h = scr;
    wchar_t path[MAX_PATH], tmp[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls..\\Textures\\SHHDFonts.utx", g_dir);
    swprintf(tmp, MAX_PATH, L"%ls..\\Textures\\SHHDFonts.tmp", g_dir);
    // the screen height (borderless full screen); the window's own only if it is a real game size
    std::vector<int> heights{ scr }; if (h != scr && h >= 720) heights.push_back(h);
    // 0.9.73: the menu / HUD fonts are Palatino Linotype Bold (the English game's serif face, with Cyrillic) from Windows'
    // fonts; EnhancedEdition.ini [Fonts] Interface=palab.ttf (another file in Windows\Fonts or a full path, "builtin": DejaVu)
    static std::vector<uint8_t> face; const uint8_t* ttf = shr_ttf;
    {
        wchar_t ini[MAX_PATH], fn[MAX_PATH]; swprintf(ini, MAX_PATH, L"%lsEnhancedEdition.ini", g_dir);
        GetPrivateProfileStringW(L"Fonts", L"Interface", L"builtin", fn, MAX_PATH, ini);
        std::wstring fp = fn;
        if (_wcsicmp(fn, L"builtin") && !wcschr(fn, L'\\') && !wcschr(fn, L'/')) { wchar_t w[MAX_PATH] = L""; GetWindowsDirectoryW(w, MAX_PATH); fp = std::wstring(w) + L"\\Fonts\\" + fn; }
        if (_wcsicmp(fn, L"builtin")) if (FILE* ff = _wfopen(fp.c_str(), L"rb")) {
            fseek(ff, 0, SEEK_END); long n = ftell(ff); fseek(ff, 0, SEEK_SET);
            if (n > 1000 && n < (32 << 20)) { face.resize(n); if (fread(face.data(), 1, n, ff) != (size_t)n) face.clear(); }
            fclose(ff);
        }
        if (!face.empty()) { ttf = face.data(); fontgen::VERSION_TAG = "SHRFONTS5P"; } else fontgen::VERSION_TAG = "SHRFONTS5D";
    }
    std::vector<std::string> marks{ fontgen::VERSION_TAG };
    for (int hh : heights) marks.push_back("SHRH" + std::to_string(hh));
    if (fileHas(path, marks)) { logf("fonts: SHHDFonts.utx already built for %d px", scr); return; }
    DWORD t0 = GetTickCount();
    auto pkg = fontgen::makePackage(ttf, heights);
    if (pkg.empty()) { logf("fonts: generation failed"); return; }
    FILE* f = _wfopen(tmp, L"wb");
    if (!f) { logf("fonts: cannot write %ls", tmp); return; }
    bool ok = fwrite(pkg.data(), 1, pkg.size(), f) == pkg.size(); ok = fclose(f) == 0 && ok;
    if (ok && MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)) logf("fonts: built for %d px%s (%u KB, %lu ms)", scr, heights.size() > 1 ? " + window height" : "", (unsigned)(pkg.size() / 1024), GetTickCount() - t0);
    else { DeleteFileW(tmp); logf("fonts: cannot replace %ls (%lu)", path, GetLastError()); }
}

static bool dxvkUsesD32() {
    wchar_t p[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"DXVK_CONFIG_FILE", p, MAX_PATH);
    if (!n || n >= MAX_PATH) swprintf(p, MAX_PATH, L"%lsdxvk.conf", g_dir);
    FILE* f = _wfopen(p, L"rb"); if (!f) return false;
    char line[512]; bool on = false;
    while (fgets(line, sizeof line, f)) {
        std::string s(line); for (auto& c : s) c = (char)tolower((unsigned char)c);
        s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }), s.end());
        if (s.rfind("d3d9.used32ford24=", 0) == 0) on = s.substr(18) == "true";
    }
    fclose(f);
    return on;
}

// ---------------------------------------------------------------- entry
typedef IDirect3D8* (WINAPI* Create8Fn)(UINT);
static HMODULE g_real, g_self;

extern "C" IDirect3D8* WINAPI Direct3DCreate8(UINT sdk) {
    static bool init = false;
    if (!init) {
        init = true;
        GetModuleFileNameW(g_self, g_dir, MAX_PATH);
        wchar_t* s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0;
        wchar_t p[MAX_PATH];
        swprintf(p, MAX_PATH, L"%lsShrekRender.ini", g_dir);
        g_wantReverse = GetPrivateProfileIntW(L"Depth", L"ReverseZ", 1, p) != 0;
        g_capture = GetPrivateProfileIntW(L"Debug", L"FrameCapture", 1, p) != 0;
        g_capKey = GetPrivateProfileIntW(L"Debug", L"CaptureKey", VK_F10, p);
        g_perPixel = GetPrivateProfileIntW(L"Lighting", L"PerPixel", 1, p) != 0;
        g_ppKey = GetPrivateProfileIntW(L"Lighting", L"ToggleKey", VK_F3, p);
        g_ppOnlyKey = GetPrivateProfileIntW(L"Lighting", L"LightOnlyKey", VK_F4, p);
        g_reliefKey = GetPrivateProfileIntW(L"Lighting", L"ReliefKey", VK_F5, p);
        g_fxKey = GetPrivateProfileIntW(L"Lighting", L"EffectsKey", VK_F6, p);
        g_casKey = GetPrivateProfileIntW(L"Lighting", L"SharpenKey", VK_F1, p);
        g_bumpIni = GetPrivateProfileIntW(L"Lighting", L"Relief", 2, p);
        g_perfLog = GetPrivateProfileIntW(L"Debug", L"PerfLog", 0, p) != 0;
        g_profile = GetPrivateProfileIntW(L"Debug", L"Profile", 0, p) != 0;
        pp_gpuEnable(g_perfLog);
        {   // vibration settings live with the other gamepad settings
            wchar_t pp[MAX_PATH]; swprintf(pp, MAX_PATH, L"%lsShrekPad.ini", g_dir);
            rumble_config(GetPrivateProfileIntW(L"ShrekPad", L"Rumble", 70, pp) > 0, GetPrivateProfileIntW(L"ShrekPad", L"RumbleSteps", 0, pp) != 0);
        }
        {   // 0 = engine's own copy, 1 = C runtime memcpy (default), 2 = plain REP MOVSD
            int m = GetPrivateProfileIntW(L"Perf", L"FastMemcpy", 1, p);
            g_fastMemcpy = m != 0; g_memcpyCrt = m == 1;
        }
        g_pinFast = GetPrivateProfileIntW(L"Perf", L"PerformanceCoresOnly", 1, p) != 0;
        g_bumpWorld = GetPrivateProfileIntW(L"Lighting", L"ReliefWorld", 1, p) != 0;
        g_lmFilterIni = GetPrivateProfileIntW(L"Lighting", L"LightmapFilter", 1, p) != 0;
        g_a2cIni = GetPrivateProfileIntW(L"Image", L"FoliageAA", 0, p) != 0;
        { wchar_t v[32];
          GetPrivateProfileStringW(L"Lighting", L"ReliefDepth", L"1", v, 32, p); g_bumpDepth = (float)_wtof(v);
          GetPrivateProfileStringW(L"Lighting", L"ReliefShade", L"1.5", v, 32, p); g_bumpAO = (float)_wtof(v);
          GetPrivateProfileStringW(L"Lighting", L"ReliefCharacters", L"0.35", v, 32, p); g_bumpChar = (float)_wtof(v);
          GetPrivateProfileStringW(L"Lighting", L"ReliefSmooth", L"2", v, 32, p); g_bumpBlur = (float)_wtof(v);
          GetPrivateProfileStringW(L"Image", L"Sharpen", L"0", v, 32, p); g_sharpenIni = (float)_wtof(v);
          GetPrivateProfileStringW(L"Lighting", L"AmbientOcclusion", L"1", v, 32, p); g_ssaoIni = (float)_wtof(v);
          GetPrivateProfileStringW(L"Lighting", L"AmbientOcclusionRadius", L"55", v, 32, p); g_ssaoRadiusIni = (float)_wtof(v);
          GetPrivateProfileStringW(L"Lighting", L"CharacterShadows", L"0.6", v, 32, p); g_shadowIni = (float)_wtof(v);
          { wchar_t e[MAX_PATH]; swprintf(e, MAX_PATH, L"%lsEnhancedEdition.ini", g_dir); pp_setBlobOff(GetPrivateProfileIntW(L"Game", L"BlobShadows", 0, e) == 0 && g_shadowIni > 0.01f); }
          GetPrivateProfileStringW(L"Image", L"Bloom", L"0", v, 32, p); g_bloomIni = (float)_wtof(v);
          GetPrivateProfileStringW(L"Lighting", L"WorldShadows", L"1", v, 32, p); g_worldShIni = (float)_wtof(v);
          GetPrivateProfileStringW(L"Lighting", L"TorchShadows", L"1", v, 32, p); pp_setTorchShadows((float)_wtof(v));
          GetPrivateProfileStringW(L"Image", L"WaterReflections", L"0.6", v, 32, p); g_waterIni = (float)_wtof(v);
          GetPrivateProfileStringW(L"Image", L"SunRays", L"0.5", v, 32, p); g_raysIni = (float)_wtof(v);
          GetPrivateProfileStringW(L"Image", L"FineDetail", L"0.35", v, 32, p); g_detailIni = (float)_wtof(v);
          GetPrivateProfileStringW(L"Image", L"AerialPerspective", L"0.35", v, 32, p); g_aerialIni = (float)_wtof(v); }
        if (GetPrivateProfileIntW(L"Depth", L"Log", 1, p)) { swprintf(p, MAX_PATH, L"%lsShrekRender.log", g_dir);
            wchar_t pv[MAX_PATH]; swprintf(pv, MAX_PATH, L"%lsShrekRender.prev.log", g_dir); MoveFileExW(p, pv, MOVEFILE_REPLACE_EXISTING);   // the run before stays (bug reports)
            g_log = _wfopen(p, L"w"); }
        subs_init(g_dir);
        logf("shrz 1.23: reversed float depth %s", g_wantReverse ? "requested" : "disabled in ShrekRender.ini");
        if (g_fastMemcpy) patchMemcpy();
        buildFonts();
        g_real = LoadLibraryW(L"d3d8.dll");
        if (g_real) { wchar_t m[MAX_PATH]; GetModuleFileNameW(g_real, m, MAX_PATH); logf("d3d8: %ls", m); }
        if (g_real && g_perPixel) pp_installHook(g_real);
        if (g_perPixel) hookMapFiles();
    }
    if (!g_real) { logf("d3d8.dll not found"); return nullptr; }
    auto fn = (Create8Fn)GetProcAddress(g_real, "Direct3DCreate8");
    IDirect3D8* real = fn ? fn(sdk) : nullptr;
    if (!real) return nullptr;
    // DXVK's d3d9.dll (loaded by its d3d8) exports DXVK_RegisterAnnotation as ordinal 28257
    HMODULE d9 = GetModuleHandleW(L"d3d9.dll");
    bool dxvk = d9 && GetProcAddress(d9, (LPCSTR)(ULONG_PTR)28257);
    bool d32 = dxvkUsesD32();
    g_planned = g_wantReverse && dxvk && d32;
    if (dxvk && !d32) logf("dxvk.conf has no d3d9.useD32forD24 = True: depth stays 24-bit integer");
    logf("DXVK %s -> %s", dxvk ? "found" : "not found", g_planned ? "vertex fog, float depth on device creation" : "pass-through");
    return new D3D8W(real);
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_self = h; DisableThreadLibraryCalls(h); }
    return TRUE;
}
