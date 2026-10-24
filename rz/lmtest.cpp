#include "ppgen.h"
using namespace ppg;
static void dump(const Key& k,const char*n){std::string a=std::string("/tmp/t_")+n;FILE*f=fopen((a+".frag.hlsl").c_str(),"w");fputs(genPS(k).c_str(),f);fclose(f);f=fopen((a+".vert.hlsl").c_str(),"w");fputs(genVS(k).c_str(),f);fclose(f);}
int main(){
  Key k; memset(&k,0,sizeof k);
  k.n=2; k.tc[0]=2; k.tc[1]=2; k.fog=1; k.fogV=FOG_LINEAR; k.mrt=1;
  k.st[0]=Stage{TOP_SELECTARG1,TOP_SELECTARG1,{0,TA_TEXTURE,TA_DIFFUSE},{0,TA_TEXTURE,TA_DIFFUSE},0,0,0,0,0,1,0};
  k.st[1]=Stage{TOP_MODULATE2X,TOP_SELECTARG2,{0,TA_TEXTURE,TA_CURRENT},{0,TA_TEXTURE,TA_CURRENT},0,1,0,0,0,1,1};
  k.bump=1; k.shadow=1; k.detail=1; k.aer=1; dump(k,"a");
  Key u=k; u.st[1].lm=0; dump(u,"b");          // detail only
  Key l=k; l.lit=1; l.hasN=1; l.n=1; l.detail=1; l.st[1]=Stage{}; dump(l,"c");
}
