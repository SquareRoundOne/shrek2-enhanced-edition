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
bool g_enabled = true, g_compilerTried = false, g_debug = false, g_broken = false;
float g_bumpDepth = 1.f, g_bumpAO = 1.5f, g_bumpChar = 0.35f, g_bumpBlur = 2.f; int g_bumpLevel = 2; bool g_world = true;   // level 0 off, 1 half, 2 normal, 3 double

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

const Prog* getProg(IDirect3DDevice9* d, const Key& k) {
    std::string key((const char*)&k, sizeof k);
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second.vs ? &it->second : nullptr;
    if (!probeCompat(d)) { g_broken = true; return nullptr; }
    Prog p;
    DWORD t0 = GetTickCount();
    loadDisk();
    std::string vsrc = genVS(k), psrc = genPS(k), src = vsrc + psrc;
    std::string vcode, pcode; bool cached = false;
    auto dk = g_disk.find(src);
    if (dk != g_disk.end()) { vcode = dk->second.first; pcode = dk->second.second; cached = true; }
    else {
        ID3DBlob* vb = compile(vsrc, "vs_3_0");
        ID3DBlob* pb = vb ? compile(psrc, "ps_3_0") : nullptr;
        vcode = blobStr(vb); pcode = blobStr(pb);
        if (vb) vb->Release();
        if (pb) pb->Release();
    }
    if (!vcode.empty() && !pcode.empty()) {
        CompatLift lift(d);
        if (FAILED(d->CreateVertexShader((const DWORD*)vcode.data(), &p.vs)) ||
            FAILED(d->CreatePixelShader((const DWORD*)pcode.data(), &p.ps))) {
            shr_log("per-pixel lighting: shader creation refused");
            if (p.vs) p.vs->Release();
            if (p.ps) p.ps->Release();
            p = Prog{};
        } else if (!cached) { g_disk[src] = { vcode, pcode }; saveDisk(src, vcode, pcode); }
    }
    g_cache[key] = p;
    shr_log("per-pixel lighting: shader %u (%d stage%s%s%s) %s in %lu ms", (unsigned)g_cache.size(), k.n, k.n == 1 ? "" : "s",
            k.c0 ? ", vertex colour" : "", k.spec ? ", specular" : "", p.vs ? (cached ? "ready" : "built") : "FAILED", GetTickCount() - t0);
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

DWORD g_fogSaved = 0; bool g_active = false;
unsigned g_draws = 0;

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

void* pp_takeDevice() { void* d = g_lastDev; g_lastDev = nullptr; return d; }
void pp_setEnabled(bool on) { g_enabled = on; }
bool pp_enabled() { return g_enabled; }
void pp_setDebug(bool on) { g_debug = on; }
bool pp_debug() { return g_debug; }
void pp_setBump(float depth, float ao, int level) { g_bumpDepth = depth; g_bumpAO = ao; g_bumpLevel = level & 3; }
void pp_setBumpExtra(float chars, float blur, bool world) { g_bumpChar = chars; g_bumpBlur = blur; g_world = world; }
int pp_bumpLevel() { return g_bumpLevel; }
unsigned pp_drawCount() { unsigned n = g_draws; g_draws = 0; return n; }

bool pp_begin(void* dv) {
    if ((!g_enabled && !g_debug) || g_broken || !dv) return false;
    auto d = (IDirect3DDevice9*)dv;
    DWORD v = 0, lit = 0;
    if (FAILED(d->GetRenderState(D3DRS_LIGHTING, &lit))) return false;
    if (!lit && (!g_world || g_debug || g_bumpLevel == 0)) return false;   // unlit geometry only for the relief
    d->GetRenderState(D3DRS_VERTEXBLEND, &v); if (v) return false;
    d->GetRenderState(D3DRS_POINTSPRITEENABLE, &v); if (v) return false;
    d->GetRenderState(D3DRS_CLIPPLANEENABLE, &v); if (v) return false;
    d->GetRenderState(D3DRS_SHADEMODE, &v); if (v == D3DSHADE_FLAT) return false;
    {
        IDirect3DVertexShader9* vs = nullptr; d->GetVertexShader(&vs);
        if (vs) { vs->Release(); return false; }
        IDirect3DPixelShader9* ps = nullptr; d->GetPixelShader(&ps);
        if (ps) { ps->Release(); return false; }
    }
    Key k; memset(&k, 0, sizeof k);
    {
        IDirect3DVertexDeclaration9* decl = nullptr;
        if (FAILED(d->GetVertexDeclaration(&decl)) || !decl) return false;
        D3DVERTEXELEMENT9 el[MAXD3DDECLLENGTH + 1]; UINT n = MAXD3DDECLLENGTH + 1;
        HRESULT h = decl->GetDeclaration(el, &n); decl->Release();
        if (FAILED(h)) return false;
        bool pos = false, nrm = false;
        for (UINT i = 0; i < n && el[i].Stream != 0xFF; i++) {
            const auto& e = el[i];
            switch (e.Usage) {
            case D3DDECLUSAGE_POSITION: if (e.UsageIndex == 0) pos = true; break;
            case D3DDECLUSAGE_POSITIONT: return false;
            case D3DDECLUSAGE_NORMAL: if (e.UsageIndex == 0) nrm = true; break;
            case D3DDECLUSAGE_COLOR: if (e.UsageIndex == 0) k.c0 = 1; else if (e.UsageIndex == 1) k.c1 = 1; break;
            case D3DDECLUSAGE_TEXCOORD: if (e.UsageIndex < 8) k.tc[e.UsageIndex] = (uint8_t)declCount(e.Type); break;
            default: break;
            }
        }
        if (!pos || (lit && !nrm)) return false;
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
        if (!opSupported((int)op) || (aop != D3DTOP_DISABLE && !opSupported((int)aop))) return false;
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
    // the texture that gives the relief: the first 2D texture a stage combines, with plain texture coordinates
    DWORD blend = 0, zw = 0;
    d->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend); d->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
    if (g_bumpLevel > 0 && (k.lit || (!blend && zw)))      // not on effects and overlays
        for (int st = 0; st < k.n && !k.bump; st++) {
            const Stage& g = k.st[st];
            bool usesTex = (g.ca[1] & 0x0F) == D3DTA_TEXTURE || (g.ca[2] & 0x0F) == D3DTA_TEXTURE;
            if (g.tex == 1 && !g.proj && g.tcf == 0 && usesTex) k.bump = (uint8_t)(st + 1);
        }
    if (!k.lit && !k.bump) return false;                            // nothing to add to unlit geometry
    if (g_debug) {                                                  // lighting-only view
        k.debug = g_enabled ? 1 : 2;
        if (k.debug == 2) { k.n = 0; memset(k.st, 0, sizeof k.st); k.bump = 0; }
    }
    if (k.n + 2 + (k.debug == 2) + k.c0 + k.c1 > 10) return false;   // ps_3_0 has 10 input registers

    DWORD colorVertex = 0, srcs[4] = {}, spec = 0, local = 0, norm = 0, range = 0, fog = 0, fogV = 0, fogT = 0;
    d->GetRenderState(D3DRS_COLORVERTEX, &colorVertex);
    d->GetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, &srcs[0]); d->GetRenderState(D3DRS_AMBIENTMATERIALSOURCE, &srcs[1]);
    d->GetRenderState(D3DRS_SPECULARMATERIALSOURCE, &srcs[2]); d->GetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, &srcs[3]);
    d->GetRenderState(D3DRS_SPECULARENABLE, &spec); d->GetRenderState(D3DRS_LOCALVIEWER, &local);
    d->GetRenderState(D3DRS_NORMALIZENORMALS, &norm); d->GetRenderState(D3DRS_RANGEFOGENABLE, &range);
    d->GetRenderState(D3DRS_FOGENABLE, &fog); d->GetRenderState(D3DRS_FOGVERTEXMODE, &fogV); d->GetRenderState(D3DRS_FOGTABLEMODE, &fogT);
    DWORD mask = colorVertex ? ((k.c0 ? D3DMCS_COLOR1 : 0) | (k.c1 ? D3DMCS_COLOR2 : 0)) : 0;
    for (int i = 0; i < 4; i++) k.src[i] = (uint8_t)(srcs[i] & mask);
    k.spec = spec != 0; k.local = local != 0; k.norm = norm != 0; k.range = range != 0;
    k.fog = fog != 0; k.fogV = (uint8_t)(fog ? fogV & 3 : 0); k.fogT = (uint8_t)(fog ? fogT & 3 : 0);

    if (!loadCompiler()) { g_broken = true; return false; }
    const Prog* p = getProg(d, k);
    if (!p) return false;

    // ---- constants
    D3DMATRIX w, vw, pr;
    d->GetTransform(D3DTS_WORLD, &w); d->GetTransform(D3DTS_VIEW, &vw); d->GetTransform(D3DTS_PROJECTION, &pr);
    M4 W = fromD3D(w), V = fromD3D(vw), P = fromD3D(pr);
    M4 WV = mul(W, V), WVP = mul(WV, P);
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

    float pc[66][4] = {};
    pc[65][0] = V.m[2][0]; pc[65][1] = V.m[2][1]; pc[65][2] = V.m[2][2]; pc[65][3] = 1;   // world up (Unreal: +Z) in view space
    int li = 0;
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
        li++;
    }
    D3DMATERIAL9 m{}; d->GetMaterial(&m);
    memcpy(pc[56], &m.Diffuse, 16); memcpy(pc[57], &m.Ambient, 16); memcpy(pc[58], &m.Specular, 16); memcpy(pc[59], &m.Emissive, 16);
    color4(amb, pc[60]); pc[61][0] = m.Power;
    {
        // characters are skinned on the CPU into dynamic vertex buffers (or drawn from memory): they
        // get a weaker relief, fur and painted faces turn into noise otherwise
        bool dynamic = true;
        IDirect3DVertexBuffer9* vb = nullptr; UINT off = 0, stride = 0;
        if (SUCCEEDED(d->GetStreamSource(0, &vb, &off, &stride)) && vb) {
            D3DVERTEXBUFFER_DESC bd{}; vb->GetDesc(&bd); dynamic = (bd.Usage & D3DUSAGE_DYNAMIC) != 0; vb->Release();
        }
        static const float lv[4] = { 0, 0.5f, 1, 2 };
        float f = lv[g_bumpLevel] * (dynamic ? g_bumpChar : 1.f);
        pc[61][1] = g_bumpDepth * f; pc[61][2] = g_bumpAO * f; pc[61][3] = g_bumpBlur;
    } color4(tf, pc[62]); color4(fc, pc[63]); memcpy(pc[64], fogC, 16);
    d->SetPixelShaderConstantF(PS_BASE, &pc[0][0], 66);
    if (k.debug == 2) d->SetVertexShaderConstantF(VS_BASE + 12, &pc[0][0], 62);   // lights for the per-vertex view

    d->SetVertexShader(p->vs); d->SetPixelShader(p->ps);
    g_fogSaved = fog;
    if (fog) d->SetRenderState(D3DRS_FOGENABLE, FALSE);      // fog is done in the pixel shader
    g_active = true; g_draws++;
    return true;
}

void pp_end(void* dv) {
    if (!g_active) return;
    auto d = (IDirect3DDevice9*)dv;
    d->SetVertexShader(nullptr); d->SetPixelShader(nullptr);
    if (g_fogSaved) d->SetRenderState(D3DRS_FOGENABLE, g_fogSaved);
    g_active = false;
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
    " return float4(saturate(r), 1);\n}\n";

struct Post {
    IDirect3DTexture9* tex = nullptr; UINT w = 0, h = 0; D3DFORMAT fmt = D3DFMT_UNKNOWN;
    IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexDeclaration9* decl = nullptr; IDirect3DStateBlock9* sb = nullptr;
    bool failed = false;
} g_post;
float g_sharpen = 0.5f;

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
float pp_sharpen() { return g_sharpen; }

void pp_reset() {                                     // before IDirect3DDevice8::Reset
    if (g_post.tex) { g_post.tex->Release(); g_post.tex = nullptr; }
    if (g_post.sb) { g_post.sb->Release(); g_post.sb = nullptr; }
    if (g_post.vs) { g_post.vs->Release(); g_post.vs = nullptr; }
    if (g_post.ps) { g_post.ps->Release(); g_post.ps = nullptr; }
    if (g_post.decl) { g_post.decl->Release(); g_post.decl = nullptr; }
    g_post.failed = false;
}

void pp_present(void* dv) {
    if (g_sharpen <= 0 || !dv || g_broken) return;
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
    float c[4] = { 1.f / bd.Width, 1.f / bd.Height, g_sharpen > 1 ? 1.f : g_sharpen, 0 };
    d->SetPixelShaderConstantF(200, c, 1);
    float ox = -1.f / bd.Width, oy = 1.f / bd.Height;      // D3D9 pixel centres
    float q[4][6] = { { -1 + ox, 1 + oy, 0.5f, 1, 0, 0 }, { 1 + ox, 1 + oy, 0.5f, 1, 1, 0 },
                      { -1 + ox, -1 + oy, 0.5f, 1, 0, 1 }, { 1 + ox, -1 + oy, 0.5f, 1, 1, 1 } };
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof q[0]);
    if (rt) { d->SetRenderTarget(0, rt); rt->Release(); }
    g_post.sb->Apply();
    bb->Release();
}
