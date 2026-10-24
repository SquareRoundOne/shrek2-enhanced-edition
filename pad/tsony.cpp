#include "sony.h"
#include <cstdio>
using namespace sony;
static void show(const char* t, const pad::XState& x){ printf("%-22s c=%d LX=%6d LY=%6d RX=%6d RY=%6d LT=%3d RT=%3d btn=%04x\n",t,x.connected,x.lx,x.ly,x.rx,x.ry,x.lt,x.rt,x.buttons); }
int main(){
  uint8_t r[80]={0}; pad::XState x;
  // DS4 USB: stick left full, up full; cross+L1+options, dpad right, L2 255
  r[0]=1; r[1]=0; r[2]=0; r[3]=128; r[4]=128; r[5]=0x20|2; r[6]=0x01|0x20; r[7]=0; r[8]=255; r[9]=0;
  parse(DS4,r,64,x,false); show("DS4 USB",x);
  memset(r,0,80); r[0]=0x11; r[4]=255; r[5]=128; r[6]=128; r[7]=128; r[8]=8|0x80; r[9]=0x10; r[10]=0x02; r[12]=200;
  parse(DS4,r,78,x,true); show("DS4 BT (0x11)",x);
  memset(r,0,80); r[0]=1; r[1]=128; r[2]=128; r[3]=255; r[4]=0; r[5]=10; r[6]=250; r[8]=0x40|0; r[9]=0x80; r[10]=1;
  parse(DUALSENSE,r,64,x,false); show("DualSense USB",x);
  memset(r,0,80); r[0]=0x31; r[2]=128; r[3]=128; r[4]=128; r[5]=128; r[9]=8|0x10;
  parse(DUALSENSE,r,78,x,true); show("DualSense BT (0x31)",x);
  uint8_t o[80]; size_t n=lightbar(DUALSENSE,true,40,170,20,o,80); printf("DS BT out %zu: id=%02x tag=%02x flags1=%02x rgb=%d,%d,%d crc=%02x%02x%02x%02x\n",n,o[0],o[2],o[4],o[47],o[48],o[49],o[74],o[75],o[76],o[77]);
  n=lightbar(DS4,false,40,170,20,o,80); printf("DS4 USB out %zu: %02x %02x rgb=%d,%d,%d\n",n,o[0],o[1],o[6],o[7],o[8]);
  // crc self-check: known crc32("123456789") = cbf43926
  const uint8_t t[]="123456789"; printf("crc32 check %08x\n", crc32(t,9)^0xFFFFFFFFu);
}
// BT simple report from the log: id 01 padded to 78 bytes, cross pressed, left stick right
int main2(){ uint8_t r[78]={0}; r[0]=1; r[1]=255; r[2]=128; r[3]=128; r[4]=128; r[5]=8|0x20; r[6]=0x20; r[8]=0; r[9]=255;
 pad::XState x; parse(DUALSENSE,r,78,x,true); show("DualSense BT simple",x); return 0; }
struct R{ R(){ main2(); } } runit;
