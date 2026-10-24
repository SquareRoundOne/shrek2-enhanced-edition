#include "lang.h"
#include <cstdio>
int main(int argc, char** argv) {
    lang::Manager m; m.init(argv[1]);
    std::string lng = "Rus";
    m.setEngineLanguage = [&](bool en) { lng = en ? "int" : "Rus"; printf("  Language=%s\n", lng.c_str()); return true; };
    std::string err;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i]; bool ok;
        if (a == "reset") ok = m.reset(err);
        else if (a == "ensure") ok = m.ensure(err);
        else { int t = a[0] - '0', v = a[1] - '0'; ok = m.apply(t, v, err); }
        printf("%s -> %s %s\n", a.c_str(), ok ? "ok" : "FAIL", ok ? "" : err.c_str());
        if (!ok) return 1;
    }
    return 0;
}
