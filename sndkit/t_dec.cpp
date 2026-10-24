#include "binkaudio.h"
#include <cstdio>
int main(int c, char** v){
  FILE* f=fopen(v[1],"rb"); std::vector<uint8_t> b; int ch; while((ch=fgetc(f))!=EOF) b.push_back(ch); fclose(f);
  std::vector<int16_t> pcm; int rate, chn;
  if(!binkaudio::decodeFile(b.data(), b.size(), pcm, rate, chn)) { printf("fail\n"); return 1; }
  FILE* o=fopen(v[2],"wb"); fwrite(pcm.data(),2,pcm.size(),o); fclose(o);
  printf("%d Hz %d ch %zu samples\n", rate, chn, pcm.size()/chn);
}
