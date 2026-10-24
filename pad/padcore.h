// ShrekPad core: turns an XInput gamepad state into the DirectInput joystick state that the
// game's WinDrv.dll polls, plus mouse/keyboard actions for the menus. Platform independent, so
// the mapping is unit-tested on Linux; dinput8.cpp is the Windows glue.
//
// What WinDrv reads from DIJOYSTATE2 (range 0..65535, centre 32768):
//   lX -> JoyX, lY -> JoyY, lZ -> JoyZ, lRx -> JoyR, lRy -> JoyU, lRz -> JoyV,
//   rgbButtons[0..11] -> Joy1..Joy12, rgdwPOV[0] -> Joy13 up / Joy14 right / Joy15 down / Joy16 left.
#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace pad {

// XInput layout (same bits as XINPUT_GAMEPAD)
enum : uint16_t {
    DPAD_UP = 0x0001, DPAD_DOWN = 0x0002, DPAD_LEFT = 0x0004, DPAD_RIGHT = 0x0008,
    START = 0x0010, BACK = 0x0020, LTHUMB = 0x0040, RTHUMB = 0x0080,
    LSHOULDER = 0x0100, RSHOULDER = 0x0200, BTN_A = 0x1000, BTN_B = 0x2000, BTN_X = 0x4000, BTN_Y = 0x8000,
};
struct XState { uint16_t buttons = 0; uint8_t lt = 0, rt = 0; int16_t lx = 0, ly = 0, rx = 0, ry = 0; bool connected = false; };

struct JoyState {            // the part of DIJOYSTATE2 that WinDrv reads
    int32_t lX, lY, lZ, lRx, lRy, lRz;
    int32_t slider[2];
    uint32_t pov[4];
    uint8_t buttons[128];
};

struct Settings {
    float deadzoneLeft = 0.20f, deadzoneRight = 0.18f;   // fraction of full deflection, radial
    float triggerThreshold = 0.15f;
    float cameraSpeed = 0.8f;           // 1.0 = about 200 degrees per second at full deflection (with SpeedBase=54)
    bool frameComp = true;              // see toJoy()
    float cameraCurve = 1.6f;           // response exponent of the right stick (precise small movements)
    bool invertY = false;
    bool swapAB = false;                // Nintendo-style face buttons
    float cursorSpeed = 1.0f;           // menu cursor, screen heights per second at full deflection
};

// radial dead zone with rescaling: output 0..1 magnitude, direction preserved
inline void stick(int16_t x, int16_t y, float dz, float curve, float& ox, float& oy) {
    float fx = std::max(-1.f, x / 32767.f), fy = std::max(-1.f, y / 32767.f);
    float m = std::sqrt(fx * fx + fy * fy);
    if (m <= dz || m <= 0) { ox = oy = 0; return; }
    float n = std::min(1.f, (m - dz) / (1.f - dz));
    if (curve != 1.f) n = std::pow(n, curve);
    ox = fx / m * n; oy = fy / m * n;
}
inline int32_t axis(float v) {   // -1..1 -> 0..65535
    v = std::clamp(v, -1.f, 1.f);
    return (int32_t)std::lround(32767.5f + v * 32767.5f);
}

struct Mapper {
    Settings s;
    uint16_t maskUntilRelease = 0;     // buttons held during a menu <-> game switch stay silent until released
    bool lastMenu = false;

    // gameplay: fill the joystick state
    // The game turns the camera by aTurn * DeltaTime twice (ShPlayerInput scales the axis by
    // DeltaTime, BaseCam.ApplyMouseXToDestYaw multiplies by DeltaTime again), so a constant stick
    // angle turns slower the higher the frame rate. The right stick is therefore sent as
    // value * (DT_REF / frame time): the camera speed is then the same at 30, 60 or 144 FPS.
    // DT_REF is small enough to keep the value inside the DirectInput range up to ~400 FPS.
    static constexpr float DT_REF = 1.f / 400.f;
    void toJoy(const XState& x, JoyState& j, float dt = 1.f / 60.f) {
        memset(&j, 0, sizeof j);
        j.lX = j.lY = j.lZ = j.lRx = j.lRy = j.lRz = 32768; j.slider[0] = j.slider[1] = 0;
        for (auto& p : j.pov) p = 0xFFFFFFFFu;
        if (!x.connected) return;
        float lx, ly, rx, ry;
        stick(x.lx, x.ly, s.deadzoneLeft, 1.f, lx, ly);
        stick(x.rx, x.ry, s.deadzoneRight, s.cameraCurve, rx, ry);
        // the camera binding uses SpeedBase, which is already frame-rate independent in this engine
        float comp = 1.f; (void)dt;
        rx *= s.cameraSpeed * comp; ry *= s.cameraSpeed * comp;
        if (s.invertY) ry = -ry;
        // DirectInput Y grows downwards, XInput upwards
        j.lX = axis(lx); j.lY = axis(-ly); j.lRx = axis(rx); j.lRy = axis(-ry);
        float lt = x.lt / 255.f, rt = x.rt / 255.f;
        j.lZ = axis(lt); j.lRz = axis(rt);
        j.slider[0] = (int32_t)(lt * 65535); j.slider[1] = (int32_t)(rt * 65535);
        uint16_t b = x.buttons & ~maskUntilRelease;
        uint16_t a = s.swapAB ? BTN_B : BTN_A, bb = s.swapAB ? BTN_A : BTN_B;
        const uint16_t order[10] = { a, bb, BTN_X, BTN_Y, LSHOULDER, RSHOULDER, BACK, START, LTHUMB, RTHUMB };
        for (int i = 0; i < 10; i++) j.buttons[i] = (b & order[i]) ? 0x80 : 0;
        bool ltOn = lt > s.triggerThreshold && !(maskUntilRelease & 0x400), rtOn = rt > s.triggerThreshold && !(maskUntilRelease & 0x800);
        j.buttons[10] = ltOn ? 0x80 : 0; j.buttons[11] = rtOn ? 0x80 : 0;
        // D-pad -> one POV direction (WinDrv maps it to Joy13..16); diagonals resolve to the vertical
        if (b & DPAD_UP) j.pov[0] = 0; else if (b & DPAD_DOWN) j.pov[0] = 18000;
        else if (b & DPAD_RIGHT) j.pov[0] = 9000; else if (b & DPAD_LEFT) j.pov[0] = 27000;
    }

    // which pad buttons are held, including triggers (bits 0x400/0x800)
    static uint16_t held(const XState& x, float thr) {
        uint16_t h = x.buttons & 0xF3FF;
        if (x.lt / 255.f > thr) h |= 0x400;
        if (x.rt / 255.f > thr) h |= 0x800;
        return h;
    }
    // call on every poll with the current mode; masks what is held across a mode switch
    void setMode(bool menu, const XState& x) {
        uint16_t h = held(x, s.triggerThreshold);
        if (menu != lastMenu) maskUntilRelease = h;
        maskUntilRelease &= h;
        lastMenu = menu;
    }
};

// Menu mode. The menus of the game are GUI pages that also take the keyboard: the arrow keys move the
// focus from button to button (the focused one gets the same highlight as under the mouse), Enter
// presses it, Tab / Shift+Tab step through everything, Left / Right turn sliders and lists.
//   D-pad          arrow keys (held: repeats). The first press in a menu sends Tab instead (nothing
//                  has the focus yet) and parks the mouse cursor in a corner, so only one thing is lit.
//   LB / RB        Shift+Tab / Tab
//   A              Enter while navigating with the D-pad, a left click after the stick moved the cursor
//   B, Start, Back Escape
//   left stick     the cursor (mouse), as before; right stick scrolls lists
enum : uint8_t { K_TAB = 0x09, K_RETURN = 0x0D, K_SHIFT = 0x10, K_LEFT = 0x25, K_UP = 0x26, K_RIGHT = 0x27, K_DOWN = 0x28 };
struct KeyTap { uint8_t vk; uint8_t down; uint8_t up; uint8_t shift; };   // down / up: send the press / release
struct MenuOut {
    float dx = 0, dy = 0; int leftDown = 0, leftUp = 0, escape = 0, wheel = 0;
    bool park = false;                  // move the cursor out of the way (bottom right corner)
    // focus navigation, carried out by the caller (cursor onto the next menu item, or keys)
    int nav = 0;                        // 1 up, 2 down, 3 left, 4 right, 5 next (RB), 6 previous (LB)
    int select = 0;                     // 1 press, 2 release (A while navigating)
    bool navStart = false;              // navigation begins: put the cursor on the first item
    KeyTap keys[4]; int nkeys = 0;
    void tap(uint8_t vk, bool shift = false) { if (nkeys < 4) keys[nkeys++] = KeyTap{ vk, 1, 1, (uint8_t)shift }; }
    void press(uint8_t vk, bool down) { if (nkeys < 4) keys[nkeys++] = KeyTap{ vk, (uint8_t)down, (uint8_t)!down, 0 }; }
};

struct MenuDriver {
    // Menus are driven like a console game: the left stick and the D-pad move from item to item
    // (MenuOut.nav: the caller puts the cursor onto the next item of the game's menu page), A
    // presses it, B goes back. The right stick still moves the cursor freely.
    uint16_t prev = 0, stickPrev = 0;
    float accx = 0, accy = 0;
    bool nav = false;                   // keyboard focus in use: A is Enter
    bool enterHeld = false, clickHeld = false;
    uint16_t repDir = 0; float repT = 0;
    bool cursorUsed = false;            // the right stick moved the cursor since the menu opened
    void leave() { prev = 0; stickPrev = 0; nav = false; cursorUsed = false; repDir = 0; enterHeld = clickHeld = false; }
    void step(const XState& x, const Settings& s, uint16_t mask, float dt, float screenH, MenuOut& o) {
        o = MenuOut();
        if (!x.connected) { prev = 0; stickPrev = 0; return; }
        float lx, ly, rx, ry;
        stick(x.lx, x.ly, s.deadzoneLeft, 1.f, lx, ly);
        stick(x.rx, x.ry, s.deadzoneRight, 1.f, rx, ry);
        // right stick: the cursor (fallback)
        float vx = rx, vy = -ry;
        float m = std::sqrt(vx * vx + vy * vy);
        if (m > 0) {
            float speed = s.cursorSpeed * screenH * (0.15f + 0.85f * m * m) / m;   // fine near the centre, fast at full tilt
            accx += vx * speed * dt; accy += vy * speed * dt;
            if (m > 0.25f) { nav = false; cursorUsed = true; }     // the cursor is back in use
        }
        o.dx = std::trunc(accx); o.dy = std::trunc(accy); accx -= o.dx; accy -= o.dy;
        // left stick: a direction once it is pushed past 60% (with hysteresis: back under 40% releases it)
        uint16_t sd = 0;
        {
            float ax = std::fabs(lx), ay = std::fabs(ly), mm = std::max(ax, ay);
            float need = stickPrev ? 0.4f : 0.6f;
            if (mm > need) sd = ax > ay ? (lx > 0 ? DPAD_RIGHT : DPAD_LEFT) : (ly > 0 ? DPAD_UP : DPAD_DOWN);
            stickPrev = sd;
        }
        uint16_t b = (x.buttons & ~mask) | sd;
        uint16_t a = s.swapAB ? BTN_B : BTN_A, bk = s.swapAB ? BTN_A : BTN_B;
        uint16_t pressed = b & ~prev, released = prev & ~b;
        // directions: one at a time, repeating while held (0.4 s, then every 0.12 s)
        const uint16_t dirs = DPAD_UP | DPAD_DOWN | DPAD_LEFT | DPAD_RIGHT;
        uint16_t d = b & dirs;
        uint16_t fire = 0;
        if (pressed & dirs) { repDir = pressed & dirs; repDir &= (uint16_t)-(int16_t)repDir; repT = 0.4f; fire = repDir; }   // lowest bit
        else if (repDir && (d & repDir)) { repT -= dt; if (repT <= 0) { repT += 0.12f; fire = repDir; } }
        else repDir = 0;
        if (fire) {
            if (!nav) { nav = true; o.navStart = true; }
            else o.nav = fire == DPAD_UP ? 1 : fire == DPAD_DOWN ? 2 : fire == DPAD_LEFT ? 3 : 4;
        }
        if (pressed & LSHOULDER) { if (!nav) { nav = true; o.navStart = true; } else o.nav = 6; }
        if (pressed & RSHOULDER) { if (!nav) { nav = true; o.navStart = true; } else o.nav = 5; }
        if (pressed & a) {
            if (nav) { o.select = 1; enterHeld = true; }
            else if (cursorUsed) { o.leftDown = 1; clickHeld = true; }
            else { nav = true; o.navStart = true; }   // nothing chosen yet: the first A goes to the first item
        }
        if (released & a) {
            if (enterHeld) { o.select = 2; enterHeld = false; }
            if (clickHeld) { o.leftUp = 1; clickHeld = false; }
        }
        if (pressed & (bk | START | BACK)) o.escape = 1;
        prev = b;
    }
};

}  // namespace pad
