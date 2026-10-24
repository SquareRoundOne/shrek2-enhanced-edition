// pp9.cpp - per-pixel lighting for the draws Shrek 2 lights with the fixed-function pipeline.
//
// DXVK's d3d8.dll runs on its d3d9.dll. The d3d9 device behind it is caught when it is created
// (Direct3DCreate9 in d3d8.dll's import table, then IDirect3D9::CreateDevice), and for every draw
// that uses fixed-function lighting the d3d9 device gets a shader model 3 vertex and pixel shader
// for the duration of that one draw. They do what DXVK's fixed-function shaders do - same
// matrices, same lights and material, same texture stages, same fog - except that the lighting
// sum is evaluated for every pixel instead of at the vertices. Shaders are generated as HLSL
// for each combination of vertex layout and texture stage setup and compiled with the system's
// d3dcompiler_47.dll on first use.
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include "ppgen.h"

void shr_log(const char* f, ...);

namespace {

typedef IDirect3D9* (WINAPI* Create9Fn)(UINT);
typedef HRESULT (STDMETHODCALLTYPE* CreateDevFn)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT (WINAPI* CompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

Create9Fn g_create9;
CreateDevFn g_createDev;
IDirect3DDevice9* g_lastDev;
CompileFn g_compile;
bool g_relSkip = false;   // pp_forget: drop the pointers of another device without releasing them
bool g_enabled = true, g_compilerTried = false, g_debug = false, g_broken = false, g_forceKey = false;
float g_bumpDepth = 1.f, g_bumpAO = 1.5f, g_bumpChar = 0.35f, g_bumpBlur = 2.f; int g_bumpLevel = 2; bool g_world = true; bool g_lmFilter = true; float g_detailAmt = 0.6f, g_aerial = 0.35f;   // level 0 off, 1 half, 2 normal, 3 double

// ---------------------------------------------------------------- hooks
HRESULT STDMETHODCALLTYPE hkCreateDevice(IDirect3D9* self, UINT a, D3DDEVTYPE t, HWND w, DWORD f, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    HRESULT h = g_createDev(self, a, t, w, f, pp, out);
    if (SUCCEEDED(h) && out) g_lastDev = *out;
    return h;
}
IDirect3D9* WINAPI hkCreate9(UINT sdk) {
    IDirect3D9* d = g_create9(sdk);
    if (d && !g_createDev) {
        void** vt = *(void***)d; DWORD old;
        if (VirtualProtect(&vt[16], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
            g_createDev = (CreateDevFn)vt[16]; vt[16] = (void*)hkCreateDevice;
            VirtualProtect(&vt[16], sizeof(void*), old, &old);
        }
    }
    return d;
}

// ---------------------------------------------------------------- matrices (as DXVK computes them)
struct M4 { float m[4][4]; };
M4 mul(const M4& a, const M4& b) {          // row-vector convention: a then b; DXVK's summation order
    M4 r;
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        float s = a.m[i][0] * b.m[0][j];
        s = s + a.m[i][1] * b.m[1][j];
        s = s + a.m[i][2] * b.m[2][j];
        s = s + a.m[i][3] * b.m[3][j];
        r.m[i][j] = s;
    }
    return r;
}
M4 fromD3D(const D3DMATRIX& d) { M4 r; memcpy(r.m, d.m, 64); return r; }

using namespace ppg;

struct Prog { IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr; };
std::unordered_map<std::string, Prog> g_cache;
// character shadow map: 4096 texels over 4800 units (the 2048 / 2400 of 1.15 at the same sharpness,
// twice the reach: shadows no longer appear only when the camera comes close)
const int SHN = 4096; const float SHS = 4800.f;

bool loadCompiler() {
    if (g_compilerTried) return g_compile != nullptr;
    g_compilerTried = true;
    const wchar_t* names[] = { L"d3dcompiler_47.dll", L"d3dcompiler_46.dll", L"d3dcompiler_43.dll" };
    for (auto n : names) {
        HMODULE m = LoadLibraryW(n);
        if (m && (g_compile = (CompileFn)GetProcAddress(m, "D3DCompile"))) { shr_log("per-pixel lighting: shader compiler %ls", n); return true; }
    }
    shr_log("per-pixel lighting: no d3dcompiler_47.dll, lighting stays per vertex");
    return false;
}

ID3DBlob* compile(const std::string& src, const char* target) {
    ID3DBlob *code = nullptr, *err = nullptr;
    HRESULT h = g_compile(src.data(), src.size(), "shrek_pp", nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(h)) {
        shr_log("per-pixel lighting: %s compile failed: %s", target, err ? (const char*)err->GetBufferPointer() : "?");
        shr_log("%s", src.c_str());
        if (code) code->Release();
        code = nullptr;
    }
    if (err) err->Release();
    return code;
}

// DXVK puts the d3d9 device it runs d3d8 games on into a D3D8 compatibility mode, which (since
// DXVK 2.6) also refuses shaders newer than vs_1_1 / ps_1_4. The flag is a member of the device
// object: it is found once by flipping candidate bytes and trying to create a tiny ps_3_0 shader,
// and it is lifted only for the moment our own shaders are created.
int g_compatOff = -1; BYTE g_compatLift = 0;
bool g_compatProbed = false;
const GUID IID_LegacyBridge = { 0xD3D0D3D9, 0x42A9, 0x4C1E, { 0xAA, 0x97, 0xBE, 0xEF, 0xCA, 0xFE, 0x20, 0x00 } };

bool tryPS(IDirect3DDevice9* d, const DWORD* code) {
    IDirect3DPixelShader9* ps = nullptr;
    if (FAILED(d->CreatePixelShader(code, &ps)) || !ps) return false;
    ps->Release(); return true;
}

bool probeCompat(IDirect3DDevice9* d) {
    if (g_compatProbed) return g_compatOff != -1;
    g_compatProbed = true;
    ID3DBlob* b = compile("float4 main() : COLOR0 { return float4(0, 0, 0, 0); }", "ps_3_0");
    if (!b) return false;
    const DWORD* code = (const DWORD*)b->GetBufferPointer();
    if (tryPS(d, code)) { g_compatOff = -2; b->Release(); shr_log("per-pixel lighting: shader model 3 available"); return true; }
    BYTE* base = (BYTE*)d;
    MEMORY_BASIC_INFORMATION mi{};
    size_t lim = 0x40000;
    if (VirtualQuery(base, &mi, sizeof mi)) lim = std::min(lim, (size_t)((BYTE*)mi.BaseAddress + mi.RegionSize - base));
    auto test = [&](size_t off, BYTE nv) -> bool {
        BYTE old = base[off]; base[off] = nv;
        bool ok = tryPS(d, code);
        base[off] = old;
        if (ok && !tryPS(d, code)) { g_compatOff = (int)off; g_compatLift = nv; return true; }
        return false;
    };
    bool found = false;
    // newer DXVK: compatibility flags (D3D8 = bit 1) right after the legacy device bridge
    void* br = nullptr;
    if (SUCCEEDED(d->QueryInterface(IID_LegacyBridge, &br)) && br) {
        BYTE* p = (BYTE*)br;
        ((IUnknown*)br)->Release();
        if (p > base && (size_t)(p - base) + 8 < lim && *(void**)(p + 4) == (void*)d && (p[8] & 2))
            found = test((size_t)(p + 8 - base), (BYTE)(p[8] & ~2));
    }
    // DXVK 2.6 - 2.7: bool m_isD3D8Compatible among other bools
    int tried = 0;
    for (size_t off = 16; !found && off + 1 < lim; off++) {
        if (base[off] != 1 || base[off - 1] > 1 || base[off + 1] > 1) continue;
        tried++;
        found = test(off, 0);
    }
    b->Release();
    if (found) shr_log("per-pixel lighting: D3D8 compatibility flag of the d3d9 device at +0x%x (%d candidates)", g_compatOff, tried);
    else shr_log("per-pixel lighting: this DXVK refuses shader model 3 for d3d8 games and its flag was not found");
    return found;
}

struct CompatLift {
    IDirect3DDevice9* d; BYTE old = 0;
    explicit CompatLift(IDirect3DDevice9* dev) : d(dev) { if (g_compatOff >= 0) { old = ((BYTE*)d)[g_compatOff]; ((BYTE*)d)[g_compatOff] = g_compatLift; } }
    ~CompatLift() { if (g_compatOff >= 0) ((BYTE*)d)[g_compatOff] = old; }
};

// compiled shaders are kept in System\ShrekRender_shaders.bin (keyed by their HLSL source), so
// a shader is compiled only the first time its combination appears
std::unordered_map<std::string, std::pair<std::string, std::string>> g_disk;
bool g_diskLoaded = false;
std::wstring diskPath() {
    HMODULE self = nullptr; wchar_t p[MAX_PATH] = L"";
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&shr_log, &self);
    GetModuleFileNameW(self, p, MAX_PATH);
    std::wstring s(p); size_t k = s.find_last_of(L'\\');
    return (k == std::wstring::npos ? std::wstring() : s.substr(0, k + 1)) + L"ShrekRender_shaders.bin";
}
bool rd(FILE* f, std::string& s) {
    uint32_t n; if (fread(&n, 4, 1, f) != 1 || n > (1u << 24)) return false;
    s.resize(n); return n == 0 || fread(&s[0], 1, n, f) == n;
}
void wr(FILE* f, const std::string& s) { uint32_t n = (uint32_t)s.size(); fwrite(&n, 4, 1, f); fwrite(s.data(), 1, n, f); }
void loadDisk() {
    if (g_diskLoaded) return;
    g_diskLoaded = true;
    FILE* f = _wfopen(diskPath().c_str(), L"rb"); if (!f) return;
    char magic[8];
    if (fread(magic, 1, 8, f) == 8 && !memcmp(magic, "SHRPPC01", 8)) {
        std::string k, v, p;
        while (rd(f, k) && rd(f, v) && rd(f, p)) g_disk[k] = { v, p };
    }
    fclose(f);
    if (!g_disk.empty()) shr_log("per-pixel lighting: %u compiled shaders loaded", (unsigned)g_disk.size());
}
void saveDisk(const std::string& k, const std::string& v, const std::string& p) {
    std::wstring path = diskPath();
    FILE* f = _wfopen(path.c_str(), L"rb+");
    if (f) { fseek(f, 0, SEEK_END); if (ftell(f) < 8) { fclose(f); f = nullptr; } }
    if (!f) { f = _wfopen(path.c_str(), L"wb"); if (!f) return; fwrite("SHRPPC01", 1, 8, f); }
    wr(f, k); wr(f, v); wr(f, p); fclose(f);
}
std::string blobStr(ID3DBlob* b) { return b ? std::string((const char*)b->GetBufferPointer(), b->GetBufferSize()) : std::string(); }

// Shaders are compiled on background threads: a draw whose shader is not ready yet stays
// fixed-function for that frame instead of stopping the game. Every shader combination met is kept
// in ShrekRender_keys.bin, and at start all of them that are not in the compiled cache (after an
// update of the generator) are compiled in the background at once.
CRITICAL_SECTION g_cs; bool g_csInit = false;
struct Job { std::string src, vsrc, psrc; };
std::vector<Job> g_jobs; std::unordered_map<std::string, std::pair<std::string, std::string>> g_done; std::unordered_set<std::string> g_queued;
HANDLE g_jobEvt = nullptr; int g_workers = 0; unsigned g_pending = 0;
std::wstring keysPath() { std::wstring p = diskPath(); return p.substr(0, p.size() - 11) + L"keys.bin"; }   // ShrekRender_keys.bin
DWORD WINAPI compileWorker(void*) {
    for (;;) {
        WaitForSingleObject(g_jobEvt, 2000);
        for (;;) {
            Job j;
            EnterCriticalSection(&g_cs);
            if (g_jobs.empty()) { LeaveCriticalSection(&g_cs); break; }
            j = std::move(g_jobs.back()); g_jobs.pop_back();
            LeaveCriticalSection(&g_cs);
            ID3DBlob *vb = nullptr, *pb = nullptr, *err = nullptr;
            g_compile(j.vsrc.data(), j.vsrc.size(), "shrek_pp", nullptr, nullptr, "main", "vs_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vb, &err);
            if (err) { err->Release(); err = nullptr; }
            if (vb) g_compile(j.psrc.data(), j.psrc.size(), "shrek_pp", nullptr, nullptr, "main", "ps_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pb, &err);
            std::string v = blobStr(vb), p = blobStr(pb);
            EnterCriticalSection(&g_cs);
            if (!pb) { shr_log("per-pixel lighting: background compile failed: %s", err ? (const char*)err->GetBufferPointer() : "?"); shr_log("%s", (j.vsrc + j.psrc).c_str()); }
            g_done[j.src] = { v, p };
            if (!v.empty() && !p.empty()) { g_disk[j.src] = { v, p }; saveDisk(j.src, v, p); }
            LeaveCriticalSection(&g_cs);
            if (vb) vb->Release();
            if (pb) pb->Release();
            if (err) err->Release();
        }
    }
    return 0;
}
void queueCompile(const std::string& src, const std::string& vsrc, const std::string& psrc) {
    if (g_queued.count(src)) return;
    g_queued.insert(src);
    g_jobs.push_back({ src, vsrc, psrc });
    if (!g_workers) {
        SYSTEM_INFO si; GetSystemInfo(&si);
        g_workers = std::max(1, std::min(6, (int)si.dwNumberOfProcessors - 2));
        g_jobEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        for (int i = 0; i < g_workers; i++) { HANDLE h = CreateThread(nullptr, 0, compileWorker, nullptr, 0, nullptr); if (h) { SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(h); } }
    }
    SetEvent(g_jobEvt);
}
std::unordered_set<std::string> g_knownKeys;
void rememberKey(const Key& k) {
    std::string kb((const char*)&k, sizeof k);
    if (!g_knownKeys.insert(kb).second) return;
    std::wstring p = keysPath();
    FILE* f = _wfopen(p.c_str(), L"rb+");
    if (f) { fseek(f, 0, SEEK_END); if (ftell(f) < 12) { fclose(f); f = nullptr; } }
    if (!f) { f = _wfopen(p.c_str(), L"wb"); if (!f) return; uint32_t n = sizeof(Key); fwrite("SHRKEYS1", 1, 8, f); fwrite(&n, 4, 1, f); }
    fwrite(&k, sizeof k, 1, f); fclose(f);
}
// all combinations met before: whatever the cache lacks is compiled now, in the background
std::vector<Key> g_warmKeys;   // combinations met before whose code is on disk: their shaders are made up front
void warmUp() {
    static bool done = false; if (done) return; done = true;
    FILE* f = _wfopen(keysPath().c_str(), L"rb"); if (!f) return;
    char magic[8]; uint32_t n = 0; int queued = 0, total = 0;
    bool hdr = fread(magic, 1, 8, f) == 8 && !memcmp(magic, "SHRKEYS1", 8) && fread(&n, 4, 1, f) == 1;
    if (hdr && n >= 40 && n < sizeof(Key)) {   // an older, shorter key: the fields added since read as 0 (the same shaders); the file is rewritten
        std::vector<Key> old; std::vector<char> b(n);
        while (fread(b.data(), n, 1, f) == 1) { Key k; memset(&k, 0, sizeof k); memcpy(&k, b.data(), n); old.push_back(k); }
        fclose(f); f = nullptr;
        if (FILE* w = _wfopen(keysPath().c_str(), L"wb")) { uint32_t nn = sizeof(Key); fwrite("SHRKEYS1", 1, 8, w); fwrite(&nn, 4, 1, w); for (auto& k : old) fwrite(&k, sizeof k, 1, w); fclose(w); }
        shr_log("per-pixel lighting: %u known shader combinations carried over to the new layout", (unsigned)old.size());
        f = _wfopen(keysPath().c_str(), L"rb");
        hdr = f && fread(magic, 1, 8, f) == 8 && fread(&n, 4, 1, f) == 1;
    }
    if (hdr && n == sizeof(Key)) {
        Key k;
        EnterCriticalSection(&g_cs);
        while (fread(&k, sizeof k, 1, f) == 1) {
            total++;
            g_knownKeys.insert(std::string((const char*)&k, sizeof k));
            std::string vsrc = genVS(k), psrc = genPS(k), src = vsrc + psrc;
            if (g_disk.count(src)) { g_warmKeys.push_back(k); continue; }
            if (g_queued.count(src)) continue;
            queueCompile(src, vsrc, psrc); queued++;
        }
        LeaveCriticalSection(&g_cs);
    } else { if (f) fclose(f); _wremove(keysPath().c_str()); f = nullptr; }
    if (f) fclose(f);
    if (queued) shr_log("per-pixel lighting: %d of %d known shaders are compiled in the background", queued, total);
}

// Every shader in the compiled cache is made when the device appears (the game's start and level
// loads go on behind it): no shader is created in the middle of play, and DXVK prepares their
// pipelines on its worker threads while the game is still loading. Keyed by HLSL source, so this
// holds across updates that change the combination layout.
std::unordered_map<std::string, Prog> g_srcProg;
unsigned g_made = 0;
IDirect3DDevice9* g_built = nullptr;
void prebuild(IDirect3DDevice9* d) {
    if (g_built == d) return;
    g_built = d;
    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    unsigned n = 0, bad = 0;
    EnterCriticalSection(&g_cs);
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> all(g_disk.begin(), g_disk.end());
    LeaveCriticalSection(&g_cs);
    CompatLift lift(d);
    for (auto& e : all) {
        if (g_srcProg.count(e.first) || e.second.first.empty() || e.second.second.empty()) continue;
        Prog w;
        if (SUCCEEDED(d->CreateVertexShader((const DWORD*)e.second.first.data(), &w.vs)) &&
            SUCCEEDED(d->CreatePixelShader((const DWORD*)e.second.second.data(), &w.ps))) { g_srcProg[e.first] = w; n++; }
        else { if (w.vs) w.vs->Release(); if (w.ps) w.ps->Release(); bad++; }
    }
    QueryPerformanceCounter(&t1);
    shr_log("per-pixel lighting: %u shaders made up front in %.0f ms%s", n, (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart,
            bad ? (" (" + std::to_string(bad) + " refused)").c_str() : "");
}
void dropPrebuilt(bool release) {
    if (release) for (auto& kv : g_srcProg) { kv.second.vs->Release(); kv.second.ps->Release(); }
    g_srcProg.clear(); g_built = nullptr;
}

const Prog* getProg(IDirect3DDevice9* d, const Key& k) {
    std::string key((const char*)&k, sizeof k);
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second.vs ? &it->second : nullptr;
    if (!probeCompat(d)) { g_broken = true; return nullptr; }
    if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; }
    Prog p;
    loadDisk();
    warmUp();
    prebuild(d);
    {   // made up front from the compiled cache (same HLSL): taken over, no new shader objects
        std::string src0 = genVS(k) + genPS(k);
        auto pb = g_srcProg.find(src0);
        if (pb != g_srcProg.end()) {
            Prog w = pb->second; w.vs->AddRef(); w.ps->AddRef();
            g_cache[key] = w; rememberKey(k);
            return &g_cache[key];
        }
    }
    // a combination waiting for its background compile: no new HLSL every draw, only "is it done yet"
    static std::unordered_map<std::string, std::string> pending;
    auto pe = pending.find(key);
    if (pe != pending.end()) {
        EnterCriticalSection(&g_cs); bool ready = g_done.count(pe->second) != 0; LeaveCriticalSection(&g_cs);
        if (!ready) return nullptr;
        pending.erase(pe);
    }
    std::string vsrc = genVS(k), psrc = genPS(k), src = vsrc + psrc;
    std::string vcode, pcode; bool cached = false, background = false;
    EnterCriticalSection(&g_cs);
    auto dk = g_disk.find(src);
    if (dk != g_disk.end()) { vcode = dk->second.first; pcode = dk->second.second; cached = true; }
    else {
        auto dn = g_done.find(src);
        if (dn == g_done.end()) { queueCompile(src, vsrc, psrc); LeaveCriticalSection(&g_cs); pending[key] = src; return nullptr; }   // fixed-function until it is ready
        vcode = dn->second.first; pcode = dn->second.second; background = true;
    }
    LeaveCriticalSection(&g_cs);
    if (!vcode.empty() && !pcode.empty()) {
        CompatLift lift(d);
        if (FAILED(d->CreateVertexShader((const DWORD*)vcode.data(), &p.vs)) ||
            FAILED(d->CreatePixelShader((const DWORD*)pcode.data(), &p.ps))) {
            shr_log("per-pixel lighting: shader creation refused");
            if (p.vs) p.vs->Release();
            if (p.ps) p.ps->Release();
            p = Prog{};
        }
    }
    g_cache[key] = p;
    if (p.vs) { rememberKey(k); g_made++; }
    if (background || !p.vs)
        shr_log("per-pixel lighting: shader %u (%d stage%s%s%s) %s", (unsigned)g_cache.size(), k.n, k.n == 1 ? "" : "s",
                k.c0 ? ", vertex colour" : "", k.spec ? ", specular" : "", p.vs ? "compiled in the background" : "FAILED");
    (void)cached;
    return p.vs ? &g_cache[key] : nullptr;
}

int declCount(BYTE type) {
    switch (type) {
    case D3DDECLTYPE_FLOAT1: return 1;
    case D3DDECLTYPE_FLOAT2: case D3DDECLTYPE_SHORT2: case D3DDECLTYPE_SHORT2N: case D3DDECLTYPE_USHORT2N: case D3DDECLTYPE_FLOAT16_2: return 2;
    case D3DDECLTYPE_FLOAT3: case D3DDECLTYPE_UDEC3: case D3DDECLTYPE_DEC3N: return 3;
    default: return 4;
    }
}

void color4(D3DCOLOR c, float* o) {
    o[0] = ((c >> 16) & 255) / 255.f; o[1] = ((c >> 8) & 255) / 255.f; o[2] = (c & 255) / 255.f; o[3] = (c >> 24) / 255.f;
}

unsigned g_draws = 0;

// ---- ambient occlusion, bloom, character shadows (settings and per-frame state)
float g_bloomScale = 1.f, g_bloomThr = 0.62f;   // per level (the storybook: weaker)
// 0.9.52: the level's sun shadows on characters, reflections on water, sun rays
float g_worldSh = 0.f, g_water = 0.f, g_rays = 0.f;
bool g_waterDrawn = false, g_waterPrev = false;   // a water surface this frame / the last frame's pictures for it are there
float g_ssao = 1.f, g_ssaoRadius = 55.f, g_bloom = 0.f, g_shadow = 0.6f; int g_fxView = 0;   // view: 0 normal, 1 occlusion only, 2 effects off
bool g_rt1 = false, g_rtKnown = false, g_rt0BB = true, g_zWritten = false, g_postDone = false, g_anyPP = false; DWORD g_cwe1 = ~0u;
const Key* g_infoKey = nullptr;
bool g_mapSet = false, g_mapSun = false; float g_mapDir[3] = { 0, 0, -1 }, g_mapChars = 1.f, g_mapShadow = 1.f;
bool g_keyMulti = false; unsigned g_zDraws = 0, g_sceneZ = 0; bool g_zFrozen = false;   // the vertex layout uses several streams (skinned characters: positions apart from texture coordinates)
struct Cam { float p[4]; M4 v; unsigned n; };
Cam g_cams[4]; int g_nCams = 0;   // cameras of this frame's 3D draws: the one most draws use is the scene's
bool g_lastDyn = false, g_lastPersp = true; float g_aoP[4] = { 1, 1, 0, 0 };
M4 g_lastV; bool g_haveV = false;
float g_sunNext[3] = { 0, 0, 0 }; float g_sunNextLum = 0;
struct ShadowMaps {
    IDirect3DTexture9* map[2] = {}; IDirect3DSurface9* depth = nullptr;
    // props (tables, chairs, walls as static meshes) seen from the same light: they do not cast, but a
    // character's shadow stops at them (no shadow on the floor under a table through its top)
    IDirect3DTexture9* bmap[2] = {}; IDirect3DSurface9* bdepth = nullptr; bool bcleared[2] = {};
    IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexShader9* avs = nullptr; IDirect3DPixelShader9* aps = nullptr;   // alpha-tested casters
    int w = 0; bool cleared[2] = {}, written[2] = {}, valid[2] = {};
    M4 uv[2], clip[2]; float dir[2][3] = {}; bool failed = false; unsigned casts = 0, lastCasts = 0;
} g_sh;
// the level's own sun shadows: one map kept from frame to frame (level geometry and props do not move),
// 9600 units around a point that follows the camera in steps; cleared when it moves on, with a new
// level or another sun
int WSN = 4096; const float WSS = 9600.f, WSD = 6000.f;   // WSN 2048 with "low" (weak cards): half the memory, simpler edges
float g_wsQuality = 1.f;
std::unordered_set<uint64_t> g_wsDone;   // draws already in the kept map: not drawn into it again (the CPU cost of a second pass is paid once)
struct WorldShadow {
    IDirect3DTexture9* tex = nullptr; IDirect3DSurface9* depth = nullptr; bool failed = false, clear = true, valid = false;
    M4 uv, clip; float anchor[3] = {}, dir[3] = {}; unsigned casts = 0;
} g_ws;
bool fxOn() { return g_fxView != 2; }
bool shadowReadValid() { return g_shadow > 0 && fxOn() && g_sh.valid[1 - g_sh.w] && g_sh.map[1 - g_sh.w]; }

M4 inverse(const M4& a) {
    const float* m = &a.m[0][0]; float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    double det = (double)m[0] * inv[0] + (double)m[1] * inv[4] + (double)m[2] * inv[8] + (double)m[3] * inv[12];
    M4 r; if (fabs(det) < 1e-30) { memset(&r, 0, sizeof r); for (int i = 0; i < 4; i++) r.m[i][i] = 1; return r; }
    for (int i = 0; i < 16; i++) (&r.m[0][0])[i] = (float)(inv[i] / det);
    return r;
}

} // namespace

// ---------------------------------------------------------------- interface for shrz.cpp
void pp_installHook(HMODULE mod) {
    HMODULE d9 = GetModuleHandleW(L"d3d9.dll");
    FARPROC real = d9 ? GetProcAddress(d9, "Direct3DCreate9") : nullptr;
    if (!mod || !real) { shr_log("per-pixel lighting: d3d9.dll not loaded by d3d8.dll"); return; }
    auto base = (BYTE*)mod;
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    int patched = 0;
    for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
        for (auto th = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk); th->u1.Function; th++) {
            if ((FARPROC)th->u1.Function != real) continue;
            DWORD old;
            if (VirtualProtect(&th->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                g_create9 = (Create9Fn)real; th->u1.Function = (ULONG_PTR)hkCreate9; patched++;
                VirtualProtect(&th->u1.Function, sizeof(void*), old, &old);
            }
        }
    }
    shr_log(patched ? "per-pixel lighting: d3d9 hook ready" : "per-pixel lighting: Direct3DCreate9 not imported by d3d8.dll");
}

void pp_reset();
void pp_dropBound();
void pp_forget();
// The game may create a second device (a level started straight from the command line sets the video
// mode again). Everything made on the first one - shaders, render targets, shadow maps - goes while
// that device still exists: pp_deviceGone() runs when the game releases it (before the device itself),
// and the new device gets everything made again.
static void* g_ppDev = nullptr;
void pp_deviceGone() {
    pp_reset();
    for (auto& kv : g_cache) { if (kv.second.vs) kv.second.vs->Release(); if (kv.second.ps) kv.second.ps->Release(); }
    g_cache.clear();
    dropPrebuilt(true);
    pp_dropBound();
    g_ppDev = nullptr;
    shr_log("per-pixel lighting: device released, its shaders and effect targets freed");
}
void* pp_takeDevice() {
    void* d = g_lastDev; g_lastDev = nullptr;
    if (d && g_ppDev && d != g_ppDev) {
        // a second device while the first one lives on: its objects cannot be used here and are
        // left to it (forgotten, not released)
        g_cache.clear(); dropPrebuilt(false); pp_dropBound(); pp_forget();
        shr_log("per-pixel lighting: another device, shaders and effect targets made again");
    }
    if (d) g_ppDev = d;
    return d;
}
void pp_setEnabled(bool on) { g_enabled = on; g_forceKey = true; }
bool pp_enabled() { return g_enabled; }
void pp_setDebug(bool on) { g_debug = on; g_forceKey = true; }
bool pp_debug() { return g_debug; }
void pp_setLightmapFilter(bool on) { g_lmFilter = on; g_forceKey = true; }
bool g_book = false;   // the storybook: no relief or detail brightening (burnt-out parchment)
void pp_setBook(bool b) { g_book = b; g_forceKey = true; }
void pp_setDetail(float a) { g_detailAmt = a; g_forceKey = true; }
void pp_setAerial(float a) { g_aerial = a; g_forceKey = true; }
// alpha-tested draws (leaves, grass, fences) with multisampling: their alpha becomes coverage
// (NVIDIA's "ATOC" switch, which DXVK honours), so the cut-out edges are smoothed like geometry edges
extern bool g_a2cIni;
void pp_alphaToCoverage(void* dv, bool alphaTest) {
    if (!dv || !g_a2cIni) return;
    static void* lastDev = nullptr; static int last = -1;    // only real changes: each one is a new pipeline state for DXVK
    int want = alphaTest ? 1 : 0;
    if (dv == lastDev && want == last) return;
    if (want && !lastDev) shr_log("foliage smoothing: alpha to coverage in use (needs multisampling)");
    lastDev = dv; last = want;
    ((IDirect3DDevice9*)dv)->SetRenderState(D3DRS_ADAPTIVETESS_Y, alphaTest ? (DWORD)MAKEFOURCC('A', 'T', 'O', 'C') : 0);
}
void pp_setBump(float depth, float ao, int level) { g_bumpDepth = depth; g_bumpAO = ao; g_bumpLevel = level & 3; g_forceKey = true; }
void pp_setBumpExtra(float chars, float blur, bool world) { g_bumpChar = chars; g_bumpBlur = blur; g_world = world; }
int pp_bumpLevel() { return g_bumpLevel; }
unsigned pp_takeMade() { unsigned n = g_made; g_made = 0; return n; }
unsigned pp_drawCount() { unsigned n = g_draws; g_draws = 0; return n; }

namespace {
IDirect3DVertexShader9* g_boundVS = nullptr; IDirect3DPixelShader9* g_boundPS = nullptr;
bool g_bound = false, g_fogForced = false;

// why a draw stays fixed-function (each reason logged once)
std::string g_lastReject;
void reject(const char* why) {
    g_lastReject = why;
    static std::vector<std::string> seen;
    for (auto& x : seen) if (x == why) return;
    seen.push_back(why); shr_log("per-pixel lighting: some draws stay fixed-function: %s", why);
}
// state that decides the shader; false = this draw stays fixed-function
bool prepare(IDirect3DDevice9* d, DWORD gameFog, Key& k, const Prog*& p) {
    DWORD v = 0, lit = 0;
    if (FAILED(d->GetRenderState(D3DRS_LIGHTING, &lit))) return false;
    bool aoDepth = g_ssao > 0 && fxOn() && g_rt1 && !g_debug;
    bool shadowRecv = shadowReadValid() && !g_debug;
    if (!lit && g_debug) return false;
    if (!lit && !(g_world && g_bumpLevel > 0) && !aoDepth && !shadowRecv && !g_lmFilter && g_detailAmt <= 0) return false;   // unlit geometry: relief, occlusion depth, shadows
    d->GetRenderState(D3DRS_VERTEXBLEND, &v); if (v) { reject("vertex blending"); return false; }
    d->GetRenderState(D3DRS_POINTSPRITEENABLE, &v); if (v) { reject("point sprites"); return false; }
    d->GetRenderState(D3DRS_CLIPPLANEENABLE, &v); if (v) { reject("clip planes"); return false; }
    d->GetRenderState(D3DRS_SHADEMODE, &v); if (v == D3DSHADE_FLAT) { reject("flat shading"); return false; }
    {   // our own shaders may still be bound from the previous draw: they count as fixed function
        IDirect3DVertexShader9* vs = nullptr; d->GetVertexShader(&vs);
        if (vs) { vs->Release(); if (vs != g_boundVS) { reject("the game's own vertex shader"); return false; } }
        IDirect3DPixelShader9* ps = nullptr; d->GetPixelShader(&ps);
        if (ps) { ps->Release(); if (ps != g_boundPS) { reject("the game's own pixel shader"); return false; } }
    }
    memset(&k, 0, sizeof k);
    {
        IDirect3DVertexDeclaration9* decl = nullptr;
        if (FAILED(d->GetVertexDeclaration(&decl)) || !decl) { reject("no vertex declaration"); return false; }
        D3DVERTEXELEMENT9 el[MAXD3DDECLLENGTH + 1]; UINT n = MAXD3DDECLLENGTH + 1;
        HRESULT h = decl->GetDeclaration(el, &n); decl->Release();
        if (FAILED(h)) return false;
        bool pos = false, nrm = false;
        g_keyMulti = false;
        for (UINT i = 0; i < n && el[i].Stream != 0xFF; i++) {
            const auto& e = el[i];
            if (e.Stream > 0) g_keyMulti = true;
            switch (e.Usage) {
            case D3DDECLUSAGE_POSITION: if (e.UsageIndex == 0) pos = true; break;
            case D3DDECLUSAGE_POSITIONT: return false;   // 2D
            case D3DDECLUSAGE_NORMAL: if (e.UsageIndex == 0) nrm = true; break;
            case D3DDECLUSAGE_COLOR: if (e.UsageIndex == 0) k.c0 = 1; else if (e.UsageIndex == 1) k.c1 = 1; break;
            case D3DDECLUSAGE_TEXCOORD: if (e.UsageIndex < 8) k.tc[e.UsageIndex] = (uint8_t)declCount(e.Type); break;
            default: break;
            }
        }
        if (!pos || (lit && !nrm)) { reject(pos ? "lit geometry without normals" : "no position"); return false; }
        k.hasN = nrm; k.lit = lit != 0;
    }
    for (int st = 0; st < 8; st++) {
        Stage& g = k.st[st];
        DWORD op = 0; d->GetTextureStageState(st, D3DTSS_COLOROP, &op);
        if (op == D3DTOP_DISABLE) break;
        DWORD aop = 0, a[6] = {}, res = 0, tci = 0, ttf = 0;
        d->GetTextureStageState(st, D3DTSS_ALPHAOP, &aop);
        d->GetTextureStageState(st, D3DTSS_COLORARG0, &a[0]); d->GetTextureStageState(st, D3DTSS_COLORARG1, &a[1]); d->GetTextureStageState(st, D3DTSS_COLORARG2, &a[2]);
        d->GetTextureStageState(st, D3DTSS_ALPHAARG0, &a[3]); d->GetTextureStageState(st, D3DTSS_ALPHAARG1, &a[4]); d->GetTextureStageState(st, D3DTSS_ALPHAARG2, &a[5]);
        d->GetTextureStageState(st, D3DTSS_RESULTARG, &res);
        d->GetTextureStageState(st, D3DTSS_TEXCOORDINDEX, &tci);
        d->GetTextureStageState(st, D3DTSS_TEXTURETRANSFORMFLAGS, &ttf);
        if (!opSupported((int)op) || (aop != D3DTOP_DISABLE && !opSupported((int)aop))) { char b[64]; snprintf(b, sizeof b, "texture operation %lu/%lu", op, aop); reject(b); return false; }
        g.cop = (uint8_t)op; g.aop = (uint8_t)aop;
        for (int i = 0; i < 3; i++) { g.ca[i] = (uint8_t)a[i]; g.aa[i] = (uint8_t)a[3 + i]; }
        g.temp = (res & 0x0F) == D3DTA_TEMP;
        g.tci = (uint8_t)(tci & 7); g.tcf = (uint8_t)((tci >> 16) & 7);
        g.ttf = (uint8_t)(ttf & 7); g.proj = (ttf & D3DTTFF_PROJECTED) != 0;
        IDirect3DBaseTexture9* t = nullptr; d->GetTexture(st, &t);
        if (t) {
            switch (t->GetType()) { case D3DRTYPE_TEXTURE: g.tex = 1; break; case D3DRTYPE_CUBETEXTURE: g.tex = 2; break; case D3DRTYPE_VOLUMETEXTURE: g.tex = 3; break; default: break; }
            t->Release();
        }
        k.n = (uint8_t)(st + 1);
    }
    // lightmaps (level geometry): an unlit draw, a stage that multiplies a 2D texture read with the
    // second texture coordinate set, untransformed
    bool anyLm = false;
    if (g_lmFilter && !lit && k.tc[1] == 2)
        for (int st = 0; st < k.n; st++) {
            Stage& g = k.st[st];
            bool mul = g.cop == D3DTOP_MODULATE || g.cop == D3DTOP_MODULATE2X || g.cop == D3DTOP_MODULATE4X;
            bool usesTex = (g.ca[1] & 0x0F) == D3DTA_TEXTURE || (g.ca[2] & 0x0F) == D3DTA_TEXTURE;
            if (g.tex == 1 && mul && usesTex && g.tci == 1 && g.tcf == 0 && !g.proj && g.ttf <= 1) { g.lm = 1; anyLm = true; }
        }
    // the texture that gives the relief: the first 2D texture a stage combines, with plain texture coordinates
    DWORD blend = 0, zw = 0;
    d->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend); d->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
    if (g_bumpLevel > 0 && (k.lit || (!blend && zw)))      // not on effects and overlays
        for (int st = 0; st < k.n && !k.bump; st++) {
            const Stage& g = k.st[st];
            bool usesTex = (g.ca[1] & 0x0F) == D3DTA_TEXTURE || (g.ca[2] & 0x0F) == D3DTA_TEXTURE;
            if (g.tex == 1 && !g.proj && g.tcf == 0 && usesTex && !g.lm) k.bump = (uint8_t)(st + 1);
        }
    bool opaque = !blend && zw;
    k.aer = (uint8_t)(g_aerial > 0 && !g_debug);
    // fine detail on stretched textures: level geometry and props (static vertex buffers), not the
    // characters (skinned into dynamic buffers every frame: faces and fur must stay as painted)
    if (g_detailAmt > 0 && opaque && !g_debug) {
        int ds = -1;
        for (int st = 0; st < k.n && ds < 0; st++) {
            const Stage& g = k.st[st];
            bool usesTex = (g.ca[1] & 0x0F) == D3DTA_TEXTURE || (g.ca[2] & 0x0F) == D3DTA_TEXTURE;
            if (g.tex == 1 && !g.proj && g.tcf == 0 && usesTex && !g.lm) ds = st;
        }
        bool dyn = true;
        IDirect3DVertexBuffer9* vb = nullptr; UINT off = 0, stride = 0;
        if (SUCCEEDED(d->GetStreamSource(0, &vb, &off, &stride)) && vb) { D3DVERTEXBUFFER_DESC bd{}; vb->GetDesc(&bd); dyn = (bd.Usage & D3DUSAGE_DYNAMIC) != 0; vb->Release(); }
        if (ds >= 0 && !dyn) k.detail = (uint8_t)(ds + 1);
    }
    // alpha-blended surfaces that are solid where their texture is (tables, fences, leaves: alpha test or
    // depth writes with the usual SRCALPHA / INVSRCALPHA blend): their view depth goes to the second
    // target too, wherever they are mostly opaque - else the ambient occlusion and the effects took the
    // depth of what lies behind them, and its shading showed through
    bool masked = false;
    if (blend && !opaque) {
        DWORD sb = 0, db = 0, at = 0; d->GetRenderState(D3DRS_SRCBLEND, &sb); d->GetRenderState(D3DRS_DESTBLEND, &db); d->GetRenderState(D3DRS_ALPHATESTENABLE, &at);
        masked = sb == D3DBLEND_SRCALPHA && db == D3DBLEND_INVSRCALPHA && (zw || at) && (k.lit || k.bump || anyLm || k.detail);
    }
    k.mrt = (uint8_t)(aoDepth && opaque ? 1 : aoDepth && masked ? 2 : 0);
    k.shadow = (uint8_t)(shadowRecv && !blend && (opaque || k.lit));
    {   // characters (skinned into dynamic vertex buffers) get the level's own sun shadows too
        bool dynVB = true;
        IDirect3DVertexBuffer9* vb = nullptr; UINT off = 0, stride = 0;
        if (SUCCEEDED(d->GetStreamSource(0, &vb, &off, &stride)) && vb) { D3DVERTEXBUFFER_DESC bd{}; vb->GetDesc(&bd); dynVB = (bd.Usage & D3DUSAGE_DYNAMIC) != 0; vb->Release(); }
        // lit draws (characters, props lit by the engine): through their sun light; the level itself (baked
        // lighting): darkened where something stands between it and the sun
        k.wsh = (uint8_t)(g_worldSh > 0 && k.shadow && !g_debug ? (k.lit ? 1 : 2) : 0);
        (void)dynVB;
    }
    // water: unlit level geometry with a texture that moves (a texture transform on a 2D stage); the
    // shader itself keeps the reflections to flat, upward parts
    if (g_water > 0 && !lit && !g_debug && g_ssao > 0 && fxOn()) {
        // a cube map read with the camera's reflection vector on blended level geometry: the old fake
        // reflection - read at its smallest level (its average colour), the real one goes on top
        if (blend) for (int st = 0; st < k.n; st++) { Stage& g = k.st[st]; if (g.tex == 2 && g.tcf == 3) { k.water = 2; g.tcf = 5; } }   // reflection per pixel
        static bool said = false; if (k.water && !said) { said = true; shr_log("water: our shaders take a water surface (kind %d)", k.water); }
    }
    if (!k.lit && !k.bump && !k.mrt && !k.shadow && !anyLm && !k.detail && !k.water) return false;     // nothing to add to unlit geometry
    if (g_debug) {                                                  // lighting-only view
        k.mrt = 0; k.shadow = 0; k.wsh = 0; k.water = 0;
        k.debug = g_enabled ? 1 : 2;
        if (k.debug == 2) { k.n = 0; memset(k.st, 0, sizeof k.st); k.bump = 0; }
    }
    if (interpolators(k) > 10) { reject("too many texture stages"); return false; }   // ps_3_0 has 10 input registers

    DWORD colorVertex = 0, srcs[4] = {}, spec = 0, local = 0, norm = 0, range = 0, fog = 0, fogV = 0, fogT = 0;
    d->GetRenderState(D3DRS_COLORVERTEX, &colorVertex);
    d->GetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, &srcs[0]); d->GetRenderState(D3DRS_AMBIENTMATERIALSOURCE, &srcs[1]);
    d->GetRenderState(D3DRS_SPECULARMATERIALSOURCE, &srcs[2]); d->GetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, &srcs[3]);
    d->GetRenderState(D3DRS_SPECULARENABLE, &spec); d->GetRenderState(D3DRS_LOCALVIEWER, &local);
    d->GetRenderState(D3DRS_NORMALIZENORMALS, &norm); d->GetRenderState(D3DRS_RANGEFOGENABLE, &range);
    fog = gameFog; d->GetRenderState(D3DRS_FOGVERTEXMODE, &fogV); d->GetRenderState(D3DRS_FOGTABLEMODE, &fogT);
    DWORD mask = colorVertex ? ((k.c0 ? D3DMCS_COLOR1 : 0) | (k.c1 ? D3DMCS_COLOR2 : 0)) : 0;
    for (int i = 0; i < 4; i++) k.src[i] = (uint8_t)(srcs[i] & mask);
    k.spec = spec != 0; k.local = local != 0; k.norm = norm != 0; k.range = range != 0;
    k.fog = fog != 0; k.fogV = (uint8_t)(fog ? fogV & 3 : 0); k.fogT = (uint8_t)(fog ? fogT & 3 : 0);

    if (!loadCompiler()) { g_broken = true; return false; }
    p = getProg(d, k);
    return p != nullptr;
}

// shadow receiving constants (c166-c171): view space -> world -> shadow map of the previous frame
void shadowConsts(const M4& V, int sunIdx, float shc[6][4]) {
    int rd = 1 - g_sh.w; const float* shDir = g_sh.dir[rd];
    M4 S = mul(inverse(V), g_sh.uv[rd]);
    memset(shc, 0, 6 * 16);
    for (int j = 0; j < 3; j++) for (int i = 0; i < 4; i++) shc[j][i] = S.m[i][j];
    shc[3][0] = g_shadow * g_mapShadow; shc[3][1] = 10.f / 8000.f; shc[3][2] = 1.f / SHN; shc[3][3] = (float)sunIdx;
    shc[4][0] = 1.5f; shc[4][1] = g_shadow * g_mapShadow * 0.75f;
    {   // occluders count only up to about a character's height above the surface (measured along the
        // light: longer for a low sun), so a shadow does not go on through floors, tables and walls
        float up = std::max(0.45f, -shDir[2]), range = 140.f / up, fade = 90.f / up;
        shc[4][2] = range / 8000.f; shc[4][3] = 8000.f / fade;
    }
    for (int j = 0; j < 3; j++) shc[5][j] = shDir[0] * V.m[0][j] + shDir[1] * V.m[1][j] + shDir[2] * V.m[2][j];
    shc[5][3] = 8.f / 8000.f;   // a prop blocks a character's shadow when it lies 8 units clear of both
}
// constants: matrices, lights, material, fog
void upload(IDirect3DDevice9* d, const Key& k) {
    D3DMATRIX w, vw, pr;
    d->GetTransform(D3DTS_WORLD, &w); d->GetTransform(D3DTS_VIEW, &vw); d->GetTransform(D3DTS_PROJECTION, &pr);
    M4 W = fromD3D(w), V = fromD3D(vw), P = fromD3D(pr);
    M4 WV = mul(W, V), WVP = mul(WV, P);
    bool persp = fabsf(P.m[2][3] - 1) < 1e-3 && fabsf(P.m[3][3]) < 1e-3;
    g_lastPersp = persp;
    if (persp && g_rt0BB && !g_postDone) {
        float pp4[4] = { P.m[0][0], P.m[1][1], P.m[2][0], P.m[2][1] };
        int ci = -1;
        for (int i = 0; i < g_nCams && ci < 0; i++) if (!memcmp(g_cams[i].p, pp4, 16) && !memcmp(&g_cams[i].v, &V, sizeof V)) ci = i;
        if (ci < 0 && g_nCams < 4) { ci = g_nCams++; memcpy(g_cams[ci].p, pp4, 16); g_cams[ci].v = V; g_cams[ci].n = 0; }
        if (ci >= 0) g_cams[ci].n++;
    }
    float vc[12 + 4 + 32][4] = {};
    for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) { vc[j][i] = WVP.m[i][j]; vc[4 + j][i] = WV.m[i][j]; }
    {   // normal matrix: rows of the inverse of the upper 3x3 of world*view
        const float (*a)[4] = WV.m;
        double c00 = (double)a[1][1] * a[2][2] - (double)a[1][2] * a[2][1], c01 = (double)a[1][2] * a[2][0] - (double)a[1][0] * a[2][2], c02 = (double)a[1][0] * a[2][1] - (double)a[1][1] * a[2][0];
        double det = a[0][0] * c00 + a[0][1] * c01 + a[0][2] * c02;
        if (fabs(det) > 1e-30) {
            double id = 1.0 / det;
            double inv[3][3] = {
                { c00 * id, ((double)a[0][2] * a[2][1] - (double)a[0][1] * a[2][2]) * id, ((double)a[0][1] * a[1][2] - (double)a[0][2] * a[1][1]) * id },
                { c01 * id, ((double)a[0][0] * a[2][2] - (double)a[0][2] * a[2][0]) * id, ((double)a[0][2] * a[1][0] - (double)a[0][0] * a[1][2]) * id },
                { c02 * id, ((double)a[0][1] * a[2][0] - (double)a[0][0] * a[2][1]) * id, ((double)a[0][0] * a[1][1] - (double)a[0][1] * a[1][0]) * id } };
            // n' = n * transpose(inverse): n'_j = dot(n, row j of the inverse)
            for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) vc[8 + j][i] = (float)inv[j][i];
        } else for (int j = 0; j < 3; j++) vc[8 + j][j] = 1;
    }
    DWORD fs = 0, fe = 0, fd = 0, fc = 0, tf = 0, amb = 0;
    d->GetRenderState(D3DRS_FOGSTART, &fs); d->GetRenderState(D3DRS_FOGEND, &fe); d->GetRenderState(D3DRS_FOGDENSITY, &fd);
    d->GetRenderState(D3DRS_FOGCOLOR, &fc); d->GetRenderState(D3DRS_TEXTUREFACTOR, &tf); d->GetRenderState(D3DRS_AMBIENT, &amb);
    float fStart, fEnd, fDens; memcpy(&fStart, &fs, 4); memcpy(&fEnd, &fe, 4); memcpy(&fDens, &fd, 4);
    float fogC[4] = { fEnd, 1.f / (fEnd - fStart), fDens, 0 };
    memcpy(vc[11], fogC, 16);
    int nTex = 0;
    for (int st = 0; st < k.n; st++) {
        int fl = k.st[st].ttf;
        if (fl > 1 && fl <= 4) {
            D3DMATRIX t; d->GetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + st), &t);
            for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) vc[12 + st * 4 + j][i] = t.m[i][j];
            nTex = st + 1;
        }
    }
    d->SetVertexShaderConstantF(VS_BASE, &vc[0][0], 12 + nTex * 4);

    float pc[124][4] = {};
    pc[65][0] = V.m[2][0]; pc[65][1] = V.m[2][1]; pc[65][2] = V.m[2][2]; pc[65][3] = 1;   // world up (Unreal: +Z) in view space
    int li = 0, sunIdx = -1;
    const float* shDir = g_sh.dir[1 - g_sh.w];
    bool dynamicDraw = true;
    {
        IDirect3DVertexBuffer9* vb = nullptr; UINT off = 0, stride = 0;
        if (SUCCEEDED(d->GetStreamSource(0, &vb, &off, &stride)) && vb) {
            D3DVERTEXBUFFER_DESC bd{}; vb->GetDesc(&bd); dynamicDraw = (bd.Usage & D3DUSAGE_DYNAMIC) != 0; vb->Release();
        }
    }
    g_lastDyn = dynamicDraw;
    for (DWORD i = 0; i < 16 && li < 8; i++) {
        BOOL on = FALSE;
        if (FAILED(d->GetLightEnable(i, &on)) || !on) continue;
        D3DLIGHT9 L; if (FAILED(d->GetLight(i, &L))) continue;
        if (L.Type < D3DLIGHT_POINT || L.Type > D3DLIGHT_DIRECTIONAL) continue;
        float p4[4] = { L.Position.x, L.Position.y, L.Position.z, 1 }, d4[4] = { L.Direction.x, L.Direction.y, L.Direction.z, 0 };
        float pv[3], dv3[3];
        for (int j = 0; j < 3; j++) {
            pv[j] = p4[0] * V.m[0][j] + p4[1] * V.m[1][j] + p4[2] * V.m[2][j] + p4[3] * V.m[3][j];
            dv3[j] = d4[0] * V.m[0][j] + d4[1] * V.m[1][j] + d4[2] * V.m[2][j];
        }
        float dl = sqrtf(dv3[0] * dv3[0] + dv3[1] * dv3[1] + dv3[2] * dv3[2]); if (dl > 0) for (float& x : dv3) x /= dl;
        float* lp = pc[li]; lp[0] = pv[0]; lp[1] = pv[1]; lp[2] = pv[2]; lp[3] = (float)L.Type;
        float* ld = pc[8 + li]; ld[0] = dv3[0]; ld[1] = dv3[1]; ld[2] = dv3[2];
        memcpy(pc[16 + li], &L.Diffuse, 16); memcpy(pc[24 + li], &L.Specular, 16); memcpy(pc[32 + li], &L.Ambient, 16);
        float* la = pc[40 + li]; la[0] = L.Attenuation0; la[1] = L.Attenuation1; la[2] = L.Attenuation2; la[3] = L.Range;
        float* ls = pc[48 + li]; ls[0] = L.Falloff; ls[1] = cosf(L.Theta / 2); ls[2] = cosf(L.Phi / 2);
        if (L.Type == D3DLIGHT_DIRECTIONAL) {
            float wl = sqrtf(L.Direction.x * L.Direction.x + L.Direction.y * L.Direction.y + L.Direction.z * L.Direction.z);
            if (wl > 0) {
                float wd[3] = { L.Direction.x / wl, L.Direction.y / wl, L.Direction.z / wl };
                if (k.shadow && sunIdx < 0 && wd[0] * shDir[0] + wd[1] * shDir[1] + wd[2] * shDir[2] > 0.98f) sunIdx = li;
                // the strongest directional light on characters casts their shadows next frame
                float lum = L.Diffuse.r * 0.3f + L.Diffuse.g * 0.59f + L.Diffuse.b * 0.11f;
                if (dynamicDraw && k.lit && lum > g_sunNextLum && g_rt0BB) { g_sunNextLum = lum; memcpy(g_sunNext, wd, 12); }
            }
        }
        li++;
    }
    D3DMATERIAL9 m{}; d->GetMaterial(&m);
    memcpy(pc[56], &m.Diffuse, 16); memcpy(pc[57], &m.Ambient, 16); memcpy(pc[58], &m.Specular, 16); memcpy(pc[59], &m.Emissive, 16);
    color4(amb, pc[60]); pc[61][0] = m.Power;
    {
        // characters are skinned on the CPU into dynamic vertex buffers (or drawn from memory): they
        // get a weaker relief, fur and painted faces turn into noise otherwise
        bool dynamic = dynamicDraw;
        static const float lv[4] = { 0, 0.5f, 1, 2 };
        float f = lv[g_bumpLevel] * (dynamic ? g_bumpChar : 1.f);
        if (g_book) f = 0;
        pc[61][1] = g_bumpDepth * f; pc[61][2] = g_bumpAO * f; pc[61][3] = g_bumpBlur;
    } color4(tf, pc[62]); color4(fc, pc[63]); memcpy(pc[64], fogC, 16);
    int npc = 66;
    for (int st = 0; st < k.n; st++) {     // texture transforms of stages generated in the pixel shader
        if (!psGen(k.st[st])) continue;
        D3DMATRIX t; d->GetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + st), &t);
        for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) pc[72 + st * 4 + j][i] = t.m[i][j];
        npc = std::max(npc, 72 + st * 4 + 4);
    }
    if (k.shadow) { float shc[6][4]; shadowConsts(V, sunIdx, shc); memcpy(pc[66], shc, sizeof shc); npc = std::max(npc, 72); }
    if (k.lit) {   // actors (own world transform) take the level's character brightness
        bool actor = !(w.m[3][0] == 0 && w.m[3][1] == 0 && w.m[3][2] == 0 && w.m[0][0] == 1 && w.m[1][1] == 1 && w.m[2][2] == 1);
        pc[104][0] = actor ? g_mapChars : 1.f; npc = std::max(npc, 105);
    }
    for (int st = 0; st < k.n; st++) {     // lightmap sizes for the bicubic filter
        bool det = k.detail == st + 1;
        if (!k.st[st].lm && !det) continue;
        IDirect3DBaseTexture9* bt = nullptr; d->GetTexture(st, &bt);
        float w = 1, h = 1;
        if (bt) {
            if (bt->GetType() == D3DRTYPE_TEXTURE) {
                D3DSURFACE_DESC sd{};
                if (SUCCEEDED(((IDirect3DTexture9*)bt)->GetLevelDesc(0, &sd))) { w = (float)sd.Width; h = (float)sd.Height; }
                static bool said = false;
                if (!said && !det) { said = true; shr_log("lightmaps: bicubic filtering (first one %ux%u, format %u)", sd.Width, sd.Height, (unsigned)sd.Format); }
            }
            bt->Release();
        }
        pc[105 + st][0] = w; pc[105 + st][1] = h; pc[105 + st][2] = det ? (g_book ? 0.f : g_detailAmt) : 1.f / w; pc[105 + st][3] = 1.f / h;
        npc = std::max(npc, 106 + st);
    }
    if (k.aer) {   // aerial perspective: from 1500 units on, full at 12000
        pc[113][0] = 1500.f; pc[113][1] = 1.f / 10500.f; pc[113][2] = g_aerial;
        pc[114][0] = 0.92f; pc[114][1] = 1.0f; pc[114][2] = 1.12f; pc[114][3] = 0.03f;
        npc = std::max(npc, 115);
    }
    if (k.wsh) {   // c217: strength on the level, texel, bias; c218..220: view space -> the kept map
        pc[117][0] = g_worldSh * 0.5f * g_mapShadow; pc[117][1] = 1.f / WSN; pc[117][2] = 30.f / (2 * WSD); pc[117][3] = g_wsQuality;
        M4 S = mul(inverse(V), g_ws.uv);
        for (int j = 0; j < 3; j++) for (int i = 0; i < 4; i++) pc[118 + j][i] = S.m[i][j];
        npc = std::max(npc, 121);
    }
    if (k.water) {   // c215: projection of this frame; c216: strength, last frame's pictures there
        pc[115][0] = P.m[0][0]; pc[115][1] = P.m[1][1]; pc[115][2] = P.m[2][0]; pc[115][3] = P.m[2][1];
        pc[116][0] = g_water; pc[116][3] = g_waterPrev ? 1.f : 0.f;
        npc = std::max(npc, 117);
    }
    d->SetPixelShaderConstantF(PS_BASE, &pc[0][0], npc);
    if (k.debug == 2) d->SetVertexShaderConstantF(VS_BASE + 12, &pc[0][0], 62);   // lights for the per-vertex view

}
} // namespace
void pp_dropBound() { g_boundVS = nullptr; g_boundPS = nullptr; g_bound = false; g_forceKey = true; }

// genK / genC: counters the d3d8 wrapper advances whenever the game changes state that decides the
// shader (render and texture stage states, textures, streams, shaders) or only its constants
// (transforms, lights, material). Draws in between reuse everything: no queries, no uploads.
// Our shaders stay bound across draws and are taken off (pp_unbind) only before a draw that is
// not ours, before Present and around state blocks.
void pp_rtChanged(void* dv);
namespace { void fxBindShadow(IDirect3DDevice9* d); void fxBindWater(IDirect3DDevice9* d); }
bool pp_begin(void* dv, unsigned genK, unsigned genC, DWORD gameFog) {
    static unsigned lastK = ~0u, lastC = ~0u; static bool lastUse = false; static const Prog* lastProg = nullptr;
    static Key lastKey;
    if ((!g_enabled && !g_debug) || g_broken || !dv) return false;
    auto d = (IDirect3DDevice9*)dv;
    if (!g_rtKnown) pp_rtChanged(dv);
    bool keyFresh = genK != lastK || g_forceKey;
    g_forceKey = false;
    if (keyFresh) {
        lastK = genK; lastC = ~0u;
        g_lastReject = "-";
        lastUse = prepare(d, gameFog, lastKey, lastProg);
    }
    g_infoKey = lastUse ? &lastKey : nullptr;
    if (!lastUse) return false;
    if (keyFresh || genC != lastC) { lastC = genC; upload(d, lastKey); }
    if (!g_bound || keyFresh) {
        d->SetVertexShader(lastProg->vs); d->SetPixelShader(lastProg->ps);
        g_boundVS = lastProg->vs; g_boundPS = lastProg->ps; g_bound = true;
    }
    if (lastKey.fog && !g_fogForced) { d->SetRenderState(D3DRS_FOGENABLE, FALSE); g_fogForced = true; }   // fog is done in the pixel shader
    if (g_rt1) {   // view depth only from opaque geometry drawn by our shaders
        DWORD want = lastKey.mrt && g_lastPersp && !g_zFrozen ? 0xF : 0;     // not for orthographic 3D (HUD items)
        if (want != g_cwe1) { d->SetRenderState(D3DRS_COLORWRITEENABLE1, want); g_cwe1 = want; }
        if (want) { g_zWritten = true; g_zDraws++; g_sceneZ++; }
    }
    if (lastKey.shadow) fxBindShadow(d);
    if (lastKey.water) { fxBindWater(d); g_waterDrawn = true; }
    g_anyPP = true;
    g_draws++;
    return true;
}

void pp_unbind(void* dv, DWORD gameFog) {
    if (!g_bound || !dv) return;
    auto d = (IDirect3DDevice9*)dv;
    IDirect3DVertexShader9* vs = nullptr; d->GetVertexShader(&vs);
    if (vs) { vs->Release(); if (vs == g_boundVS) d->SetVertexShader(nullptr); }
    IDirect3DPixelShader9* ps = nullptr; d->GetPixelShader(&ps);
    if (ps) { ps->Release(); if (ps == g_boundPS) d->SetPixelShader(nullptr); }
    if (g_fogForced) d->SetRenderState(D3DRS_FOGENABLE, gameFog);
    if (g_rt1 && g_cwe1 != 0) { d->SetRenderState(D3DRS_COLORWRITEENABLE1, 0); g_cwe1 = 0; }
    g_bound = false; g_fogForced = false;
}

// ---------------------------------------------------------------- sharpening (AMD FidelityFX CAS)
// Applied to the finished frame just before Present: the back buffer is resolved into a texture
// and drawn back through a contrast adaptive sharpening shader. All device state the pass touches
// is captured in a state block and restored afterwards.
namespace {
const char* CAS_VS =
    "struct O { float4 p : POSITION; float2 t : TEXCOORD0; };\n"
    "O main(float4 p : POSITION, float2 t : TEXCOORD0) { O o; o.p = p; o.t = t; return o; }\n";
const char* CAS_PS =
    "sampler2D s : register(s0);\nfloat4 cP : register(c200);\n"
    "float3 T(float2 uv, float x, float y) { return tex2D(s, uv + float2(x, y) * cP.xy).rgb; }\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    " float3 a = T(uv, -1, -1), b = T(uv, 0, -1), c = T(uv, 1, -1);\n"
    " float3 d = T(uv, -1, 0), e = T(uv, 0, 0), f = T(uv, 1, 0);\n"
    " float3 g = T(uv, -1, 1), h = T(uv, 0, 1), i = T(uv, 1, 1);\n"
    " float3 mn = min(min(min(d, e), min(f, b)), h);\n"
    " mn += min(mn, min(min(a, c), min(g, i)));\n"
    " float3 mx = max(max(max(d, e), max(f, b)), h);\n"
    " mx += max(mx, max(max(a, c), max(g, i)));\n"
    " float3 amp = sqrt(saturate(min(mn, 2.0 - mx) / max(mx, 1e-5)));\n"
    " float3 w = amp * (-1.0 / lerp(8.0, 5.0, cP.z));\n"
    " float3 r = (b * w + d * w + f * w + h * w + e) / (1.0 + 4.0 * w);\n"
    " return float4(saturate(r * cP.w), 1);\n}\n";

struct Post {
    IDirect3DTexture9* tex = nullptr; UINT w = 0, h = 0; D3DFORMAT fmt = D3DFMT_UNKNOWN;
    IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexDeclaration9* decl = nullptr; IDirect3DStateBlock9* sb = nullptr;
    bool failed = false;
} g_post;
float g_sharpen = 0.5f, g_exposure = 1.f;

bool postInit(IDirect3DDevice9* d) {
    if (g_post.vs) return true;
    if (g_post.failed) return false;
    g_post.failed = true;
    if (!loadCompiler() || !probeCompat(d)) return false;
    ID3DBlob* vb = compile(CAS_VS, "vs_3_0");
    ID3DBlob* pb = vb ? compile(CAS_PS, "ps_3_0") : nullptr;
    bool ok = false;
    if (vb && pb) {
        CompatLift lift(d);
        ok = SUCCEEDED(d->CreateVertexShader((const DWORD*)vb->GetBufferPointer(), &g_post.vs)) &&
             SUCCEEDED(d->CreatePixelShader((const DWORD*)pb->GetBufferPointer(), &g_post.ps));
    }
    if (vb) vb->Release();
    if (pb) pb->Release();
    D3DVERTEXELEMENT9 el[] = { { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
                               { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 }, D3DDECL_END() };
    ok = ok && SUCCEEDED(d->CreateVertexDeclaration(el, &g_post.decl)) && SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL, &g_post.sb));
    if (!ok) { shr_log("sharpening: setup failed"); return false; }
    g_post.failed = false;
    shr_log("sharpening: ready (strength %.2f)", g_sharpen);
    return true;
}
} // namespace

void pp_setSharpen(float s) { g_sharpen = s; }
void pp_setExposure(float e) { g_exposure = e; }
float pp_sharpen() { return g_sharpen; }

namespace { void fxRelease(); }
void subs_reset(bool rel);
void pp_reset() {                                     // before IDirect3DDevice8::Reset
    fxRelease();
    subs_reset(!g_relSkip);
    if (g_post.tex) { if (!g_relSkip) g_post.tex->Release(); g_post.tex = nullptr; }
    if (g_post.sb) { if (!g_relSkip) g_post.sb->Release(); g_post.sb = nullptr; }
    if (g_post.vs) { if (!g_relSkip) g_post.vs->Release(); g_post.vs = nullptr; }
    if (g_post.ps) { if (!g_relSkip) g_post.ps->Release(); g_post.ps = nullptr; }
    if (g_post.decl) { if (!g_relSkip) g_post.decl->Release(); g_post.decl = nullptr; }
    g_post.failed = false;
}
void pp_forget() { g_relSkip = true; pp_reset(); g_relSkip = false; }

void pp_present(void* dv) {
    if ((g_sharpen <= 0 && g_exposure >= 1.f) || !dv || g_broken) return;
    auto d = (IDirect3DDevice9*)dv;
    if (!postInit(d)) return;
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    D3DSURFACE_DESC bd{}; bb->GetDesc(&bd);
    if (!g_post.tex || g_post.w != bd.Width || g_post.h != bd.Height || g_post.fmt != bd.Format) {
        if (g_post.tex) { g_post.tex->Release(); g_post.tex = nullptr; }
        if (FAILED(d->CreateTexture(bd.Width, bd.Height, 1, D3DUSAGE_RENDERTARGET, bd.Format, D3DPOOL_DEFAULT, &g_post.tex, nullptr))) {
            shr_log("sharpening: no %ux%u texture", bd.Width, bd.Height); bb->Release(); g_sharpen = 0; return;
        }
        g_post.w = bd.Width; g_post.h = bd.Height; g_post.fmt = bd.Format;
    }
    IDirect3DSurface9* ts = nullptr; g_post.tex->GetSurfaceLevel(0, &ts);
    HRESULT h = d->StretchRect(bb, nullptr, ts, nullptr, D3DTEXF_NONE);   // also resolves MSAA
    ts->Release();
    if (FAILED(h)) { shr_log("sharpening: back buffer copy failed (0x%08lx)", h); bb->Release(); g_sharpen = 0; return; }

    g_post.sb->Capture();
    IDirect3DSurface9* rt = nullptr; d->GetRenderTarget(0, &rt);
    d->SetRenderTarget(0, bb);
    D3DVIEWPORT9 vp = { 0, 0, bd.Width, bd.Height, 0, 1 }; d->SetViewport(&vp);
    const DWORD rs[][2] = { { D3DRS_ZENABLE, FALSE }, { D3DRS_ZWRITEENABLE, FALSE }, { D3DRS_ALPHABLENDENABLE, FALSE },
        { D3DRS_ALPHATESTENABLE, FALSE }, { D3DRS_CULLMODE, D3DCULL_NONE }, { D3DRS_FOGENABLE, FALSE }, { D3DRS_STENCILENABLE, FALSE },
        { D3DRS_COLORWRITEENABLE, 0xF }, { D3DRS_SCISSORTESTENABLE, FALSE }, { D3DRS_SRGBWRITEENABLE, FALSE }, { D3DRS_FILLMODE, D3DFILL_SOLID },
        { D3DRS_CLIPPLANEENABLE, 0 } };
    for (auto& r : rs) d->SetRenderState((D3DRENDERSTATETYPE)r[0], r[1]);
    d->SetTexture(0, g_post.tex);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT); d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    d->SetVertexDeclaration(g_post.decl); d->SetVertexShader(g_post.vs); d->SetPixelShader(g_post.ps);
    float c[4] = { 1.f / bd.Width, 1.f / bd.Height, g_sharpen > 1 ? 1.f : g_sharpen, g_exposure };
    d->SetPixelShaderConstantF(200, c, 1);
    float ox = -1.f / bd.Width, oy = 1.f / bd.Height;      // D3D9 pixel centres
    float q[4][6] = { { -1 + ox, 1 + oy, 0.5f, 1, 0, 0 }, { 1 + ox, 1 + oy, 0.5f, 1, 1, 0 },
                      { -1 + ox, -1 + oy, 0.5f, 1, 0, 1 }, { 1 + ox, -1 + oy, 0.5f, 1, 1, 1 } };
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof q[0]);
    if (rt) { d->SetRenderTarget(0, rt); rt->Release(); }
    g_post.sb->Apply();
    bb->Release();
}

// ---------------------------------------------------------------- ambient occlusion, bloom, character shadows
// Ambient occlusion: while the 3D scene is drawn into the back buffer, our shaders also write the
// view depth of opaque geometry into a second render target (R32F, same multisampling). Before
// the first 2D draw (HUD, menus) - or at Present - the depth is resolved, the occlusion computed
// at half resolution (scalable ambient obscurance with a depth-aware blur) and multiplied into the
// back buffer. Bloom works on the same point: bright parts, blurred at quarter resolution, are
// screened on top. Character shadows: every character draw (lit, skinned into a dynamic vertex
// buffer) is drawn once more into a 2048x2048 depth map seen from the strongest directional light
// (straight down without one); the next frame's shaders darken what lies behind it.
namespace {
const char* FX_VS =
    "struct O { float4 p : POSITION; float2 t : TEXCOORD0; };\n"
    "O main(float4 p : POSITION, float2 t : TEXCOORD0) { O o; o.p = p; o.t = t; return o; }\n";
const char* AO_PS =
    "sampler2D sZ : register(s0);\n"
    "float4 cT : register(c200);\nfloat4 cP : register(c201);\nfloat4 cA : register(c202);\n"
    "float3 vpos(float2 uv, float z) { return float3(((uv.x * 2 - 1) - cP.z) * z / cP.x, ((1 - uv.y * 2) - cP.w) * z / cP.y, z); }\n"
    // positions at texel centres of the full-size depth: a half-size pixel lies between four of them,
    // and a depth read from one texel with the position of another tilts every row of a floor a little
    "float2 snap(float2 uv) { return (floor(uv / cT.xy + 0.25) + 0.5) * cT.xy; }\n"
    "float3 at(float2 uv) { uv = snap(uv); return vpos(uv, tex2Dlod(sZ, float4(uv, 0, 0)).r); }\n"
    "float4 main(float2 uv : TEXCOORD0, float2 sp : VPOS) : COLOR0 {\n"
    " uv = snap(uv);\n"
    " float z = tex2Dlod(sZ, float4(uv, 0, 0)).r;\n"
    " if (z < 1) return float4(1, 0, 0, 1);\n"
    " float3 P = vpos(uv, z);\n"
    " float3 px = at(uv + float2(cT.x * 2, 0)) - P, nx = P - at(uv - float2(cT.x * 2, 0));\n"
    " float3 py = at(uv + float2(0, cT.y * 2)) - P, ny = P - at(uv - float2(0, cT.y * 2));\n"
    " float3 dx = abs(px.z) < abs(nx.z) ? px : nx, dy = abs(py.z) < abs(ny.z) ? py : ny;\n"
    " float3 N = normalize(cross(dx, dy)); if (dot(N, P) > 0) N = -N;\n"
    " float R = cA.x;\n"
    " float2 ruv = float2(R * cP.x, R * cP.y) * 0.5 / z;\n"
    " float lim = 0.12 / max(ruv.x, 1e-6); ruv *= min(1, lim);\n"
    " float ang = frac(52.9829189 * frac(dot(sp, float2(0.06711056, 0.00583715)))) * 6.2831853;\n"
    " float occ = 0;\n"
    " [unroll] for (int i = 0; i < 12; i++) {\n"
    "  float a = ang + i * 2.3999632, r = sqrt((i + 0.5) / 12);\n"
    "  float2 suv = snap(uv + float2(cos(a), sin(a)) * r * ruv);\n"
    "  float sz = tex2Dlod(sZ, float4(suv, 0, 0)).r;\n"
    "  float3 v = vpos(suv, sz) - P; float vv = dot(v, v);\n"
    "  float o = min(1, max(0, dot(v, N) - 0.03 * z) * R / (vv + 0.05 * R * R));\n"
    "  occ += sz < 1 ? 0 : o * saturate(2 - vv / (R * R));\n"
    " }\n"
    " float ao = saturate(1 - cA.y * occ / 12);\n"
    " ao = lerp(1, ao, saturate(1 - (z - cA.z) * cA.w));\n"
    " return float4(ao, z, 0, 1);\n}\n";
const char* BLUR_PS =   // depth-aware blur of (occlusion, depth), direction in c203.xy
    // depth is compared with the plane through the centre (its slope from the neighbours): a floor seen
    // at a low angle changes depth quickly from row to row and must still blur along the rows' normal
    "sampler2D s : register(s0);\nfloat4 cD : register(c203);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    " float2 c = tex2Dlod(s, float4(uv, 0, 0)).rg; if (c.g < 1) return float4(1, 0, 0, 1);\n"
    " float zp = tex2Dlod(s, float4(uv + cD.xy, 0, 0)).g, zm = tex2Dlod(s, float4(uv - cD.xy, 0, 0)).g;\n"
    " float k = abs(zp - c.g) < abs(c.g - zm) ? zp - c.g : c.g - zm;\n"
    " if (zp < 1 || zm < 1) k = zp < 1 ? (zm < 1 ? 0 : c.g - zm) : zp - c.g;\n"
    " float sum = c.r, wsum = 1;\n"
    " [unroll] for (int i = -4; i <= 4; i++) {\n"
    "  if (i == 0) continue;\n"
    "  float2 t = tex2Dlod(s, float4(uv + cD.xy * i, 0, 0)).rg;\n"
    "  float w = exp(-i * i / 10.0) * saturate(1 - abs(t.g - c.g - k * i) / (0.02 * c.g + 2)) * (t.g >= 1 ? 1 : 0);\n"
    "  sum += t.r * w; wsum += w;\n }\n"
    " return float4(sum / wsum, c.g, 0, 1);\n}\n";
const char* MUL_PS =    // occlusion onto the back buffer (multiply blend), or shown alone (c204.x = 1)
    "sampler2D s : register(s0);\nfloat4 cV : register(c204);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 { float a = tex2D(s, uv).r; return float4(a, a, a, 1); }\n";
const char* BRIGHT_PS = // bloom: 4x4 texels -> one, bright part only (threshold c205.x)
    "sampler2D s : register(s0);\nfloat4 cT : register(c200);\nfloat4 cB : register(c205);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    " float3 c = tex2D(s, uv + cT.xy * float2(-1, -1)).rgb + tex2D(s, uv + cT.xy * float2(1, -1)).rgb +\n"
    "            tex2D(s, uv + cT.xy * float2(-1, 1)).rgb + tex2D(s, uv + cT.xy * float2(1, 1)).rgb;\n"
    " c *= 0.25; float l = max(c.r, max(c.g, c.b));\n"
    " float k = saturate((l - cB.x) / (1 - cB.x)); return float4(c * k * k, 1);\n}\n";
const char* GAUSS_PS =  // 9-tap gaussian through 5 bilinear fetches, direction c203.xy
    "sampler2D s : register(s0);\nfloat4 cD : register(c203);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    " float3 c = tex2D(s, uv).rgb * 0.2270270;\n"
    " c += (tex2D(s, uv + cD.xy * 1.3846154).rgb + tex2D(s, uv - cD.xy * 1.3846154).rgb) * 0.3162162;\n"
    " c += (tex2D(s, uv + cD.xy * 3.2307692).rgb + tex2D(s, uv - cD.xy * 3.2307692).rgb) * 0.0702703;\n"
    " return float4(c, 1);\n}\n";
const char* ADD_PS =    // bloom on top (screen blend), intensity c205.y
    "sampler2D s : register(s0);\nfloat4 cB : register(c205);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 { return float4(saturate(tex2D(s, uv).rgb * cB.y), 1); }\n";
// sun rays: bright parts of the picture near the sun (sky through leaves, windows), then blurred along
// the lines to the sun's place on the screen and screened on top. c200: sun (u, v), aspect, threshold
const char* RAYMASK_PS =
    "sampler2D s : register(s0);\nfloat4 cR : register(c200);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    " float3 c = tex2D(s, uv).rgb; float l = dot(c, float3(0.3, 0.59, 0.11));\n"
    " float k = saturate((l - cR.w) / (1 - cR.w));\n"
    " float2 dd = (uv - cR.xy) * float2(cR.z, 1);\n"
    " return float4(c * k * k * exp(-dot(dd, dd) * 2.2), 1);\n}\n";
const char* RAYBLUR_PS =   // 16 taps towards the sun, each weaker; c201.x: length of the streak (part of the way)
    "sampler2D s : register(s0);\nfloat4 cR : register(c200);\nfloat4 cB : register(c201);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    " float2 dl = (uv - cR.xy) * cB.x / 16; float2 p = uv; float3 sum = 0; float w = 1, ws = 0;\n"
    " [unroll] for (int i = 0; i < 16; i++) { sum += tex2Dlod(s, float4(p, 0, 0)).rgb * w; ws += w; w *= 0.93; p -= dl; }\n"
    " return float4(sum / ws, 1);\n}\n";
const char* SH_VS =
    "float4 cS[4] : register(c240);\n"
    "struct O { float4 p : POSITION; float z : TEXCOORD0; };\n"
    "O main(float4 p : POSITION0) { O o; o.p = float4(dot(p, cS[0]), dot(p, cS[1]), dot(p, cS[2]), dot(p, cS[3])); o.z = o.p.z; return o; }\n";
const char* SH_PS = "float4 main(float z : TEXCOORD0) : COLOR0 { return float4(z, 0, 0, 1); }\n";
// leaves, grass, fences (alpha-tested or masked): cast where their texture is solid
const char* SHA_VS =
    "float4 cS[4] : register(c240);\n"
    "struct O { float4 p : POSITION; float z : TEXCOORD0; float2 t : TEXCOORD1; };\n"
    "O main(float4 p : POSITION0, float2 t : TEXCOORD0) { O o; o.p = float4(dot(p, cS[0]), dot(p, cS[1]), dot(p, cS[2]), dot(p, cS[3])); o.z = o.p.z; o.t = t; return o; }\n";
const char* SHA_PS = "sampler2D s0 : register(s0);\nfloat4 main(float z : TEXCOORD0, float2 t : TEXCOORD1) : COLOR0 { clip(tex2D(s0, t).a - 0.5); return float4(z, 0, 0, 1); }\n";

struct FxRes {
    IDirect3DSurface9* zRT = nullptr; IDirect3DTexture9* zTex = nullptr; IDirect3DTexture9* ao[2] = {};
    IDirect3DTexture9* scene = nullptr; IDirect3DTexture9* bl[2] = {};
    UINT w = 0, h = 0; D3DMULTISAMPLE_TYPE ms = D3DMULTISAMPLE_NONE; DWORD msq = 0; D3DFORMAT fmt = D3DFMT_UNKNOWN;
    IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9 *ao_ps = nullptr, *blur = nullptr, *mul = nullptr, *bright = nullptr, *gauss = nullptr, *add = nullptr;
    IDirect3DPixelShader9 *rmask = nullptr, *rblur = nullptr; bool raysFailed = false;
    IDirect3DVertexDeclaration9* decl = nullptr; IDirect3DStateBlock9* sb = nullptr;
    bool shadersFailed = false, sizeFailed = false;
} g_fx;

template <class T> void rel(T*& p) { if (p) { if (!g_relSkip) p->Release(); p = nullptr; } }

bool makePS(IDirect3DDevice9* d, const char* src, IDirect3DPixelShader9** out) {
    ID3DBlob* b = compile(src, "ps_3_0"); if (!b) return false;
    CompatLift lift(d);
    bool ok = SUCCEEDED(d->CreatePixelShader((const DWORD*)b->GetBufferPointer(), out));
    b->Release(); return ok;
}
bool makeVS(IDirect3DDevice9* d, const char* src, IDirect3DVertexShader9** out) {
    ID3DBlob* b = compile(src, "vs_3_0"); if (!b) return false;
    CompatLift lift(d);
    bool ok = SUCCEEDED(d->CreateVertexShader((const DWORD*)b->GetBufferPointer(), out));
    b->Release(); return ok;
}

bool fxShaders(IDirect3DDevice9* d) {
    if (g_fx.vs) return true;
    if (g_fx.shadersFailed) return false;
    g_fx.shadersFailed = true;
    if (!loadCompiler() || !probeCompat(d)) return false;
    bool ok = makeVS(d, FX_VS, &g_fx.vs) && makePS(d, AO_PS, &g_fx.ao_ps) && makePS(d, BLUR_PS, &g_fx.blur) && makePS(d, MUL_PS, &g_fx.mul) &&
              makePS(d, BRIGHT_PS, &g_fx.bright) && makePS(d, GAUSS_PS, &g_fx.gauss) && makePS(d, ADD_PS, &g_fx.add);
    D3DVERTEXELEMENT9 el[] = { { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
                               { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 }, D3DDECL_END() };
    ok = ok && SUCCEEDED(d->CreateVertexDeclaration(el, &g_fx.decl)) && SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL, &g_fx.sb));
    if (!ok) { shr_log("effects: shader setup failed (ambient occlusion and bloom off)"); g_ssao = 0; g_bloom = 0; return false; }
    if (!(makePS(d, RAYMASK_PS, &g_fx.rmask) && makePS(d, RAYBLUR_PS, &g_fx.rblur))) { shr_log("effects: sun ray shaders failed (rays off)"); g_fx.raysFailed = true; }
    g_fx.shadersFailed = false;
    return true;
}

// targets for the back buffer's size and multisampling
bool fxTargets(IDirect3DDevice9* d, const D3DSURFACE_DESC& bd) {
    if (g_fx.zRT && g_fx.w == bd.Width && g_fx.h == bd.Height && g_fx.ms == bd.MultiSampleType && g_fx.msq == bd.MultiSampleQuality && g_fx.fmt == bd.Format) return true;
    if (g_fx.sizeFailed && g_fx.w == bd.Width && g_fx.h == bd.Height) return false;
    rel(g_fx.zRT); rel(g_fx.zTex); rel(g_fx.ao[0]); rel(g_fx.ao[1]); rel(g_fx.scene); rel(g_fx.bl[0]); rel(g_fx.bl[1]);
    g_fx.w = bd.Width; g_fx.h = bd.Height; g_fx.ms = bd.MultiSampleType; g_fx.msq = bd.MultiSampleQuality; g_fx.fmt = bd.Format;
    UINT hw = (bd.Width + 1) / 2, hh = (bd.Height + 1) / 2, qw = (bd.Width + 3) / 4, qh = (bd.Height + 3) / 4;
    bool ok = SUCCEEDED(d->CreateRenderTarget(bd.Width, bd.Height, D3DFMT_R32F, bd.MultiSampleType, bd.MultiSampleQuality, FALSE, &g_fx.zRT, nullptr)) &&
              SUCCEEDED(d->CreateTexture(bd.Width, bd.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &g_fx.zTex, nullptr)) &&
              SUCCEEDED(d->CreateTexture(hw, hh, 1, D3DUSAGE_RENDERTARGET, D3DFMT_G16R16F, D3DPOOL_DEFAULT, &g_fx.ao[0], nullptr)) &&
              SUCCEEDED(d->CreateTexture(hw, hh, 1, D3DUSAGE_RENDERTARGET, D3DFMT_G16R16F, D3DPOOL_DEFAULT, &g_fx.ao[1], nullptr)) &&
              SUCCEEDED(d->CreateTexture(bd.Width, bd.Height, 1, D3DUSAGE_RENDERTARGET, bd.Format, D3DPOOL_DEFAULT, &g_fx.scene, nullptr)) &&
              SUCCEEDED(d->CreateTexture(qw, qh, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &g_fx.bl[0], nullptr)) &&
              SUCCEEDED(d->CreateTexture(qw, qh, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &g_fx.bl[1], nullptr));
    if (!ok) {
        shr_log("effects: no render targets for %ux%u (ambient occlusion and bloom off)", bd.Width, bd.Height);
        rel(g_fx.zRT); rel(g_fx.zTex); rel(g_fx.ao[0]); rel(g_fx.ao[1]); rel(g_fx.scene); rel(g_fx.bl[0]); rel(g_fx.bl[1]);
        g_fx.sizeFailed = true; return false;
    }
    g_fx.sizeFailed = false;
    d->ColorFill(g_fx.zRT, nullptr, 0);
    shr_log("effects: targets for %ux%u (multisampling %d)", bd.Width, bd.Height, (int)bd.MultiSampleType);
    return true;
}

bool shadowSetup(IDirect3DDevice9* d) {
    if (g_sh.map[0]) return true;
    if (g_sh.failed) return false;
    g_sh.failed = true;
    if (!loadCompiler() || !probeCompat(d)) return false;
    bool ok = makeVS(d, SH_VS, &g_sh.vs) && makePS(d, SH_PS, &g_sh.ps) &&
              SUCCEEDED(d->CreateTexture(SHN, SHN, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &g_sh.map[0], nullptr)) &&
              SUCCEEDED(d->CreateTexture(SHN, SHN, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &g_sh.map[1], nullptr)) &&
              SUCCEEDED(d->CreateDepthStencilSurface(SHN, SHN, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &g_sh.depth, nullptr)) &&
              SUCCEEDED(d->CreateTexture(SHN, SHN, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &g_sh.bmap[0], nullptr)) &&
              SUCCEEDED(d->CreateTexture(SHN, SHN, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &g_sh.bmap[1], nullptr)) &&
              SUCCEEDED(d->CreateDepthStencilSurface(SHN, SHN, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &g_sh.bdepth, nullptr));
    if (ok && !(makeVS(d, SHA_VS, &g_sh.avs) && makePS(d, SHA_PS, &g_sh.aps))) { rel(g_sh.avs); rel(g_sh.aps); shr_log("shadows: no casting for leaves (shader failed)"); }
    if (!ok) { shr_log("character shadows: setup failed"); rel(g_sh.map[0]); rel(g_sh.map[1]); rel(g_sh.depth); rel(g_sh.bmap[0]); rel(g_sh.bmap[1]); rel(g_sh.bdepth); rel(g_sh.vs); rel(g_sh.ps); g_shadow = 0; return false; }
    for (int i = 0; i < 2; i++) { IDirect3DSurface9* bs = nullptr; g_sh.bmap[i]->GetSurfaceLevel(0, &bs); if (bs) { d->ColorFill(bs, nullptr, 0xFFFFFFFF); bs->Release(); } }
    g_sh.failed = false;
    shr_log("character shadows: ready (%dx%d map, %d units)", SHN, SHN, (int)SHS);
    return true;
}

bool worldShadowSetup(IDirect3DDevice9* d) {
    if (g_ws.tex) return true;
    if (g_ws.failed) return false;
    g_ws.failed = true;
    bool ok = SUCCEEDED(d->CreateTexture(WSN, WSN, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &g_ws.tex, nullptr)) &&
              SUCCEEDED(d->CreateDepthStencilSurface(WSN, WSN, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, FALSE, &g_ws.depth, nullptr));
    if (!ok) { rel(g_ws.tex); rel(g_ws.depth); shr_log("level shadows: no map (%dx%d)", WSN, WSN); return false; }
    g_ws.failed = false; g_ws.clear = true; g_ws.valid = false;
    shr_log("level shadows: ready (%dx%d map kept between frames, %d units)", WSN, WSN, (int)WSS);
    return true;
}
// where the kept map lies: around the camera's point of interest; a new place (and a clear) once that
// point has gone 2400 units from its centre, or the light has turned
void worldShadowPlan(const float* L, const float* c) {
    float dx = c[0] - g_ws.anchor[0], dy = c[1] - g_ws.anchor[1], dz = c[2] - g_ws.anchor[2];
    bool turned = fabsf(L[0] - g_ws.dir[0]) + fabsf(L[1] - g_ws.dir[1]) + fabsf(L[2] - g_ws.dir[2]) > 1e-3f;
    if (g_ws.valid && !turned && dx * dx + dy * dy + dz * dz < 2400.f * 2400.f) return;
    memcpy(g_ws.dir, L, 12);
    float up0[3] = { 0, 0, 1 }; if (fabsf(L[2]) > 0.95f) { up0[0] = 1; up0[2] = 0; }
    auto cross = [](const float* a, const float* b, float* o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; };
    auto norm = [](float* v) { float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 0) for (int j = 0; j < 3; j++) v[j] /= l; };
    float r[3], u[3]; cross(up0, L, r); norm(r); cross(L, r, u); norm(u);
    float tex = WSS / WSN;
    float cr = floorf((c[0] * r[0] + c[1] * r[1] + c[2] * r[2]) / tex) * tex, cu = floorf((c[0] * u[0] + c[1] * u[1] + c[2] * u[2]) / tex) * tex;
    float cf = c[0] * L[0] + c[1] * L[1] + c[2] * L[2];
    M4 m; memset(&m, 0, sizeof m);
    for (int j = 0; j < 3; j++) { m.m[j][0] = r[j] / WSS; m.m[j][1] = -u[j] / WSS; m.m[j][2] = L[j] / (2 * WSD); }
    m.m[3][0] = 0.5f - cr / WSS; m.m[3][1] = 0.5f + cu / WSS; m.m[3][2] = 0.5f - cf / (2 * WSD); m.m[3][3] = 1;
    g_ws.uv = m;
    M4 cl = m; for (int j = 0; j < 4; j++) { cl.m[j][0] = 2 * m.m[j][0]; cl.m[j][1] = -2 * m.m[j][1]; } cl.m[3][0] -= 1; cl.m[3][1] += 1;
    g_ws.clip = cl;
    memcpy(g_ws.anchor, c, 12);
    g_ws.valid = true; g_ws.clear = true; g_ws.casts = 0; g_wsDone.clear();
}

// shadow map projection for the next frame: a 2400-unit box in front of the camera, seen along the light
void shadowPlan(int i) {
    float L[3] = { g_sunNext[0], g_sunNext[1], g_sunNext[2] };
    // the level's sun (or moon) from the table; any other level (interiors, prisons, the factory)
    // straight down (Unreal: +Z up): the lights of a scene change from shot to shot in cutscenes and
    // gave long stripes from boards and tables for a few frames
    if (g_mapSet) memcpy(L, g_mapDir, 12);
    else { L[0] = 0; L[1] = 0; L[2] = -1; }
    if (L[2] > -0.45f) {                                                      // a low sun gives endless shadows
        float hl = sqrtf(L[0] * L[0] + L[1] * L[1]); float k = hl > 0 ? sqrtf(1 - 0.45f * 0.45f) / hl : 0;
        L[0] *= k; L[1] *= k; L[2] = -0.45f;
    }
    memcpy(g_sh.dir[i], L, 12);
    M4 iv = inverse(g_lastV);
    float cam[3] = { iv.m[3][0], iv.m[3][1], iv.m[3][2] }, fwd[3] = { iv.m[2][0], iv.m[2][1], iv.m[2][2] };
    float fl = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]); if (fl > 0) for (float& x : fwd) x /= fl;
    const float S = SHS, D = 4000.f, N = (float)SHN;
    float c[3]; for (int j = 0; j < 3; j++) c[j] = cam[j] + fwd[j] * (SHS * 0.4f);
    if (g_worldSh > 0) worldShadowPlan(L, cam);   // 0.9.66: around the camera itself - turning it no longer moves the kept map (it did, by the point ahead of the camera, and the map was cleared)
    float up0[3] = { 0, 0, 1 }; if (fabsf(L[2]) > 0.95f) { up0[0] = 1; up0[2] = 0; }
    auto cross = [](const float* a, const float* b, float* o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; };
    auto norm = [](float* v) { float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 0) for (int j = 0; j < 3; j++) v[j] /= l; };
    float r[3], u[3]; cross(up0, L, r); norm(r); cross(L, r, u); norm(u);
    // snap the box centre to whole shadow map texels: no shimmering edges while the camera moves
    float cr = c[0] * r[0] + c[1] * r[1] + c[2] * r[2], cu = c[0] * u[0] + c[1] * u[1] + c[2] * u[2], cf = c[0] * L[0] + c[1] * L[1] + c[2] * L[2];
    float tex = S / N; cr = floorf(cr / tex) * tex; cu = floorf(cu / tex) * tex;
    M4 m; memset(&m, 0, sizeof m);
    for (int j = 0; j < 3; j++) { m.m[j][0] = r[j] / S; m.m[j][1] = -u[j] / S; m.m[j][2] = L[j] / (2 * D); }
    m.m[3][0] = 0.5f - cr / S; m.m[3][1] = 0.5f + cu / S; m.m[3][2] = 0.5f - cf / (2 * D); m.m[3][3] = 1;
    g_sh.uv[i] = m;
    M4 cl = m;   // clip space: x = 2u - 1, y = 1 - 2v
    for (int j = 0; j < 4; j++) { cl.m[j][0] = 2 * m.m[j][0]; cl.m[j][1] = -2 * m.m[j][1]; }
    cl.m[3][0] -= 1; cl.m[3][1] += 1;
    g_sh.clip[i] = cl;
}

IDirect3DBaseTexture9* g_s14 = nullptr;     // what we last put at sampler 14 (reset when a state block may have changed it)
void fxBindShadow(IDirect3DDevice9* d) {
    IDirect3DBaseTexture9* t = g_sh.map[1 - g_sh.w];
    if (g_s14 == t) return;
    g_s14 = t;
    d->SetTexture(14, t);
    d->SetSamplerState(14, D3DSAMP_MINFILTER, D3DTEXF_POINT); d->SetSamplerState(14, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    d->SetSamplerState(14, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetSamplerState(14, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); d->SetSamplerState(14, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d->SetSamplerState(14, D3DSAMP_SRGBTEXTURE, FALSE);
    d->SetTexture(11, g_ws.valid ? g_ws.tex : nullptr);
    d->SetSamplerState(11, D3DSAMP_MINFILTER, D3DTEXF_POINT); d->SetSamplerState(11, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    d->SetSamplerState(11, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetSamplerState(11, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); d->SetSamplerState(11, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d->SetSamplerState(11, D3DSAMP_SRGBTEXTURE, FALSE);
    d->SetTexture(15, g_sh.bmap[1 - g_sh.w]);
    d->SetSamplerState(15, D3DSAMP_MINFILTER, D3DTEXF_POINT); d->SetSamplerState(15, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    d->SetSamplerState(15, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetSamplerState(15, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); d->SetSamplerState(15, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d->SetSamplerState(15, D3DSAMP_SRGBTEXTURE, FALSE);
}

// the last frame's colour and view depth for the water shader (s12, s13)
void fxBindWater(IDirect3DDevice9* d) {
    d->SetTexture(12, g_waterPrev ? g_fx.scene : nullptr); d->SetTexture(13, g_waterPrev ? g_fx.zTex : nullptr);
    for (DWORD s = 12; s <= 13; s++) {
        d->SetSamplerState(s, D3DSAMP_MINFILTER, s == 12 ? D3DTEXF_LINEAR : D3DTEXF_POINT); d->SetSamplerState(s, D3DSAMP_MAGFILTER, s == 12 ? D3DTEXF_LINEAR : D3DTEXF_POINT);
        d->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        d->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); d->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        d->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
    }
}

void quad(IDirect3DDevice9* d, UINT w, UINT h) {
    float ox = -1.f / w, oy = 1.f / h;      // D3D9 pixel centres
    float q[4][6] = { { -1 + ox, 1 + oy, 0.5f, 1, 0, 0 }, { 1 + ox, 1 + oy, 0.5f, 1, 1, 0 },
                      { -1 + ox, -1 + oy, 0.5f, 1, 0, 1 }, { 1 + ox, -1 + oy, 0.5f, 1, 1, 1 } };
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof q[0]);
}
void target(IDirect3DDevice9* d, IDirect3DTexture9* t, UINT& w, UINT& h) {
    IDirect3DSurface9* s = nullptr; t->GetSurfaceLevel(0, &s);
    D3DSURFACE_DESC sd{}; s->GetDesc(&sd); w = sd.Width; h = sd.Height;
    d->SetRenderTarget(0, s); s->Release();
}
void sampler(IDirect3DDevice9* d, IDirect3DBaseTexture9* t, bool linear) {
    d->SetTexture(0, t);
    DWORD f = linear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
    d->SetSamplerState(0, D3DSAMP_MINFILTER, f); d->SetSamplerState(0, D3DSAMP_MAGFILTER, f); d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
}

void sceneCamera() {
    int b = -1; for (int i = 0; i < g_nCams; i++) if (b < 0 || g_cams[i].n > g_cams[b].n) b = i;
    if (b < 0) return;
    memcpy(g_aoP, g_cams[b].p, 16); g_lastV = g_cams[b].v; g_haveV = true;
}
void runPost(IDirect3DDevice9* d) {
    g_postDone = true;
    sceneCamera();
    // a frame with only a handful of 3D draws (movies, loading and menu screens) is no scene to shade
    bool scene = g_sceneZ >= 24;
    bool ao = g_ssao > 0 && g_zWritten && scene && fxOn(), bloom = g_bloom > 0 && g_anyPP && scene && fxOn();
    bool water = g_waterDrawn && scene && fxOn(); g_waterDrawn = false;
    // sun rays: a level with a sun that is in front of the camera
    float sunUV[2] = { 0, 0 }, sunVis = 0;
    if (g_rays > 0 && scene && fxOn() && g_mapSet && g_mapSun && g_haveV && g_anyPP) {
        float ts[3] = { -g_mapDir[0], -g_mapDir[1], -g_mapDir[2] }, sv[3];
        for (int j = 0; j < 3; j++) sv[j] = ts[0] * g_lastV.m[0][j] + ts[1] * g_lastV.m[1][j] + ts[2] * g_lastV.m[2][j];
        if (sv[2] > 0.05f && g_aoP[0] != 0) {
            float nx = g_aoP[0] * sv[0] / sv[2] + g_aoP[2], ny = g_aoP[1] * sv[1] / sv[2] + g_aoP[3];
            sunUV[0] = nx * 0.5f + 0.5f; sunUV[1] = 0.5f - ny * 0.5f;
            float off = std::max(fabsf(nx), fabsf(ny));       // fades as the sun leaves the screen, and when it is low in front
            sunVis = std::min(1.f, (sv[2] - 0.05f) * 4) * std::max(0.f, std::min(1.f, (1.8f - off) / 0.8f));
        }
    }
    bool rays = sunVis > 0.01f;
    if (g_rt1) { d->SetRenderTarget(1, nullptr); g_rt1 = false; g_forceKey = true; }
    if ((!ao && !bloom && !rays && !water) || !fxShaders(d)) { g_waterPrev = false; return; }
    if (rays && g_fx.raysFailed) rays = false;
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    D3DSURFACE_DESC bd{}; bb->GetDesc(&bd);
    if (!fxTargets(d, bd)) { bb->Release(); return; }
    IDirect3DSurface9 *rt0 = nullptr, *ds = nullptr;
    d->GetRenderTarget(0, &rt0); d->GetDepthStencilSurface(&ds);
    g_fx.sb->Capture();
    d->SetDepthStencilSurface(nullptr);
    const DWORD rs[][2] = { { D3DRS_ZENABLE, FALSE }, { D3DRS_ZWRITEENABLE, FALSE }, { D3DRS_ALPHABLENDENABLE, FALSE },
        { D3DRS_ALPHATESTENABLE, FALSE }, { D3DRS_CULLMODE, D3DCULL_NONE }, { D3DRS_FOGENABLE, FALSE }, { D3DRS_STENCILENABLE, FALSE },
        { D3DRS_COLORWRITEENABLE, 0xF }, { D3DRS_SCISSORTESTENABLE, FALSE }, { D3DRS_SRGBWRITEENABLE, FALSE }, { D3DRS_FILLMODE, D3DFILL_SOLID },
        { D3DRS_CLIPPLANEENABLE, 0 }, { D3DRS_SEPARATEALPHABLENDENABLE, FALSE }, { D3DRS_BLENDOP, D3DBLENDOP_ADD } };
    for (auto& r : rs) d->SetRenderState((D3DRENDERSTATETYPE)r[0], r[1]);
    d->SetVertexDeclaration(g_fx.decl); d->SetVertexShader(g_fx.vs);
    UINT w = 0, h = 0;
    bool depthOk = false, sceneOk = false;
    if (ao) {
        IDirect3DSurface9* zs = nullptr; g_fx.zTex->GetSurfaceLevel(0, &zs);
        HRESULT hr = d->StretchRect(g_fx.zRT, nullptr, zs, nullptr, D3DTEXF_NONE);   // resolves the multisampling
        zs->Release();
        depthOk = SUCCEEDED(hr);
        if (SUCCEEDED(hr)) {
            target(d, g_fx.ao[0], w, h);
            sampler(d, g_fx.zTex, false); d->SetPixelShader(g_fx.ao_ps);
            float c[3][4] = { { 1.f / bd.Width, 1.f / bd.Height, 1.f / w, 1.f / h }, { g_aoP[0], g_aoP[1], g_aoP[2], g_aoP[3] },
                              { g_ssaoRadius, g_ssao, 2500.f, 1.f / 3000.f } };
            d->SetPixelShaderConstantF(200, &c[0][0], 3);
            quad(d, w, h);
            d->SetPixelShader(g_fx.blur);
            float dx[4] = { 1.f / w, 0, 0, 0 }, dy[4] = { 0, 1.f / h, 0, 0 };
            target(d, g_fx.ao[1], w, h); sampler(d, g_fx.ao[0], false); d->SetPixelShaderConstantF(203, dx, 1); quad(d, w, h);
            target(d, g_fx.ao[0], w, h); sampler(d, g_fx.ao[1], false); d->SetPixelShaderConstantF(203, dy, 1); quad(d, w, h);
            d->SetRenderTarget(0, bb);
            if (g_fxView != 1) {
                d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO); d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
            }
            sampler(d, g_fx.ao[0], true); d->SetPixelShader(g_fx.mul);
            quad(d, bd.Width, bd.Height);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        } else { static bool once = false; if (!once) { once = true; shr_log("effects: depth resolve failed (0x%08lx)", hr); } }
    }
    if (bloom) {
        IDirect3DSurface9* ss = nullptr; g_fx.scene->GetSurfaceLevel(0, &ss);
        HRESULT hr = d->StretchRect(bb, nullptr, ss, nullptr, D3DTEXF_NONE);
        ss->Release();
        sceneOk = SUCCEEDED(hr);
        if (SUCCEEDED(hr)) {
            target(d, g_fx.bl[0], w, h);
            sampler(d, g_fx.scene, true); d->SetPixelShader(g_fx.bright);
            float c[4] = { 1.f / bd.Width, 1.f / bd.Height, 0, 0 }, b[4] = { g_bloomThr, g_bloom * g_bloomScale, 0, 0 };
            d->SetPixelShaderConstantF(200, c, 1); d->SetPixelShaderConstantF(205, b, 1);
            quad(d, w, h);
            d->SetPixelShader(g_fx.gauss);
            for (int pass = 0; pass < 2; pass++) {
                float sp = pass ? 2.5f : 1.f;
                float dx[4] = { sp / w, 0, 0, 0 }, dy[4] = { 0, sp / h, 0, 0 };
                target(d, g_fx.bl[1], w, h); sampler(d, g_fx.bl[0], true); d->SetPixelShaderConstantF(203, dx, 1); quad(d, w, h);
                target(d, g_fx.bl[0], w, h); sampler(d, g_fx.bl[1], true); d->SetPixelShaderConstantF(203, dy, 1); quad(d, w, h);
            }
            d->SetRenderTarget(0, bb);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE); d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);   // screen
            sampler(d, g_fx.bl[0], true); d->SetPixelShader(g_fx.add);
            quad(d, bd.Width, bd.Height);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        }
    }
    if ((rays || water) && !sceneOk) {   // the picture before the rays / for the water of the next frame
        IDirect3DSurface9* ss = nullptr; g_fx.scene->GetSurfaceLevel(0, &ss);
        sceneOk = SUCCEEDED(d->StretchRect(bb, nullptr, ss, nullptr, D3DTEXF_NONE));
        ss->Release();
    }
    if (rays && sceneOk) {
        float aspect = (float)bd.Width / bd.Height;
        float cr[4] = { sunUV[0], sunUV[1], aspect, 0.72f };
        target(d, g_fx.bl[0], w, h);
        sampler(d, g_fx.scene, true); d->SetPixelShader(g_fx.rmask);
        d->SetPixelShaderConstantF(200, cr, 1);
        quad(d, w, h);
        d->SetPixelShader(g_fx.rblur);
        float b1[4] = { 0.9f, 0, 0, 0 }, b2[4] = { 0.45f, 0, 0, 0 }, b3[4] = { 0.2f, 0, 0, 0 };
        target(d, g_fx.bl[1], w, h); sampler(d, g_fx.bl[0], true); d->SetPixelShaderConstantF(201, b1, 1); quad(d, w, h);
        target(d, g_fx.bl[0], w, h); sampler(d, g_fx.bl[1], true); d->SetPixelShaderConstantF(201, b2, 1); quad(d, w, h);
        target(d, g_fx.bl[1], w, h); sampler(d, g_fx.bl[0], true); d->SetPixelShaderConstantF(201, b3, 1); quad(d, w, h);
        d->SetRenderTarget(0, bb);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE); d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);   // screen
        float bb4[4] = { 0, g_rays * sunVis * 1.6f, 0, 0 };
        d->SetPixelShaderConstantF(205, bb4, 1);
        sampler(d, g_fx.bl[1], true); d->SetPixelShader(g_fx.add);
        quad(d, bd.Width, bd.Height);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    }
    g_waterPrev = water && depthOk && sceneOk;
    {   // what the water had to reflect with (ShrekRender.log, when it changes)
        static int last = -1; int now = water ? (depthOk ? 1 : 0) | (sceneOk ? 2 : 0) | 4 : 0;
        if (now != last) { last = now; if (water) shr_log("water: last frame's pictures for the reflections - depth %s, colour %s", depthOk ? "yes" : "no (needs ambient occlusion)", sceneOk ? "yes" : "no"); }
    }
    if (rt0) { d->SetRenderTarget(0, rt0); rt0->Release(); }
    d->SetDepthStencilSurface(ds); if (ds) ds->Release();
    g_fx.sb->Apply();
    bb->Release();
}
} // namespace

// the game changed its render target: the depth target goes along only with the back buffer
void pp_rtChanged(void* dv) {
    if (!dv) return;
    auto d = (IDirect3DDevice9*)dv;
    g_rtKnown = true;
    IDirect3DSurface9 *rt = nullptr, *bb = nullptr;
    d->GetRenderTarget(0, &rt); d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    g_rt0BB = rt && rt == bb;
    bool want = g_rt0BB && !g_postDone && g_ssao > 0 && fxOn() && g_enabled && !g_debug && !g_broken;
    if (want) {
        D3DSURFACE_DESC bd{}; bb->GetDesc(&bd);
        want = fxShaders(d) && fxTargets(d, bd);
    }
    if (want && !g_rt1) {
        HRESULT h = d->SetRenderTarget(1, g_fx.zRT);
        if (SUCCEEDED(h)) { g_rt1 = true; d->SetRenderState(D3DRS_COLORWRITEENABLE1, 0); g_cwe1 = 0; g_forceKey = true; }
        else { shr_log("ambient occlusion: second render target refused (0x%08lx), off", h); g_ssao = 0; }
    }
    else if (!want && g_rt1) { d->SetRenderTarget(1, nullptr); g_rt1 = false; g_forceKey = true; }
    if (rt) rt->Release();
    if (bb) bb->Release();
}
// a state block was applied: it may have brought back an old colour mask or texture at stage 14
void pp_stateApplied(void* dv) {
    if (!dv) return;
    auto d = (IDirect3DDevice9*)dv;
    g_s14 = nullptr;
    if (g_rt1) { d->SetRenderState(D3DRS_COLORWRITEENABLE1, 0); g_cwe1 = 0; }
    else g_cwe1 = ~0u;
}
// the game cleared its depth buffer (e.g. after the sky): depth from before is no longer the scene
// (the sky zone is drawn first, with a few draws: its depth goes; after the scene itself - many
// draws - a new depth buffer is for overlays, and the scene's depth is kept)
void pp_zCleared(void* dv) {
    if (!dv || !g_rt1 || !g_fx.zRT || !g_zWritten || g_postDone) return;
    if (g_zDraws < 40) { ((IDirect3DDevice9*)dv)->ColorFill(g_fx.zRT, nullptr, 0); g_zWritten = false; g_zDraws = 0; }
    else g_zFrozen = true;
}
// before a 2D draw (HUD, menu): the 3D scene is finished
bool pp_postPending() { return !g_postDone && g_anyPP && (g_ssao > 0 || g_bloom > 0 || g_rays > 0 || g_waterDrawn) && g_rt0BB; }
void pp_post(void* dv) { if (dv && !g_broken && pp_postPending()) runPost((IDirect3DDevice9*)dv); }

// a draw of the game, just before it happens: characters are drawn into the shadow map too
void pp_castShadow(void* dv, int kind, int prim, UINT a, UINT b, UINT c, UINT e, INT base, const void* idx, int ifmt, const void* data, UINT stride) {
    if (!dv || g_shadow <= 0 || !fxOn() || !g_bound || !g_lastPersp || !g_rt0BB || g_postDone || g_debug) return;
    auto d = (IDirect3DDevice9*)dv;
    DWORD lit = 0; d->GetRenderState(D3DRS_LIGHTING, &lit);
    // casters: lit actors (characters, props, coins) - drawn with their own world transform; the
    // level itself (terrain, BSP, merged static meshes) comes in world coordinates and does not cast
    // onto the level (its shadows are baked in the lightmaps) - with "the level's shadows on
    // characters" it goes into the props map, which only the characters' shaders read as shadow
    D3DMATRIX wm; d->GetTransform(D3DTS_WORLD, &wm);
    bool ident = wm.m[3][0] == 0 && wm.m[3][1] == 0 && wm.m[3][2] == 0 && wm.m[0][0] == 1 && wm.m[1][1] == 1 && wm.m[2][2] == 1;
    bool world = ident || !lit;
    bool cutout = false;   // drawn with the alpha-tested shaders: leaves, grass, fences
    if (world || !g_lastDyn) {
        DWORD at = 0, ab = 0, zw = 0; d->GetRenderState(D3DRS_ALPHATESTENABLE, &at); d->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab); d->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
        if (world && (g_worldSh <= 0 || g_lastDyn)) return;
        if (!g_sh.bdepth) return;
        if (at || ab) {
            if (g_worldSh <= 0 || !(at || zw)) return;    // see-through effects (glass, smoke) cast nothing
            cutout = true;
        } else if (world && !zw) return;
    }
    // only characters cast: their meshes are skinned into dynamic vertex buffers every frame. Props
    // (static meshes: boards, tables, mugs) already have their shadows baked into the level's
    // lighting; a second, sharp shadow from them only drew stripes across each other
    bool blocker = !g_lastDyn || world;
    int wi = g_sh.w;
    // into this frame's map (characters; props near them) and, for the level and props, into the kept map
    bool intoB = true, intoW = blocker && g_worldSh > 0 && g_ws.valid;
    auto inBox = [&](const M4& u, float m) {
        float x = wm.m[3][0] * u.m[0][0] + wm.m[3][1] * u.m[1][0] + wm.m[3][2] * u.m[2][0] + u.m[3][0];
        float y = wm.m[3][0] * u.m[0][1] + wm.m[3][1] * u.m[1][1] + wm.m[3][2] * u.m[2][1] + u.m[3][1];
        return !(x < -m || x > 1 + m || y < -m || y > 1 + m);
    };
    if (!ident) {   // level geometry in world coordinates: no place to test
        intoB = inBox(g_sh.uv[wi], blocker ? 0.6f : 0.15f);
        if (intoW) intoW = inBox(g_ws.uv, 0.15f);
    }
    if (intoW && kind <= 1 && !g_ws.clear) {   // the same draw (buffers, range, place) is in the kept map already
        IDirect3DVertexBuffer9* vb = nullptr; IDirect3DIndexBuffer9* ib = nullptr; UINT off = 0, st = 0;
        d->GetStreamSource(0, &vb, &off, &st); if (kind == 1) d->GetIndices(&ib);
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
        mix((uintptr_t)vb); mix((uintptr_t)ib); mix(off); mix((uint64_t)kind); mix((uint64_t)prim); mix(a); mix(b); mix(c); mix(e); mix((uint64_t)(int64_t)base);
        uint32_t t3[3]; memcpy(t3, &wm.m[3][0], 12); mix(t3[0]); mix(t3[1]); mix(t3[2]);
        // only buffers that do not change: the level's BSP is put into dynamic buffers anew every frame
        bool fixedBufs = vb != nullptr;
        if (vb) { D3DVERTEXBUFFER_DESC vd{}; vb->GetDesc(&vd); if (vd.Usage & D3DUSAGE_DYNAMIC) fixedBufs = false; vb->Release(); }
        if (ib) { D3DINDEXBUFFER_DESC id{}; ib->GetDesc(&id); if (id.Usage & D3DUSAGE_DYNAMIC) fixedBufs = false; ib->Release(); }
        if (fixedBufs && !g_wsDone.insert(h).second) intoW = false;
        if (g_wsDone.size() > 200000) g_wsDone.clear();
    }
    if (!intoB && !intoW) return;
    if (!shadowSetup(d)) return;
    IDirect3DSurface9 *rt0 = nullptr, *ds = nullptr, *ms = nullptr;
    d->GetRenderTarget(0, &rt0); d->GetDepthStencilSurface(&ds);
    D3DVIEWPORT9 vp{}; d->GetViewport(&vp);
    const D3DRENDERSTATETYPE keep[] = { D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE,
        D3DRS_COLORWRITEENABLE, D3DRS_FOGENABLE, D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_DEPTHBIAS,
        D3DRS_SLOPESCALEDEPTHBIAS, D3DRS_SRGBWRITEENABLE };
    const int NK = sizeof keep / sizeof keep[0];
    DWORD old[NK]; for (int i = 0; i < NK; i++) d->GetRenderState(keep[i], &old[i]);
    (blocker ? g_sh.bmap[wi] : g_sh.map[wi])->GetSurfaceLevel(0, &ms);
    if (g_rt1) d->SetRenderTarget(1, nullptr);
    d->SetRenderTarget(0, ms); d->SetDepthStencilSurface(blocker ? g_sh.bdepth : g_sh.depth);
    const DWORD set[NK] = { TRUE, TRUE, D3DCMP_LESSEQUAL, FALSE, FALSE, D3DCULL_NONE, 0xF, FALSE, FALSE, FALSE, 0, 0, 0, FALSE };
    for (int i = 0; i < NK; i++) d->SetRenderState(keep[i], set[i]);
    bool& clr = blocker ? g_sh.bcleared[wi] : g_sh.cleared[wi];
    if (intoB && !clr) { d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 1.f, 0); clr = true; }
    M4 m = mul(fromD3D(wm), g_sh.clip[wi]);
    float cs[4][4]; for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) cs[j][i] = m.m[i][j];
    d->SetVertexShaderConstantF(240, &cs[0][0], 4);
    if (cutout && !(g_sh.avs && g_sh.aps)) { d->SetRenderTarget(0, rt0); d->SetDepthStencilSurface(ds); for (int i = 0; i < NK; i++) d->SetRenderState(keep[i], old[i]); if (g_rt1) d->SetRenderTarget(1, g_fx.zRT); d->SetViewport(&vp); if (rt0) rt0->Release(); if (ds) ds->Release(); if (ms) ms->Release(); return; }
    d->SetVertexShader(cutout ? g_sh.avs : g_sh.vs); d->SetPixelShader(cutout ? g_sh.aps : g_sh.ps);
    auto drawIt = [&]() {
        switch (kind) {
        case 0: d->DrawPrimitive((D3DPRIMITIVETYPE)prim, a, b); break;
        case 1: d->DrawIndexedPrimitive((D3DPRIMITIVETYPE)prim, base, a, b, c, e); break;
        case 2: d->DrawPrimitiveUP((D3DPRIMITIVETYPE)prim, a, data, stride); break;
        case 3: d->DrawIndexedPrimitiveUP((D3DPRIMITIVETYPE)prim, a, b, c, idx, (D3DFORMAT)ifmt, data, stride); break;
        }
    };
    if (intoB) drawIt();
    if (intoW && worldShadowSetup(d)) {   // the kept map: not read while it is drawn into
        d->SetTexture(11, nullptr); g_s14 = nullptr;
        IDirect3DSurface9* wsf = nullptr; g_ws.tex->GetSurfaceLevel(0, &wsf);
        d->SetRenderTarget(0, wsf); d->SetDepthStencilSurface(g_ws.depth);
        if (g_ws.clear) { d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 1.f, 0); g_ws.clear = false; }
        M4 mw = mul(fromD3D(wm), g_ws.clip);
        float cw[4][4]; for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) cw[j][i] = mw.m[i][j];
        d->SetVertexShaderConstantF(240, &cw[0][0], 4);
        drawIt(); g_ws.casts++;
        if (wsf) wsf->Release();
    }
    if (!blocker) { g_sh.written[wi] = true; g_sh.casts++; }
    d->SetVertexShader(g_boundVS); d->SetPixelShader(g_boundPS);
    for (int i = 0; i < NK; i++) d->SetRenderState(keep[i], old[i]);
    d->SetRenderTarget(0, rt0); d->SetDepthStencilSurface(ds);
    if (g_rt1) d->SetRenderTarget(1, g_fx.zRT);
    d->SetViewport(&vp);
    if (rt0) rt0->Release();
    if (ds) ds->Release();
    if (ms) ms->Release();
}

void pp_info(bool used, char* b, int n) {
    snprintf(b, n, "   pp=%d rt0bb=%d rt1=%d cwe1=%lx zWritten=%d zDraws=%u frozen=%d postDone=%d dyn=%d multi=%d persp=%d", used ? 1 : 0, g_rt0BB, g_rt1,
             (unsigned long)g_cwe1, g_zWritten, g_zDraws, g_zFrozen, g_postDone, g_lastDyn, g_keyMulti, g_lastPersp);
    if (!used) { size_t l = strlen(b); snprintf(b + l, n - l, " why=%s", g_lastReject.c_str()); }
    if (used && g_infoKey) { size_t l = strlen(b); snprintf(b + l, n - l, " key: lit=%d mrt=%d shadow=%d bump=%d n=%d", g_infoKey->lit, g_infoKey->mrt, g_infoKey->shadow, g_infoKey->bump, g_infoKey->n); }
}
// the level changed: its sun / moon direction (Unreal rotation), character brightness, shadow strength
void pp_setMapLight(bool known, bool sun, float pitchDeg, float yawDeg, float chars, float shadows) {
    g_mapSet = known; g_mapSun = sun; g_mapChars = known ? chars : 1.f; g_mapShadow = known ? shadows : 1.f;
    if (known && sun) {
        float p = pitchDeg * 3.14159265f / 180, y = yawDeg * 3.14159265f / 180;
        g_mapDir[0] = cosf(p) * cosf(y); g_mapDir[1] = cosf(p) * sinf(y); g_mapDir[2] = sinf(p);
    } else { g_mapDir[0] = 0; g_mapDir[1] = 0; g_mapDir[2] = -1; }
    g_forceKey = true;
}
void pp_fillBlack(void* dev9) {
    auto d = (IDirect3DDevice9*)dev9; IDirect3DSurface9* bb = nullptr;
    if (d && SUCCEEDED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) { d->ColorFill(bb, nullptr, D3DCOLOR_XRGB(0, 0, 0)); bb->Release(); }
}
unsigned pp_compileBusy() { if (!g_csInit) return 0; EnterCriticalSection(&g_cs); unsigned n = (unsigned)g_jobs.size(); LeaveCriticalSection(&g_cs); return n; }
void pp_worldShadowReset() { g_ws.valid = false; g_wsDone.clear(); }
void pp_setExtra(float worldSh, float water, float rays) {
    // the level's shadows: 1 high (4096 map, soft edges by distance), 0.5 low (2048 map, a fixed small edge)
    int n = worldSh > 0 && worldSh < 0.75f ? 2048 : 4096;
    if (n != WSN) { WSN = n; if (g_ws.tex) { g_ws.tex->Release(); g_ws.tex = nullptr; } if (g_ws.depth) { g_ws.depth->Release(); g_ws.depth = nullptr; } g_ws.valid = false; }
    g_wsQuality = worldSh >= 0.75f ? 1.f : 0.f;
    g_worldSh = worldSh > 0 ? 1.f : 0.f; g_water = water; g_rays = rays; g_forceKey = true;
}
void pp_setBloomLevel(float scale, float threshold) { g_bloomScale = scale; g_bloomThr = threshold; }
void pp_setEffects(float ssao, float radius, float shadow, float bloom) {
    g_ssao = ssao; g_ssaoRadius = radius > 1 ? radius : 55.f; g_shadow = shadow; g_bloom = bloom; g_forceKey = true; g_rtKnown = false;
}
int pp_fxView() { return g_fxView; }
void pp_setFxView(int v) { g_fxView = v; g_forceKey = true; g_rtKnown = false; }
unsigned pp_shadowCasts() { return g_sh.lastCasts; }

// end of a frame (before Present): effects not done yet, shadow maps swap, depth target cleared
void pp_frameEnd(void* dv) {
    if (!dv) return;
    auto d = (IDirect3DDevice9*)dv;
    if (pp_postPending()) runPost(d);
    if (g_rt1) { d->SetRenderTarget(1, nullptr); g_rt1 = false; g_forceKey = true; }
    if (g_fx.zRT && g_zWritten) d->ColorFill(g_fx.zRT, nullptr, 0);
    // the map written this frame becomes the one read next frame
    int wi = g_sh.w;
    if (g_sh.bmap[wi] && !g_sh.bcleared[wi]) {   // no prop drawn into it: nothing blocks
        IDirect3DSurface9* bs = nullptr; g_sh.bmap[wi]->GetSurfaceLevel(0, &bs);
        if (bs) { d->ColorFill(bs, nullptr, 0xFFFFFFFF); bs->Release(); }
    }
    bool wasValid = g_sh.valid[1 - wi];
    g_sh.valid[wi] = g_sh.written[wi];
    g_sh.lastCasts = g_sh.casts; g_sh.casts = 0;
    g_sh.w = 1 - wi;
    g_sh.cleared[g_sh.w] = false; g_sh.written[g_sh.w] = false; g_sh.bcleared[g_sh.w] = false;
    sceneCamera();
    if (g_haveV) shadowPlan(g_sh.w);
    g_nCams = 0;
    g_sunNextLum = 0;
    if (wasValid != g_sh.valid[1 - g_sh.w]) g_forceKey = true;
    g_zDraws = 0; g_sceneZ = 0; g_zFrozen = false;
    if (!g_postDone) { g_waterPrev = false; g_waterDrawn = false; }   // no scene this frame: nothing for the water to reflect
    g_zWritten = false; g_postDone = false; g_anyPP = false; g_rtKnown = false; g_haveV = false; g_s14 = nullptr;
}

namespace {
void auxRelease();
void fxRelease() {
    auxRelease();
    rel(g_fx.zRT); rel(g_fx.zTex); rel(g_fx.ao[0]); rel(g_fx.ao[1]); rel(g_fx.scene); rel(g_fx.bl[0]); rel(g_fx.bl[1]);
    rel(g_fx.vs); rel(g_fx.ao_ps); rel(g_fx.blur); rel(g_fx.mul); rel(g_fx.bright); rel(g_fx.gauss); rel(g_fx.add); rel(g_fx.rmask); rel(g_fx.rblur); g_fx.raysFailed = false;
    g_waterPrev = false;
    rel(g_fx.decl); rel(g_fx.sb); g_fx.shadersFailed = false; g_fx.sizeFailed = false; g_fx.w = g_fx.h = 0;
    rel(g_sh.map[0]); rel(g_sh.map[1]); rel(g_sh.depth); rel(g_sh.bmap[0]); rel(g_sh.bmap[1]); rel(g_sh.bdepth); rel(g_sh.vs); rel(g_sh.ps); rel(g_sh.avs); rel(g_sh.aps); g_sh.failed = false;
    rel(g_ws.tex); rel(g_ws.depth); g_ws.failed = false; g_ws.valid = false; g_ws.clear = true;
    g_sh.bcleared[0] = g_sh.bcleared[1] = false;
    g_sh.valid[0] = g_sh.valid[1] = false; g_sh.cleared[0] = g_sh.cleared[1] = false; g_sh.written[0] = g_sh.written[1] = false;
    g_rt1 = false; g_rtKnown = false; g_s14 = nullptr; g_cwe1 = ~0u; g_forceKey = true;
}
} // namespace

// ---------------------------------------------------------------- draws with the game's own pixel shader
// The terrain is drawn with a pixel shader of the game (its layers). It keeps it; right after it the
// same geometry is drawn twice more: its view depth into the occlusion target (colour masked off) and,
// with a multiply blend, the character shadows that fall on it.
namespace {
const char* AUX_VS =
    "float4 cM[8] : register(c240);\nfloat4 cN[3] : register(c248);\n"
    "struct O { float4 p : POSITION; float4 v : TEXCOORD0; float3 n : TEXCOORD1; };\n"
    "O main(float4 p : POSITION0, float3 n : NORMAL0) { O o;\n"
    " o.p = float4(dot(p, cM[0]), dot(p, cM[1]), dot(p, cM[2]), dot(p, cM[3]));\n"
    " o.v = float4(dot(p, cM[4]), dot(p, cM[5]), dot(p, cM[6]), 1);\n"
    " o.n = float3(dot(n, cN[0].xyz), dot(n, cN[1].xyz), dot(n, cN[2].xyz)); return o; }\n";
const char* AUXZ_PS =
    "struct POut { float4 c : COLOR0; float4 z : COLOR1; };\n"
    "POut main(float4 v : TEXCOORD0) { POut o; o.c = 0; o.z = float4(v.z, 0, 0, 1); return o; }\n";
IDirect3DVertexShader9* g_auxVS = nullptr; IDirect3DPixelShader9 *g_auxZ = nullptr, *g_auxSh = nullptr; bool g_auxFailed = false;
void waterRelease();
void auxRelease() { rel(g_auxVS); rel(g_auxZ); rel(g_auxSh); g_auxFailed = false; waterRelease(); }
bool auxSetup(IDirect3DDevice9* d) {
    if (g_auxVS) return true;
    if (g_auxFailed) return false;
    g_auxFailed = true;
    if (!loadCompiler() || !probeCompat(d)) return false;
    std::string sh = shadowCode() + ppg::worldShadowCode() +
        "float4 main(float4 v : TEXCOORD0, float3 n : TEXCOORD1) : COLOR0 {\n"
        " float3 P = v.xyz; float3 N = dot(n, n) > 1e-6 ? normalize(n) : normalize(cross(ddx(P), ddy(P)));\n"
        " if (dot(N, P) > 0) N = -N;\n"
        " float s = shadowAt(P, N);\n"
        " float k = cSh2.y * saturate(dot(N, -cShL.xyz) * 3);\n"
        " float m = lerp(1, s, k);\n"
        " m = min(m, lerp(1, worldShadowAt(P, N), cWsh.x * saturate(dot(N, -cShL.xyz) * 3)));\n"
        " return float4(m, m, m, 1);\n}\n";
    bool ok = makeVS(d, AUX_VS, &g_auxVS) && makePS(d, AUXZ_PS, &g_auxZ) && makePS(d, sh.c_str(), &g_auxSh);
    if (!ok) { shr_log("effects: extra pass for the game's pixel shaders not available"); rel(g_auxVS); rel(g_auxZ); rel(g_auxSh); return false; }
    g_auxFailed = false;
    return true;
}
} // namespace

void pp_auxPass(void* dv, int kind, int prim, UINT a, UINT b, UINT c, UINT e, INT base) {
    if (!dv || g_broken || !g_enabled || g_debug || !fxOn() || g_postDone || !g_rt0BB || kind > 1) return;
    bool wantZ = g_rt1 && g_ssao > 0 && !g_zFrozen, wantSh = shadowReadValid();   // (the level's own shadows come with wantSh)
    if (!wantZ && !wantSh) return;
    auto d = (IDirect3DDevice9*)dv;
    IDirect3DPixelShader9* gps = nullptr; d->GetPixelShader(&gps);
    if (!gps) return;
    gps->Release();
    if (gps == g_boundPS || gps == g_auxZ || gps == g_auxSh) return;
    IDirect3DVertexShader9* gvs = nullptr; d->GetVertexShader(&gvs);
    if (gvs) { gvs->Release(); return; }                       // positions from the game's vertex shader: not ours to repeat
    DWORD blend = 0, zw = 0, zen = 0; d->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend); d->GetRenderState(D3DRS_ZWRITEENABLE, &zw); d->GetRenderState(D3DRS_ZENABLE, &zen);
    if (blend || !zw || !zen) return;
    D3DMATRIX w, vw, pr; d->GetTransform(D3DTS_WORLD, &w); d->GetTransform(D3DTS_VIEW, &vw); d->GetTransform(D3DTS_PROJECTION, &pr);
    M4 W = fromD3D(w), V = fromD3D(vw), P = fromD3D(pr);
    if (!(fabsf(P.m[2][3] - 1) < 1e-3 && fabsf(P.m[3][3]) < 1e-3)) return;
    if (!auxSetup(d)) return;
    M4 WV = mul(W, V), WVP = mul(WV, P);
    float vc[11][4] = {};
    for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) { vc[j][i] = WVP.m[i][j]; vc[4 + j][i] = WV.m[i][j]; }
    for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) vc[8 + j][i] = WV.m[i][j];   // rigid transforms: the rotation itself
    const D3DRENDERSTATETYPE keep[] = { D3DRS_COLORWRITEENABLE, D3DRS_COLORWRITEENABLE1, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE,
        D3DRS_FOGENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_DEPTHBIAS, D3DRS_SLOPESCALEDEPTHBIAS, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_BLENDOP };
    const int NK = sizeof keep / sizeof keep[0];
    DWORD old[NK]; for (int i = 0; i < NK; i++) d->GetRenderState(keep[i], &old[i]);
    d->SetVertexShaderConstantF(240, &vc[0][0], 11);
    d->SetVertexShader(g_auxVS);
    // our vertex shader is not bit-identical to DXVK's fixed-function one: the copy is pulled towards the
    // camera (reversed depth: larger) so it passes the depth test everywhere, not in bands
    DWORD zf = 0; d->GetRenderState(D3DRS_ZFUNC, &zf);
    float sign = (zf == D3DCMP_GREATER || zf == D3DCMP_GREATEREQUAL) ? 1.f : -1.f;
    float bias = 2e-5f * sign, slope = 1.5f * sign; DWORD bb, sb; memcpy(&bb, &bias, 4); memcpy(&sb, &slope, 4);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE); d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE); d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetRenderState(D3DRS_DEPTHBIAS, bb); d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, sb); d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE); d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    auto draw = [&]() {
        if (kind == 0) d->DrawPrimitive((D3DPRIMITIVETYPE)prim, a, b);
        else d->DrawIndexedPrimitive((D3DPRIMITIVETYPE)prim, base, a, b, c, e);
    };
    if (wantZ) {
        d->SetPixelShader(g_auxZ);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); d->SetRenderState(D3DRS_COLORWRITEENABLE, 0); d->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF);
        draw(); g_zWritten = true; g_zDraws++; g_sceneZ++;
    }
    if (wantSh) {
        float shc[6][4]; shadowConsts(V, -1, shc);
        d->SetPixelShaderConstantF(166, &shc[0][0], 6);
        float ws[4][4] = { { g_worldSh * 0.5f * g_mapShadow, 1.f / WSN, 30.f / (2 * WSD), g_wsQuality } };
        { M4 S = mul(inverse(V), g_ws.uv); for (int j = 0; j < 3; j++) for (int i = 0; i < 4; i++) ws[1 + j][i] = S.m[i][j]; }
        d->SetPixelShaderConstantF(217, &ws[0][0], 4);
        fxBindShadow(d);
        d->SetPixelShader(g_auxSh);
        d->SetRenderState(D3DRS_COLORWRITEENABLE, 7); d->SetRenderState(D3DRS_COLORWRITEENABLE1, 0);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE); d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO); d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
        draw();
    }
    for (int i = 0; i < NK; i++) d->SetRenderState(keep[i], old[i]);
    g_cwe1 = old[1];
    d->SetVertexShader(nullptr); d->SetPixelShader(gps);
}

// ---------------------------------------------------------------- water drawn with the game's own pixel shader
// The swamp's water is the game's pixel shader with a cube map picked by the camera's reflection
// vector: a fixed picture of a sky that slides over the water as the camera moves. For level water
// (world coordinates, blended, fixed-function vertices, a cube map on a stage) the cube map is swapped
// for a plain one of its own average colour during the draw, and the same surface is drawn once more
// on top with reflections of the last frame (the flat, upward parts; the plain colour stays where the
// ray finds nothing).
namespace {
struct WaterSwap { int stage = -1; IDirect3DBaseTexture9* orig = nullptr; } g_wsw;
std::unordered_map<IDirect3DBaseTexture9*, IDirect3DCubeTexture9*> g_flatCube;
IDirect3DPixelShader9* g_auxW = nullptr; bool g_auxWFailed = false;
void waterRelease() { rel(g_auxW); g_auxWFailed = false; for (auto& kv : g_flatCube) if (kv.second && !g_relSkip) kv.second->Release(); g_flatCube.clear(); }
IDirect3DCubeTexture9* flatCube(IDirect3DDevice9* d, IDirect3DCubeTexture9* src) {
    auto it = g_flatCube.find(src); if (it != g_flatCube.end()) return it->second;
    float acc[3] = { 0, 0, 0 }; int n = 0;
    DWORD lv = src->GetLevelCount(); D3DSURFACE_DESC sd{};
    if (lv && SUCCEEDED(src->GetLevelDesc(lv - 1, &sd)) && (sd.Format == D3DFMT_A8R8G8B8 || sd.Format == D3DFMT_X8R8G8B8))
        for (int f = 0; f < 6; f++) {
            D3DLOCKED_RECT lr;
            if (FAILED(src->LockRect((D3DCUBEMAP_FACES)f, lv - 1, &lr, nullptr, D3DLOCK_READONLY))) continue;
            for (UINT y = 0; y < sd.Height; y++) for (UINT x = 0; x < sd.Width; x++) {
                const uint8_t* p = (const uint8_t*)lr.pBits + y * lr.Pitch + x * 4; acc[0] += p[2]; acc[1] += p[1]; acc[2] += p[0]; n++;
            }
            src->UnlockRect((D3DCUBEMAP_FACES)f, lv - 1);
        }
    DWORD col = n ? (0xFF000000u | ((DWORD)(acc[0] / n) << 16) | ((DWORD)(acc[1] / n) << 8) | (DWORD)(acc[2] / n)) : 0xFF6E8C96u;
    IDirect3DCubeTexture9* t = nullptr;
    if (SUCCEEDED(d->CreateCubeTexture(1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr))) {
        for (int f = 0; f < 6; f++) { D3DLOCKED_RECT lr; if (SUCCEEDED(t->LockRect((D3DCUBEMAP_FACES)f, 0, &lr, nullptr, 0))) { *(DWORD*)lr.pBits = col; t->UnlockRect((D3DCUBEMAP_FACES)f, 0); } }
    }
    g_flatCube[src] = t;
    shr_log("water: cube map %ux%u (format %d) -> plain colour %06lx%s", sd.Width, sd.Height, (int)sd.Format, col & 0xFFFFFF, n ? "" : " (default)");
    return t;
}
bool waterAuxSetup(IDirect3DDevice9* d) {
    if (g_auxW) return true;
    if (g_auxWFailed || !auxSetup(d)) return false;
    g_auxWFailed = true;
    std::string ps = std::string("float4 cUp : register(c165);\n") + ppg::waterCode() +
        "float4 main(float4 v : TEXCOORD0, float3 n : TEXCOORD1) : COLOR0 {\n"
        " float3 P = v.xyz; float3 N = normalize(cross(ddx(P), ddy(P)));\n"
        " float up = abs(dot(N, cUp.xyz)); if (up < 0.8 || cWat1.w <= 0) return float4(0, 0, 0, 0);\n"
        " float a; float3 c = waterReflect(P, normalize(cUp.xyz), float3(0, 0, 0), a);\n"
        " return float4(c, a);\n}\n";
    if (!makePS(d, ps.c_str(), &g_auxW)) { shr_log("water: reflection shader failed"); return false; }
    g_auxWFailed = false;
    return true;
}
} // namespace

// before a draw that is not ours: level water with a cube map -> plain cube map (true: swapped)
bool pp_waterPre(void* dv) {
    if (!dv || g_broken || !g_enabled || g_water <= 0 || g_debug || !fxOn() || g_postDone || !g_rt0BB) return false;
    auto d = (IDirect3DDevice9*)dv;
    // the game's own pixel shader, or fixed function our shaders do not take (bump environment mapping)
    IDirect3DPixelShader9* gps = nullptr; d->GetPixelShader(&gps);
    if (gps) gps->Release();
    if (gps && (gps == g_boundPS || gps == g_auxZ || gps == g_auxSh || gps == g_auxW)) return false;
    IDirect3DVertexShader9* gvs = nullptr; d->GetVertexShader(&gvs);
    if (gvs) { gvs->Release(); return false; }
    DWORD blend = 0; d->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
    D3DMATRIX w; d->GetTransform(D3DTS_WORLD, &w);
    bool ident = w.m[3][0] == 0 && w.m[3][1] == 0 && w.m[3][2] == 0 && w.m[0][0] == 1 && w.m[1][1] == 1 && w.m[2][2] == 1;
    // only a cube map read with the reflection vector is a fake reflection; one read with the normal
    // (the swamp's light projector over the ground, Shrek and the bushes) is lighting and stays
    int cube = -1;
    for (int st = 0; st < 8 && cube < 0; st++) {
        DWORD op = 0; d->GetTextureStageState(st, D3DTSS_COLOROP, &op); if (op == D3DTOP_DISABLE) break;
        IDirect3DBaseTexture9* t = nullptr; d->GetTexture(st, &t);
        DWORD tci = 0; d->GetTextureStageState(st, D3DTSS_TEXCOORDINDEX, &tci);
        if (t) { if (t->GetType() == D3DRTYPE_CUBETEXTURE && (tci & 0xFFFF0000) == D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR) cube = st; t->Release(); }
    }
    if (cube < 0) return false;
    static int logged = 0;
    if (logged < 6) { logged++; DWORD tci = 0, op = 0; d->GetTextureStageState(cube, D3DTSS_TEXCOORDINDEX, &tci); d->GetTextureStageState(0, D3DTSS_COLOROP, &op);
        shr_log("water: %s with a cube map at stage %d (texcoord %lx, stage 0 op %lu, blend %lu, level geometry %d)", gps ? "game pixel shader" : "fixed function", cube, tci, op, blend, ident ? 1 : 0); }
    if (!blend || !ident) return false;
    IDirect3DBaseTexture9* t = nullptr; d->GetTexture(cube, &t);
    IDirect3DCubeTexture9* flat = flatCube(d, (IDirect3DCubeTexture9*)t);
    if (!flat) { t->Release(); return false; }
    g_wsw.stage = cube; g_wsw.orig = t;     // keeps the reference until it is put back
    d->SetTexture(cube, flat);
    g_waterDrawn = true;
    return true;
}
// after it: the cube map back, and the reflections on top
void pp_waterPost(void* dv, int kind, int prim, UINT a, UINT b, UINT c, UINT e, INT base) {
    if (!dv || g_wsw.stage < 0) return;
    auto d = (IDirect3DDevice9*)dv;
    d->SetTexture(g_wsw.stage, g_wsw.orig); g_wsw.orig->Release(); g_wsw.orig = nullptr; g_wsw.stage = -1;
    if (!g_waterPrev || kind > 1) return;
    D3DMATRIX w, vw, pr; d->GetTransform(D3DTS_WORLD, &w); d->GetTransform(D3DTS_VIEW, &vw); d->GetTransform(D3DTS_PROJECTION, &pr);
    M4 W = fromD3D(w), V = fromD3D(vw), P = fromD3D(pr);
    if (!(fabsf(P.m[2][3] - 1) < 1e-3 && fabsf(P.m[3][3]) < 1e-3)) return;
    if (!waterAuxSetup(d)) return;
    IDirect3DPixelShader9* gps = nullptr; d->GetPixelShader(&gps); if (gps) gps->Release();
    M4 WV = mul(W, V), WVP = mul(WV, P);
    float vc[11][4] = {};
    for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) { vc[j][i] = WVP.m[i][j]; vc[4 + j][i] = WV.m[i][j]; }
    for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) vc[8 + j][i] = WV.m[i][j];
    float pc[3][4] = { { V.m[2][0], V.m[2][1], V.m[2][2], 1 } };   // c165: world up in view space
    float wc[2][4] = { { P.m[0][0], P.m[1][1], P.m[2][0], P.m[2][1] }, { g_water, 0, 0, 1 } };
    const D3DRENDERSTATETYPE keep[] = { D3DRS_COLORWRITEENABLE, D3DRS_COLORWRITEENABLE1, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE,
        D3DRS_FOGENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_DEPTHBIAS, D3DRS_SLOPESCALEDEPTHBIAS, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_BLENDOP };
    const int NK = sizeof keep / sizeof keep[0];
    DWORD old[NK]; for (int i = 0; i < NK; i++) d->GetRenderState(keep[i], &old[i]);
    d->SetVertexShaderConstantF(240, &vc[0][0], 11);
    d->SetPixelShaderConstantF(165, &pc[0][0], 1);
    d->SetPixelShaderConstantF(215, &wc[0][0], 2);
    d->SetVertexShader(g_auxVS); d->SetPixelShader(g_auxW);
    fxBindWater(d);
    DWORD zf = 0; d->GetRenderState(D3DRS_ZFUNC, &zf);
    float sign = (zf == D3DCMP_GREATER || zf == D3DCMP_GREATEREQUAL) ? 1.f : -1.f;
    float bias = 2e-5f * sign, slope = 1.5f * sign; DWORD bb, sb; memcpy(&bb, &bias, 4); memcpy(&sb, &slope, 4);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE); d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE); d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetRenderState(D3DRS_DEPTHBIAS, bb); d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, sb); d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE); d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 7); d->SetRenderState(D3DRS_COLORWRITEENABLE1, 0);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE); d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    if (kind == 0) d->DrawPrimitive((D3DPRIMITIVETYPE)prim, a, b);
    else d->DrawIndexedPrimitive((D3DPRIMITIVETYPE)prim, base, a, b, c, e);
    for (int i = 0; i < NK; i++) d->SetRenderState(keep[i], old[i]);
    g_cwe1 = old[1];
    d->SetVertexShader(nullptr); d->SetPixelShader(gps);
    d->SetTexture(12, nullptr); d->SetTexture(13, nullptr);
}
