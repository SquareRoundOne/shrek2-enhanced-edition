#include "ppgen.h"
#include <cstdlib>
using namespace ppg;
static void dump(const Key& k, const char* name) {
    std::string a = std::string("/tmp/pp_") + name + ".vert.hlsl", b = std::string("/tmp/pp_") + name + ".frag.hlsl";
    FILE* f = fopen(a.c_str(), "w"); fputs(genVS(k).c_str(), f); fclose(f);
    f = fopen(b.c_str(), "w"); fputs(genPS(k).c_str(), f); fclose(f);
}
int main() {
    Key k; memset(&k, 0, sizeof k);
    // Donkey: pos normal uv0 uv1, MODULATE2X texture*diffuse, linear fog
    k.n = 1; k.tc[0] = 2; k.tc[1] = 2; k.src[0] = 0; k.norm = 1; k.fog = 1; k.fogV = FOG_LINEAR;
    k.st[0] = Stage{ TOP_MODULATE2X, TOP_MODULATE, {1, TA_TEXTURE, TA_DIFFUSE}, {1, TA_TEXTURE, TA_DIFFUSE}, 0, 0, 0, 0, 0, 1 };
    k.bump = 1; k.lit = 1; k.hasN = 1; dump(k, "a");
    Key u = k; u.lit = 0; u.hasN = 0; u.c0 = 1; dump(u, "e");
    // everything: colours, 4 stages, texgen, transform, projected, cube, temp, specular, table fog
    Key m; memset(&m, 0, sizeof m);
    m.lit = 1; m.hasN = 1; m.n = 4; m.c0 = 1; m.c1 = 1; m.tc[0] = 2; m.tc[1] = 3; m.src[0] = 1; m.src[1] = 1; m.src[2] = 2; m.spec = 1; m.local = 1; m.norm = 1; m.range = 1;
    m.fog = 1; m.fogV = FOG_EXP2; m.fogT = FOG_LINEAR;
    m.st[0] = Stage{ TOP_MODULATE, TOP_SELECTARG1, {0, TA_TEXTURE, TA_DIFFUSE}, {0, TA_TEXTURE, TA_DIFFUSE}, 0, 0, 0, 2, 0, 1 };
    m.st[1] = Stage{ TOP_BLENDTEXTUREALPHA, TOP_DISABLE, {0, TA_TEXTURE | TA_COMPLEMENT, TA_CURRENT}, {0, 0, 0}, 1, 1, 3, 3, 1, 2 };
    m.st[2] = Stage{ TOP_LERP, TOP_MULTIPLYADD, {TA_TEMP, TA_TEXTURE, TA_CURRENT | TA_ALPHAREPLICATE}, {TA_SPECULAR, TA_TFACTOR, TA_CURRENT}, 0, 0, 4, 0, 0, 3 };
    m.st[3] = Stage{ TOP_DOTPRODUCT3, TOP_ADDSMOOTH, {0, TA_TEXTURE, TA_TFACTOR}, {0, TA_TEXTURE, TA_TFACTOR}, 0, 1, 0, 4, 1, 0 };
    dump(m, "b");
    Key c = m; c.n = 0; memset(c.st, 0, sizeof c.st); c.debug = 1; dump(c, "c");
    c.debug = 2; dump(c, "d");
    Key a2 = k; a2.mrt = 1; a2.shadow = 1; dump(a2, "f");
    Key e2 = u; e2.mrt = 1; e2.shadow = 1; dump(e2, "g");
    Key e3 = u; e3.bump = 0; e3.mrt = 1; dump(e3, "h");
    Key m2 = m; m2.shadow = 1; m2.mrt = 1; dump(m2, "i");
    // terrain: 8 stages, alpha maps + camera-space layers
    Key t; memset(&t, 0, sizeof t); t.lit = 1; t.hasN = 1; t.c0 = 1; t.tc[0] = 2; t.n = 8; t.src[3] = 1; t.fog = 1; t.fogV = FOG_LINEAR; t.mrt = 1; t.shadow = 1; t.bump = 0;
    for (int i = 0; i < 8; i++) t.st[i] = (i & 1) ? Stage{ TOP_MODULATE2X, TOP_SELECTARG1, {0, TA_TEXTURE, TA_CURRENT}, {0, TA_TEXTURE, TA_CURRENT}, 0, 0, 2, 2, 0, 1 }
                                                  : Stage{ TOP_SELECTARG1, TOP_SELECTARG1, {0, TA_CURRENT, TA_CURRENT}, {0, TA_CURRENT, TA_CURRENT}, 0, 0, 0, 0, 0, 1 };
    printf("terrain interpolators %d\n", interpolators(t)); dump(t, "j");
    Key mk = k; mk.mrt = 2; mk.shadow = 0; mk.detail = 1; mk.aer = 1; dump(mk, "k");
}
