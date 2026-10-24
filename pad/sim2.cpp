// emulate Engine.dll AXIS exec + ReadInput (/dt) + ShPlayerInput + BaseCam for the new bindings
#include "padcore.h"
#include <cstdio>
int main(){ for(int fps:{30,60,144,240}){ pad::Mapper m; pad::JoyState j; pad::XState x; x.connected=true;
  float dt=1.f/fps, yaw=0, fwd=0;
  for(int f=0;f<fps;f++){
    x.rx=32767; x.ly=32767; m.toJoy(x,j,dt);
    float vR=(j.lRx-32768)*1.5259021893143654e-05f, vY=(j.lY-32768)*1.5259021893143654e-05f;
    float aTurn=54*vR;          // SpeedBase branch: += SpeedBase*value (no dt)
    aTurn/=dt;                  // ReadInput
    aTurn*=0.944f*dt*180;       // ShPlayerInput
    float md=aTurn*dt; if(md>190)md=190; yaw+=md*8;   // BaseCam
    float aBaseY=dt*(-2400)*vY; aBaseY/=dt; fwd=aBaseY; }
  printf("%3d fps: camera %.0f deg/s, aBaseY(full forward)=%.0f (keyboard W = 1200)\n",fps,yaw*360/65536,fwd);
  x=pad::XState(); x.connected=true; m.toJoy(x,j,dt); printf("      at rest: R=%d Y=%d\n", j.lRx-32768, j.lY-32768); } }
