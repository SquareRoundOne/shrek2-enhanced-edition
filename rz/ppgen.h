// ppgen.h - HLSL generation for the per-pixel lighting shaders (no Windows dependencies, so it can
// be tested on its own). Mirrors DXVK's fixed-function vertex and fragment shaders.
#pragma once
#include <string>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>

namespace ppg {
enum { TA_DIFFUSE = 0, TA_CURRENT, TA_TEXTURE, TA_TFACTOR, TA_SPECULAR, TA_TEMP, TA_COMPLEMENT = 0x10, TA_ALPHAREPLICATE = 0x20 };
enum { TOP_DISABLE = 1, TOP_SELECTARG1, TOP_SELECTARG2, TOP_MODULATE, TOP_MODULATE2X, TOP_MODULATE4X, TOP_ADD, TOP_ADDSIGNED,
       TOP_ADDSIGNED2X, TOP_SUBTRACT, TOP_ADDSMOOTH, TOP_BLENDDIFFUSEALPHA, TOP_BLENDTEXTUREALPHA, TOP_BLENDFACTORALPHA,
       TOP_BLENDTEXTUREALPHAPM, TOP_BLENDCURRENTALPHA, TOP_PREMODULATE, TOP_MODULATEALPHA_ADDCOLOR, TOP_MODULATECOLOR_ADDALPHA,
       TOP_MODULATEINVALPHA_ADDCOLOR, TOP_MODULATEINVCOLOR_ADDALPHA, TOP_BUMPENVMAP, TOP_BUMPENVMAPLUMINANCE, TOP_DOTPRODUCT3,
       TOP_MULTIPLYADD, TOP_LERP };
enum { FOG_NONE = 0, FOG_EXP, FOG_EXP2, FOG_LINEAR };

// ---------------------------------------------------------------- shader key
#pragma pack(push, 1)
struct Stage { uint8_t cop, aop, ca[3], aa[3], temp, tci, tcf, ttf, proj, tex, lm; };   // lm: a lightmap, filtered bicubically
struct Key {
    uint8_t n;               // active texture stages
    uint8_t c0, c1;          // vertex has diffuse / specular colour
    uint8_t tc[8];           // components of each texcoord set in the vertex (0 = absent)
    uint8_t src[4];          // material source: diffuse, ambient, specular, emissive (0 material, 1 colour0, 2 colour1)
    uint8_t spec, local, norm, range, fog, fogV, fogT;
    uint8_t debug;           // 1: output the lighting alone (no textures, no fog)
    uint8_t bump;            // texture stage + 1 whose texture gives the relief (0 = none)
    uint8_t lit, hasN;       // fixed-function lighting on / vertex has a normal
    uint8_t mrt;             // view depth to the second render target (ambient occlusion): 1 opaque, 2 blended but solid where alpha >= 0.5
    uint8_t shadow;          // receives the character shadows (shadow map at s14)
    uint8_t detail;          // texture stage + 1 that gets fine detail where it is stretched (level geometry)
    uint8_t aer;             // aerial perspective: far things paler and bluer (c213, c214)
    Stage st[8];
    // added in 0.9.52, after the stages: older key files read with these at 0 (the same shaders)
    uint8_t wsh;             // 1 a character: the level's own shadows from the sun fall on it (the props map at s15); 2 the level; 3 the level by night
    uint8_t water;           // water: 1 a flat surface with a moving texture, 2 one with a cube map by the reflection vector; reflections of the last frame (s12 colour, s13 depth)
};
#pragma pack(pop)

const int VS_BASE = 160, PS_BASE = 100;
// texture coordinates from the camera-space position (terrain layers): computed in the pixel
// shader from the interpolated position, which saves an interpolator per stage (ps_3_0 has 10)
// tcf 5 (ours, water): the camera-space reflection vector per pixel instead of per vertex - a cube map
// on a big water polygon of four corners no longer swims with the camera
inline bool psGen(const Stage& g) { return g.tcf == 2 || g.tcf == 5; }
inline int interpolators(const Key& k) {
    int n = 0; for (int i = 0; i < k.n; i++) if (!psGen(k.st[i])) n++;
    return n + 2 + (k.debug == 2) + k.c0 + k.c1;
}

// ---------------------------------------------------------------- HLSL generation
inline std::string fmt(const char* f, ...) {
    char b[1024]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a); return b;
}

// lighting declarations: lights, material, ambient at constant register 'base' (65 registers)
inline std::string lightDecl(int b) {
    return fmt("float4 lPos[8] : register(c%d);\nfloat4 lDir[8] : register(c%d);\nfloat4 lDif[8] : register(c%d);\n"
               "float4 lSpc[8] : register(c%d);\nfloat4 lAmb[8] : register(c%d);\nfloat4 lAtt[8] : register(c%d);\n"
               "float4 lSpot[8] : register(c%d);\n", b, b + 8, b + 16, b + 24, b + 32, b + 40, b + 48) +
           fmt("float4 mDif : register(c%d);\nfloat4 mAmb : register(c%d);\nfloat4 mSpc : register(c%d);\nfloat4 mEmi : register(c%d);\n"
               "float4 gAmb : register(c%d);\nfloat4 cMisc : register(c%d);\n", b + 56, b + 57, b + 58, b + 59, b + 60, b + 61);
}
// D3D lighting at view-space position P, unit normal N -> dif (colour 0) and spc (colour 1)
inline std::string lightBody(const Key& k) {
    std::string s;
    s += k.shadow ? " int SUNL = (int)cSh.w;\n" : " int SUNL = -1; float shF = 1;\n";
    s += " float4 aD = 0, aA = 0, aS = 0;\n"
         " [unroll] for (int l = 0; l < 8; l++) {\n"
         "  [branch] if (lPos[l].w > 0.5) {\n"
         "   float3 delta = lPos[l].xyz - P;\n   float dist = length(delta);\n"
         "   float3 hit = -lDir[l].xyz;\n   float att = 1;\n"
         "   if (lPos[l].w < 2.5) {\n"
         "    att = dist * lAtt[l].z + lAtt[l].y;\n    att = dist * att + lAtt[l].x;\n"
         "    att = min(1 / att, 3.402823466e38);\n    att *= saturate((lAtt[l].w - dist) / (0.3 * lAtt[l].w));\n"
         "    hit = normalize(delta);\n   }\n"
         "   if (lPos[l].w > 1.5 && lPos[l].w < 2.5) {\n"
         "    float rho = dot(-hit, lDir[l].xyz);\n"
         "    float sa = pow(max((rho - lSpot[l].z) / (lSpot[l].y - lSpot[l].z), 0), lSpot[l].x);\n"
         "    sa = rho > lSpot[l].z ? sa : 0;\n    sa = rho <= lSpot[l].y ? sa : 1;\n"
         "    att *= saturate(sa);\n   }\n"
         "   if (l == SUNL) att *= shF;\n"
         "   float hd = saturate(dot(N, hit));\n"
         "   aA += lAmb[l] * att;\n   aD += lDif[l] * (hd * att);\n";
    s += k.local ? "   float3 mid = hit - normalize(P);\n" : "   float3 mid = hit - float3(0, 0, 1);\n";
    s += "   float md = saturate(dot(N, normalize(mid)));\n"
         "   if (md > 0 && hd > 0) aS += lSpc[l] * (pow(md, cMisc.x) * att);\n"
         "  }\n }\n";
    auto src = [&](int which, const char* mat) -> std::string {
        if (k.src[which] == 1 && k.c0) return "i.c0";
        if (k.src[which] == 2 && k.c1) return "i.c1";
        return mat;
    };
    s += " float4 mD = " + src(0, "mDif") + ", mA = " + src(1, "mAmb") + ", mS = " + src(2, "mSpc") + ", mE = " + src(3, "mEmi") + ";\n";
    s += " float4 dif = mA * gAmb * ao + mE;\n dif = mA * aA * ao + dif;\n dif = mD * aD + dif;\n dif.a = mD.a;\n dif = saturate(dif);\n";
    if (k.spec) s += " float4 spc = saturate(mS * aS);\n";
    else s += k.c1 ? " float4 spc = i.c1;\n" : " float4 spc = float4(0, 0, 0, 1);\n";
    return s;
}

// shadow map lookup (registers c166-c171, sampler 14), shared with the extra passes in pp9.cpp
inline std::string shadowCode() {
    // 4x4 texels with bilinear weights (a 3x3 PCF); an occluder counts only up to cSh2.z behind it
    // (fading over 1/cSh2.w): no shadows through floors, walls and tables. The 8 outer + inner taps
    // decide first: fully lit (most of the screen) costs 8 fetches instead of 16.
    return std::string("sampler2D sShadow : register(s14);\nfloat4 cShM[3] : register(c166);\nfloat4 cSh : register(c169);\n"
             "float4 cSh2 : register(c170);\nfloat4 cShL : register(c171);\n"
             "sampler2D sBlock : register(s15);\n"
             // a prop between the character and this surface (the table top over the floor) stops the shadow
             "float shTap(float2 uv, float r) { float d = tex2Dlod(sShadow, float4(uv, 0, 0)).r; float g = r - d;\n"
             " float b = tex2Dlod(sBlock, float4(uv, 0, 0)).r;\n"
             " if (b < r - cShL.w && b > d + cShL.w) return 0;\n"
             " return g > 0 ? saturate(1 - (g - cSh2.z) * cSh2.w) : 0; }\n"
             "float shadowAt(float3 P, float3 N) {\n"
             // the offset along the normal grows where the light grazes the surface (no stripes on boards)
             " float4 q = float4(P + N * cSh2.x * max(P.z * 0.002, 1) * (1 + 3 * (1 - abs(dot(N, cShL.xyz)))), 1);\n"
             " float3 s = float3(dot(q, cShM[0]), dot(q, cShM[1]), dot(q, cShM[2]));\n"
             " if (s.x <= 0 || s.y <= 0 || s.x >= 1 || s.y >= 1 || s.z >= 1) return 1;\n"
             " float2 t = s.xy / cSh.z - 0.5; float2 f = frac(t); float2 b = (floor(t) - 0.5) * cSh.z;\n"
             " float r = s.z - cSh.y;\n"
             " float4 o1 = float4(shTap(b + float2(0.5, 0.5) * cSh.z, r), shTap(b + float2(3.5, 0.5) * cSh.z, r),\n"
             "                    shTap(b + float2(0.5, 3.5) * cSh.z, r), shTap(b + float2(3.5, 3.5) * cSh.z, r));\n"
             " float4 o2 = float4(shTap(b + float2(1.5, 1.5) * cSh.z, r), shTap(b + float2(2.5, 1.5) * cSh.z, r),\n"
             "                    shTap(b + float2(1.5, 2.5) * cSh.z, r), shTap(b + float2(2.5, 2.5) * cSh.z, r));\n"
             " [branch] if (dot(o1 + o2, 1) == 0) return 1;\n"
             " float acc = dot(o1, float4((1 - f.x) * (1 - f.y), f.x * (1 - f.y), (1 - f.x) * f.y, f.x * f.y)) + dot(o2, 1);\n"
             " acc += (shTap(b + float2(1.5, 0.5) * cSh.z, r) + shTap(b + float2(2.5, 0.5) * cSh.z, r)) * (1 - f.y);\n"
             " acc += (shTap(b + float2(1.5, 3.5) * cSh.z, r) + shTap(b + float2(2.5, 3.5) * cSh.z, r)) * f.y;\n"
             " acc += (shTap(b + float2(0.5, 1.5) * cSh.z, r) + shTap(b + float2(0.5, 2.5) * cSh.z, r)) * (1 - f.x);\n"
             " acc += (shTap(b + float2(3.5, 1.5) * cSh.z, r) + shTap(b + float2(3.5, 2.5) * cSh.z, r)) * f.x;\n"
             // fades out over the last 8% of the shadow box instead of ending in a hard line
             " float e = min(min(s.x, s.y), min(1 - s.x, 1 - s.y));\n"
             " return 1 - acc / 9 * saturate(e * 12);\n}\n");
}
// the level's own sun shadows: a map of the level's surfaces seen from the sun that is kept from frame
// to frame (what has been on the screen keeps its shadow while the camera turns), s11; cWsM: view space
// -> that map; cWsh: strength, texel, depth bias, quality (1 high). Soft like real sun shadows: the
// further the occluder, the wider the edge (a tree's crown blurs, a post's foot stays sharp) - the
// occluders' distance is averaged first, then the edge is sampled that wide. Low quality: one small
// fixed disc (weak graphics cards).
// 0.9.72: after the kept map has moved, the old one (s10, cWsO: its shift in map space, w = how much it
// still counts) is read too for 2.5 s and fades out - the darker of the two, only while w > 0.
inline std::string worldShadowCode() {
    return std::string("sampler2D sWorld : register(s11);\nsampler2D sWorldOld : register(s10);\nfloat4 cWsM[3] : register(c218);\nfloat4 cWsh : register(c217);\nfloat4 cWsO : register(c221);\n"
             "static const float2 wsD[12] = { float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696, 0.457), float2(-0.203, 0.621),\n"
             "   float2(0.962, -0.195), float2(0.473, -0.480), float2(0.519, 0.767), float2(0.185, -0.893),\n"
             "   float2(0.507, 0.064), float2(0.896, 0.412), float2(-0.322, -0.933), float2(-0.792, -0.598) };\n"
             "float wsMap(sampler2D sm, float3 s) {\n"
             " if (s.x <= 0 || s.y <= 0 || s.x >= 1 || s.y >= 1 || s.z >= 1) return 1;\n"
             " float r = s.z - cWsh.z; float acc = 0; float rad = 2;\n"
             " [branch] if (cWsh.w > 0.5) {\n"
             "  float bs = 0, bn = 0;\n"
             "  [unroll] for (int i = 0; i < 6; i++) { float b = tex2Dlod(sm, float4(s.xy + wsD[i * 2] * cWsh.y * 10, 0, 0)).r; if (b < r) { bs += b; bn += 1; } }\n"
             "  if (bn == 0) return 1;\n"
             "  rad = clamp((r - bs / bn) * 12000 * 0.045 / (cWsh.y * 9600), 1.5, 14);\n"   // edge width from the occluder's distance (units -> texels)
             " }\n"
             " [unroll] for (int i = 0; i < 12; i++) { float b = tex2Dlod(sm, float4(s.xy + wsD[i] * cWsh.y * rad, 0, 0)).r; acc += b < r ? 1 : 0; }\n"
             " float e = min(min(s.x, s.y), min(1 - s.x, 1 - s.y));\n"
             " return 1 - acc / 12 * saturate(e * 12);\n}\n"
             "float worldShadowAt(float3 P, float3 N) {\n"
             " float4 q = float4(P + N * max(P.z * 0.004, 2) * (2 + 6 * (1 - abs(dot(N, cShL.xyz)))), 1);\n"
             " float3 s = float3(dot(q, cWsM[0]), dot(q, cWsM[1]), dot(q, cWsM[2]));\n"
             " float m = wsMap(sWorld, s);\n"
             " [branch] if (cWsO.w > 0.001) m = min(m, lerp(1, wsMap(sWorldOld, s + cWsO.xyz), cWsO.w));\n"
             " return m;\n}\n"
             // 0.9.72: the shadows of what moves (characters, pickups) from the two nearest visible lights. cTorch: the
             // light in view space, 1 / its radius (0: none); the cube maps hold distance / radius along the kept map's
             // axes (cWsM without its scale). Strongest close to the light, gone at its radius.
             "samplerCUBE sTorch0 : register(s8);\nsamplerCUBE sTorch1 : register(s9);\nfloat4 cTorch[2] : register(c222);\n"
             "float torchOne(samplerCUBE sm, float4 t, float3 P, float3 N) {\n"
             " [branch] if (t.w <= 0) return 1;\n"
             " float3 dv = P + N * 4 - t.xyz;\n"
             " float3 c = float3(dot(dv, cWsM[0].xyz) * 9600, dot(dv, cWsM[1].xyz) * 9600, dot(dv, cWsM[2].xyz) * 12000);\n"
             " float l = length(c); float x = l * t.w;\n"
             " [branch] if (x >= 1 || l < 1) return 1;\n"
             " float3 dn = c / l; float3 a = abs(dn.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0);\n"
             " float3 u = normalize(cross(dn, a)) * 0.012; float3 v = cross(dn, u);\n"
             " float r = x - 0.012;\n"
             " float o = (texCUBElod(sm, float4(dn + u + v, 0)).r < r ? 1 : 0) + (texCUBElod(sm, float4(dn - u + v, 0)).r < r ? 1 : 0)\n"
             "         + (texCUBElod(sm, float4(dn + u - v, 0)).r < r ? 1 : 0) + (texCUBElod(sm, float4(dn - u - v, 0)).r < r ? 1 : 0);\n"
             " float att = saturate(1.3 - 1.3 * x) * saturate((l - 60) / 120);\n"   // full up to a quarter of the radius, then down to nothing at it
             " return 1 - o * 0.25 * att * 0.5;\n}\n"
             "float torchShadowAt(float3 P, float3 N) { return min(torchOne(sTorch0, cTorch[0], P, N), torchOne(sTorch1, cTorch[1], P, N)); }\n");
}
// reflections on water: a ray from the surface along the mirrored view, marched through the last
// frame's view depth (s13) and coloured from the last frame (s12); where it leaves the picture or
// finds nothing the surface keeps its own look. cWat0: projection (P00, P11, P20, P21); cWat1:
// strength, -, -, 1 when the last frame's pictures are there
inline std::string waterCode() {
    // the march: steps growing with distance, then the crossing found by halving (no stair steps); a hit
    // counts only where the surface met is close behind the ray (no smearing of things in front, such as
    // bushes and Shrek, over what is behind them); fades towards the screen edges and with distance
    return std::string("sampler2D sPrevC : register(s12);\nsampler2D sPrevZ : register(s13);\n"
             "float4 cWat0 : register(c215);\nfloat4 cWat1 : register(c216);\n"
             "float2 wUV(float3 Q) { return float2((Q.x * cWat0.x / Q.z + cWat0.z) * 0.5 + 0.5, 0.5 - (Q.y * cWat0.y / Q.z + cWat0.w) * 0.5); }\n"
             "float3 waterReflect(float3 P, float3 Nu, float3 base, out float amt) {\n"
             " amt = 0;\n"
             " float3 Vd = normalize(P); float3 R = reflect(Vd, Nu);\n"
             " float cv = saturate(dot(-Vd, Nu));\n"
             " float fres = 0.05 + 0.95 * pow(1 - cv, 5);\n"
             " float st = max(P.z * 0.025, 8); float3 Q = P, Qp = P; float3 hit = base; float found = 0;\n"
             " [loop] for (int i = 0; i < 32; i++) {\n"
             "  Qp = Q; Q += R * st; st *= 1.1;\n"
             "  if (Q.z <= 2) break;\n"
             "  float2 uv = wUV(Q);\n"
             "  if (uv.x < 0 || uv.y < 0 || uv.x > 1 || uv.y > 1) break;\n"
             "  float sz = tex2Dlod(sPrevZ, float4(uv, 0, 0)).r;\n"
             "  if (sz >= 1 && Q.z > sz) {\n"
             "   float3 a = Qp, b = Q;\n"
             "   [unroll] for (int j = 0; j < 5; j++) { float3 m = (a + b) * 0.5; float mz = tex2Dlod(sPrevZ, float4(wUV(m), 0, 0)).r; if (mz >= 1 && m.z > mz) b = m; else a = m; }\n"
             "   float2 hu = wUV(b); float hz = tex2Dlod(sPrevZ, float4(hu, 0, 0)).r;\n"
             "   if (hz < 1 || b.z - hz > max(st * 1.5, 0.02 * b.z)) break;\n"   // passed behind something in front: no reflection there
             "   hit = tex2Dlod(sPrevC, float4(hu, 0, 0)).rgb;\n"
             "   float e = min(min(hu.x, hu.y), min(1 - hu.x, 1 - hu.y));\n"
             "   found = saturate(e * 8) * saturate(1.2 - i / 32.0); break; }\n"
             " }\n"
             " amt = fres * found * cWat1.x;\n"
             " return hit;\n}\n");
}
inline std::string genVS(const Key& k) {
    std::string s;
    s += "float4 cWVP[4] : register(c160);\nfloat4 cWV[4] : register(c164);\nfloat4 cNM[3] : register(c168);\n"
         "float4 cFog : register(c171);\n";
    if (k.debug == 2) s += lightDecl(172);         // per-vertex comparison view: no texture stages
    else s += "float4 cTex[32] : register(c172);\n";
    s += "struct VIn {\n float4 pos : POSITION0;\n";
    if (k.hasN) s += " float4 nrm : NORMAL0;\n";
    if (k.c0) s += " float4 c0 : COLOR0;\n";
    if (k.c1) s += " float4 c1 : COLOR1;\n";
    for (int i = 0; i < 8; i++) if (k.tc[i]) s += fmt(" float4 t%d : TEXCOORD%d;\n", i, i);
    s += "};\nstruct VOut {\n float4 pos : POSITION;\n";
    for (int i = 0; i < k.n; i++) if (!psGen(k.st[i])) s += fmt(" float4 t%d : TEXCOORD%d;\n", i, i);
    s += fmt(" float4 vp : TEXCOORD%d;\n float3 vn : TEXCOORD%d;\n", k.n, k.n + 1);
    if (k.debug == 2) s += fmt(" float4 lv : TEXCOORD%d;\n", k.n + 2);
    if (k.c0) s += " float4 c0 : COLOR0;\n";
    if (k.c1) s += " float4 c1 : COLOR1;\n";
    s += "};\nVOut main(VIn i) {\n VOut o;\n float4 p = i.pos;\n"
         " o.pos = float4(dot(p, cWVP[0]), dot(p, cWVP[1]), dot(p, cWVP[2]), dot(p, cWVP[3]));\n"
         " float4 v = float4(dot(p, cWV[0]), dot(p, cWV[1]), dot(p, cWV[2]), dot(p, cWV[3]));\n"
         "";
    s += k.hasN ? " float3 n = float3(dot(i.nrm.xyz, cNM[0].xyz), dot(i.nrm.xyz, cNM[1].xyz), dot(i.nrm.xyz, cNM[2].xyz));\n"
                : " float3 n = float3(0, 0, 0);\n";
    if (k.norm && k.hasN) s += " n = n * rsqrt(max(dot(n, n), 1e-30));\n";
    // fog factor (vertex fog), as DXVK computes it
    s += " float f = 1;\n";
    if (k.fog) {
        s += k.range ? " float d = length(v.xyz);\n" : " float d = abs(v.z);\n";
        switch (k.fogV) {
        case FOG_LINEAR: s += " f = saturate((cFog.x - d) * cFog.y);\n"; break;
        case FOG_EXP:    s += " f = exp(-(d * cFog.z));\n"; break;
        case FOG_EXP2:   s += " f = d * cFog.z; f = exp(-(f * f));\n"; break;
        default:            s += k.c1 ? " f = i.c1.w;\n" : " f = 1;\n"; break;
        }
    }
    s += " o.vp = float4(v.xyz, f);\n o.vn = n;\n";
    if (k.debug == 2) {
        s += " {\n float3 P = v.xyz;\n float3 N = normalize(n);\n float ao = 1;\n" + lightBody(k);
        s += k.spec ? " o.lv = float4(saturate(dif.rgb + spc.rgb), 1);\n }\n" : " o.lv = float4(dif.rgb, 1);\n }\n";
    }
    if (k.c0) s += " o.c0 = i.c0;\n";
    if (k.c1) s += " o.c1 = i.c1;\n";
    for (int st = 0; st < k.n; st++) {
        const Stage& g = k.st[st];
        if (psGen(g)) continue;
        int in = g.tci & 7, cnt = k.tc[in], flags = g.ttf;
        bool apply = flags > 1 && flags <= 4;
        int count = flags < 4 ? flags : 4;
        int projIndex = count ? count - 1 : 4;
        s += " {\n float4 tr = 0;\n";
        switch (g.tcf) {
        default:
        case 0:
            if (cnt) s += fmt(" tr = i.t%d;\n", in);
            if (cnt < 4) s += " tr.w = 0;\n";
            if (apply) { if (cnt >= 1 && cnt < 4) s += fmt(" tr[%d] = 1;\n", cnt); }
            else if (cnt) count = cnt;
            projIndex = count ? count - 1 : 4;
            break;
        case 1: s += " tr = float4(n, 1);\n"; if (!apply) { count = 3; projIndex = 4; } break;
        case 2: s += " tr = v;\n"; if (!apply) { count = 3; projIndex = 4; } break;
        case 3: s += " tr = float4(reflect(normalize(v.xyz), n), 1);\n"; if (!apply) { count = 3; projIndex = 4; } break;
        case 4: s += " { float3 r = reflect(normalize(v.xyz), n); float m = length(r + float3(0, 0, 1)) * 2; tr = float4(r.xy / m + 0.5, 0, 1); }\n"; break;
        }
        if (apply) s += fmt(" tr = float4(dot(tr, cTex[%d]), dot(tr, cTex[%d]), dot(tr, cTex[%d]), dot(tr, cTex[%d]));\n", st * 4, st * 4 + 1, st * 4 + 2, st * 4 + 3);
        s += " float4 r = tr;\n";
        for (int c = 0; c < 4; c++) if (count < c + 1) s += fmt(" r[%d] = 0;\n", c);
        if (g.proj && projIndex < 4) s += fmt(" r.w = tr[%d];\n", projIndex);
        s += fmt(" o.t%d = r;\n }\n", st);
    }
    s += " return o;\n}\n";
    return s;
}

inline std::string argExpr(int st, int a, const Key&) {
    std::string r;
    switch (a & 0x0F) {
    case TA_DIFFUSE:  r = "dif"; break;
    case TA_CURRENT:  r = "cur"; break;
    case TA_TEXTURE:  r = fmt("x%d", st); break;
    case TA_TFACTOR:  r = "cTF"; break;
    case TA_SPECULAR: r = "spc"; break;
    case TA_TEMP:     r = "tmp"; break;
    default:             r = "float4(1, 1, 1, 1)"; break;
    }
    if (a & TA_COMPLEMENT) r = "(1 - " + r + ")";
    if (a & TA_ALPHAREPLICATE) r = "(" + r + ").aaaa";
    return r;
}

inline bool opSupported(int op) {
    return op >= TOP_SELECTARG1 && op <= TOP_LERP && op != TOP_PREMODULATE &&
           op != TOP_BUMPENVMAP && op != TOP_BUMPENVMAPLUMINANCE;
}

inline std::string opExpr(int op, const std::string& a0, const std::string& a1, const std::string& a2, int st) {
    std::string tx = fmt("x%d", st);
    switch (op) {
    case TOP_SELECTARG1: return a1;
    case TOP_SELECTARG2: return a2;
    case TOP_MODULATE4X: return "saturate(" + a1 + " * " + a2 + " * 4)";
    case TOP_MODULATE2X: return "saturate(" + a1 + " * " + a2 + " * 2)";
    case TOP_MODULATE:   return "saturate(" + a1 + " * " + a2 + ")";
    case TOP_ADDSIGNED2X: return "saturate(2 * (" + a1 + " + (" + a2 + " - 0.5)))";
    case TOP_ADDSIGNED:  return "saturate(" + a1 + " + (" + a2 + " - 0.5))";
    case TOP_ADD:        return "saturate(" + a1 + " + " + a2 + ")";
    case TOP_SUBTRACT:   return "saturate(" + a1 + " - " + a2 + ")";
    case TOP_ADDSMOOTH:  return "saturate((1 - " + a1 + ") * " + a2 + " + " + a1 + ")";
    case TOP_BLENDDIFFUSEALPHA: return "lerp(" + a2 + ", " + a1 + ", dif.aaaa)";
    case TOP_BLENDTEXTUREALPHA: return "lerp(" + a2 + ", " + a1 + ", " + tx + ".aaaa)";
    case TOP_BLENDFACTORALPHA:  return "lerp(" + a2 + ", " + a1 + ", cTF.aaaa)";
    case TOP_BLENDTEXTUREALPHAPM: return "saturate(" + a2 + " * (1 - " + tx + ".aaaa) + " + a1 + ")";
    case TOP_BLENDCURRENTALPHA: return "lerp(" + a2 + ", " + a1 + ", cur.aaaa)";
    case TOP_MODULATEALPHA_ADDCOLOR: return "saturate((" + a1 + ").aaaa * " + a2 + " + " + a1 + ")";
    case TOP_MODULATECOLOR_ADDALPHA: return "saturate(" + a1 + " * " + a2 + " + (" + a1 + ").aaaa)";
    case TOP_MODULATEINVALPHA_ADDCOLOR: return "saturate((1 - (" + a1 + ").aaaa) * " + a2 + " + " + a1 + ")";
    case TOP_MODULATEINVCOLOR_ADDALPHA: return "saturate((1 - " + a1 + ") * " + a2 + " + (" + a1 + ").aaaa)";
    case TOP_DOTPRODUCT3: return "saturate(dot((" + a1 + ").rgb - 0.5, (" + a2 + ").rgb - 0.5) * 4).xxxx";
    case TOP_MULTIPLYADD: return "saturate(" + a1 + " * " + a2 + " + " + a0 + ")";
    case TOP_LERP:        return "lerp(" + a2 + ", " + a1 + ", " + a0 + ")";
    }
    return "prev";
}

inline std::string genPS(const Key& k) {
    std::string s;
    s += lightDecl(100);
    s += "float4 cTF : register(c162);\nfloat4 cFogC : register(c163);\nfloat4 cFog : register(c164);\nfloat4 cUp : register(c165);\n";
    if (k.lit) s += "float4 cDim : register(c204);\n";
    if (k.aer) s += "float4 cAer : register(c213);\nfloat4 cAerT : register(c214);\n";
    { bool any = false; for (int i = 0; i < k.n; i++) if (psGen(k.st[i])) any = true;
      if (any) s += "float4 cTexP[32] : register(c172);\n"; }
    if (k.shadow)
        // cShM: view space -> shadow map (u, v, depth); cSh: strength, depth bias, texel, sun light (-1: none);
        // cSh2: normal offset, strength on surfaces without the sun light, sun direction (view space) in cShL
        s += shadowCode();
    if (k.shadow && k.wsh) s += worldShadowCode();
    if (k.water) s += waterCode();
    if (k.detail && !k.st[k.detail - 1].lm) { bool lmAny = false; for (int i = 0; i < k.n; i++) if (k.st[i].lm) lmAny = true;
      if (!lmAny) s += "float4 cTS[8] : register(c205);\n";
      s += "float hash2(float2 p) { float3 p3 = frac(float3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return frac((p3.x + p3.y) * p3.z); }\n"
           "float vnoise(float2 p) { float2 i = floor(p), f = frac(p); f = f * f * (3 - 2 * f);\n"
           " return lerp(lerp(hash2(i), hash2(i + float2(1, 0)), f.x), lerp(hash2(i + float2(0, 1)), hash2(i + float2(1, 1)), f.x), f.y); }\n"; }
    { bool any = false; for (int i = 0; i < k.n; i++) if (k.st[i].lm) any = true;
      // lightmaps: cubic B-spline from 4 bilinear fetches (Sigg & Hadwiger 2005). The baked light
      // gets smooth gradients instead of the diamond pattern of bilinear filtering on its large texels.
      // cTS: texture size (w, h, 1/w, 1/h) per stage
      if (any) s += "float4 cTS[8] : register(c205);\n"
                    "float4 bspline(sampler2D s, float2 uv, float4 ts) {\n"
                    " float2 dx = ddx(uv), dy = ddy(uv);\n"
                    // only where a lightmap texel covers more than ~2 pixels: further away bilinear looks the same
                    // and costs a quarter (most of the screen at 4K)
                    " float2 g = max(abs(dx), abs(dy)) * ts.xy;\n"
                    " [branch] if (max(g.x, g.y) > 0.5) return tex2Dgrad(s, uv, dx, dy);\n"
                    " float2 t = uv * ts.xy - 0.5; float2 f = frac(t); float2 c = t - f;\n"
                    " float2 f2 = f * f, f3 = f2 * f;\n"
                    " float2 w0 = (1 - 3 * f + 3 * f2 - f3) / 6, w1 = (4 - 6 * f2 + 3 * f3) / 6;\n"
                    " float2 w2 = (1 + 3 * f + 3 * f2 - 3 * f3) / 6, w3 = f3 / 6;\n"
                    " float2 g0 = w0 + w1, g1 = w2 + w3;\n"
                    " float2 h0 = (c - 0.5 + w1 / g0) * ts.zw, h1 = (c + 1.5 + w3 / g1) * ts.zw;\n"
                    " return g0.y * (g0.x * tex2Dgrad(s, float2(h0.x, h0.y), dx, dy) + g1.x * tex2Dgrad(s, float2(h1.x, h0.y), dx, dy)) +\n"
                    "        g1.y * (g0.x * tex2Dgrad(s, float2(h0.x, h1.y), dx, dy) + g1.x * tex2Dgrad(s, float2(h1.x, h1.y), dx, dy));\n}\n"; }
    for (int i = 0; i < k.n; i++) {
        switch (k.st[i].tex) {
        case 1: s += fmt("sampler2D s%d : register(s%d);\n", i, i); break;
        case 2: s += fmt("samplerCUBE s%d : register(s%d);\n", i, i); break;
        case 3: s += fmt("sampler3D s%d : register(s%d);\n", i, i); break;
        }
    }
    s += "struct PIn {\n";
    for (int i = 0; i < k.n; i++) if (!psGen(k.st[i])) s += fmt(" float4 t%d : TEXCOORD%d;\n", i, i);
    s += fmt(" float4 vp : TEXCOORD%d;\n float3 vn : TEXCOORD%d;\n", k.n, k.n + 1);
    if (k.debug == 2) s += fmt(" float4 lv : TEXCOORD%d;\n", k.n + 2);
    if (k.c0) s += " float4 c0 : COLOR0;\n";
    if (k.c1) s += " float4 c1 : COLOR1;\n";
    if (k.debug == 2) { s += "};\nfloat4 main(PIn i) : COLOR0 {\n return i.lv;\n}\n"; return s; }
    if (k.mrt) s += "};\nstruct POut { float4 c : COLOR0; float4 z : COLOR1; };\nPOut main(PIn i) {\n";
    else s += "};\nfloat4 main(PIn i) : COLOR0 {\n";
    s += ""
         " float3 P = i.vp.xyz;\n float ao = 1;\n";
    // geometric normal: the vertex normal, or the face itself for geometry without normals
    s += k.hasN ? " float3 N = normalize(i.vn);\n" : " float3 N = normalize(cross(ddx(P), ddy(P)));\n";
    s += " float3 N0 = N;\n";
    if (k.bump) {
        // relief from the texture's brightness as a height field (derivative bump mapping,
        // Mikkelsen 2010): no tangents needed, scales with distance by itself
        int b = k.bump - 1;
        // a coarser mip level: the large shapes of the texture, not its pixel noise
        s += fmt(" {\n  float hgt = dot(tex2Dbias(s%d, float4(i.t%d.xy, 0, cMisc.w)).rgb, float3(0.299, 0.587, 0.114));\n", b, b);
        s += "  float3 sx = ddx(P), sy = ddy(P);\n  float3 r1 = cross(sy, N), r2 = cross(N, sx);\n"
             "  float det = dot(sx, r1);\n"
             "  float3 g = (det < 0 ? -1 : 1) * (ddx(hgt) * r1 + ddy(hgt) * r2);\n"
             "  float3 nb = abs(det) * N - cMisc.y * g;\n"
             "  N = dot(nb, nb) > 1e-30 ? normalize(nb) : N;\n"
             "  ao = saturate(1 - cMisc.z * (1 - dot(N, N0)));\n }\n";
    }
    if (k.shadow)
        s += " float shRaw = shadowAt(P, N0);\n float shF = lerp(1, shRaw, cSh.x);\n";
    if (k.shadow && k.wsh == 1)   // the level's shadow on a character or prop: only through the sun light (shF), a little softer
        s += " shF = min(shF, lerp(1, worldShadowAt(P, N0), cSh.x * 0.85));\n";   // in two shadows at once: as dark as the darker one
    if (k.lit) s += lightBody(k) + " dif.rgb *= cDim.x;\n";   // characters and props lit by the engine: level brightness
    else {   // unlit: the vertex colours as they are (baked lighting), as in DXVK
        s += k.c0 ? " float4 dif = i.c0;\n" : " float4 dif = float4(1, 1, 1, 1);\n";
        s += k.c1 ? " float4 spc = i.c1;\n" : " float4 spc = float4(0, 0, 0, 1);\n";
    }
    if (k.lit && k.shadow && k.wsh == 1) s += " dif.rgb *= lerp(1, torchShadowAt(P, N0), 0.6);\n";   // in another's shadow from a torch
    if (k.debug == 1) { s += k.spec ? " return float4(saturate(dif.rgb + spc.rgb), 1);\n}\n" : " return float4(dif.rgb, 1);\n}\n"; return s; }
    s += " float4 cur = dif, tmp = 0;\n";
    for (int st = 0; st < k.n; st++) {
        const Stage& g = k.st[st];
        s += " {\n";
        if (g.tex) {
            if (psGen(g)) {   // camera-space position, as the vertex shader would have given it
                int flags = g.ttf; bool apply = flags > 1 && flags <= 4;
                int count = flags < 4 ? flags : 4, projIndex = count ? count - 1 : 4;
                if (!apply) { count = 3; projIndex = 4; }
                s += g.tcf == 5 ? "  float4 tc;\n  {\n  float4 tr = float4(reflect(normalize(P), N0), 1);\n" : "  float4 tc;\n  {\n  float4 tr = float4(P, 1);\n";
                if (apply) s += fmt("  tr = float4(dot(tr, cTexP[%d]), dot(tr, cTexP[%d]), dot(tr, cTexP[%d]), dot(tr, cTexP[%d]));\n", st * 4, st * 4 + 1, st * 4 + 2, st * 4 + 3);
                s += "  float4 r = tr;\n";
                for (int c = 0; c < 4; c++) if (count < c + 1) s += fmt("  r[%d] = 0;\n", c);
                if (g.proj && projIndex < 4) s += fmt("  r.w = tr[%d];\n", projIndex);
                s += "  tc = r;\n  }\n";
            } else s += fmt("  float4 tc = i.t%d;\n", st);
            if (g.proj) s += "  tc /= tc.w;\n";
            switch (g.tex) {
            case 1: s += g.lm ? fmt("  float4 x%d = bspline(s%d, tc.xy, cTS[%d]);\n", st, st, st)
                              : fmt("  float4 x%d = tex2D(s%d, tc.xy);\n", st, st); break;
            case 2: s += fmt("  float4 x%d = texCUBE(s%d, tc.xyz);\n", st, st); break;
            case 3: s += fmt("  float4 x%d = tex3D(s%d, tc.xyz);\n", st, st); break;
            }
            if (g.tex == 1 && k.detail == st + 1) {
                // stretched textures (one texel over several pixels): fine neutral grain, value noise four
                // cells to a texel, brightness only. It follows no picture of its own (no copies of the
                // texture's rings or stains). Fades in from ~3 pixels per texel; strength in cTS.z
                s += fmt("  {\n   float2 du = ddx(tc.xy), dv = ddy(tc.xy);\n"
                         "   float2 gg = max(abs(du), abs(dv)) * cTS[%d].xy;\n"
                         "   float dw = saturate((0.4 - max(gg.x, gg.y)) * 4) * cTS[%d].z;\n"
                         "   [branch] if (dw > 0.01) {\n"
                         "    float2 q = frac(tc.xy / 64) * 64 * cTS[%d].xy * 4;\n"
                         "    float gr = (vnoise(q) + 0.5 * vnoise(q * 2.03 + 17.1)) / 1.5 - 0.5;\n"
                         "    x%d.rgb *= 1 + dw * gr * 0.35;\n   }\n  }\n", st, st, st, st);
            }
        } else s += fmt("  float4 x%d = 1;\n", st);
        s += g.temp ? "  float4 prev = tmp;\n" : "  float4 prev = cur;\n";
        s += "  float4 nx = prev;\n";
        std::string c0 = argExpr(st, g.ca[0], k), c1 = argExpr(st, g.ca[1], k), c2 = argExpr(st, g.ca[2], k);
        bool fast = g.cop == g.aop && !memcmp(g.ca, g.aa, 3);
        if (fast || g.cop == TOP_DOTPRODUCT3) s += "  nx = " + opExpr(g.cop, c0, c1, c2, st) + ";\n";
        else {
            s += "  nx.rgb = (" + opExpr(g.cop, c0, c1, c2, st) + ").rgb;\n";
            if (g.aop != TOP_DISABLE)
                s += "  nx.a = (" + opExpr(g.aop, argExpr(st, g.aa[0], k), argExpr(st, g.aa[1], k), argExpr(st, g.aa[2], k), st) + ").a;\n";
        }
        s += g.temp ? "  tmp = nx;\n" : "  cur = nx;\n";
        s += " }\n";
    }
    if (!k.lit && k.bump)    // baked lighting has no direction: relief shaded as if lit from above and from the viewer
        s += " {\n  float3 Lr = normalize(cUp.xyz - normalize(P) * 0.6);\n"
             "  float rf = (1 + cUp.w * (dot(N, Lr) - dot(N0, Lr))) * ao;\n"
             "  cur.rgb *= clamp(rf, 0, 1.6);\n }\n";
    if (k.shadow && k.wsh >= 2)   // the level (baked light; 3: by night, its shadows also where the baked light is dark): the characters' shadow and what stands between it and the sun -
        // roofs, trees, rocks - the darker of the two, not both (no shadow in a shadow); less where the baked
        // light is dark already (its own shadows)
        s += " {\n  float sl = saturate(dot(N0, -cShL.xyz) * 3);\n"
             "  float fC = (cSh.w < 0 || !LITKEY) ? lerp(1, shRaw, cSh2.y * sl) : 1;\n"
             "  float lum = dot(cur.rgb, float3(0.3, 0.59, 0.11));\n"
             "  float fW = lerp(1, worldShadowAt(P, N0), cWsh.x * sl * saturate(lum * " + std::string(k.wsh == 3 ? "9" : "2.5") + "));\n"
             "  cur.rgb *= min(min(fC, fW), torchShadowAt(P, N0));\n }\n";
    else if (k.shadow)   // surfaces the sun light does not reach in this draw (baked lighting, point lights): darkened
        s += " if (cSh.w < 0 || !LITKEY) cur.rgb *= lerp(1, shRaw, cSh2.y * saturate(dot(N0, -cShL.xyz) * 3));\n";
    if (k.spec) s += " cur.rgb += spc.rgb;\n";
    if (k.water)   // only on its flat, upward parts
        s += " [branch] if (cWat1.w > 0 && abs(dot(N0, cUp.xyz)) > 0.8) {\n"   // the level's water polygons face either way
             "  float wa; float3 wr = waterReflect(P, normalize(cUp.xyz), cur.rgb, wa);\n"
             "  cur.rgb = lerp(cur.rgb, wr, wa);\n }\n";
    if (k.aer)   // the further, the more of the air's colour: less saturation, a little lighter and bluer
        s += " {\n  float ad = saturate((P.z - cAer.x) * cAer.y) * cAer.z;\n"
             "  float lum = dot(cur.rgb, float3(0.3, 0.59, 0.11));\n"
             "  cur.rgb = lerp(cur.rgb, lum * cAerT.rgb + cAerT.w, ad);\n }\n";
    if (k.fog) {
        if (k.fogT != FOG_NONE) {
            s += " float d = abs(P.z), f;\n";
            switch (k.fogT) {
            case FOG_LINEAR: s += " f = (cFog.x - d) * cFog.y;\n"; break;
            case FOG_EXP:    s += " f = exp(-(d * cFog.z));\n"; break;
            default:            s += " f = d * cFog.z; f = exp(-(f * f));\n"; break;
            }
        } else s += " float f = i.vp.w;\n";
        s += " f = saturate(f);\n cur.rgb = lerp(cFogC.rgb, cur.rgb, f);\n";
    }
    // mrt 2: blended with SRCALPHA / INVSRCALPHA, so the depth's alpha says where it counts (mostly opaque)
    if (k.mrt) s += k.mrt == 2 ? " POut o; o.c = cur; o.z = float4(P.z, 0, 0, cur.a >= 0.5 ? 1 : 0);\n return o;\n}\n"
                               : " POut o; o.c = cur; o.z = float4(P.z, 0, 0, 1);\n return o;\n}\n";
    else s += " return cur;\n}\n";
    size_t lk; while ((lk = s.find("LITKEY")) != std::string::npos) s.replace(lk, 6, k.lit ? "true" : "false");
    return s;
}

} // namespace ppg
