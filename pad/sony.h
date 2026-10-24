// DualShock 4 and DualSense input reports (USB and Bluetooth) -> pad::XState, plus the output
// report that sets the light bar. Platform independent; the HID I/O lives in dinput8.cpp.
#pragma once
#include <cstdint>
#include <cstring>
#include "padcore.h"

namespace sony {

enum Model { NONE = 0, DS4 = 1, DUALSENSE = 2 };

inline Model modelOf(uint16_t vid, uint16_t pid) {
    if (vid != 0x054C) return NONE;
    switch (pid) {
    case 0x05C4: case 0x09CC: case 0x0BA0: return DS4;          // DS4 v1, v2, USB wireless adapter
    case 0x0CE6: case 0x0DF2: return DUALSENSE;                   // DualSense, DualSense Edge
    }
    return NONE;
}

// stick byte 0..255 (128 centre, Y grows downwards) -> XInput range (Y grows upwards)
inline int16_t sx(uint8_t v) { int r = ((int)v - 128) * 258; return (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r); }
inline int16_t sy(uint8_t v) { int r = (128 - (int)v) * 258; return (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r); }

inline uint16_t hat(uint8_t h) {
    static const uint16_t m[8] = { pad::DPAD_UP, pad::DPAD_UP | pad::DPAD_RIGHT, pad::DPAD_RIGHT, pad::DPAD_RIGHT | pad::DPAD_DOWN,
                                   pad::DPAD_DOWN, pad::DPAD_DOWN | pad::DPAD_LEFT, pad::DPAD_LEFT, pad::DPAD_LEFT | pad::DPAD_UP };
    return h < 8 ? m[h] : 0;
}

// Common block (both pads): sticks, face buttons, shoulders, options/share, thumbs.
// b0: hat(0-3) square(4) cross(5) circle(6) triangle(7); b1: L1 R1 L2 R2 share/create options L3 R3
inline void buttons(uint8_t b0, uint8_t b1, uint8_t ps, pad::XState& x) {
    uint16_t b = hat(b0 & 15);
    if (b0 & 0x10) b |= pad::BTN_X;       // square
    if (b0 & 0x20) b |= pad::BTN_A;       // cross
    if (b0 & 0x40) b |= pad::BTN_B;       // circle
    if (b0 & 0x80) b |= pad::BTN_Y;       // triangle
    if (b1 & 0x01) b |= pad::LSHOULDER;
    if (b1 & 0x02) b |= pad::RSHOULDER;
    if (b1 & 0x10) b |= pad::BACK;        // share / create
    if (b1 & 0x20) b |= pad::START;       // options
    if (b1 & 0x40) b |= pad::LTHUMB;
    if (b1 & 0x80) b |= pad::RTHUMB;
    if (ps & 0x02) b |= pad::BACK;        // touchpad click works as Back too
    x.buttons = b;
}

// Bluetooth "simple" report 0x01 (both pads until switched to full reports):
// LX LY RX RY b0 b1 b2(ps,tpad) L2 R2. Windows pads it to the full report length.
inline void parseSimple(const uint8_t* r, pad::XState& x) {
    x.lx = sx(r[1]); x.ly = sy(r[2]); x.rx = sx(r[3]); x.ry = sy(r[4]);
    buttons(r[5], r[6], r[7], x); x.lt = r[8]; x.rt = r[9];
    x.connected = true;
}

// Parse one input report (first byte = report id). The transport decides the layout of report
// 0x01: on USB it is the full report, over Bluetooth the simple one (whatever length Windows gives).
inline bool parse(Model m, const uint8_t* r, size_t n, pad::XState& x, bool bluetooth) {
    if (n < 10) return false;
    if (bluetooth && r[0] == 0x01) { parseSimple(r, x); return true; }
    const uint8_t* d = nullptr;
    if (m == DS4) {
        if (r[0] == 0x01) d = r + 1;                 // USB, or Bluetooth "simple" report: same layout
        else if (r[0] == 0x11 && n >= 13) d = r + 3; // Bluetooth full report
        if (!d) return false;
        // d: LX LY RX RY b0 b1 (ps,tpad,counter) L2 R2
        x.lx = sx(d[0]); x.ly = sy(d[1]); x.rx = sx(d[2]); x.ry = sy(d[3]);
        buttons(d[4], d[5], d[6], x); x.lt = d[7]; x.rt = d[8];
        x.connected = true; return true;
    }
    if (m == DUALSENSE) {
        if (r[0] == 0x01 && n >= 11) d = r + 1;                    // USB
        else if (r[0] == 0x31 && n >= 12) d = r + 2;               // Bluetooth full report
        if (!d) return false;
        // d: LX LY RX RY L2 R2 counter b0 b1 b2
        x.lx = sx(d[0]); x.ly = sy(d[1]); x.rx = sx(d[2]); x.ry = sy(d[3]);
        x.lt = d[4]; x.rt = d[5];
        buttons(d[7], d[8], d[9], x);
        x.connected = true; return true;
    }
    return false;
}

inline uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
    return c;
}

// DualSense Bluetooth output reports carry a 4-bit sequence number: a report that repeats the last
// number may be dropped by the pad (a "motors off" lost that way leaves the motors running).
inline uint8_t btSeq() { static uint8_t s = 0; s = (uint8_t)((s + 1) & 15); return (uint8_t)(s << 4); }

// Output report that sets the light bar colour (no rumble). Returns its length.
inline size_t lightbar(Model m, bool bluetooth, uint8_t R, uint8_t G, uint8_t B, uint8_t* out, size_t cap) {
    memset(out, 0, cap);
    if (m == DS4) {
        if (!bluetooth) { if (cap < 32) return 0; out[0] = 0x05; out[1] = 0x02; out[6] = R; out[7] = G; out[8] = B; return 32; }
        if (cap < 78) return 0;
        out[0] = 0x11; out[1] = 0xC0; out[3] = 0x02; out[8] = R; out[9] = G; out[10] = B;
        uint8_t seed = 0xA2; uint32_t c = crc32(&seed, 1); c = crc32(out, 74, c) ^ 0xFFFFFFFFu;
        memcpy(out + 74, &c, 4); return 78;
    }
    if (m == DUALSENSE) {
        uint8_t* p; size_t len;
        if (!bluetooth) { if (cap < 48) return 0; out[0] = 0x02; p = out + 1; len = 48; }
        else { if (cap < 78) return 0; out[0] = 0x31; out[1] = btSeq(); out[2] = 0x10; p = out + 3; len = 78; }   // seq, tag
        p[1] = 0x04;                  // valid_flag1: light bar colour
        p[38] = 0x02;                 // valid_flag2: light bar setup -> fade the default blue out
        p[41] = 0x02;                 // light bar setup: light on
        p[44] = R; p[45] = G; p[46] = B;
        if (bluetooth) { uint8_t seed = 0xA2; uint32_t c = crc32(&seed, 1); c = crc32(out, 74, c) ^ 0xFFFFFFFFu; memcpy(out + 74, &c, 4); }
        return len;
    }
    return 0;
}

// Output report that runs the two motors (low = big/left, high = small/right, 0..255) and leaves the
// light bar alone. DualSense: "compatible vibration" (the motor emulation of its haptic actuators).
inline size_t rumble(Model m, bool bluetooth, uint8_t low, uint8_t high, uint8_t* out, size_t cap) {
    memset(out, 0, cap);
    if (m == DS4) {
        if (!bluetooth) { if (cap < 32) return 0; out[0] = 0x05; out[1] = 0x01; out[4] = high; out[5] = low; return 32; }
        if (cap < 78) return 0;
        out[0] = 0x11; out[1] = 0xC0; out[3] = 0x01; out[6] = high; out[7] = low;
        uint8_t seed = 0xA2; uint32_t c = crc32(&seed, 1); c = crc32(out, 74, c) ^ 0xFFFFFFFFu;
        memcpy(out + 74, &c, 4); return 78;
    }
    if (m == DUALSENSE) {
        uint8_t* p; size_t len;
        if (!bluetooth) { if (cap < 48) return 0; out[0] = 0x02; p = out + 1; len = 48; }
        else { if (cap < 78) return 0; out[0] = 0x31; out[1] = btSeq(); out[2] = 0x10; p = out + 3; len = 78; }
        p[0] = 0x03;                  // valid_flag0: compatible vibration + haptics select
        p[2] = high; p[3] = low;      // right (small) and left (big) motor
        p[38] = 0x04;                 // valid_flag2: compatible vibration v2 (newer firmware)
        if (bluetooth) { uint8_t seed = 0xA2; uint32_t c = crc32(&seed, 1); c = crc32(out, 74, c) ^ 0xFFFFFFFFu; memcpy(out + 74, &c, 4); }
        return len;
    }
    return 0;
}

// DualSense adaptive triggers: one effect per trigger (11 bytes each, the classic modes every
// firmware knows). effect[0]: 0x05 off, 0x01 continuous resistance (start, force),
// 0x02 section resistance (start, end, force). Positions and force 0..255.
struct TriggerFx { uint8_t b[11] = { 0x05 }; };
inline TriggerFx fxOff() { return TriggerFx(); }
inline TriggerFx fxResist(uint8_t start, uint8_t force) { TriggerFx f; f.b[0] = 0x01; f.b[1] = start; f.b[2] = force; return f; }
inline TriggerFx fxSection(uint8_t start, uint8_t end, uint8_t force) { TriggerFx f; f.b[0] = 0x02; f.b[1] = start; f.b[2] = end; f.b[3] = force; return f; }
inline size_t triggers(Model m, bool bluetooth, const TriggerFx& left, const TriggerFx& right, uint8_t* out, size_t cap) {
    memset(out, 0, cap);
    if (m != DUALSENSE) return 0;
    uint8_t* p; size_t len;
    if (!bluetooth) { if (cap < 48) return 0; out[0] = 0x02; p = out + 1; len = 48; }
    else { if (cap < 78) return 0; out[0] = 0x31; out[1] = btSeq(); out[2] = 0x10; p = out + 3; len = 78; }
    p[0] = 0x0C;                  // valid_flag0: right (0x04) and left (0x08) trigger effects
    memcpy(p + 10, right.b, 11); memcpy(p + 21, left.b, 11);
    if (bluetooth) { uint8_t seed = 0xA2; uint32_t c = crc32(&seed, 1); c = crc32(out, 74, c) ^ 0xFFFFFFFFu; memcpy(out + 74, &c, 4); }
    return len;
}

}  // namespace sony
