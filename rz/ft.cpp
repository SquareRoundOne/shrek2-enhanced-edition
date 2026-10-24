#define STB_TRUETYPE_IMPLEMENTATION
#include "fontgen.h"
#include <cstdio>
#include <ctime>
#include <cstdlib>
int main(int c,char**v){ FILE*f=fopen("/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf","rb"); std::vector<uint8_t> t(1<<20); t.resize(fread(t.data(),1,t.size(),f));
 int H=atoi(v[1]); auto t0=clock(); auto p=fontgen::makePackage(t.data(),{H}); printf("%zu bytes %.2fs\n",p.size(),(clock()-t0)/(double)CLOCKS_PER_SEC);
 FILE*o=fopen(v[2],"wb"); fwrite(p.data(),1,p.size(),o); fclose(o);}
