#include <vector>
#include <cstdio>
#include <cstdint>
typedef uint32_t DWORD; static const DWORD BIAS_REG=95; static void logf(const char*,...){}
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


int main(){
 // vs.1.1; dp4 oPos.x,v0,c0 ; dp4 oPos.y,v0,c1; dp4 oPos.z,v0,c2; dp4 oPos.w,v0,c3; mov r3, v1; mov oD0, r3
 auto P=[](DWORD t,DWORD r,DWORD m){return 0x80000000u|(t<<28)|r|(m<<16);};
 auto S=[](DWORD t,DWORD r){return 0x80000000u|(t<<28)|r|(0xE4u<<16);};
 std::vector<DWORD> v={0xFFFE0101};
 for(int i=0;i<4;i++){v.push_back(9);v.push_back(P(4,0,1u<<i));v.push_back(S(1,0));v.push_back(S(2,i));}
 v.push_back(1);v.push_back(P(0,3,15));v.push_back(S(1,1));
 v.push_back(1);v.push_back(P(5,0,15));v.push_back(S(0,3));
 v.push_back(0xFFFE|(1<<16)); v.push_back(0x1234);   // comment
 v.push_back(0xFFFF);
 std::vector<DWORD> o; printf("ok=%d\n",rewriteVS(v.data(),o));
 for(auto t:o) printf("%08x ",t); printf("\n");
}