// camera speed per second in Shrek units (65536 = 360 deg) at several frame rates, emulating the game maths
#include "padcore.h"
#include <cstdio>
int main(){ for(int fps: {30,60,144,240}){ pad::Mapper m; pad::JoyState j; pad::XState x; x.connected=true; x.rx=32767;
  float dt=1.f/fps, yaw=0; for(int f=0; f<fps; f++){ m.toJoy(x,j,dt);
    float vdi=(j.lRx-32768)*1.5259021893143654e-05f; float aTurn=21600*vdi; aTurn*=0.944f*dt*180; float md=aTurn*dt; if(md>190)md=190; yaw+=md*8; }
  printf("%3d fps: %.0f deg/s\n",fps,yaw*360/65536);} }
