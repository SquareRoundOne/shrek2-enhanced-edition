#include "padcore.h"
#include <cstdio>
#include <cassert>
using namespace pad;
static double wd(int32_t v){ return (v-32768)*1.5259021893143654e-05; }   // what WinDrv computes
int main(){
  Mapper m; JoyState j; XState x; x.connected=true;
  // idle -> centred
  m.toJoy(x,j); printf("idle: X=%.3f Y=%.3f R=%.3f U=%.3f Z=%.3f pov=%u\n",wd(j.lX),wd(j.lY),wd(j.lRx),wd(j.lRy),wd(j.lZ),j.pov[0]);
  // small drift inside deadzone
  x.lx=5000; x.ly=-4000; m.toJoy(x,j); printf("drift: X=%.3f Y=%.3f\n",wd(j.lX),wd(j.lY));
  // full forward
  x.lx=0; x.ly=32767; m.toJoy(x,j); printf("forward: X=%.3f Y=%.3f (DI y down -> negative = forward)\n",wd(j.lX),wd(j.lY));
  // diagonal full: radial clamp
  x.lx=32767; x.ly=32767; m.toJoy(x,j); printf("diag: X=%.3f Y=%.3f\n",wd(j.lX),wd(j.lY));
  // right stick half & full with curve and camera speed
  x.lx=x.ly=0; x.rx=16384; m.toJoy(x,j); printf("cam half: R=%.3f\n",wd(j.lRx));
  x.rx=32767; m.toJoy(x,j); printf("cam full: R=%.3f\n",wd(j.lRx));
  x.rx=0; x.ry=32767; m.toJoy(x,j); printf("cam up: U=%.3f\n",wd(j.lRy));
  // buttons and triggers
  x.ry=0; x.buttons=BTN_A|BTN_Y|DPAD_LEFT; x.rt=200; m.toJoy(x,j);
  printf("buttons:"); for(int i=0;i<12;i++) printf(" %d",j.buttons[i]?1:0); printf(" pov=%u V=%.3f\n",j.pov[0],wd(j.lRz));
  // mode switch masking: A held entering game from menu
  Mapper k; XState a; a.connected=true; a.buttons=BTN_A;
  k.setMode(true,a); k.setMode(false,a); k.toJoy(a,j); printf("A held across switch -> joy1=%d\n",j.buttons[0]?1:0);
  a.buttons=0; k.setMode(false,a); a.buttons=BTN_A; k.setMode(false,a); k.toJoy(a,j); printf("A pressed again -> joy1=%d\n",j.buttons[0]?1:0);
  // menu driver
  MenuDriver md; MenuOut o; Settings s; XState mx; mx.connected=true; mx.lx=32767;
  float tx=0; for(int i=0;i<60;i++){ md.step(mx,s,0,1/60.f,2160,o); tx+=o.dx; } printf("menu: 1s full right moves %.0f px (screen 2160)\n",tx);
  mx.lx=0; mx.buttons=BTN_A; md.step(mx,s,0,0.016f,2160,o); printf("A down -> leftDown=%d\n",o.leftDown);
  mx.buttons=0; md.step(mx,s,0,0.016f,2160,o); printf("A up -> leftUp=%d\n",o.leftUp);
  mx.buttons=BTN_B; md.step(mx,s,0,0.016f,2160,o); printf("B -> escape=%d\n",o.escape);

  // D-pad navigation: first press Tab + park, then arrows; A = Enter while navigating
  MenuDriver nd; XState nx; nx.connected=true; MenuOut no;
  nx.buttons=DPAD_DOWN; nd.step(nx,s,0,0.016f,2160,no); printf("first down: nkeys=%d vk=%02x park=%d\n",no.nkeys,no.nkeys?no.keys[0].vk:0,no.park);
  nx.buttons=0; nd.step(nx,s,0,0.016f,2160,no);
  nx.buttons=DPAD_DOWN; nd.step(nx,s,0,0.016f,2160,no); printf("second down: vk=%02x\n",no.nkeys?no.keys[0].vk:0);
  int reps=0; for(int i=0;i<60;i++){ nd.step(nx,s,0,1/60.f,2160,no); reps+=no.nkeys; } printf("held 1 s: %d repeats\n",reps);
  nx.buttons=BTN_A; nd.step(nx,s,0,0.016f,2160,no); printf("A in nav: vk=%02x down=%d up=%d click=%d\n",no.keys[0].vk,no.keys[0].down,no.keys[0].up,no.leftDown);
  nx.buttons=0; nd.step(nx,s,0,0.016f,2160,no); printf("A up in nav: vk=%02x down=%d up=%d\n",no.keys[0].vk,no.keys[0].down,no.keys[0].up);
  nx.lx=32767; nd.step(nx,s,0,0.016f,2160,no); nx.lx=0; nx.buttons=BTN_A; nd.step(nx,s,0,0.016f,2160,no); printf("A after stick: click=%d nkeys=%d\n",no.leftDown,no.nkeys);
}
