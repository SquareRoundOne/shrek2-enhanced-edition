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
#include <d3d8.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <vector>
#include <string>
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
bool pp_begin(void* dev9);
void pp_end(void* dev9);
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
static float g_bumpChar = 0.35f, g_bumpBlur = 2.f; static bool g_bumpWorld = true;
void pp_present(void* dev9);
static float g_bumpDepth = 1.f, g_bumpAO = 1.5f, g_sharpenIni = 0.f; static int g_bumpIni = 2;
unsigned pp_drawCount();
static bool g_perPixel = true;
static int g_ppKey = VK_F11;

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
        if (!c) { r->Release(); parent->Release(); delete this; }
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
        if (dev9) pp_reset();
        if (active && pp && pp->EnableAutoDepthStencil) {
            D3DFORMAT f = pp->AutoDepthStencilFormat; pp->AutoDepthStencilFormat = FMT_DEPTH;
            h = r->Reset(pp); pp->AutoDepthStencilFormat = f;
            if (FAILED(h)) logf("Reset with the float depth buffer failed: 0x%08lx", h);
        } else h = r->Reset(pp);
        if (SUCCEEDED(h)) initState();
        return h;
    }
    HRESULT STDMETHODCALLTYPE Clear(DWORD n, const D3DRECT* rc, DWORD flags, D3DCOLOR col, float z, DWORD st) override {
        return r->Clear(n, rc, flags, col, active ? 1.f - z : z, st);
    }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE s, const D3DMATRIX* m) override {
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
        if (active) switch (s) {
        case D3DRS_ZFUNC: zfunc = v; return r->SetRenderState(s, flipCmp(v));
        case D3DRS_ZBIAS: if (v != zbias) { zbias = v; applyProj(); applyBiasConst(); } return D3D_OK;
        case D3DRS_ZENABLE: zenable = v; return r->SetRenderState(s, v == D3DZB_USEW ? D3DZB_TRUE : v);
        default: break;
        }
        return r->SetRenderState(s, v);
    }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE s, DWORD* v) override {
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
        fprintf(cf, "#%d %s prims=%u verts=%u stride=%u vs=", capDraw, kind, prims, verts, stride);
        if (si >= 0) fprintf(cf, "shader%d", si); else fprintf(cf, "fvf%08lx", curVS);
        fprintf(cf, " light=%lu amb=%08lx colvtx=%lu diffsrc=%lu ambsrc=%lu emissrc=%lu spec=%lu norm=%lu blend=%lu(%lu,%lu) atest=%lu zw=%lu fog=%lu cull=%lu tfac=%08lx\n",
                rs[0], rs[1], rs[2], rs[3], rs[4], rs[5], rs[6], rs[7], rs[8], rs[9], rs[10], rs[11], rs[12], rs[13], rs[14], rs[15]);
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
        for (DWORD st = 0; st < 4; st++) {
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
        bool pp = pp_begin(dev9); HRESULT h = r->DrawPrimitive(t, sv, pc); if (pp) pp_end(dev9);
        return h;
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t, UINT mi, UINT nv, UINT si, UINT pc) override {
        if (cf) capState("DIP", pc, nv, curStride);
        bool pp = pp_begin(dev9); HRESULT h = r->DrawIndexedPrimitive(t, mi, nv, si, pc); if (pp) pp_end(dev9);
        return h;
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t, UINT pc, const void* d, UINT stride) override {
        if (cf) capState("DPUP", pc, 0, stride);
        bool pp = pp_begin(dev9); HRESULT h = r->DrawPrimitiveUP(t, pc, d, stride); if (pp) pp_end(dev9);
        return h;
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t, UINT mi, UINT nv, UINT pc, const void* id, D3DFORMAT f, const void* d, UINT stride) override {
        if (cf) capState("DIPUP", pc, nv, stride);
        bool pp = pp_begin(dev9); HRESULT h = r->DrawIndexedPrimitiveUP(t, mi, nv, pc, id, f, d, stride); if (pp) pp_end(dev9);
        return h;
    }
    HRESULT STDMETHODCALLTYPE Present(const RECT* a, const RECT* b, HWND w, const RGNDATA* d) override {
        unsigned ppDraws = pp_drawCount();
        if (cf) { saveFrame(); fclose(cf); cf = nullptr; logf("capture: %d draws written to ShrekRender_frame.txt, %u with per-pixel lighting", capDraw, ppDraws); }
        if (dev9 && g_ppKey && (GetAsyncKeyState(g_ppKey) & 1)) {
            bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            if (ctrl && shift) {
                static float last = 0.35f;
                if (pp_sharpen() > 0) { last = pp_sharpen(); pp_setSharpen(0); } else pp_setSharpen(last > 0 ? last : 0.35f);
                logf("sharpening %.2f (Ctrl+Shift+key)", pp_sharpen());
            } else if (shift) {
                int lv = (pp_bumpLevel() + 1) & 3; pp_setBump(g_bumpDepth, g_bumpAO, lv);
                static const char* names[] = { "off", "half", "normal", "double" };
                logf("relief %s (Shift+key)", names[lv]);
            } else if (ctrl) { pp_setDebug(!pp_debug()); if (pp_debug()) pp_setEnabled(true); logf("lighting-only view %s (Ctrl+key)", pp_debug() ? "on" : "off"); }
            else { pp_setEnabled(!pp_enabled()); logf("per-pixel lighting %s (key)", pp_enabled() ? "on" : "off"); }
        }
        if (g_capture && ((GetAsyncKeyState(g_capKey) & 1) | (GetAsyncKeyState(VK_SCROLL) & 1))) {
            wchar_t p[MAX_PATH]; swprintf(p, MAX_PATH, L"%lsShrekRender_frame.txt", g_dir);
            cf = _wfopen(p, L"w"); capDraw = 0;
            for (int i = 0; i < 96; i++) vscDirty[i] = true;
        }
        if (dev9) pp_present(dev9);
        return r->Present(a, b, w, d);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShader(DWORD h) override {
        curVS = h;
        if (active && !warnedRHW && !(h & 1) && (h & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) {
            warnedRHW = true; logf("note: pre-transformed (XYZRHW) geometry drawn, its depth is not reversed");
        }
        return r->SetVertexShader(h);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstant(DWORD reg, const void* data, DWORD count) override {
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
    pp_setBump(g_bumpDepth, g_bumpAO, g_bumpIni); pp_setBumpExtra(g_bumpChar, g_bumpBlur, g_bumpWorld); pp_setSharpen(g_sharpenIni);
    if (g_perPixel) logf(d->dev9 ? "per-pixel lighting: on (toggle with the key set in ShrekRender.ini, default F11)" : "per-pixel lighting: d3d9 device not found, lighting stays per vertex");
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
    std::vector<std::string> marks{ fontgen::VERSION_TAG };
    for (int hh : heights) marks.push_back("SHRH" + std::to_string(hh));
    if (fileHas(path, marks)) { logf("fonts: SHHDFonts.utx already built for %d px", scr); return; }
    DWORD t0 = GetTickCount();
    auto pkg = fontgen::makePackage(shr_ttf, heights);
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
        g_ppKey = GetPrivateProfileIntW(L"Lighting", L"ToggleKey", VK_F11, p);
        g_bumpIni = GetPrivateProfileIntW(L"Lighting", L"Relief", 2, p);
        g_bumpWorld = GetPrivateProfileIntW(L"Lighting", L"ReliefWorld", 1, p) != 0;
        { wchar_t v[32];
          GetPrivateProfileStringW(L"Lighting", L"ReliefDepth", L"1", v, 32, p); g_bumpDepth = (float)_wtof(v);
          GetPrivateProfileStringW(L"Lighting", L"ReliefShade", L"1.5", v, 32, p); g_bumpAO = (float)_wtof(v);
          GetPrivateProfileStringW(L"Lighting", L"ReliefCharacters", L"0.35", v, 32, p); g_bumpChar = (float)_wtof(v);
          GetPrivateProfileStringW(L"Lighting", L"ReliefSmooth", L"2", v, 32, p); g_bumpBlur = (float)_wtof(v);
          GetPrivateProfileStringW(L"Image", L"Sharpen", L"0", v, 32, p); g_sharpenIni = (float)_wtof(v); }
        if (GetPrivateProfileIntW(L"Depth", L"Log", 1, p)) { swprintf(p, MAX_PATH, L"%lsShrekRender.log", g_dir); g_log = _wfopen(p, L"w"); }
        logf("shrz 1.5: reversed float depth %s", g_wantReverse ? "requested" : "disabled in ShrekRender.ini");
        buildFonts();
        g_real = LoadLibraryW(L"d3d8.dll");
        if (g_real) { wchar_t m[MAX_PATH]; GetModuleFileNameW(g_real, m, MAX_PATH); logf("d3d8: %ls", m); }
        if (g_real && g_perPixel) pp_installHook(g_real);
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
