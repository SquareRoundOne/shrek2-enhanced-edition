// ShrekPad: dinput8.dll for Shrek 2 (System folder). Forwards everything to the system DirectInput
// and adds one virtual game controller fed by XInput (Xbox pads, and PlayStation pads through Steam
// Input / DS4Windows). WinDrv.dll already polls a DirectInput joystick every frame; it simply gets
// ours: radial dead zones, separate triggers, hot-plug. While a menu is open (the game shows the
// Windows cursor then) the pad drives the mouse instead: sticks / D-pad move it, A clicks, B is Esc.
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cstdio>
#include <cwchar>
#include <vector>
#include <mutex>
#include "padcore.h"
#include "sony.h"
#include "../launcher/pkg.h"
#include <setupapi.h>
#include <shlobj.h>
#include <atomic>
#include <thread>
#include <mutex>
extern "C" {
#include <hidsdi.h>
}
#include <winsock2.h>

// {5368524B-5041-4421-9E2E-584950414400} "ShRKPAD"
static const GUID GUID_ShrekPad = { 0x5368524B, 0x5041, 0x4421, { 0x9E, 0x2E, 0x58, 0x49, 0x50, 0x41, 0x44, 0x00 } };

// ------------------------------------------------------------------ XInput (loaded at runtime)
struct XINPUT_GAMEPAD_ { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; };
struct XINPUT_STATE_ { DWORD dwPacketNumber; XINPUT_GAMEPAD_ Gamepad; };
typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE_*);
struct XINPUT_VIBRATION_ { WORD wLeftMotorSpeed, wRightMotorSpeed; };
typedef DWORD(WINAPI* XInputSetState_t)(DWORD, XINPUT_VIBRATION_*);
static XInputGetState_t g_xget;
static XInputSetState_t g_xset;
static std::atomic<int> g_xIndex{ -1 };            // the XInput pad being read
// vibration: what the game asks for (0..1, already scaled by the strength setting)
static float g_rumbleStrength = 0.7f;
static int g_adaptive = 1;                       // DualSense adaptive triggers: 0 off, 1 on
static int g_trR2 = 10, g_trR2Point = 0, g_trL2 = 5, g_trSmall = 40;   // g_trSmall: % of the force while playing Donkey or Puss (lighter heroes)
static std::atomic<bool> g_heroByClass{ false };   // the hero is known from the player's pawn: the guess from the sounds is not needed
static std::atomic<int> g_hero{ 0 };   // 0 Shrek (and the rest), 1 Donkey, 2 Puss - told by shrz.dll from the hero's own sounds    // force of R2 / L2 (%), where the R2 click starts (% of the travel, 0: no click, smooth)
static bool g_dsx = true;                        // ... also through DSX
static std::atomic<int> g_rumLow{ 0 }, g_rumHigh{ 0 };   // 0..255
static wchar_t g_dir[MAX_PATH];
static FILE* g_log;
static pad::Settings g_set;
static bool g_enabled = true;
static bool g_sonyEnabled = true, g_lightbar = true, g_cursorOn = true, g_rawMouse = true;
static float g_mouseSens = 1.5f;
static uint8_t g_lbR = 40, g_lbG = 170, g_lbB = 20;
static int g_lbMode = 3;                       // light bar: 0 fixed colour, 1 health, 2 hero, 3 hero + health
static std::atomic<uint32_t> g_lbWant{ 0xFFFFFFFFu };   // RGB the light bar should show now (none: the fixed colour)

static std::mutex g_logMu;
static void logf(const char* fmt, ...) {
    if (!g_log) return;
    std::lock_guard<std::mutex> l(g_logMu);
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d  ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fputc('\n', g_log); fflush(g_log);
}

static float iniF(const wchar_t* key, float def) {
    wchar_t buf[64], path[MAX_PATH]; swprintf(path, MAX_PATH, L"%lsShrekPad.ini", g_dir);
    wchar_t d[32]; swprintf(d, 32, L"%g", def);
    GetPrivateProfileStringW(L"ShrekPad", key, d, buf, 64, path);
    return (float)_wtof(buf);
}
static void loadSettings() {
    g_enabled = iniF(L"Enabled", 1) != 0;
    g_set.deadzoneLeft = iniF(L"DeadzoneLeft", g_set.deadzoneLeft);
    g_set.deadzoneRight = iniF(L"DeadzoneRight", g_set.deadzoneRight);
    g_set.triggerThreshold = iniF(L"TriggerThreshold", g_set.triggerThreshold);
    // CameraSensitivity: camera speed relative to the arrow keys (1.0 = same, default 1.3, up to 2.5).
    // The binding is "Axis aTurn SpeedBase=8"; a full stick sends 0.5 * cameraSpeed.
    g_set.cameraSpeed = std::min(1.f, std::max(0.05f, 0.4f * iniF(L"CameraSensitivity", 1.3f)));
    g_set.cameraCurve = iniF(L"CameraCurve", g_set.cameraCurve);
    g_set.invertY = iniF(L"InvertY", 0) != 0;
    g_set.swapAB = iniF(L"SwapAB", 0) != 0;
    g_set.cursorSpeed = iniF(L"CursorSpeed", g_set.cursorSpeed);
    g_set.frameComp = iniF(L"CameraFrameCompensation", 1) != 0;
    g_sonyEnabled = iniF(L"Sony", 1) != 0;
    g_lightbar = iniF(L"Lightbar", 1) != 0;
    g_lbMode = std::min(3, std::max(0, (int)iniF(L"LightbarMode", 3)));
    g_cursorOn = iniF(L"GameCursor", 1) != 0;
    g_rawMouse = iniF(L"RawMouse", 1) != 0;
    g_mouseSens = std::min(10.f, std::max(0.1f, iniF(L"MouseSensitivity", 1.5f)));
    g_lbR = (uint8_t)iniF(L"LightbarR", g_lbR); g_lbG = (uint8_t)iniF(L"LightbarG", g_lbG); g_lbB = (uint8_t)iniF(L"LightbarB", g_lbB);
    g_rumbleStrength = std::min(1.f, std::max(0.f, iniF(L"Rumble", 70) / 100.f));
    g_adaptive = iniF(L"AdaptiveTriggers", 1) != 0 ? 1 : 0;
    g_trR2 = std::min(100, std::max(0, (int)iniF(L"TriggerR2", 10)));
    g_trR2Point = std::min(90, std::max(0, (int)iniF(L"TriggerR2Point", 0)));
    g_trL2 = std::min(100, std::max(0, (int)iniF(L"TriggerL2", 5)));
    g_trSmall = std::min(100, std::max(0, (int)iniF(L"TriggerSmallHeroes", 40)));
    g_dsx = iniF(L"DSX", 1) != 0;
}

static void startSony();
static bool gameCaptured();

// ------------------------------------------------------------------ DualSense adaptive triggers
// In gameplay R2 (punch) gets a short section of resistance - a click to press through - and L2
// (walk) a light constant resistance; menus, other windows and the end of the game release them.
// Directly over HID when ShrekPad reads the DualSense itself, otherwise through DSX (DualSenseX,
// UDP on 127.0.0.1, the port DSX writes to DualSenseX_PortNumber.txt, default 6969).
static std::atomic<int> g_trigWant{ 0 };         // what the triggers should do now (0 = released)
static std::atomic<bool> g_sonyDirect{ false };  // a DualSense is open directly (the HID thread sends)
// forces for the hero now played: Donkey and Puss get a share of them
static int heroScale() { return g_hero.load() ? g_trSmall : 100; }
static int forceR2() { int f = g_trR2 * heroScale() / 100; return g_trR2 > 0 && heroScale() > 0 ? std::max(f, 1) : 0; }
static int forceL2() { int f = g_trL2 * heroScale() / 100; return g_trL2 > 0 && heroScale() > 0 ? std::max(f, 1) : 0; }
static void trigFx(int mode, sony::TriggerFx& l, sony::TriggerFx& r) {
    l = sony::fxOff(); r = sony::fxOff();
    if (mode <= 0) return;
    int R2 = forceR2(), L2 = forceL2();
    if (R2 > 0) {
        if (g_trR2Point > 0) {   // R2: a section of resistance - the click - from TriggerR2Point on
            int st = g_trR2Point * 255 / 100, en = std::min(255, st + 0x30);
            r = sony::fxSection((uint8_t)st, (uint8_t)en, (uint8_t)std::max(1, R2 * 255 / 100));
        } else r = sony::fxResist(0x10, (uint8_t)std::max(1, R2 * 255 / 100));   // smooth: the same light resistance all the way
    }
    if (L2 > 0) l = sony::fxResist(0x20, (uint8_t)std::max(1, L2 * 255 / 100));
}
static SOCKET g_udp = INVALID_SOCKET; static sockaddr_in g_dsxAddr;
static bool dsxOpen() {
    if (g_udp != INVALID_SOCKET) return true;
    static bool failed = false; if (failed) return false;
    WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w)) { failed = true; return false; }
    g_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udp == INVALID_SOCKET) { failed = true; return false; }
    u_long nb = 1; ioctlsocket(g_udp, FIONBIO, &nb);
    int port = 6969;
    wchar_t tmp[MAX_PATH], path[MAX_PATH];
    const wchar_t* files[2] = { L"%lsDualSenseX\\DualSenseX_PortNumber.txt", L"C:\\Temp\\DualSenseX\\DualSenseX_PortNumber.txt" };
    GetTempPathW(MAX_PATH, tmp);
    for (auto f : files) {
        swprintf(path, MAX_PATH, f, tmp);
        if (FILE* fp = _wfopen(path, L"r")) { int v = 0; if (fscanf(fp, "%d", &v) == 1 && v > 0 && v < 65536) port = v; fclose(fp); break; }
    }
    memset(&g_dsxAddr, 0, sizeof g_dsxAddr); g_dsxAddr.sin_family = AF_INET; g_dsxAddr.sin_port = htons((u_short)port);
    g_dsxAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    logf("DSX: adaptive triggers over UDP port %d", port);
    return true;
}
static void dsxSend(int mode) {
    if (!dsxOpen()) return;
    char b[256];
    // R2: SemiAutomaticGun (start 2-7, end 0-8, force 0-8), L2: Resistance (start 0-9, force 0-8); 0 = Normal
    int R2 = forceR2(), L2 = forceL2();
    int rf = mode > 0 ? (R2 * 8 + 50) / 100 : 0, lf = mode > 0 ? (L2 * 8 + 50) / 100 : 0;
    if (mode > 0 && R2 > 0 && !rf) rf = 1;
    if (mode > 0 && L2 > 0 && !lf) lf = 1;
    int rs = 2 + g_trR2Point * 5 / 100, re = std::min(8, rs + 2);
    char r2[64], l2[64];
    if (rf && g_trR2Point > 0) snprintf(r2, sizeof r2, "[0,2,16,%d,%d,%d]", rs, re, rf);
    else if (rf) snprintf(r2, sizeof r2, "[0,2,13,1,%d]", rf);
    else snprintf(r2, sizeof r2, "[0,2,0]");
    if (lf) snprintf(l2, sizeof l2, "[0,1,13,1,%d]", lf); else snprintf(l2, sizeof l2, "[0,1,0]");
    snprintf(b, sizeof b, "{\"instructions\":[{\"type\":1,\"parameters\":%s},{\"type\":1,\"parameters\":%s}]}", r2, l2);
    sendto(g_udp, b, (int)strlen(b), 0, (sockaddr*)&g_dsxAddr, sizeof g_dsxAddr);
}
static void dsxRelease() {
    if (!g_dsx || g_udp == INVALID_SOCKET) return;
    dsxSend(0);
}
// every poll of the game (its thread): triggers follow gameplay / menu / focus
static void trigUpdate(bool fg, bool inMenu) {
    int want = fg && !inMenu && g_enabled && g_adaptive > 0 && gameCaptured() ? g_adaptive : 0;
    g_trigWant = want ? want + g_hero.load() * 16 : 0;   // a new hero sends the triggers again
    static int dsxSent = -1; static ULONGLONG dsxAt = 0;
    if (g_dsx && g_adaptive > 0 && !g_sonyDirect) {
        ULONGLONG now = GetTickCount64();
        int w2 = g_trigWant.load();
        if (w2 != dsxSent || now - dsxAt > 3000) { dsxSend(want); dsxSent = w2; dsxAt = now; }   // again now and then: DSX may start later
    }
}

// what the player plays with right now (shrz.dll words the game's control hints for it): the last
// press on the pad or on the keyboard / mouse wins; before either, a connected pad
static std::atomic<ULONGLONG> g_lastPadUse{ 0 }, g_lastKbUse{ 0 }; static std::atomic<bool> g_padConnected{ false };
extern "C" __declspec(dllexport) int __cdecl ShrekPadUsingPad() {
    ULONGLONG p = g_lastPadUse.load(), k = g_lastKbUse.load();
    if (!p && !k) return g_padConnected.load() ? 1 : 0;
    return p > k ? 1 : 0;
}
static std::atomic<bool> g_inMenuNow{ false };   // a menu has the cursor (the speedrun timer of shrz.dll stops)
extern "C" __declspec(dllexport) int __cdecl ShrekPadInMenu() { return g_inMenuNow.load() ? 1 : 0; }
// Called by shrz.dll (the renderer) every frame with the vibration the game's sounds ask for.
// Only while the game window has the focus: no buzzing in the background.
static HWND g_hwnd;
extern "C" __declspec(dllexport) void __cdecl ShrekPadHero(int hero) {
    if (hero < 0 || hero > 2 || g_hero.load() == hero || g_heroByClass.load()) return;
    g_hero = hero;
    logf("hero: %s (triggers at %d%%)", hero == 1 ? "Donkey" : hero == 2 ? "Puss" : "Shrek", heroScale());
}
extern "C" __declspec(dllexport) void __cdecl ShrekPadRumble(float low, float high) {
    bool fg = g_hwnd && GetForegroundWindow() == g_hwnd;
    float s = fg && g_enabled ? g_rumbleStrength : 0.f;
    int L = (int)std::lround(std::min(1.f, std::max(0.f, low * s)) * 255), H = (int)std::lround(std::min(1.f, std::max(0.f, high * s)) * 255);
    // the motors barely turn below this; the end of a fading pulse would only buzz (the tail after a landing)
    if (L < 14) L = 0;
    if (H < 14) H = 0;
    if (L == g_rumLow.load() && H == g_rumHigh.load()) return;
    g_rumLow = L; g_rumHigh = H;
    int i = g_xIndex.load();
    if (g_xset && i >= 0) { XINPUT_VIBRATION_ v{ (WORD)(L * 257), (WORD)(H * 257) }; g_xset((DWORD)i, &v); }
}
static void initXInput() {
    static bool done = false; if (done) return; done = true;
    const wchar_t* names[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
    for (auto n : names) {
        HMODULE m = LoadLibraryW(n);
        if (m && (g_xget = (XInputGetState_t)GetProcAddress(m, "XInputGetState"))) { g_xset = (XInputSetState_t)GetProcAddress(m, "XInputSetState"); logf("XInput: %ls", n); break; }
    }
    if (!g_xget) logf("XInput not available");
    if (g_sonyEnabled) startSony();
}

// ------------------------------------------------------------------ DualShock 4 / DualSense over HID
// A background thread finds the first Sony pad (USB or Bluetooth), switches Bluetooth pads to full
// reports, sets the light bar and keeps the latest state. Shared access, so Steam can read it too.
static void writeReport(HANDLE h, const uint8_t* o, size_t n) {
    OVERLAPPED ov{}; ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); DWORD w = 0;
    if (n && !WriteFile(h, o, (DWORD)n, &w, &ov) && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 500) != WAIT_OBJECT_0) CancelIo(h);
    }
    CloseHandle(ov.hEvent);
}

struct SonyReader {
    std::mutex mu;
    std::mutex devMu;                    // current device, for restoring the light bar on exit
    HANDLE dev = INVALID_HANDLE_VALUE; sony::Model devModel = sony::NONE; bool devBt = false; bool restored = false;
    void restoreLightbar() {
        std::lock_guard<std::mutex> l(devMu);
        if (restored || dev == INVALID_HANDLE_VALUE || !g_lightbar) return;
        restored = true;
        // PlayStation default: blue
        uint8_t o[80]; size_t n = sony::rumble(devModel, devBt, 0, 0, o, sizeof o);
        writeReport(dev, o, n);
        n = sony::triggers(devModel, devBt, sony::fxOff(), sony::fxOff(), o, sizeof o);
        if (n) writeReport(dev, o, n);
        n = sony::lightbar(devModel, devBt, 0, 0, devModel == sony::DS4 ? 64 : 255, o, sizeof o);
        writeReport(dev, o, n);
        logf("light bar restored");
    }
    pad::XState state;
    std::atomic<ULONGLONG> lastReport{ 0 };
    std::atomic<bool> stop{ false };
    std::thread th;
    bool scanLogged = false;
    USHORT lastFeatLen = 0;

    void start() { th = std::thread([this]() { run(); }); th.detach(); }

    pad::XState get() {
        std::lock_guard<std::mutex> l(mu);
        pad::XState x = state;
        if (GetTickCount64() - lastReport.load() > 1000) x.connected = false;
        return x;
    }

    HANDLE openPad(sony::Model& model, bool& bt, USHORT& inLen) {
        GUID hid; HidD_GetHidGuid(&hid);
        HDEVINFO set = SetupDiGetClassDevsW(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
        HANDLE found = INVALID_HANDLE_VALUE;
        SP_DEVICE_INTERFACE_DATA ifd{ sizeof ifd };
        DWORD scanned = 0;
        for (DWORD i = 0; found == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(set, nullptr, &hid, i, &ifd); i++, scanned++) {
            DWORD need = 0; SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
            if (!need) continue;
            std::vector<uint8_t> buf(need);
            auto* det = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buf.data(); det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, nullptr, nullptr)) continue;
            bool sonyPath = wcsstr(det->DevicePath, L"054c") || wcsstr(det->DevicePath, L"054C");
            HANDLE h = CreateFileW(det->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (h == INVALID_HANDLE_VALUE && sonyPath) {
                DWORD e = GetLastError();
                // read-only access is enough for input (no light bar then)
                h = CreateFileW(det->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
                if (!scanLogged) logf("  Sony HID %ls: read/write open failed (error %lu), read-only %s", det->DevicePath, e, h == INVALID_HANDLE_VALUE ? "failed too" : "ok");
            }
            if (h == INVALID_HANDLE_VALUE) continue;
            HIDD_ATTRIBUTES a{ sizeof a };
            sony::Model m = HidD_GetAttributes(h, &a) ? sony::modelOf(a.VendorID, a.ProductID) : sony::NONE;
            PHIDP_PREPARSED_DATA pp = nullptr; HIDP_CAPS caps{};
            if (m != sony::NONE && HidD_GetPreparsedData(h, &pp)) { HidP_GetCaps(pp, &caps); HidD_FreePreparsedData(pp); }
            if (sonyPath && !scanLogged) logf("  HID %04x:%04x usage %04x:%04x input %u bytes -> %s", a.VendorID, a.ProductID, caps.UsagePage, caps.Usage, caps.InputReportByteLength,
                                              m == sony::NONE ? "not a supported pad" : "ok");
            if (m == sony::NONE || caps.InputReportByteLength < 10) { CloseHandle(h); continue; }
            model = m; inLen = caps.InputReportByteLength; bt = caps.InputReportByteLength != 64; lastFeatLen = caps.FeatureReportByteLength;
            logf("%s found (%s, %u-byte reports, %04x:%04x)", m == sony::DS4 ? "DualShock 4" : "DualSense", bt ? "Bluetooth" : "USB", inLen, a.VendorID, a.ProductID);
            found = h;
        }
        SetupDiDestroyDeviceInfoList(set);
        if (found == INVALID_HANDLE_VALUE && !scanLogged) logf("no DualShock 4 / DualSense found (checked %lu HID interfaces); scanning again every 2 s", scanned);
        scanLogged = true;
        return found;
    }

    void run() {
        while (!stop) {
            sony::Model model; bool bt = false; USHORT inLen = 0;
            HANDLE h = openPad(model, bt, inLen);
            USHORT featLen = lastFeatLen;
            if (h == INVALID_HANDLE_VALUE) { Sleep(2000); continue; }
            if (bt) {   // reading the calibration feature report switches the pad to full input reports
                std::vector<uint8_t> f(std::max<USHORT>(featLen, 64)); f[0] = model == sony::DS4 ? 0x02 : 0x05;
                BOOL ok = HidD_GetFeature(h, f.data(), featLen ? featLen : (model == sony::DS4 ? 37 : 41));
                logf("  feature report %02x (%u bytes): %s", f[0], featLen, ok ? "ok" : "failed");
            }
            if (g_lightbar) {
                uint8_t o[80]; size_t n = sony::lightbar(model, bt, g_lbR, g_lbG, g_lbB, o, sizeof o);
                writeReport(h, o, n);
            }
            { std::lock_guard<std::mutex> l(devMu); dev = h; devModel = model; devBt = bt; restored = false; }
            std::vector<uint8_t> rep(std::max<USHORT>(inLen, 80));
            int reports = 0; uint8_t firstId = 0; int sentLow = 0, sentHigh = 0, sentTrig = -1, offAgain = 0; uint32_t sentLb = ((uint32_t)g_lbR << 16) | ((uint32_t)g_lbG << 8) | g_lbB;
            if (model == sony::DUALSENSE) g_sonyDirect = true;
            OVERLAPPED ov{}; ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            int fails = 0;
            while (!stop) {
                ResetEvent(ov.hEvent); DWORD got = 0;
                BOOL ok = ReadFile(h, rep.data(), inLen, &got, &ov);
                if (!ok && GetLastError() == ERROR_IO_PENDING) {
                    if (WaitForSingleObject(ov.hEvent, 1000) != WAIT_OBJECT_0) { CancelIo(h); if (++fails > 3) break; continue; }
                    ok = GetOverlappedResult(h, &ov, &got, FALSE);
                }
                if (!ok) break;                     // unplugged
                fails = 0;
                pad::XState x;
                if (!reports || rep[0] != firstId) { logf("  report id %02x, %lu bytes", rep[0], got); firstId = rep[0]; }
                reports++;
                if (g_lightbar) {   // light bar colour changed (health, hero)
                    uint32_t w = g_lbWant.load();
                    if (w == 0xFFFFFFFFu) w = ((uint32_t)g_lbR << 16) | ((uint32_t)g_lbG << 8) | g_lbB;
                    if (w != sentLb) {
                        uint8_t o[80]; size_t n = sony::lightbar(model, bt, (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w, o, sizeof o);
                        if (n) writeReport(h, o, n);
                        sentLb = w;
                    }
                }
                {   // vibration changed: one output report (the pad reports every few ms, so this is prompt)
                    int L = g_rumLow.load(), H = g_rumHigh.load();
                    // "motors off" goes out a second time a few reports later: a lost one leaves a tail
                    if (L != sentLow || H != sentHigh || (offAgain && --offAgain == 0)) {
                        uint8_t o[80]; size_t n = sony::rumble(model, bt, (uint8_t)L, (uint8_t)H, o, sizeof o);
                        writeReport(h, o, n);
                        if (!L && !H && (sentLow || sentHigh)) offAgain = 8;
                        sentLow = L; sentHigh = H;
                    }
                    int T = g_trigWant.load();
                    if (model == sony::DUALSENSE && g_adaptive > 0 && T != sentTrig) {
                        sony::TriggerFx l, r; trigFx(T, l, r);
                        uint8_t o[80]; size_t n = sony::triggers(model, bt, l, r, o, sizeof o);
                        if (n) writeReport(h, o, n);
                        sentTrig = T;
                    }
                }
                if (sony::parse(model, rep.data(), got, x, bt)) {
                    std::lock_guard<std::mutex> l(mu); state = x; lastReport = GetTickCount64();
                }
            }
            { std::lock_guard<std::mutex> l(devMu); dev = INVALID_HANDLE_VALUE; }
            g_sonyDirect = false;
            CloseHandle(ov.hEvent); CloseHandle(h);
            logf("Sony controller disconnected");
            Sleep(500);
        }
    }
};
static SonyReader* g_sony;

// ------------------------------------------------------------------ game window: storybook cursor, focus
// The game shows the Windows arrow in its menus. Its own cursor (the green hand of the menu book)
// is decoded from Textures\storybookanimTX.utx and set for the game window.
static WNDPROC g_origProc; static HCURSOR g_cursor;
static HCURSOR makeGameCursor() {
    std::vector<uint8_t> rgba; int w = 0, h = 0;
    wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"%ls..\\Textures\\storybookanimTX.utx", g_dir);
    upkg::Pkg p;
    if (!p.load(osio::Path(path)) || !upkg::decodeTexture(p, "Cursor", rgba, w, h) || w <= 0) { logf("game cursor texture not found"); return nullptr; }
    int sz = (int)(32.f * GetSystemMetrics(SM_CYSCREEN) / 1080.f + 0.5f); sz = std::min(96, std::max(32, sz));
    BITMAPV5HEADER bh{}; bh.bV5Size = sizeof bh; bh.bV5Width = sz; bh.bV5Height = -sz; bh.bV5Planes = 1; bh.bV5BitCount = 32;
    bh.bV5Compression = BI_BITFIELDS; bh.bV5RedMask = 0x00FF0000; bh.bV5GreenMask = 0x0000FF00; bh.bV5BlueMask = 0x000000FF; bh.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr; HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bh, DIB_RGB_COLORS, &bits, nullptr, 0); ReleaseDC(nullptr, dc);
    if (!color) return nullptr;
    uint32_t* px = (uint32_t*)bits;
    for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {     // bilinear resample, straight alpha
        float u = (x + 0.5f) * w / sz - 0.5f, v = (y + 0.5f) * h / sz - 0.5f;
        int x0 = std::max(0, std::min(w - 1, (int)floorf(u))), y0 = std::max(0, std::min(h - 1, (int)floorf(v)));
        int x1 = std::min(w - 1, x0 + 1), y1 = std::min(h - 1, y0 + 1); float fx = std::max(0.f, u - x0), fy = std::max(0.f, v - y0);
        float c[4];
        for (int k = 0; k < 4; k++) {
            auto at = [&](int xx, int yy) { return rgba[((size_t)yy * w + xx) * 4 + k] / 255.f; };
            c[k] = (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
        }
        px[(size_t)y * sz + x] = ((uint32_t)(c[3] * 255 + .5f) << 24) | ((uint32_t)(c[0] * 255 + .5f) << 16) | ((uint32_t)(c[1] * 255 + .5f) << 8) | (uint32_t)(c[2] * 255 + .5f);
    }
    HBITMAP mask = CreateBitmap(sz, sz, 1, 1, nullptr);
    ICONINFO ii{}; ii.fIcon = FALSE; ii.hbmMask = mask; ii.hbmColor = color;
    HCURSOR cur = CreateIconIndirect(&ii);
    DeleteObject(color); DeleteObject(mask);
    logf("game cursor %dx%d %s", sz, sz, cur ? "created" : "failed");
    return cur;
}
// ---- Raw Input mouse
// In gameplay WinDrv reads the mouse from WM_MOUSEMOVE: it takes the cursor offset from the window
// centre and puts the cursor back. Those positions go through Windows pointer acceleration
// ("Enhance pointer precision") and the desktop speed setting. With RawMouse=1 the counts come
// straight from the mouse (DirectInput, see below) and are handed to the same WinDrv handler as one move of
// exactly that many counts; the accelerated WM_MOUSEMOVEs are dropped. Menus (cursor shown) keep
// the normal Windows cursor.
static DWORD g_rawSeen; static bool g_inSynth;
static HWND g_mouseWnd;                       // the window WinDrv reads the mouse from (gets WM_MOUSEMOVE)
static LONG g_nInput, g_nRel, g_nAbs, g_nInjected, g_nFail, g_nSynth, g_nDropped;
static bool cursorShown();
// WinDrv's own capture state: UWindowsViewport::SetMouseCapture hides the cursor (ShowCursor(FALSE)
// loop) when gameplay takes the mouse and shows it again for menus. Its ShowCursor import is hooked,
// so "captured" is exactly what WinDrv decided - not a guess from the cursor's visibility (the
// menus may hide the Windows cursor and draw their own).
static volatile bool g_wdCaptured;
typedef int (WINAPI* ShowCursorFn)(BOOL);
static ShowCursorFn g_realShowCursor = ShowCursor;
static volatile bool g_photoNoCursor;   // photo mode: our pause makes WinDrv show the cursor; it stays hidden
static int WINAPI myShowCursor(BOOL show) {
    if (g_photoNoCursor && show) return -1;
    bool cap = !show;
    if (cap != g_wdCaptured) { g_wdCaptured = cap; logf(cap ? "WinDrv: mouse captured (gameplay)" : "WinDrv: mouse released (menu)"); }
    return g_realShowCursor(show);
}
static bool gameCaptured() {
    HWND a = GetActiveWindow();
    return g_wdCaptured && a && (!g_hwnd || a == g_hwnd || GetAncestor(g_hwnd, GA_ROOT) == a);
}
static IDirectInputDevice8W* g_diMouse;
static bool rawActive() { return g_rawMouse && g_diMouse && gameCaptured(); }
// Mouse counts through DirectInput: on current Windows its mouse device is built on Raw Input
// (unaccelerated sensor counts) and, unlike a RegisterRawInputDevices call of our own, it shares
// the process' raw-input registration with the game's own DirectInput devices instead of taking it
// over. Read once per frame (the game polls the gamepad every frame) and handed to WinDrv's mouse
// handler as one move.
static IDirectInput8W* g_realDI;
static void rawMove(HWND h, LONG dx, LONG dy);
static bool openDIMouse(HWND h) {
    if (!g_realDI || g_diMouse) return g_diMouse != nullptr;
    IDirectInputDevice8W* d = nullptr;
    if (FAILED(g_realDI->CreateDevice(GUID_SysMouse, &d, nullptr)) || !d) return false;
    static DIOBJECTDATAFORMAT objs[2] = {
        { &GUID_XAxis, 0, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0 },
        { &GUID_YAxis, 4, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0 } };
    static DIDATAFORMAT fmt = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, 8, 2, objs };
    DIPROPDWORD buf{}; buf.diph.dwSize = sizeof buf; buf.diph.dwHeaderSize = sizeof(DIPROPHEADER); buf.diph.dwHow = DIPH_DEVICE; buf.dwData = 512;
    if (FAILED(d->SetDataFormat(&fmt)) || FAILED(d->SetCooperativeLevel(h, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND))
        || FAILED(d->SetProperty(DIPROP_BUFFERSIZE, &buf.diph))) { d->Release(); return false; }
    d->Acquire();
    g_diMouse = d;
    return true;
}
static LONG g_photoDX, g_photoDY;   // raw mouse motion since the photo mode last looked
static void pollDIMouse() {
    if (!g_diMouse) return;
    DIDEVICEOBJECTDATA ev[128]; LONG dx = 0, dy = 0;
    for (;;) {
        DWORD n = 128;
        HRESULT hr = g_diMouse->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), ev, &n, 0);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) { g_diMouse->Acquire(); return; }
        if (FAILED(hr) || !n) break;
        for (DWORD i = 0; i < n; i++) {
            if (ev[i].dwOfs == 0) dx += (LONG)ev[i].dwData; else if (ev[i].dwOfs == 4) dy += (LONG)ev[i].dwData;
        }
        if (n < 128) break;
    }
    g_nInput++;
    g_photoDX += dx; g_photoDY += dy;
    if (!dx && !dy) return;
    g_nRel++;
    if (!g_rawSeen) { g_rawSeen = GetTickCount(); logf("raw mouse input active (DirectInput)"); }
    // sensitivity with the fraction carried to the next frame: no lost sub-count motion at any setting
    static float fx, fy;
    if (!gameCaptured() || !g_mouseWnd) { fx = fy = 0; return; }
    fx += dx * g_mouseSens; fy += dy * g_mouseSens;
    LONG ix = (LONG)fx, iy = (LONG)fy; fx -= ix; fy -= iy;
    if (ix || iy) rawMove(g_mouseWnd, ix, iy);
}
static WPARAM mouseKeys() {
    WPARAM k = 0;
    if (GetKeyState(VK_LBUTTON) < 0) k |= MK_LBUTTON; if (GetKeyState(VK_RBUTTON) < 0) k |= MK_RBUTTON;
    if (GetKeyState(VK_MBUTTON) < 0) k |= MK_MBUTTON; if (GetKeyState(VK_SHIFT) < 0) k |= MK_SHIFT;
    if (GetKeyState(VK_CONTROL) < 0) k |= MK_CONTROL;
    return k;
}
static void rawMove(HWND h, LONG dx, LONG dy) {
    RECT rc; GetClientRect(h, &rc);
    int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;       // WinDrv's centre
    dx = std::max(-30000L, std::min(30000L, dx)); dy = std::max(-30000L, std::min(30000L, dy));
    g_inSynth = true; g_nSynth++;
    SendMessageW(h, WM_MOUSEMOVE, mouseKeys(), MAKELPARAM((WORD)(SHORT)(cx + dx), (WORD)(SHORT)(cy + dy)));
    g_inSynth = false;
}
// Thread message hook: sees WM_MOUSEMOVE for every window of the game thread as the
// main loop takes them from the queue, whatever window class WinDrv uses for the viewport.
static HHOOK g_msgHook;
static bool flyKeysTaken();
static bool photoKeysTaken();
static LRESULT CALLBACK msgHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == PM_REMOVE) {
        MSG* m = (MSG*)lp;
        if (m->message == WM_KEYDOWN || m->message == WM_LBUTTONDOWN || m->message == WM_RBUTTONDOWN || m->message == WM_MBUTTONDOWN || m->message == WM_MOUSEWHEEL)
            g_lastKbUse = GetTickCount64();
        if (m->message == WM_KEYDOWN && flyKeysTaken()) {   // free look flying: movement keys are not the game's
            WPARAM k = m->wParam;
            if (k == 'W' || k == 'A' || k == 'S' || k == 'D' || k == 'E' || k == 'Q' || k == 'C' || k == VK_SPACE || k == VK_CONTROL ||
                k == VK_SHIFT || k == VK_UP || k == VK_DOWN || k == VK_LEFT || k == VK_RIGHT || (k == VK_ESCAPE && photoKeysTaken())) m->message = WM_NULL;
        }
        if (m->message == WM_MOUSEMOVE) {
            if (m->hwnd != g_mouseWnd && g_hwnd && (m->hwnd == g_hwnd || IsChild(g_hwnd, m->hwnd))) g_mouseWnd = m->hwnd;
            if (rawActive()) { m->message = WM_NULL; g_nDropped++; }     // accelerated position: not used
        }
    }
    return CallNextHookEx(g_msgHook, code, wp, lp);
}
static void logRawStats() {
    static DWORD last; DWORD now = GetTickCount();
    if (!g_rawMouse || now - last < 10000) return; last = now;
    logf("raw mouse: DirectInput reads %ld (with motion %ld), moves sent %ld, dropped %ld", g_nInput, g_nRel, g_nSynth, g_nDropped);
}
typedef BOOL (WINAPI* PeekFn)(LPMSG, HWND, UINT, UINT, UINT);
static PeekFn g_realPeekW = PeekMessageW, g_realPeekA = PeekMessageA;
// WinDrv's mouse handler drains queued WM_MOUSEMOVEs and adds their offsets; with raw input they are not used
static BOOL WINAPI myPeekW(LPMSG m, HWND h, UINT a, UINT b, UINT f) { if (a == WM_MOUSEMOVE && b == WM_MOUSEMOVE && rawActive()) return FALSE; return g_realPeekW(m, h, a, b, f); }
static BOOL WINAPI myPeekA(LPMSG m, HWND h, UINT a, UINT b, UINT f) { if (a == WM_MOUSEMOVE && b == WM_MOUSEMOVE && rawActive()) return FALSE; return g_realPeekA(m, h, a, b, f); }

static LRESULT CALLBACK gameWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_SETCURSOR && g_cursor && LOWORD(lp) == HTCLIENT) { SetCursor(g_cursor); return TRUE; }
    return CallWindowProcW(g_origProc, h, m, wp, lp);
}
// The game's window: the largest visible top-level window of the process. (The active window can be
// the hidden-ish 640x480 start-up/log dialog, class #32770.)
struct WndPick { HWND h = nullptr; long area = 0; };
static BOOL CALLBACK findGameWnd(HWND h, LPARAM lp) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    wchar_t cls[64] = L""; GetClassNameW(h, cls, 64);
    if (!wcscmp(cls, L"#32770")) return TRUE;
    RECT rc; GetClientRect(h, &rc); long a = (long)(rc.right - rc.left) * (rc.bottom - rc.top);
    auto* p = (WndPick*)lp; if (a > p->area) { p->area = a; p->h = h; }
    return TRUE;
}
static BOOL CALLBACK logWnd(HWND h, LPARAM) {
        DWORD pid = 0; DWORD tid = GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId()) return TRUE;
        wchar_t cls[64] = L"", t[64] = L""; GetClassNameW(h, cls, 64); GetWindowTextW(h, t, 64); RECT rc{}; GetClientRect(h, &rc);
        logf("  window %p class '%ls' title '%ls' %ldx%ld visible %d thread %lu", (void*)h, cls, t, rc.right, rc.bottom, IsWindowVisible(h), tid);
        return TRUE;
}
static void logWindows() { EnumWindows(logWnd, 0); }
static int patchIAT(HMODULE m, const char* dll, const char* fn, void* repl);
static void hookGameWindow() {
    if (g_hwnd) return;
    WndPick pick; EnumWindows(findGameWnd, (LPARAM)&pick);
    HWND h = pick.h;
    if (!h || pick.area < 640L * 480) return;               // the viewport is not up yet: try again on the next poll
    logf("game windows (this thread %lu):", GetCurrentThreadId()); logWindows();
    g_hwnd = h;
    DWORD fpid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &fpid);
    if (fpid != GetCurrentProcessId()) { SetForegroundWindow(h); logf("game window brought to the front"); }
    // no subclassing of the viewport window: WinDrv's own window procedure is left untouched
    // (the book cursor comes from the SetCursor redirect, the mouse from the thread message hook)
    if (g_rawMouse) {
        bool ok = openDIMouse(h);
        g_mouseWnd = h;
        g_wdCaptured = !cursorShown();
        g_msgHook = SetWindowsHookExW(WH_GETMESSAGE, msgHook, nullptr, GetWindowThreadProcessId(h, nullptr));
        wchar_t cls[64] = L""; GetClassNameW(h, cls, 64); RECT rc{}; GetClientRect(h, &rc);
        logf("raw mouse: hook %s, window %p class '%ls' %ldx%ld", g_msgHook ? "ok" : "failed", (void*)h, cls, rc.right, rc.bottom);
        int n = patchIAT(GetModuleHandleW(L"WinDrv.dll"), "USER32.dll", "ShowCursor", (void*)myShowCursor)
              + patchIAT(GetModuleHandleW(L"WinDrv.dll"), "USER32.dll", "PeekMessageW", (void*)myPeekW)
              + patchIAT(GetModuleHandleW(L"WinDrv.dll"), "USER32.dll", "PeekMessageA", (void*)myPeekA);
        logf("raw mouse: DirectInput mouse %s, WinDrv hooks %d (ShowCursor, PeekMessage)", ok ? "ok" : "failed", n);
    }
    if (!g_msgHook) g_msgHook = SetWindowsHookExW(WH_GETMESSAGE, msgHook, nullptr, GetWindowThreadProcessId(h, nullptr));   // free look keys
}
// WinDrv sets the cursor itself on every mouse message (SetCursor(StandardCursors[...]) with the
// system arrow), which wins over the window class cursor and WM_SETCURSOR. Its imports of
// SetCursor (and Window.dll's) are redirected here: the system arrow / cross become the book cursor.
typedef HCURSOR (WINAPI* SetCursorFn)(HCURSOR);
static SetCursorFn g_realSetCursor = SetCursor;
static HCURSOR g_sysArrow, g_sysCross;
static HCURSOR WINAPI mySetCursor(HCURSOR c) {
    if (g_cursor && c && (c == g_sysArrow || c == g_sysCross)) c = g_cursor;
    return g_realSetCursor(c);
}
static int patchIAT(HMODULE m, const char* dll, const char* fn, void* repl) {
    if (!m) return 0;
    auto base = (uint8_t*)m; auto dos = (IMAGE_DOS_HEADER*)base; auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    void* target = (void*)GetProcAddress(GetModuleHandleA(dll), fn);
    int n = 0;
    for (auto d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp((char*)(base + d->Name), dll)) continue;
        for (auto t = (IMAGE_THUNK_DATA*)(base + d->FirstThunk); t->u1.Function; t++) {
            if ((void*)t->u1.Function != target) continue;
            DWORD old; VirtualProtect(&t->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            t->u1.Function = (ULONG_PTR)repl; VirtualProtect(&t->u1.Function, sizeof(void*), old, &old); n++;
        }
    }
    return n;
}
static void hookCursor() {
    static bool done = false; if (done || !g_cursorOn) return; done = true;
    if (!g_cursor) g_cursor = makeGameCursor();
    if (!g_cursor) return;
    g_sysArrow = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); g_sysCross = LoadCursorW(nullptr, MAKEINTRESOURCEW(32515));
    int n = patchIAT(GetModuleHandleW(L"WinDrv.dll"), "USER32.dll", "SetCursor", (void*)mySetCursor)
          + patchIAT(GetModuleHandleW(L"Window.dll"), "USER32.dll", "SetCursor", (void*)mySetCursor)
          + patchIAT(GetModuleHandleW(nullptr), "USER32.dll", "SetCursor", (void*)mySetCursor);
    logf("SetCursor redirected in %d import(s)", n);
}
static void startSony() { g_sony = new SonyReader(); g_sony->start(); }

static pad::XState readPad() {
    pad::XState x;
    if (g_sony) { x = g_sony->get(); if (x.connected) return x; }    // a Sony pad read natively wins
    if (!g_xget) return x;
    for (DWORD i = 0; i < 4; i++) {          // first connected controller
        XINPUT_STATE_ st{};
        if (g_xget(i, &st) == ERROR_SUCCESS) {
            x.connected = true; x.buttons = st.Gamepad.wButtons; x.lt = st.Gamepad.bLeftTrigger; x.rt = st.Gamepad.bRightTrigger;
            if (g_xIndex.load() != (int)i) g_xIndex = (int)i;
            x.lx = st.Gamepad.sThumbLX; x.ly = st.Gamepad.sThumbLY; x.rx = st.Gamepad.sThumbRX; x.ry = st.Gamepad.sThumbRY;
            return x;
        }
    }
    return x;
}

// Is the game the active application? Overlays (NVIDIA, Afterburner, Discord...) may own the
// foreground window while the game still has the keyboard, so the thread's own active window
// counts too (GetDeviceState is called on the game's window thread).
static bool gameInForeground() {
    if (GetActiveWindow()) return true;
    DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}
static void logForeground() {
    HWND f = GetForegroundWindow(); DWORD pid = 0; GetWindowThreadProcessId(f, &pid);
    wchar_t cls[128] = L"", title[128] = L"";
    if (f) { GetClassNameW(f, cls, 128); GetWindowTextW(f, title, 128); }
    logf("foreground window: pid %lu (game %lu) class '%ls' title '%ls', active window of the game thread: %p",
         pid, GetCurrentProcessId(), cls, title, (void*)GetActiveWindow());
}
static bool cursorShown() {
    CURSORINFO ci{ sizeof ci };
    return GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING);
}


// ------------------------------------------------------------------ menu items of the game (GUI.u)
// The game's menus ignore the keyboard (no focus, no Tab), so pad navigation reads the open menu
// page itself: GUIController.ActivePage and its Controls, each with its screen rectangle
// (GUIComponent.Bounds, viewport pixels). The cursor is put onto the item in the pushed direction
// and A clicks it. Everything is found through Core.dll's exports and checked against what it must
// be (array sizes, object indices); anything unexpected turns the reader off and the pad falls
// back to keys.
namespace gui {
typedef const wchar_t* (__fastcall* GetNameFn)(void*, void*);
typedef int (__fastcall* IsAFn)(void*, void*, void*);
typedef const wchar_t* (__fastcall* PathFn)(void*, void*, void*, wchar_t*);
struct TArr { void** data; int num, max; };
static GetNameFn getName; static IsAFn isA; static PathFn pathName; static TArr* objs;
static void *clsController, *clsMulti, *clsComponent;
static void* clsButton; static int offClick = -1;
static int offActive = -1, offControls = -1, offBounds = -1, offState = -1, offVisible = -1, offAccepts = -1, offNever = -1, offHide = -1;
static unsigned mVisible, mAccepts, mNever, mHide;
static void* controller;
static int state = 0;   // 0 not tried, 1 ready, -1 off
// the objects wanted, by path name, found in one pass over all objects
static const wchar_t* WANT[] = { L"GUI.GUIController", L"GUI.GUIMultiComponent", L"GUI.GUIComponent", L"GUI.GUIController.ActivePage",
    L"GUI.GUIMultiComponent.Controls", L"GUI.GUIComponent.Bounds", L"GUI.GUIComponent.MenuState", L"GUI.GUIComponent.bVisible",
    L"GUI.GUIComponent.bAcceptsInput", L"GUI.GUIComponent.bNeverFocus", L"GUI.GUIController.bHideMouseCursor",
    L"GUI.GUIButton", L"GUI.GUIComponent.__OnClick__Delegate",
    // cheats
    L"Engine.PlayerController", L"Engine.Controller.Pawn", L"Engine.PlayerController.Player", L"Engine.PlayerController.CheatManager",
    L"Engine.Pawn.Health", L"Engine.Actor.Level", L"Engine.Actor.Velocity",
    // free look
    L"Engine.Actor.Rotation", L"Engine.PlayerController.ViewTarget", L"Engine.PlayerController.bCheatFlying", L"Engine.Pawn.AirSpeed",
    L"Engine.Actor.Acceleration", L"Engine.Actor.Location",
    // subtitles
    L"Engine.PlayerController.myHUD", L"KWGame.KWHud.CutTextController", L"KWGame.KWCutTextController.strText",
    L"KWGame.KWCutTextController.strCutCommentText", L"KWGame.KWCutTextController.textureBorder", L"Engine.Pawn.Controller",
    // photo mode, developer menu
    L"KWGame.KWHeroController", L"KWGame.KWHeroController.savedViewLocation", L"KWGame.KWHeroController.savedViewRotation",
    L"KWGame.KWHeroController.bUseBaseCam", L"Engine.Controller.PlayerReplicationInfo", L"Engine.LevelInfo.Pauser", L"Engine.HUD.bHideHUD",
    L"SHGame.Version", L"SHGame.Version.bDebugEnabled", L"KWGame.KWVersion", L"KWGame.KWVersion.bDebugEnabled",
    // wanted posters
    L"SHGame.SHInGameWantedPoster", L"SHGame.SHInGameMenuPosterPopUp", L"SHGame.SHInGameWantedPoster.PosterBtn",
    L"SHGame.SHInGameMenuPosterPopUp.PosterBtn", L"GUI.GUIGFXButton.Position", L"GUI.GUIComponent.WinTop", L"GUI.GUIComponent.WinLeft",
    L"GUI.GUIComponent.WinWidth", L"GUI.GUIComponent.WinHeight",
    L"Engine.Player.GUIController", L"KWGame.KWCutTextController.fCurrBorderHeight",
    L"GUI.GUIGFXButton.Graphic", L"Engine.BitmapMaterial", L"Engine.BitmapMaterial.USize", L"Engine.BitmapMaterial.VSize", L"KWGame.KWCutTextController.bCutSceneInProgress",
    L"Engine.Actor", L"Engine.Actor.FastTrace", L"Engine.Actor.FastTrace.TraceEnd", L"Engine.Actor.FastTrace.TraceStart",
    L"Engine.PlayerController.DefaultFOV", L"Engine.Light", L"Engine.Actor.LightEffect", L"Engine.Actor.bDirectional",
    L"Engine.PlayerController.DesiredFOV", L"Engine.Controller.FovAngle", L"Engine.Actor.FovAngle", L"GUI.GUIComponent.bBoundToParent", L"GUI.GUIComponent.bScaleToParent" };
const int NWANT = sizeof WANT / sizeof WANT[0];
static void* found[NWANT];
static void scan() {
    for (int w = 0; w < NWANT; w++) found[w] = nullptr;
    for (int i = 0; i < objs->num; i++) {
        void* o = objs->data[i]; if (!o) continue;
        const wchar_t* n = getName(o, nullptr); if (!n) continue;
        // a cheap test on the name before the full path
        bool maybe = false;
        for (int w = 0; w < NWANT && !maybe; w++) { const wchar_t* d = wcsrchr(WANT[w], L'.'); if (d && !_wcsicmp(d + 1, n)) maybe = true; }
        if (!maybe) continue;
        const wchar_t* pn = pathName(o, nullptr, nullptr, nullptr); if (!pn) continue;
        for (int w = 0; w < NWANT; w++) if (!found[w] && !_wcsicmp(pn, WANT[w])) found[w] = o;
    }
}
static void* findObj(const wchar_t* path) { for (int w = 0; w < NWANT; w++) if (!wcscmp(WANT[w], path)) return found[w]; return nullptr; }
static int maskAt = 0x70;   // UBoolProperty::BitMask (UBoolProperty::Identical reads [this+0x70])
static void findMaskAt() {}
static bool propOff(const wchar_t* path, int& off, int dim = 1) {
    BYTE* p = (BYTE*)findObj(path); if (!p) { logf("menu items: %ls not found", path); return false; }
    // UProperty (Core.dll of this game): ArrayDim +0x38, ElementSize +0x3C, Offset +0x4C
    int ad = *(int*)(p + 0x38), o = *(int*)(p + 0x4C);
    if (ad != dim || o < 0x28 || o > 0x4000) { logf("menu items: %ls unexpected (dim %d off %d)", path, ad, o); return false; }
    off = o; return true;
}
static bool boolOff(const wchar_t* path, int& off, unsigned& mask) {
    BYTE* p = (BYTE*)findObj(path); if (!p) { logf("menu items: %ls not found", path); return false; }
    int o = *(int*)(p + 0x4C); if (o < 0x28 || o > 0x4000) return false;
    if (maskAt < 0) { logf("menu items: bit mask of bool properties not found"); return false; }
    unsigned m = *(unsigned*)(p + maskAt);
    if (m && !(m & (m - 1))) { off = o; mask = m; return true; }
    logf("menu items: %ls bit mask not found", path); return false;
}
static bool init() {
    if (state) return state > 0;
    state = -1;
    HMODULE core = GetModuleHandleW(L"Core.dll"); if (!core) return false;
    getName = (GetNameFn)GetProcAddress(core, "?GetName@UObject@@QBEPBGXZ");
    isA = (IsAFn)GetProcAddress(core, "?IsA@UObject@@QBEHPAVUClass@@@Z");
    pathName = (PathFn)GetProcAddress(core, "?GetPathName@UObject@@QBEPBGPAV1@PAG@Z");
    objs = (TArr*)GetProcAddress(core, "?GObjObjects@UObject@@0V?$TArray@PAVUObject@@@@A");
    if (!getName || !isA || !pathName || !objs) { logf("menu items: Core.dll exports missing"); return false; }
    if (objs->num <= 0 || objs->num > 4000000 || !objs->data) { logf("menu items: object list unexpected"); return false; }
    scan();
    clsController = findObj(L"GUI.GUIController"); clsMulti = findObj(L"GUI.GUIMultiComponent"); clsComponent = findObj(L"GUI.GUIComponent");
    if (!clsController || !clsMulti || !clsComponent) { logf("menu items: GUI classes not loaded yet"); state = 0; return false; }
    findMaskAt();
    bool ok = propOff(L"GUI.GUIController.ActivePage", offActive) && propOff(L"GUI.GUIMultiComponent.Controls", offControls) &&
              propOff(L"GUI.GUIComponent.Bounds", offBounds, 4) && propOff(L"GUI.GUIComponent.MenuState", offState) &&
              boolOff(L"GUI.GUIComponent.bVisible", offVisible, mVisible) && boolOff(L"GUI.GUIComponent.bAcceptsInput", offAccepts, mAccepts) &&
              boolOff(L"GUI.GUIComponent.bNeverFocus", offNever, mNever);
    if (ok && !boolOff(L"GUI.GUIController.bHideMouseCursor", offHide, mHide)) offHide = -1;
    clsButton = findObj(L"GUI.GUIButton");
    if (ok && !propOff(L"GUI.GUIComponent.__OnClick__Delegate", offClick)) offClick = -1;
    if (!ok) return false;
    logf("menu items: ready (ActivePage +%d, Controls +%d, Bounds +%d, bVisible +%d/%x, bAcceptsInput +%d/%x)", offActive, offControls, offBounds, offVisible, mVisible, offAccepts, mAccepts);
    state = 1; return true;
}
static bool readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi;
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    return (const BYTE*)p + n <= (const BYTE*)mi.BaseAddress + mi.RegionSize;
}
static bool alive(void* o) {     // still the object at its index (not destroyed and reused)
    if (!o || !objs || !objs->data || !readable(o, 0x30)) return false;
    int i = *(int*)((BYTE*)o + 4);
    return i >= 0 && i < objs->num && objs->data[i] == o;
}
static void* playerController();
static int offPlayerGUI = -1, offPlayerOf = -1;
// the menu controller: the one of the player's viewport (Player.GUIController). A search over all
// objects found other GUIControllers too (the debug windows of the developer mode) and kept the
// wrong one; the search stays only as a fallback before a player exists.
static void* findController() {
    if (offPlayerGUI < 0 && !propOff(L"Engine.Player.GUIController", offPlayerGUI)) offPlayerGUI = -2;
    if (offPlayerOf < 0 && !propOff(L"Engine.PlayerController.Player", offPlayerOf)) offPlayerOf = -2;
    if (offPlayerGUI >= 0 && offPlayerOf >= 0) {
        if (void* pc = playerController()) {
            void* pl = *(void**)((BYTE*)pc + offPlayerOf);
            if (pl && readable((BYTE*)pl + offPlayerGUI, 4)) {
                void* gc = *(void**)((BYTE*)pl + offPlayerGUI);
                if (alive(gc) && isA(gc, nullptr, clsController)) { controller = gc; return gc; }
            }
        }
    }
    if (alive(controller) && isA(controller, nullptr, clsController)) return controller;
    controller = nullptr;
    static int at = 0;
    for (int n = 0; n < 4000; n++, at++) {
        if (at >= objs->num) at = 0;
        void* o = objs->data[at];
        if (o && isA(o, nullptr, clsController) && *(void**)((BYTE*)o + offActive)) { controller = o; break; }
    }
    return controller;
}
struct Item { float x0, y0, x1, y1; };
static int dumpLines = 0;
static void dumpComp(void* c, int depth) {   // what a menu page is made of (ShrekPad.log), for the first pages seen
    if (dumpLines > 240 || !c || depth > 8 || !alive(c)) return;
    BYTE* b = (BYTE*)c;
    if (!readable(b, (size_t)std::max({ offControls + 12, offBounds + 16, offVisible + 4, offAccepts + 4, offNever + 4, offState + 1 }))) return;
    void* cls = *(void**)(b + 0x24); const wchar_t* cn = cls ? getName(cls, nullptr) : L"?";
    float* r = (float*)(b + offBounds);
    bool multi = isA(c, nullptr, clsMulti);
    int click = offClick >= 0 && readable(b + offClick, 8) ? *(int*)(b + offClick + 4) : -1;
    logf("  %*s%ls vis=%d acc=%d never=%d state=%d click=%d [%.0f %.0f %.0f %.0f]%s", depth * 2, "", cn ? cn : L"?",
         !!(*(unsigned*)(b + offVisible) & mVisible), !!(*(unsigned*)(b + offAccepts) & mAccepts), !!(*(unsigned*)(b + offNever) & mNever),
         b[offState], click, r[0], r[1], r[2], r[3], multi ? " +" : "");
    dumpLines++;
    if (multi) {
        TArr* a = (TArr*)(b + offControls);
        if (!readable(a, sizeof *a) || a->num < 0 || a->num > 512 || (a->num && !readable(a->data, a->num * sizeof(void*)))) return;
        for (int i = 0; i < a->num; i++) dumpComp(a->data[i], depth + 1);
    }
}
static void collect(void* c, std::vector<Item>& out, int depth) {
    if (!c || depth > 12 || !alive(c)) return;
    BYTE* b = (BYTE*)c;
    if (!readable(b, (size_t)std::max({ offControls + 12, offBounds + 16, offVisible + 4, offAccepts + 4, offNever + 4, offState + 1 }))) return;
    if (!(*(unsigned*)(b + offVisible) & mVisible)) return;
    if (depth == 0 && isA(c, nullptr, clsMulti)) {   // the page itself is no item, only what it holds
        TArr* a = (TArr*)(b + offControls);
        if (!readable(a, sizeof *a) || a->num < 0 || a->num > 512 || (a->num && !readable(a->data, a->num * sizeof(void*)))) return;
        for (int i = 0; i < a->num; i++) collect(a->data[i], out, depth + 1);
        return;
    }
    // an item: a button, or reacts to a click (pictures used as buttons), or takes input. The game's
    // own buttons carry bNeverFocus (its menus have no keyboard focus at all), so that flag only
    // rules out components that merely take input
    bool accepts = (*(unsigned*)(b + offAccepts) & mAccepts) != 0;
    bool button = clsButton && isA(c, nullptr, clsButton);
    bool click = offClick >= 0 && readable(b + offClick, 8) && *(int*)(b + offClick + 4) != 0;
    bool never = (*(unsigned*)(b + offNever) & mNever) != 0;
    if (isA(c, nullptr, clsMulti)) {   // a panel: its parts, or the panel itself when it is one clickable thing
        TArr* a = (TArr*)(b + offControls);
        if (!readable(a, sizeof *a) || a->num < 0 || a->num > 512 || (a->num && !readable(a->data, a->num * sizeof(void*)))) return;
        size_t before = out.size();
        for (int i = 0; i < a->num; i++) collect(a->data[i], out, depth + 1);
        if (out.size() != before || !(button || click)) return;
    }
    if (!(button || click || (accepts && !never))) return;
    if (b[offState] == 4) return;    // MSAT_Disabled
    float* r = (float*)(b + offBounds);
    if (!(r[2] - r[0] >= 6 && r[3] - r[1] >= 6 && r[0] > -1 && r[1] > -1 && r[2] < 20000 && r[3] < 20000)) return;
    out.push_back({ r[0], r[1], r[2], r[3] });
}
// items of the open menu page, in viewport pixels
static bool items(std::vector<Item>& out) {
    out.clear();
    if (!init()) return false;
    void* c = findController(); if (!c) return false;
    if (!readable((BYTE*)c + offActive, 4)) return false;
    void* page = *(void**)((BYTE*)c + offActive);
    if (!page || !alive(page) || !isA(page, nullptr, clsComponent)) return false;
    static void* dumped[8] = {}; static int nd = 0;
    bool seen = false; for (int i = 0; i < nd; i++) if (dumped[i] == page) seen = true;
    if (!seen && nd < 8) { dumped[nd++] = page; logf("menu page:"); dumpComp(page, 0); }
    collect(page, out, 0);
    return true;
}
static void hideCursor(bool hide) {
    if (state <= 0 || offHide < 0 || !alive(controller)) return;
    unsigned* w = (unsigned*)((BYTE*)controller + offHide);
    if (hide) *w |= mHide; else *w &= ~mHide;
}

// ------------------------------------------------------------------ cheats and free look
// Settings from System\EnhancedEdition.ini [Cheats] (written by the launcher); free look comes from
// the launcher through the environment (SHREK_EE_FREELOOK=1) for that one start of the game.
// Console commands of the game itself are run on the player's controller and its CheatManager
// (UObject::ScriptConsoleExec: the exec functions AddCoins, AddPotions, God, Ghost, SloMo,
// PlayersOnly), once per level; endless health keeps the hero's Health at 100.
typedef int (__fastcall* ExecFn)(void*, void*, const wchar_t*, void*, void*);
static ExecFn scriptExec; static void** glog;
static int offPawn = -1, offPlayer = -1, offCheat = -1, offHealth = -1, offLevel = -1, offVel = -1;
static int offRot = -1, offViewTarget = -1, offCheatFly = -1, offAir = -1, offAccel = -1, offPawnCtrl = -1; static unsigned mCheatFly;
static volatile uint16_t padButtons;   // the pad at the last poll (free look moves with it)
static volatile int16_t padLX, padLY, padRX, padRY; static volatile uint8_t padLT, padRT;
static volatile bool flyActive;        // free look flying: the game gets no movement from the pad or the keys
static void* clsPC;
static int cstate = 0;
struct CheatCfg { bool on = false, health = false, freeLook = false, spot = false, spotPhoto = false; float sx = 0, sy = 0, sz = 0; int syaw = -1; int coins = 0, potions = 0, speed = 100; } ccfg;
// the hero to a place: its location written while it flies as a ghost (no collision, so it is in no
// collision hash); its next move in flight puts it into the right zone
static int offLoc = -1;
static bool setLocation(void* actor, float x, float y, float z) {
    if (offLoc < 0 || !alive(actor)) return false;
    float* l = (float*)((BYTE*)actor + offLoc);
    if (!readable(l, 12)) return false;
    l[0] = x; l[1] = y; l[2] = z;
    return true;
}
static bool cheatInit() {
    if (cstate) return cstate > 0;
    cstate = -1;
    HMODULE core = GetModuleHandleW(L"Core.dll"); if (!core) { cstate = 0; return false; }
    if (!objs) {   // the GUI part may not have started yet
        getName = (GetNameFn)GetProcAddress(core, "?GetName@UObject@@QBEPBGXZ");
        isA = (IsAFn)GetProcAddress(core, "?IsA@UObject@@QBEHPAVUClass@@@Z");
        pathName = (PathFn)GetProcAddress(core, "?GetPathName@UObject@@QBEPBGPAV1@PAG@Z");
        objs = (TArr*)GetProcAddress(core, "?GObjObjects@UObject@@0V?$TArray@PAVUObject@@@@A");
    }
    scriptExec = (ExecFn)GetProcAddress(core, "?ScriptConsoleExec@UObject@@UAEHPBGAAVFOutputDevice@@PAV1@@Z");
    glog = (void**)GetProcAddress(core, "?GLog@@3PAVFOutputDevice@@A");
    if (!getName || !isA || !pathName || !objs || !scriptExec || !glog) { logf("cheats: Core.dll exports missing"); return false; }
    if (objs->num <= 0 || objs->num > 4000000 || !objs->data) return false;
    scan();
    clsPC = findObj(L"Engine.PlayerController");
    if (!clsPC) { cstate = 0; return false; }
    bool ok = propOff(L"Engine.Controller.Pawn", offPawn) && propOff(L"Engine.PlayerController.Player", offPlayer) &&
              propOff(L"Engine.PlayerController.CheatManager", offCheat) && propOff(L"Engine.Pawn.Health", offHealth) &&
              propOff(L"Engine.Actor.Level", offLevel);
    if (ok && !propOff(L"Engine.Actor.Velocity", offVel)) offVel = -1;
    if (ok && !(propOff(L"Engine.Actor.Rotation", offRot) && propOff(L"Engine.PlayerController.ViewTarget", offViewTarget) &&
                boolOff(L"Engine.PlayerController.bCheatFlying", offCheatFly, mCheatFly) && propOff(L"Engine.Pawn.AirSpeed", offAir) &&
                propOff(L"Engine.Actor.Acceleration", offAccel))) offRot = -1;   // no flying of our own
    if (ok && !propOff(L"Engine.Actor.Location", offLoc)) offLoc = -1;
    if (ok && offPawnCtrl < 0 && !propOff(L"Engine.Pawn.Controller", offPawnCtrl)) offPawnCtrl = -1;
    if (ok) {   // Health must be a float here (KnowWonder changed it from int)
        void* hp = findObj(L"Engine.Pawn.Health"); void* hc = hp ? *(void**)((BYTE*)hp + 0x24) : nullptr;
        const wchar_t* cn = hc ? getName(hc, nullptr) : nullptr;
        if (!cn || wcscmp(cn, L"FloatProperty")) { logf("cheats: Pawn.Health is %ls, endless health off", cn ? cn : L"?"); offHealth = -1; }
    }
    if (!ok) return false;
    logf("cheats: ready (Pawn +%d, CheatManager +%d, Health +%d)", offPawn, offCheat, offHealth);
    cstate = 1; return true;
}
static void loadCheatCfg() {
    wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"%lsEnhancedEdition.ini", g_dir);
    ccfg.on = GetPrivateProfileIntW(L"Cheats", L"Enabled", 0, path) != 0;
    ccfg.health = GetPrivateProfileIntW(L"Cheats", L"EndlessHealth", 0, path) != 0;
    ccfg.coins = std::max(0, (int)GetPrivateProfileIntW(L"Cheats", L"CoinsPerLevel", 0, path));
    ccfg.potions = std::max(0, (int)GetPrivateProfileIntW(L"Cheats", L"PotionsPerLevel", 0, path));
    ccfg.speed = std::min(300, std::max(25, (int)GetPrivateProfileIntW(L"Cheats", L"GameSpeed", 100, path)));
    wchar_t e[8] = L""; GetEnvironmentVariableW(L"SHREK_EE_FREELOOK", e, 8);
    ccfg.freeLook = e[0] == L'1';
    wchar_t sp[96] = L"";   // a hidden place to start at (launcher, "Скрытые места"): "x,y,z"
    if (GetEnvironmentVariableW(L"SHREK_EE_SPOT", sp, 96) && swscanf(sp, L"%f,%f,%f", &ccfg.sx, &ccfg.sy, &ccfg.sz) == 3) {
        ccfg.spot = true; ccfg.freeLook = true;
        if (const wchar_t* ph = wcsstr(sp, L"photo")) { ccfg.spotPhoto = true; if (iswdigit(ph[5])) ccfg.syaw = _wtoi(ph + 5); }   // "photo<yaw>": the camera's heading
    }
    if (ccfg.spot) logf("hidden place %.0f %.0f %.0f (%s)", ccfg.sx, ccfg.sy, ccfg.sz, ccfg.spotPhoto ? "photo camera, a scene-only level" : "the hero is taken there once he is the player's");
    if (ccfg.on || ccfg.freeLook) logf("cheats: on=%d health=%d coins=%d potions=%d speed=%d%% freelook=%d", ccfg.on, ccfg.health, ccfg.coins, ccfg.potions, ccfg.speed, ccfg.freeLook);
}
static void* ptrAt(void* o, int off) { if (!o || off < 0 || !readable((BYTE*)o + off, 4)) return nullptr; void* p = *(void**)((BYTE*)o + off); return alive(p) ? p : nullptr; }
static bool run(void* obj, const wchar_t* cmd) {
    if (!obj || !*glog) return false;
    int r = scriptExec(obj, nullptr, cmd, *glog, nullptr);
    logf("cheats: %ls -> %s", cmd, r ? "ok" : "not handled");
    return r != 0;
}
static volatile bool flyToggled;   // F8 / both stick clicks, seen at the last poll
// the state an object is in (UObject::StateFrame at +0x0C, its StateNode: the first pointer after the
// FFrame part that is a live object of class State) - "PlayerWalking" once the player has the hero
static const wchar_t* stateName(void* o) {
    if (!alive(o)) return nullptr;
    BYTE* fr = *(BYTE**)((BYTE*)o + 0x0C);
    if (!fr || !readable(fr, 0x20)) return nullptr;
    for (int off = 0x14; off <= 0x18; off += 4) {
        void* st = *(void**)(fr + off);
        if (!alive(st)) continue;
        void* cls = *(void**)((BYTE*)st + 0x24);
        const wchar_t* cn = alive(cls) ? getName(cls, nullptr) : nullptr;
        if (cn && !wcscmp(cn, L"State")) return getName(st, nullptr);
    }
    return nullptr;
}
static void* flyPc;
// one step of free look flying, every poll: the hero's velocity from the left stick / WASD / arrows
// along the camera's heading, up with A / Space / E / right trigger, down with B / C / Ctrl / Q / left
// trigger, three times faster with Shift or a shoulder button
static void flyStep(void* pawn) {
    static bool prevKey = false, prevPad = false;
    bool k = (GetAsyncKeyState(VK_F8) & 0x8000) != 0, pd = (padButtons & pad::LTHUMB) && (padButtons & pad::RTHUMB);
    bool fg = g_hwnd && GetForegroundWindow() == g_hwnd;
    if (fg && ((k && !prevKey) || (pd && !prevPad))) flyToggled = true;
    prevKey = k; prevPad = pd;
    if (!flyActive || offRot < 0 || !alive(pawn) || !alive(flyPc)) return;
    float fwd = 0, side = 0, up = 0;
    if (fg) {
        auto key = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
        fwd = padLY / 32767.f; side = padLX / 32767.f;
        if (std::fabs(fwd) < 0.2f) fwd = 0;
        if (std::fabs(side) < 0.2f) side = 0;
        if (key('W') || key(VK_UP)) fwd += 1; if (key('S') || key(VK_DOWN)) fwd -= 1;
        if (key('D') || key(VK_RIGHT)) side += 1; if (key('A') || key(VK_LEFT)) side -= 1;
        if (key(VK_SPACE) || key('E') || (padButtons & pad::BTN_A) || padRT > 60) up += 1;
        if (key('C') || key('Q') || key(VK_CONTROL) || (padButtons & pad::BTN_B) || padLT > 60) up -= 1;
        float speed = (key(VK_SHIFT) || (padButtons & (pad::LSHOULDER | pad::RSHOULDER))) ? 2400.f : 800.f;
        fwd = std::max(-1.f, std::min(1.f, fwd)) * speed; side = std::max(-1.f, std::min(1.f, side)) * speed; up *= speed * 0.75f;
    }
    // heading: the camera when the view is a camera of its own, else the controller
    void* vt = ptrAt(flyPc, offViewTarget);
    void* src = vt && vt != pawn ? vt : flyPc;
    int* rot = (int*)((BYTE*)src + offRot);
    if (!readable(rot, 12)) return;
    float yaw = (rot[1] & 0xFFFF) * (6.2831853f / 65536.f);
    float cx = std::cos(yaw), cy = std::sin(yaw);
    float* v = (float*)((BYTE*)pawn + offVel); float* a = (float*)((BYTE*)pawn + offAccel);
    if (!readable(v, 12) || !readable(a, 12)) return;
    v[0] = cx * fwd - cy * side; v[1] = cy * fwd + cx * side; v[2] = up;
    a[0] = a[1] = a[2] = 0;
}
// the player's controller (the PlayerController with a viewport): looked for a slice of the objects
// per call - a pass over all of them at once stalls a frame, and menus have none, so it goes on there
static void* g_pc;
static void* playerController() {
    if (cstate <= 0) {
        static ULONGLONG retry = 0; ULONGLONG t = GetTickCount64();
        if (cstate < 0 || t < retry) return nullptr;
        retry = t + 2000;
        if (!cheatInit()) return nullptr;
    }
    if (alive(g_pc) && isA(g_pc, nullptr, clsPC) && ptrAt(g_pc, offPlayer)) return g_pc;
    g_pc = nullptr;
    static int at = 0;
    for (int n = 0; n < 3000 && !g_pc; n++, at++) {
        if (at >= objs->num) at = 0;
        void* o = objs->data[at];
        if (o && isA(o, nullptr, clsPC) && ptrAt(o, offPlayer)) g_pc = o;
    }
    return g_pc;
}

// ------------------------------------------------------------------ subtitles (with shrz.dll)
// shrz.dll draws subtitles of its own for every voice line. Here, with the game's objects at hand:
// the game's own subtitle text is taken away before it is drawn (handed to shrz.dll first, for lines
// without a voice), and the black bars of the scenes go - during play (bump lines: talking to the
// people around), or in every scene - as chosen in the launcher (EnhancedEdition.ini [Subtitles] Mode:
// 0 the game's own, 1 bars only in scenes, 2 no bars at all). The loading screen keeps its black.
struct FStr { wchar_t* data; int num, max; };
static int offHUD = -1, offCTC = -1, offText = -1, offComment = -1, offBorder = -1;
static int subsState = 0;
static bool subsInit() {
    if (subsState) return subsState > 0;
    static ULONGLONG retry = 0; static int tries = 0; ULONGLONG t = GetTickCount64();
    if (t < retry) return false;
    retry = t + 5000;
    if (cstate <= 0 || !objs) return false;
    if (++tries > 12) { subsState = -1; logf("subtitles: the game's subtitle objects not found"); return false; }
    if (!findObj(L"KWGame.KWHud.CutTextController")) scan();
    bool ok = propOff(L"Engine.PlayerController.myHUD", offHUD) && propOff(L"KWGame.KWHud.CutTextController", offCTC) &&
              propOff(L"KWGame.KWCutTextController.strText", offText) && propOff(L"KWGame.KWCutTextController.strCutCommentText", offComment) &&
              propOff(L"KWGame.KWCutTextController.textureBorder", offBorder) && propOff(L"Engine.Pawn.Controller", offPawnCtrl);
    if (!ok) return false;
    subsState = 1; logf("subtitles: game subtitle objects found (CutTextController +%d, strText +%d, textureBorder +%d)", offCTC, offText, offBorder);
    return true;
}
static void subsTick() {
    static int mode = -1; static bool on = true;
    if (mode < 0) {
        wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"%lsEnhancedEdition.ini", g_dir);
        mode = (int)GetPrivateProfileIntW(L"Subtitles", L"Mode", 1, path);
        on = GetPrivateProfileIntW(L"Subtitles", L"Enabled", 1, path) != 0;
    }
    if ((mode <= 0 && on) || !subsInit()) return;     // the game's own subtitles: nothing to do (off: its text goes too)
    void* pc = playerController(); if (!pc) return;
    void* hud = ptrAt(pc, offHUD); if (!hud) return;
    void* ctc = ptrAt(hud, offCTC); if (!ctc) return;
    BYTE* b = (BYTE*)ctc;
    typedef void (__cdecl* GameTextFn)(const wchar_t*);
    static GameTextFn hand = nullptr; static bool looked = false;
    if (!looked) { looked = true; HMODULE z = GetModuleHandleW(L"shrz.dll"); hand = z ? (GameTextFn)GetProcAddress(z, "ShrSubsGameText") : nullptr; if (!hand) logf("subtitles: shrz.dll without ShrSubsGameText"); }
    for (int off : { offText, offComment }) {
        FStr* f = (FStr*)(b + off);
        if (!readable(f, sizeof *f) || f->num <= 1 || !f->data || !readable(f->data, (size_t)f->num * 2)) continue;
        if (off == offText && hand && on) hand(f->data);
        f->num = 0;                                   // the game draws nothing ('' = no text)
    }
    // the black bars
    static void* border = nullptr;
    void** bp = (void**)(b + offBorder);
    if (!readable(bp, 4)) return;
    if (*bp && *bp != border) border = *bp;
    // the screen that hides the setting up after a scene (Pinocchio's hourglass) uses the same black
    // tiles, half the screen or all of it: such a cover stays; only the thin bars of the scenes go
    static int offH = -2;
    if (offH == -2 && !propOff(L"KWGame.KWCutTextController.fCurrBorderHeight", offH)) offH = -1;
    bool loading = offH >= 0 && readable(b + offH, 4) && *(float*)(b + offH) > 0.3f;
    bool play = false;
    if (void* pawn = ptrAt(pc, offPawn)) play = ptrAt(pawn, offPawnCtrl) == pc;
    bool hide = mode > 0 && !loading && (mode >= 2 || play);
    void* want = hide ? nullptr : (alive(border) ? border : *bp);
    if (*bp != want) *bp = want;
    // the bars' height to shrz.dll: its subtitles go into the bottom bar then
    typedef void (__cdecl* BarsFn)(float);
    static BarsFn bars = nullptr; static bool lookedB = false;
    if (!lookedB) { lookedB = true; HMODULE z = GetModuleHandleW(L"shrz.dll"); bars = z ? (BarsFn)GetProcAddress(z, "ShrSubsBars") : nullptr; }
    if (bars) {
        static int offIn = -2; static unsigned mIn = 0;
        if (offIn == -2 && !boolOff(L"KWGame.KWCutTextController.bCutSceneInProgress", offIn, mIn)) offIn = -1;
        bool inScene = offIn >= 0 && readable(b + offIn, 4) && (*(unsigned*)(b + offIn) & mIn);
        float h = offH >= 0 && readable(b + offH, 4) ? *(float*)(b + offH) : 0.f;
        bars(want && inScene && !loading ? h : 0.f);
    }
}

// ------------------------------------------------------------------ wanted posters
// A poster just found (SHInGameWantedPoster) and the one opened from the pause menu
// (SHInGameMenuPosterPopUp) were drawn at the texture's own pixel size - tiny at high resolutions.
// Their page now takes 90% of the screen and the picture is fitted into it, proportions kept.
static void posterTick() {
    static int st = 0; static ULONGLONG look = 0;
    static int o1 = -1, o2 = -1, oPos = -1, oTop = -1, oLeft = -1, oW = -1, oH = -1, oGfx = -1, oUS = -1, oVS = -1;
    if (st < 0) return;
    ULONGLONG t = GetTickCount64();
    if (t < look) return;
    look = t + 100;
    if (state == 0) { static ULONGLONG retry = 0; if (t < retry) return; retry = t + 3000; if (!init()) return; }
    if (state < 0) return;
    void* c = findController(); if (!c || offActive < 0) return;
    void* page = *(void**)((BYTE*)c + offActive);
    if (!page || !alive(page)) return;
    void* cls = *(void**)((BYTE*)page + 0x24); const wchar_t* cn = alive(cls) ? getName(cls, nullptr) : nullptr;
    if (!cn || (wcscmp(cn, L"SHInGameWantedPoster") && wcscmp(cn, L"SHInGameMenuPosterPopUp"))) return;
    if (st == 0) {   // the classes exist only now (the game loads them for the first poster): their properties
        scan();
        bool ok = propOff(L"GUI.GUIGFXButton.Position", oPos) && propOff(L"GUI.GUIComponent.WinTop", oTop) && propOff(L"GUI.GUIComponent.WinLeft", oLeft) &&
                  propOff(L"GUI.GUIComponent.WinWidth", oW) && propOff(L"GUI.GUIComponent.WinHeight", oH) && propOff(L"GUI.GUIGFXButton.Graphic", oGfx) &&
                  propOff(L"Engine.BitmapMaterial.USize", oUS) && propOff(L"Engine.BitmapMaterial.VSize", oVS);
        bool b1 = findObj(L"SHGame.SHInGameWantedPoster.PosterBtn") && propOff(L"SHGame.SHInGameWantedPoster.PosterBtn", o1);
        bool b2 = findObj(L"SHGame.SHInGameMenuPosterPopUp.PosterBtn") && propOff(L"SHGame.SHInGameMenuPosterPopUp.PosterBtn", o2);
        if (!ok || !(b1 || b2)) { st = -1; logf("posters: objects not found"); return; }
        st = 1;
    }
    bool first = !wcscmp(cn, L"SHInGameWantedPoster");
    int& oBtnRef = first ? o1 : o2;
    if (oBtnRef < 0) {   // the other poster page met for the first time: its class was not loaded before
        static bool tried[2]; if (!tried[first]) { tried[first] = true; scan();
            propOff(first ? L"SHGame.SHInGameWantedPoster.PosterBtn" : L"SHGame.SHInGameMenuPosterPopUp.PosterBtn", oBtnRef); }
    }
    void* btn = oBtnRef >= 0 ? ptrAt(page, oBtnRef) : nullptr;
    if (!btn) return;
    // the picture fills its button (ICP_Scaled: drawn over the whole button; 4 = ICP_Bound tiled it), the
    // button fills the page, and the page gets the picture's proportions: 90% of the screen's height
    void* tex = ptrAt(btn, oGfx);
    static void* clsBitmap = nullptr; if (!clsBitmap) clsBitmap = findObj(L"Engine.BitmapMaterial");
    int tw = 512, th = 1024;   // a poster's proportions when the picture is not a plain texture
    if (tex && clsBitmap && isA(tex, nullptr, clsBitmap) && readable((BYTE*)tex + oUS, 4) && readable((BYTE*)tex + oVS, 4)) {
        int u = *(int*)((BYTE*)tex + oUS), v = *(int*)((BYTE*)tex + oVS);
        if (u >= 16 && v >= 16 && u <= 16384 && v <= 16384) { tw = u; th = v; }
    }
    RECT rc{}; if (!g_hwnd || !GetClientRect(g_hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0 || tw <= 0 || th <= 0) return;
    float H = 0.9f, W = H * (float)tw / th * (float)rc.bottom / rc.right;
    if (W > 0.95f) { H *= 0.95f / W; W = 0.95f; }
    BYTE* pg = (BYTE*)page;
    if (!readable(pg + oTop, 4) || !readable(pg + oLeft, 4) || !readable(pg + oW, 4) || !readable(pg + oH, 4)) return;
    *(float*)(pg + oLeft) = (1 - W) / 2; *(float*)(pg + oTop) = (1 - H) / 2; *(float*)(pg + oW) = W; *(float*)(pg + oH) = H;
    // the button was laid out against the whole screen, not its page (the picture went across the screen):
    // it is bound and scaled to the page now and fills it
    {
        static int oBound = -2, oScale = -2; static unsigned mBound = 0, mScale = 0;
        if (oBound == -2) { scan(); if (!boolOff(L"GUI.GUIComponent.bBoundToParent", oBound, mBound)) oBound = -1; if (!boolOff(L"GUI.GUIComponent.bScaleToParent", oScale, mScale)) oScale = -1; }
        BYTE* bb = (BYTE*)btn;
        if (oBound >= 0 && oScale >= 0 && readable(bb + oBound, 4) && readable(bb + oScale, 4) && readable(bb + oTop, 4) && readable(bb + oLeft, 4) &&
            readable(bb + oW, 4) && readable(bb + oH, 4)) {
            *(unsigned*)(bb + oBound) |= mBound; *(unsigned*)(bb + oScale) |= mScale;
            *(float*)(bb + oLeft) = 0; *(float*)(bb + oTop) = 0; *(float*)(bb + oW) = 1; *(float*)(bb + oH) = 1;
        } else {   // no flags: the button's own place on the screen
            *(float*)(bb + oLeft) = (1 - W) / 2; *(float*)(bb + oTop) = (1 - H) / 2; *(float*)(bb + oW) = W; *(float*)(bb + oH) = H;
        }
    }
    if (readable((BYTE*)btn + oPos, 1) && *((BYTE*)btn + oPos) != 2) {
        *((BYTE*)btn + oPos) = 2;
        logf("posters: %ls %dx%d shown at %.0f%% x %.0f%% of the screen", cn, tw, th, W * 100, H * 100);
    }
}

// ------------------------------------------------------------------ light bar (DualShock 4 / DualSense)
// The colour of the hero (Shrek green, Donkey grey, Puss orange) and / or of his health: from half
// health down it turns red, below a quarter it blinks. In menus the fixed colour of ShrekPad.ini.
static void lightTick() {
    static ULONGLONG next = 0; ULONGLONG t = GetTickCount64();
    if (t < next) return;
    next = t + 100;
    bool lb = g_lightbar && g_lbMode != 0;
    if (!lb) g_lbWant = 0xFFFFFFFFu;
    if (!lb && g_adaptive <= 0) return;
    void* pc = playerController(); void* pawn = pc ? ptrAt(pc, offPawn) : nullptr;
    if (!pawn) { if (lb) g_lbWant = 0xFFFFFFFFu; return; }
    // the hero by the class of the pawn the player has (at once, also after a switch); the trigger
    // strength follows it too
    static const float HERO[5][3] = { { 50, 220, 10 }, { 140, 140, 175 }, { 255, 110, 0 }, { 235, 170, 70 }, { 160, 80, 20 } };
    int hero = std::min(2, std::max(0, g_hero.load()));
    {
        void* cls = *(void**)((BYTE*)pawn + 0x24); const wchar_t* cn = alive(cls) ? getName(cls, nullptr) : nullptr;
        static std::wstring lastCls;
        if (cn) {
            std::wstring n = cn; for (auto& ch : n) ch = towlower(ch);
            int h = -1;
            if (n.find(L"human") != std::wstring::npos) h = 3;
            else if (n.find(L"mongo") != std::wstring::npos) h = 4;
            else if (n.find(L"donkey") != std::wstring::npos || n.find(L"steed") != std::wstring::npos) h = 1;
            else if (n.find(L"pib") != std::wstring::npos || n.find(L"puss") != std::wstring::npos) h = 2;
            else if (n.find(L"shrek") != std::wstring::npos) h = 0;
            if (n != lastCls) { lastCls = n; logf("hero: %ls", cn); }
            if (h >= 0) {
                hero = h; g_heroByClass = true;
                int trig = h == 1 || h == 2 ? h : 0;
                if (g_hero.load() != trig) { g_hero = trig; logf("hero: triggers at %d%%", heroScale()); }
            }
        }
    }
    if (!lb || offHealth < 0 || !readable((BYTE*)pawn + offHealth, 4)) { if (lb) g_lbWant = 0xFFFFFFFFu; return; }
    float h = std::min(1.f, std::max(0.f, *(float*)((BYTE*)pawn + offHealth) / 100.f));
    float c[3];
    if (g_lbMode == 1) {   // green - yellow - red
        c[0] = h > 0.5f ? (1 - h) * 2 * 255 : 255; c[1] = h > 0.5f ? 255 : h * 2 * 255; c[2] = 0;
    } else {
        for (int i = 0; i < 3; i++) c[i] = HERO[hero][i];
        if (g_lbMode == 3 && h < 0.5f) { float k = 1 - h * 2; const float R[3] = { 255, 0, 0 }; for (int i = 0; i < 3; i++) c[i] = c[i] * (1 - k) + R[i] * k; }
    }
    if (g_lbMode != 2 && h > 0 && h < 0.25f && (t / 300) % 2) for (float& v : c) v *= 0.15f;   // blinking: almost no health left
    g_lbWant = ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | (uint32_t)c[2];
}

// ------------------------------------------------------------------ world collision for the photo camera
// Actor.FastTrace (the game's own line check against the level: BSP, static meshes, terrain), called
// the way a script call would: a frame on the function's parameter code (FFrame constructor of Core.dll)
// and AActor::execFastTrace of Engine.dll. All on the game's thread (the input poll).
typedef void* (__fastcall* FrameCtorFn)(void*, void*, void*, void*, int, void*);
typedef void (__fastcall* ExecNatFn)(void*, void*, void*, void*);
static bool worldClear(void* actor, const float* from, const float* to) {
    static int st = 0; static FrameCtorFn ctor; static ExecNatFn fastTrace; static void* fn; static int oEnd = -1, oStart = -1;
    if (st < 0) return true;
    if (st == 0) {
        st = -1;
        HMODULE core = GetModuleHandleW(L"Core.dll"), eng = GetModuleHandleW(L"Engine.dll");
        ctor = core ? (FrameCtorFn)GetProcAddress(core, "??0FFrame@@QAE@PAVUObject@@PAVUStruct@@HPAX@Z") : nullptr;
        fastTrace = eng ? (ExecNatFn)GetProcAddress(eng, "?execFastTrace@AActor@@QAEXAAUFFrame@@QAX@Z") : nullptr;
        fn = findObj(L"Engine.Actor.FastTrace");
        if (!ctor || !fastTrace || !fn || !propOff(L"Engine.Actor.FastTrace.TraceEnd", oEnd) || !propOff(L"Engine.Actor.FastTrace.TraceStart", oStart)) {
            logf("photo mode: no world collision (FastTrace not found)"); return true;
        }
        if (oEnd > 64 || oStart > 64) { logf("photo mode: FastTrace parameters unexpected"); return true; }
        st = 1; logf("photo mode: camera stops at walls (FastTrace)");
    }
    if (!alive(actor)) return true;
    alignas(16) BYTE parms[96] = {}; alignas(16) BYTE frame[128] = {}; DWORD result = 1;
    memcpy(parms + oEnd, to, 12); memcpy(parms + oStart, from, 12);
    ctor(frame, nullptr, actor, fn, 0, parms);
    fastTrace(actor, nullptr, frame, &result);
    return result != 0;
}
// the box the camera stays in: where the level's actors are (most of them), with room around it
static void* clsActorObj;
static bool levelBox(void* level, float lo[3], float hi[3]) {
    if (!clsActorObj) clsActorObj = findObj(L"Engine.Actor");
    if (!clsActorObj || offLoc < 0 || offLevel < 0) return false;
    std::vector<float> c[3];
    for (int i = 0; i < objs->num; i++) {
        void* o = objs->data[i];
        if (!o || !isA(o, nullptr, clsActorObj)) continue;
        BYTE* b = (BYTE*)o;
        if (!readable(b + offLoc, 12) || !readable(b + offLevel, 4) || *(void**)(b + offLevel) != level) continue;
        float* l = (float*)(b + offLoc);
        if (!std::isfinite(l[0]) || !std::isfinite(l[1]) || !std::isfinite(l[2])) continue;
        for (int k = 0; k < 3; k++) c[k].push_back(l[k]);
    }
    if (c[0].size() < 20) return false;
    for (int k = 0; k < 3; k++) {
        std::sort(c[k].begin(), c[k].end());
        size_t n = c[k].size();
        lo[k] = c[k][n / 100] - 2000; hi[k] = c[k][n - 1 - n / 100] + (k == 2 ? 2500 : 2000);
    }
    logf("photo mode: level box %.0f..%.0f %.0f..%.0f %.0f..%.0f (%u actors)", lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], (unsigned)c[0].size());
    return true;
}

// ------------------------------------------------------------------ photo mode
// F9 (or View/Share + Y on the pad): the world stops (LevelInfo.Pauser, as the game's own pause, but
// without its menu), the HUD and the subtitles go, and the camera flies freely: WASD / arrows / left
// stick move along the view, the mouse / right stick look, Space E RT up, C Q LT down, Shift or a
// shoulder button faster. The view comes from KWHeroController.savedViewLocation/Rotation (what the
// game shows while nothing ticks) and, should the controller tick, from the controller itself
// (ViewTarget = controller, bUseBaseCam off). F9 again puts everything back.
static int offSavedLoc = -1, offSavedRot = -1, offUseBaseCam = -1, offPRI = -1, offPauser = -1, offHideHUD = -1;
static unsigned mUseBaseCam, mHideHUD; static void* clsKWHero; static int photoState = 0;
static volatile bool photoActive;
static bool photoInit() {
    if (photoState) return photoState > 0;
    if (cstate <= 0) return false;
    photoState = -1;
    clsKWHero = findObj(L"KWGame.KWHeroController");
    if (!clsKWHero || !propOff(L"KWGame.KWHeroController.savedViewLocation", offSavedLoc) || !propOff(L"KWGame.KWHeroController.savedViewRotation", offSavedRot) ||
        !boolOff(L"KWGame.KWHeroController.bUseBaseCam", offUseBaseCam, mUseBaseCam) || !propOff(L"Engine.Controller.PlayerReplicationInfo", offPRI) ||
        !propOff(L"Engine.LevelInfo.Pauser", offPauser) || !boolOff(L"Engine.HUD.bHideHUD", offHideHUD, mHideHUD) || offRot < 0 || offLoc < 0 || offViewTarget < 0) {
        logf("photo mode: objects not found, off"); return false;
    }
    photoState = 1; return true;
}
static void photoTick(float dt) {
    static bool prevKey = false, prevPad = false;
    bool fg = g_hwnd && GetForegroundWindow() == g_hwnd;
    bool k = (GetAsyncKeyState(VK_F9) & 0x8000) != 0, pd = (padButtons & pad::BACK) && (padButtons & pad::BTN_Y);
    bool toggle = fg && ((k && !prevKey) || (pd && !prevPad));
    prevKey = k; prevPad = pd;
    // leaving by Esc / Start, and whenever the game opens a menu (its own pause would meet ours)
    static bool prevEsc = false, prevStart = false;
    bool esc = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0, st = (padButtons & pad::START) != 0;
    if (photoActive && fg && ((esc && !prevEsc) || (st && !prevStart))) toggle = true;
    prevEsc = esc; prevStart = st;
    bool byMenu = false;   // the game opened a menu: its own pause holds the world now, so ours is not undone
    static ULONGLONG photoSince = 0;
    if (photoActive && !toggle && fg && cursorShown() && GetTickCount64() - photoSince > 1500) {
        // our own pause makes the game release the mouse: a menu is only a page opened in the game's GUI
        void* c = state > 0 ? findController() : nullptr; bool page = c && offActive >= 0 && *(void**)((BYTE*)c + offActive) != nullptr;
        if (page) { toggle = true; byMenu = true; logf("photo mode: a menu opened, leaving"); }
    }
    static void *pc = nullptr, *level = nullptr, *hud = nullptr, *oldView = nullptr;
    static float loc[3], oldLoc[3]; static int rot[3], oldRot[3]; static bool oldHide = false;
    static float boxLo[3], boxHi[3]; static bool boxOk = false;
    typedef void (__cdecl* HideFn)(int);
    auto subsHide = [](int h) { HMODULE z = GetModuleHandleW(L"shrz.dll"); auto f = z ? (HideFn)GetProcAddress(z, "ShrSubsHide") : nullptr; if (f) f(h); };
    // a hidden place of a level made of scenes only: the photo camera starts there 2 s after the level
    // is there, tried again every half second (up to 40 s) until the camera can be taken
    static bool reqDone = false; static ULONGLONG reqSeen = 0, reqNext = 0; static void* reqLevel = nullptr; static int reqTries = 0;
    bool fromSpot = false;
    if (ccfg.spot && ccfg.spotPhoto && !reqDone && !photoActive) {
        void* p0 = playerController(); void* l0 = p0 ? ptrAt(p0, offLevel) : nullptr;
        ULONGLONG t = GetTickCount64();
        if (l0 != reqLevel) { reqLevel = l0; reqSeen = t; }
        if (l0 && t - reqSeen > 2000 && t >= reqNext) {
            reqNext = t + 500; toggle = true; fromSpot = true;
            if (++reqTries > 80) { reqDone = true; logf("hidden place: the photo camera could not be taken"); toggle = false; fromSpot = false; }
        }
    }
    if (!photoActive && toggle && !fromSpot && cursorShown()) return;      // not from a menu
    if (toggle && !photoActive) {
        static int whyLogged = 0;
        auto fail = [&](const char* why) { if (fromSpot && !(whyLogged & 1 << (why[0] & 15))) { whyLogged |= 1 << (why[0] & 15); logf("hidden place: waiting (%s)", why); } };
        if (!photoInit()) { fail("photo mode unavailable"); return; }
        pc = playerController();
        if (!pc || !isA(pc, nullptr, clsKWHero)) { fail("no hero controller yet"); return; }
        level = ptrAt(pc, offLevel); void* pri = ptrAt(pc, offPRI);
        if (!level || !pri || !readable((BYTE*)level + offPauser, 4)) { fail("level not ready"); return; }
        if (*(void**)((BYTE*)level + offPauser)) { fail("game paused"); return; }   // already paused: a menu is open
        hud = ptrAt(pc, offHUD >= 0 ? offHUD : -1);
        float* sl = (float*)((BYTE*)pc + offSavedLoc); int* sr = (int*)((BYTE*)pc + offSavedRot);
        if (!readable(sl, 12) || !readable(sr, 12)) { fail("no camera"); return; }
        if (fromSpot) { reqDone = true; logf("hidden place: photo camera at %.0f %.0f %.0f", ccfg.sx, ccfg.sy, ccfg.sz); }
        memcpy(loc, sl, 12); memcpy(rot, sr, 12); rot[2] = 0;
        if (fromSpot) { loc[0] = ccfg.sx; loc[1] = ccfg.sy; loc[2] = ccfg.sz; rot[0] = -1500; if (ccfg.syaw >= 0) rot[1] = ccfg.syaw; }
        memcpy(oldLoc, (BYTE*)pc + offLoc, 12); memcpy(oldRot, (BYTE*)pc + offRot, 12);
        oldView = *(void**)((BYTE*)pc + offViewTarget);
        *(void**)((BYTE*)level + offPauser) = pri;
        if (hud && offHideHUD >= 0) { unsigned* w = (unsigned*)((BYTE*)hud + offHideHUD); oldHide = (*w & mHideHUD) != 0; *w |= mHideHUD; }
        subsHide(1);
        g_photoDX = g_photoDY = 0;
        boxOk = levelBox(level, boxLo, boxHi);
        if (boxOk) for (int k = 0; k < 3; k++) { boxLo[k] = std::min(boxLo[k], loc[k] - 200); boxHi[k] = std::max(boxHi[k], loc[k] + 200); }
        photoActive = true; photoSince = GetTickCount64(); logf("photo mode on");
        g_photoNoCursor = true; while (g_realShowCursor(FALSE) >= 0) {}
    } else if (toggle && photoActive) {
        photoActive = false; g_photoNoCursor = false;
        if (byMenu) while (g_realShowCursor(TRUE) < 0) {}
        if (alive(pc)) {
            *(unsigned*)((BYTE*)pc + offUseBaseCam) |= mUseBaseCam;
            if (alive(oldView)) *(void**)((BYTE*)pc + offViewTarget) = oldView;
            memcpy((BYTE*)pc + offLoc, oldLoc, 12); memcpy((BYTE*)pc + offRot, oldRot, 12);
        }
        if (!byMenu && alive(level)) *(void**)((BYTE*)level + offPauser) = nullptr;
        if (alive(hud) && offHideHUD >= 0 && !oldHide) *(unsigned*)((BYTE*)hud + offHideHUD) &= ~mHideHUD;
        subsHide(0);
        logf("photo mode off");
        return;
    }
    if (!photoActive) return;
    if (!alive(pc) || !alive(level)) { photoActive = false; g_photoNoCursor = false; subsHide(0); return; }
    // looking: raw mouse (counts) and the right stick
    LONG mx = g_photoDX, my = g_photoDY; g_photoDX = g_photoDY = 0;
    float rx = padRX / 32767.f, ry = padRY / 32767.f;
    if (std::fabs(rx) < 0.15f) rx = 0;
    if (std::fabs(ry) < 0.15f) ry = 0;
    rot[1] += (int)(mx * 12 * g_mouseSens + rx * 22000 * dt);
    rot[0] -= (int)(my * 12 * g_mouseSens - ry * 16000 * dt);
    rot[0] = std::max(-16000, std::min(16000, rot[0]));
    // moving along the view
    float fwd = 0, side = 0, up = 0;
    if (fg) {
        auto key = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
        fwd = padLY / 32767.f; side = padLX / 32767.f;
        if (std::fabs(fwd) < 0.2f) fwd = 0;
        if (std::fabs(side) < 0.2f) side = 0;
        if (key('W') || key(VK_UP)) fwd += 1; if (key('S') || key(VK_DOWN)) fwd -= 1;
        if (key('D') || key(VK_RIGHT)) side += 1; if (key('A') || key(VK_LEFT)) side -= 1;
        if (key(VK_SPACE) || key('E') || padRT > 60) up += 1;
        if (key('C') || key('Q') || key(VK_CONTROL) || padLT > 60) up -= 1;
        float sp = (key(VK_SHIFT) || (padButtons & (pad::LSHOULDER | pad::RSHOULDER))) ? 1500.f : 400.f;
        fwd = std::max(-1.f, std::min(1.f, fwd)) * sp * dt; side = std::max(-1.f, std::min(1.f, side)) * sp * dt; up *= sp * dt;
    }
    float yaw = (rot[1] & 0xFFFF) * (6.2831853f / 65536.f), pit = rot[0] * (6.2831853f / 65536.f);
    float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pit), spt = std::sin(pit);
    float mv[3] = { cy * cp * fwd - sy * side, sy * cp * fwd + cy * side, spt * fwd + up };
    if (mv[0] || mv[1] || mv[2]) {
        // walls: the move (with 30 units to spare) must not cross the level's geometry; else each axis alone
        auto clear = [&](const float* d) {
            float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (len <= 0) return false;
            float to[3]; for (int k = 0; k < 3; k++) to[k] = loc[k] + d[k] * (1 + 30 / len);
            return worldClear(pc, loc, to);
        };
        if (clear(mv)) { for (int k = 0; k < 3; k++) loc[k] += mv[k]; }
        else for (int k = 0; k < 3; k++) { float d[3] = {}; d[k] = mv[k]; if (d[k] && clear(d)) loc[k] += d[k]; }
        if (boxOk) for (int k = 0; k < 3; k++) loc[k] = std::min(boxHi[k], std::max(boxLo[k], loc[k]));
    }
    BYTE* b = (BYTE*)pc;
    memcpy(b + offSavedLoc, loc, 12); memcpy(b + offSavedRot, rot, 12);
    memcpy(b + offLoc, loc, 12); memcpy(b + offRot, rot, 12);
    *(unsigned*)(b + offUseBaseCam) &= ~mUseBaseCam;
    *(void**)(b + offViewTarget) = pc;
    if (hud && alive(hud) && offHideHUD >= 0) *(unsigned*)((BYTE*)hud + offHideHUD) |= mHideHUD;
}

// ------------------------------------------------------------------ developer menu
// EnhancedEdition.ini [Game] DevMenu=1: the developers' switch (Version / KWVersion bDebugEnabled, as
// the console's "set") is turned on when the game starts; F7 opens their menu (KWHeroController.
// ShowDevMenu: levels, bookmarks), F2 their debug windows.
typedef void (__cdecl* GlobalSetFn)(const wchar_t*, void*, void*, int, int);
static void devTick() {
    static int st = 0;   // 0 not yet, 1 done, -1 off
    static int devOn = -1;
    if (devOn < 0) { wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"%lsEnhancedEdition.ini", g_dir); devOn = GetPrivateProfileIntW(L"Game", L"DevMenu", 0, path) != 0; }
    if (!devOn || st < 0) return;
    if (st == 0) {
        static ULONGLONG retry = 0; static int tries = 0; ULONGLONG t = GetTickCount64();
        if (t < retry || cstate <= 0) return;
        retry = t + 3000;
        if (++tries > 10) { st = -1; logf("developer menu: classes not found"); return; }
        if (!findObj(L"SHGame.Version")) scan();
        HMODULE core = GetModuleHandleW(L"Core.dll");
        auto gs = core ? (GlobalSetFn)GetProcAddress(core, "?GlobalSetProperty@UObject@@SAXPBGPAVUClass@@PAVUProperty@@HH@Z") : nullptr;
        if (!gs) { st = -1; logf("developer menu: GlobalSetProperty missing"); return; }
        int n = 0;
        for (auto pr : { std::make_pair(L"SHGame.Version", L"SHGame.Version.bDebugEnabled"), std::make_pair(L"KWGame.KWVersion", L"KWGame.KWVersion.bDebugEnabled") }) {
            void* cls = findObj(pr.first); BYTE* prop = (BYTE*)findObj(pr.second);
            if (!cls || !prop) continue;
            int off = *(int*)(prop + 0x4C);
            gs(L"True", cls, prop, off, 1); n++;
        }
        if (!n) return;
        st = 1; logf("developer menu: on (%d switches), F7 opens it, F2 the debug windows", n);
    }
    static bool prev = false;
    bool k = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    if (k && !prev && g_hwnd && GetForegroundWindow() == g_hwnd) { void* pc = playerController(); if (pc) run(pc, L"ShowDevMenu"); }
    prev = k;
}

// F11 (pad: View/Share + LB): a bug mark - a screenshot (shrz.dll) and a line with the level, the place
// and the state of things in System\BugReports\marks.txt and ShrekPad.log; the launcher packs them
static void bugTick() {
    static bool prevK = false, prevP = false;
    bool k = (GetAsyncKeyState(VK_F11) & 0x8000) != 0, pd = (padButtons & pad::BACK) && (padButtons & pad::LSHOULDER);
    bool fg = g_hwnd && GetForegroundWindow() == g_hwnd;
    bool press = fg && ((k && !prevK) || (pd && !prevP)); prevK = k; prevP = pd;
    if (!press) return;
    static int n = 0; n++;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t dir[MAX_PATH], name[64], shot[MAX_PATH];
    swprintf(dir, MAX_PATH, L"%lsBugReports", g_dir); CreateDirectoryW(dir, nullptr);
    swprintf(name, 64, L"bug_%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    swprintf(shot, MAX_PATH, L"%ls\\%ls.png", dir, name);
    // the state of things
    char info[768]; int len = 0;
    void* pc = playerController(); void* pawn = pc ? ptrAt(pc, offPawn) : nullptr; void* level = pc ? ptrAt(pc, offLevel) : nullptr;
    const wchar_t* map = L"?";
    if (level) { void* outer = *(void**)((BYTE*)level + 0x18); if (alive(outer)) { const wchar_t* m = getName(outer, nullptr); if (m) map = m; } }
    len += snprintf(info + len, sizeof info - len, "%04d-%02d-%02d %02d:%02d:%02d  %ls  level %ls", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, name, map);
    if (pawn) {
        void* cls = *(void**)((BYTE*)pawn + 0x24); const wchar_t* cn = alive(cls) ? getName(cls, nullptr) : L"?";
        float* l = offLoc >= 0 ? (float*)((BYTE*)pawn + offLoc) : nullptr;
        len += snprintf(info + len, sizeof info - len, "  hero %ls", cn ? cn : L"?");
        if (l && readable(l, 12)) len += snprintf(info + len, sizeof info - len, " at %.0f,%.0f,%.0f", l[0], l[1], l[2]);
        if (offHealth >= 0 && readable((BYTE*)pawn + offHealth, 4)) len += snprintf(info + len, sizeof info - len, " health %.0f", *(float*)((BYTE*)pawn + offHealth));
    }
    len += snprintf(info + len, sizeof info - len, "  menu=%d photo=%d fly=%d pad=%d", cursorShown() ? 1 : 0, photoActive ? 1 : 0, flyActive ? 1 : 0, ShrekPadUsingPad());
    logf("=== BUG MARK %d: %s", n, info);
    wchar_t mp[MAX_PATH]; swprintf(mp, MAX_PATH, L"%ls\\marks.txt", dir);
    if (FILE* f = _wfopen(mp, L"ab")) { fprintf(f, "%s\r\n", info); fclose(f); }
    HMODULE z = GetModuleHandleW(L"shrz.dll");
    typedef void (__cdecl* ShotFn)(const wchar_t*); typedef void (__cdecl* NoteFn)(const wchar_t*);
    auto shotF = z ? (ShotFn)GetProcAddress(z, "ShrBugShot") : nullptr; auto note = z ? (NoteFn)GetProcAddress(z, "ShrNotify") : nullptr;
    if (shotF) shotF(shot);
    typedef void (__cdecl* CapFn)(); auto cap = z ? (CapFn)GetProcAddress(z, "ShrCaptureFrame") : nullptr;
    if (cap) cap();   // every draw of the next frame and its state: ShrekRender_frame.txt
    static int en = -1;
    if (en < 0) { wchar_t ini[MAX_PATH], b[16]; swprintf(ini, MAX_PATH, L"%lsDefault.ini", g_dir); GetPrivateProfileStringW(L"Engine.Engine", L"Language", L"Rus", b, 16, ini); en = !_wcsicmp(b, L"int"); }
    wchar_t msg[96];
    if (en) swprintf(msg, 96, L"Bug mark %d saved", n); else swprintf(msg, 96, L"\u041e\u0442\u043c\u0435\u0442\u043a\u0430 \u2116%d \u0441\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u0430", n);   // "Отметка №N сохранена"
    if (note) note(msg);
}

// the level's sun for shrz.dll (shadows, rays), once per level - a saved game is loaded under the save's
// name, so the renderer cannot tell the map by its file. This engine has no Sunlight class: the sun is a
// Light with LightEffect LE_Sunlight (19 here - the enum has no LE_Negative), else a directional Light.
static void sunTick() {
    static void* lastLevel = nullptr; static ULONGLONG since = 0; static int tries = 0;
    void* pc = playerController(); void* level = pc ? ptrAt(pc, offLevel) : nullptr;
    ULONGLONG t = GetTickCount64();
    if (level != lastLevel) { lastLevel = level; since = t; tries = 0; }
    if (!level || tries >= 3 || t - since < 1000u + 1500u * tries || offRot < 0) return;
    tries++;
    static int oFx = -2, oDir = -2; static unsigned mDir = 0;
    void* cls = findObj(L"Engine.Light");
    if (!cls || oFx == -2) { scan(); cls = findObj(L"Engine.Light"); if (cls && !propOff(L"Engine.Actor.LightEffect", oFx)) oFx = -1; if (cls && !boolOff(L"Engine.Actor.bDirectional", oDir, mDir)) oDir = -1; }
    if (!cls) { logf("sun: no Light class"); return; }
    void* best = nullptr; int bestRank = 0, nLights = 0;
    for (int i = 0; i < objs->num; i++) {
        void* o = objs->data[i];
        if (!o || !isA(o, nullptr, cls)) continue;
        BYTE* b = (BYTE*)o;
        if (!readable(b + offLevel, 4) || *(void**)(b + offLevel) != level || !readable(b + offRot, 12)) continue;
        nLights++;
        int rank = 0;
        if (oFx >= 0 && readable(b + oFx, 1) && b[oFx] == 19) rank = 2;
        else if (oDir >= 0 && readable(b + oDir, 4) && (*(unsigned*)(b + oDir) & mDir)) rank = 1;
        if (rank > bestRank) { bestRank = rank; best = o; }
    }
    if (!best) { if (tries >= 3) logf("sun: none in this level (%d lights)", nLights); return; }
    tries = 3;
    int* r = (int*)((BYTE*)best + offRot);
    float pitch = (r[0] & 0xFFFF) * 360.f / 65536.f, yaw = (r[1] & 0xFFFF) * 360.f / 65536.f;
    if (pitch > 180) pitch -= 360;
    if (yaw > 180) yaw -= 360;
    HMODULE z = GetModuleHandleW(L"shrz.dll");
    typedef void (__cdecl* SunFn)(float, float); auto f = z ? (SunFn)GetProcAddress(z, "ShrSetSun") : nullptr;
    if (f) f(pitch, yaw);
    logf("sun: %ls, pitch %.1f yaw %.1f (%d lights)", bestRank == 2 ? L"LE_Sunlight" : L"directional light", pitch, yaw, nLights);
}

// The renderer's warm-up after a level load (shrz.dll keeps the screen black until the level's shaders
// and shadows are ready): the game stays paused meanwhile, so no scene runs on unseen. At most 5 s.
static void warmTick() {
    typedef int (__cdecl* BusyFn)(); static BusyFn busy = nullptr; static bool looked = false;
    if (!looked) { looked = true; HMODULE z = GetModuleHandleW(L"shrz.dll"); busy = z ? (BusyFn)GetProcAddress(z, "ShrWarmupBusy") : nullptr; }
    if (!busy) return;
    static int oPRI = -2, oPauser = -2; static void* held = nullptr; static ULONGLONG since = 0;
    if (oPRI == -2) { if (!propOff(L"Engine.Controller.PlayerReplicationInfo", oPRI)) oPRI = -1; if (!propOff(L"Engine.LevelInfo.Pauser", oPauser)) oPauser = -1; }
    if (oPRI < 0 || oPauser < 0) return;
    ULONGLONG t = GetTickCount64();
    bool b = busy() != 0;
    if (held) {
        if (!alive(held)) { held = nullptr; return; }
        void** pz = (void**)((BYTE*)held + oPauser);
        if (!b || t - since > 5000) { if (readable(pz, 4)) *pz = nullptr; held = nullptr; logf("warm-up: game resumed"); }
        return;
    }
    if (!b) return;
    void* pc = playerController(); void* level = pc ? ptrAt(pc, offLevel) : nullptr; void* pri = pc ? ptrAt(pc, oPRI) : nullptr;
    if (!level || !pri || !readable((BYTE*)level + oPauser, 4) || *(void**)((BYTE*)level + oPauser)) return;
    *(void**)((BYTE*)level + oPauser) = pri; held = level; since = t;
    logf("warm-up: game paused while the level is prepared");
}

// The angle of view. The game's camera uses FovAngle and sets it from DefaultFOV only at the end of a
// scene, and DefaultFOV is read from the ini section of the hero's own controller class - so the value of
// [Engine.PlayerController] (the launcher, the FOV command) never reached the screen: it stayed at 85.
// ShrekPad keeps the chosen angle itself: User.ini [Engine.PlayerController] DefaultFOV, outside scenes.
// Insert: by turns old 85, modern 95, new 105.
static std::wstring userIniPath() {
    wchar_t docs[MAX_PATH] = L""; SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs);
    wchar_t p[MAX_PATH]; swprintf(p, MAX_PATH, L"%ls\\Shrek 2\\User.ini", docs); return p;
}
typedef int (__cdecl* BookFn)();
static float g_fov = 0;
static bool inScene(void* pc) {
    static int oHud = -2, oCtc = -2, oIn = -2; static unsigned mIn = 0;
    if (oHud == -2 && !propOff(L"Engine.PlayerController.myHUD", oHud)) oHud = -1;
    if (oCtc == -2 && !propOff(L"KWGame.KWHud.CutTextController", oCtc)) oCtc = -1;
    if (oIn == -2 && !boolOff(L"KWGame.KWCutTextController.bCutSceneInProgress", oIn, mIn)) oIn = -1;
    if (oHud < 0 || oCtc < 0 || oIn < 0) return false;
    void* hud = ptrAt(pc, oHud); void* ctc = hud ? ptrAt(hud, oCtc) : nullptr;
    return ctc && readable((BYTE*)ctc + oIn, 4) && (*(unsigned*)((BYTE*)ctc + oIn) & mIn);
}
static bool isBookNow() {
    static BookFn f = nullptr; static bool looked = false;
    if (!looked) { looked = true; HMODULE z = GetModuleHandleW(L"shrz.dll"); f = z ? (BookFn)GetProcAddress(z, "ShrIsBook") : nullptr; }
    return f && f();
}
static void fovTick() {
    static bool prev = false, loaded = false; static ULONGLONG next = 0;
    static int oDef = -2, oDes = -2, oAng = -2;
    if (!loaded) {
        loaded = true;
        wchar_t v[32]; GetPrivateProfileStringW(L"Engine.PlayerController", L"DefaultFOV", L"95", v, 32, userIniPath().c_str());
        g_fov = (float)_wtof(v); if (g_fov < 60 || g_fov > 130) g_fov = 95;
        logf("fov: %.0f", g_fov);
    }
    void* pc = playerController(); if (!pc) return;
    bool k = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    bool press = k && !prev; prev = k;
    if (press && g_hwnd && GetForegroundWindow() == g_hwnd && !cursorShown() && !(isBookNow())) {
        g_fov = g_fov < 90 ? 95.f : g_fov < 100 ? 105.f : 85.f;
        wchar_t cmd[32]; swprintf(cmd, 32, L"FOV %d", (int)g_fov); run(pc, cmd);
        wchar_t v[32]; swprintf(v, 32, L"%.6f", g_fov);
        std::wstring u = userIniPath();
        WritePrivateProfileStringW(L"Engine.PlayerController", L"DefaultFOV", v, u.c_str());
        WritePrivateProfileStringW(L"Engine.PlayerController", L"DesiredFOV", v, u.c_str());
        next = 0; logf("fov: %.0f (Insert)", g_fov);
        HMODULE z = GetModuleHandleW(L"shrz.dll"); typedef void (__cdecl* NoteFn)(const wchar_t*);
        if (auto n = z ? (NoteFn)GetProcAddress(z, "ShrNotify") : nullptr) { wchar_t m[48]; swprintf(m, 48, L"FOV %d", (int)g_fov); n(m); }
    }
    ULONGLONG t = GetTickCount64(); if (t < next) return; next = t + 16;
    if (oDef == -2) { if (!propOff(L"Engine.PlayerController.DefaultFOV", oDef)) oDef = -1; if (!propOff(L"Engine.PlayerController.DesiredFOV", oDes)) oDes = -1; if (!propOff(L"Engine.Controller.FovAngle", oAng) && !propOff(L"Engine.Actor.FovAngle", oAng)) oAng = -1; }
    if (oDef < 0 || oDes < 0 || oAng < 0) return;
    BYTE* b = (BYTE*)pc;
    if (!readable(b + oDef, 4) || !readable(b + oDes, 4) || !readable(b + oAng, 4)) return;
    // the storybook (menu, story pages): its text is laid over the picture for the original angle 85 -
    // a wider or narrower view makes the book bigger or smaller under the text
    static BookFn isBook = nullptr; static bool lookedBook = false;
    if (!lookedBook) { lookedBook = true; HMODULE z = GetModuleHandleW(L"shrz.dll"); isBook = z ? (BookFn)GetProcAddress(z, "ShrIsBook") : nullptr; }
    if (isBook && isBook()) {
        *(float*)(b + oDef) = 85.f; *(float*)(b + oDes) = 85.f; *(float*)(b + oAng) = 85.f;
        return;
    }
    *(float*)(b + oDef) = g_fov;                 // a scene's end resets to this
    if (inScene(pc) || cursorShown() || photoActive) return;   // scenes keep their own angles; menus too
    { void* pawn = ptrAt(pc, offPawn); if (!pawn || ptrAt(pawn, offPawnCtrl) != pc) return; }   // only while the player has the hero
    float& des = *(float*)(b + oDes); float& ang = *(float*)(b + oAng);
    {   // an angle change of the game going on: wait while the two keep moving; if they stand still apart
        // (a level script left a different DesiredFOV), take over after 0.4 s instead of waiting for ever
        static float pd = 0, pa = 0; static ULONGLONG still = 0;
        if (fabsf(des - ang) > 0.01f) {
            if (fabsf(des - pd) > 0.01f || fabsf(ang - pa) > 0.01f) { pd = des; pa = ang; still = t; return; }
            if (!still) still = t;
            if (t - still < 400) return;
            logf("fov: stalled at %.1f/%.1f, taking over", ang, des);
        } else still = 0;
    }
    if (fabsf(ang - g_fov) > 0.01f) { des = g_fov; ang = g_fov; }
}

static void cheatTick() {
    static bool cfgLoaded = false; if (!cfgLoaded) { cfgLoaded = true; loadCheatCfg(); }
    if (!ccfg.on && !ccfg.freeLook) return;
    ULONGLONG now = GetTickCount64();
    static ULONGLONG next = 0;
    static void* flyPawn = nullptr; static float savedAir = -1.f;
    if (ccfg.freeLook) flyStep(flyPawn);
    void* pc = playerController();
    if (!pc) return;
    if (now < next) return;
    next = now + 250;
    void* pawn = ptrAt(pc, offPawn); void* level = ptrAt(pc, offLevel);
    if (!pawn || !level) return;
    if (pawn != flyPawn) {   // a new hero (or a switch of heroes): it walks until flying is switched on again
        if (flyActive) logf("free look: the hero changed, flying off");
        flyActive = false; flyPawn = pawn; savedAir = -1.f;
    }
    // once per level, two seconds after the hero is there
    static void* doneLevel = nullptr; static void* seenLevel = nullptr; static ULONGLONG seenAt = 0;
    if (level != seenLevel) { seenLevel = level; seenAt = now; }
    if (level != doneLevel && now - seenAt > 2000) {
        doneLevel = level;
        void* cm = ptrAt(pc, offCheat);
        if (ccfg.on && ccfg.coins > 0) { wchar_t c[48]; swprintf(c, 48, L"AddCoins %d", ccfg.coins); run(pc, c); }
        if (ccfg.on && ccfg.potions > 0) { wchar_t c[48]; swprintf(c, 48, L"AddPotions %d", ccfg.potions); run(pc, c); }
        if (ccfg.on && ccfg.speed != 100 && cm) { wchar_t c[48]; swprintf(c, 48, L"SloMo %.2f", ccfg.speed / 100.0); run(cm, c); }
    }
    // a hidden place: the hero is taken there (and walks on) once the player has had him for a moment -
    // the pawn's controller is the player's own, not a scene's; levels made of scenes only get the
    // photo camera there instead
    if (ccfg.spot) {
        static bool spotDone = false; static ULONGLONG ownSince = 0;
        bool own = ptrAt(pawn, offPawnCtrl >= 0 ? offPawnCtrl : -1) == pc;
        if (!spotDone && !ccfg.spotPhoto) {
            if (own) { if (!ownSince) ownSince = now; else if (now - ownSince > 1500) {
                spotDone = true;
                void* cm = ptrAt(pc, offCheat);
                bool ok = cm && run(cm, L"Ghost") && setLocation(pawn, ccfg.sx, ccfg.sy, ccfg.sz);
                if (offVel >= 0) memset((BYTE*)pawn + offVel, 0, 12);
                if (cm) run(cm, L"Walk");
                logf("hidden place: the hero to %.0f %.0f %.0f -> %s", ccfg.sx, ccfg.sy, ccfg.sz, ok ? "ok" : "refused");
            } } else ownSince = 0;
        }
    }
    // free look: F8 or both stick clicks switch flying on and off - only when the player has the hero
    // for a while (never during the level's loading or the start of a cutscene)
    if (ccfg.freeLook && offRot >= 0) {
        bool want = flyToggled;
        flyToggled = false;
        if (want && now - seenAt > 1500) {
            void* cm = ptrAt(pc, offCheat);
            if (!flyActive && cm && run(cm, L"Ghost")) {
                // the game's own flying moves along the view, pitch included, and stops the hero whenever
                // no key is held: off, the movement is done here
                unsigned* w = (unsigned*)((BYTE*)pc + offCheatFly); if (readable(w, 4)) *w &= ~mCheatFly;
                float* air = (float*)((BYTE*)pawn + offAir);
                if (readable(air, 4)) { savedAir = *air; *air = 6000.f; }
                flyPc = pc; flyActive = true;
                logf("free look: flying on");
            } else if (flyActive) {
                flyActive = false;
                float* air = (float*)((BYTE*)pawn + offAir);
                if (savedAir > 0 && readable(air, 4)) *air = savedAir;
                if (cm) run(cm, L"Walk");
                logf("free look: flying off");
            }
        }
    }
    if (((ccfg.on && ccfg.health) || ccfg.freeLook) && offHealth >= 0) {
        float* h = (float*)((BYTE*)pawn + offHealth);
        if (readable(h, 4) && *h > 0 && *h < 100) *h = 100;     // never revives (0 = already dying)
    }
}
}  // namespace gui
static bool flyKeysTaken() { return gui::flyActive || gui::photoActive; }
static bool photoKeysTaken() { return gui::photoActive; }

// ------------------------------------------------------------------ the virtual device
struct VPad : IDirectInputDevice8W {
    LONG refs = 1;
    pad::Mapper map;
    pad::MenuDriver menu;
    LARGE_INTEGER last{}, freq{};
    bool loggedPad = false;

    VPad() { map.s = g_set; QueryPerformanceFrequency(&freq); }

    STDMETHODIMP QueryInterface(REFIID riid, void** p) override {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDirectInputDevice8W)) { *p = this; AddRef(); return S_OK; }
        *p = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&refs);
        if (!r) { if (g_sony) g_sony->restoreLightbar(); dsxRelease(); delete this; }   // the game releases its joystick on exit
        return r;
    }

    STDMETHODIMP GetCapabilities(LPDIDEVCAPS c) override {
        if (!c) return E_POINTER;
        DWORD sz = c->dwSize; memset(c, 0, sz); c->dwSize = sz;
        c->dwFlags = DIDC_ATTACHED | DIDC_EMULATED; c->dwDevType = DI8DEVTYPE_GAMEPAD | (DI8DEVTYPEGAMEPAD_STANDARD << 8);
        c->dwAxes = 6; c->dwButtons = 12; c->dwPOVs = 1;
        return DI_OK;
    }
    STDMETHODIMP EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKW cb, LPVOID ref, DWORD flags) override {
        static const GUID* g[6] = { &GUID_XAxis, &GUID_YAxis, &GUID_ZAxis, &GUID_RxAxis, &GUID_RyAxis, &GUID_RzAxis };
        static const wchar_t* n[6] = { L"Left X", L"Left Y", L"Left trigger", L"Right X", L"Right Y", L"Right trigger" };
        if (flags == DIDFT_ALL || (flags & DIDFT_AXIS)) {
            for (int i = 0; i < 6; i++) {
                DIDEVICEOBJECTINSTANCEW o{}; o.dwSize = sizeof o; o.guidType = *g[i];
                o.dwOfs = (DWORD)(i * 4); o.dwType = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(i);
                wcscpy(o.tszName, n[i]);
                if (cb(&o, ref) == DIENUM_STOP) return DI_OK;
            }
        }
        return DI_OK;
    }
    STDMETHODIMP GetProperty(REFGUID, LPDIPROPHEADER) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP SetProperty(REFGUID, LPCDIPROPHEADER) override { return DI_OK; }     // range etc.: we always report 0..65535
    STDMETHODIMP Acquire() override { return DI_OK; }
    STDMETHODIMP Unacquire() override { return DI_OK; }
    unsigned polls = 0; ULONGLONG lastSummary = 0; bool lastMenuLogged = false, lastFg = true;
    uint16_t lastButtons = 0; int changes = 0;
    STDMETHODIMP GetDeviceState(DWORD cb, LPVOID data) override {
        if (!data) return E_POINTER;
        if (polls++ == 0) logf("game polls the gamepad (state size %lu)", cb);
        if (!g_hwnd) hookGameWindow();
        pollDIMouse();
        gui::cheatTick();
        gui::subsTick();
        gui::devTick();
        gui::fovTick();
        gui::sunTick();
        gui::warmTick();
        gui::bugTick();
        gui::lightTick();
        gui::posterTick();
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        float dt = last.QuadPart ? (float)(now.QuadPart - last.QuadPart) / freq.QuadPart : 0.f;
        last = now; dt = std::min(std::max(dt, 0.f), 0.1f);
        gui::photoTick(dt > 0 ? dt : 1.f / 60.f);

        pad::XState x = g_enabled ? readPad() : pad::XState();
        gui::padButtons = x.buttons; gui::padLX = x.lx; gui::padLY = x.ly; gui::padRX = x.rx; gui::padRY = x.ry; gui::padLT = x.lt; gui::padRT = x.rt;
        g_padConnected = x.connected;
        if (x.connected && (x.buttons || std::abs(x.lx) > 9000 || std::abs(x.ly) > 9000 || std::abs(x.rx) > 9000 || std::abs(x.ry) > 9000 || x.lt > 60 || x.rt > 60))
            g_lastPadUse = GetTickCount64();
        if (x.connected != loggedPad) { logf(x.connected ? "controller connected" : "controller disconnected"); loggedPad = x.connected; }
        bool fg = gameInForeground();
        if (fg != lastFg) { logf(fg ? "game active" : "game not active"); if (!fg) logForeground(); lastFg = fg; }
        if (!fg) x = pad::XState();
        bool inMenu = fg && cursorShown();
        g_inMenuNow = cursorShown();
        {   // menus at 60 frames per second (shrz.dll): no full load of the card behind a menu
            typedef void (__cdecl* CapFn)(int); static CapFn cap = nullptr; static bool looked = false; static int sent = -1;
            if (!looked) { looked = true; HMODULE z = GetModuleHandleW(L"shrz.dll"); cap = z ? (CapFn)GetProcAddress(z, "ShrSetFrameCap") : nullptr; }
            int want = inMenu ? 60 : 0;
            if (cap && want != sent) { cap(want); sent = want; }
        }
        if (inMenu != lastMenuLogged) { logf(inMenu ? "menu mode (pad drives the mouse)" : "game mode"); lastMenuLogged = inMenu; }
        if (GetTickCount64() - lastSummary > 10000) {
            lastSummary = GetTickCount64();
            logf("poll %u: pad=%d fg=%d buttons=%04x L=(%d,%d) R=(%d,%d) LT=%u RT=%u", polls, x.connected, fg, x.buttons, x.lx, x.ly, x.rx, x.ry, x.lt, x.rt);
            logRawStats();
        }
        map.setMode(inMenu, x);
        trigUpdate(fg, inMenu);

        pad::JoyState j;
        if (inMenu) {
            map.toJoy(pad::XState(), j);       // the game sees a neutral pad while the menu has the cursor
            pad::MenuOut o;
            menu.step(x, map.s, map.maskUntilRelease, dt, (float)GetSystemMetrics(SM_CYSCREEN), o);
            keyNav(o);
            sendMenu(o);
        } else {
            menu.leave();
            gui::hideCursor(false);
            pad::XState g = x;
            if (gui::photoActive) g = pad::XState();   // photo mode: the pad is the camera's
            else if (gui::flyActive) {   // free look flying takes the left stick, A / B, the triggers and the shoulders
                g.lx = g.ly = 0; g.lt = g.rt = 0;
                g.buttons &= ~(pad::BTN_A | pad::BTN_B | pad::LTHUMB | pad::RTHUMB | pad::LSHOULDER | pad::RSHOULDER);
            }
            map.toJoy(g, j, dt > 0 ? dt : 1.f / 60.f);
        }
        if (x.buttons != lastButtons && changes < 60) {
            changes++;
            logf("buttons %04x (%s): joy buttons sent to the game:%s%s%s%s%s%s%s%s pov=%lu  LX/LY=%ld/%ld", x.buttons, inMenu ? "menu" : "game",
                 j.buttons[0] ? " 1" : "", j.buttons[1] ? " 2" : "", j.buttons[2] ? " 3" : "", j.buttons[3] ? " 4" : "",
                 j.buttons[4] ? " 5" : "", j.buttons[5] ? " 6" : "", j.buttons[6] ? " 7" : "", j.buttons[7] ? " 8" : "", (unsigned long)j.pov[0], (long)j.lX, (long)j.lY);
            lastButtons = x.buttons;
        }
        // DIJOYSTATE / DIJOYSTATE2 share the leading layout
        DIJOYSTATE2 s{};
        s.lX = j.lX; s.lY = j.lY; s.lZ = j.lZ; s.lRx = j.lRx; s.lRy = j.lRy; s.lRz = j.lRz;
        s.rglSlider[0] = j.slider[0]; s.rglSlider[1] = j.slider[1];
        for (int i = 0; i < 4; i++) s.rgdwPOV[i] = j.pov[i];
        memcpy(s.rgbButtons, j.buttons, 128);
        memcpy(data, &s, std::min<DWORD>(cb, sizeof s));
        return DI_OK;
    }
    // the item to go to from point (px, py) in direction dir (1 up 2 down 3 left 4 right 5 next 6 previous)
    static int pickItem(const std::vector<gui::Item>& it, float px, float py, int dir) {
        int cur = -1;
        for (size_t i = 0; i < it.size(); i++) if (px >= it[i].x0 && px <= it[i].x1 && py >= it[i].y0 && py <= it[i].y1) cur = (int)i;
        auto cx = [&](int i) { return (it[i].x0 + it[i].x1) * 0.5f; };
        auto cy = [&](int i) { return (it[i].y0 + it[i].y1) * 0.5f; };
        // reading order: rows top to bottom, left to right
        std::vector<int> ord(it.size()); for (size_t i = 0; i < it.size(); i++) ord[i] = (int)i;
        std::sort(ord.begin(), ord.end(), [&](int a, int b) { float d = cy(a) - cy(b); if (std::fabs(d) > 8) return d < 0; return cx(a) < cx(b); });
        if (cur < 0 || dir == 0) {   // not on an item: the first one
            return ord.empty() ? -1 : ord[0];
        }
        if (dir >= 5) {
            size_t k = std::find(ord.begin(), ord.end(), cur) - ord.begin();
            return ord[(k + (dir == 5 ? 1 : ord.size() - 1)) % ord.size()];
        }
        float ox = cx(cur), oy = cy(cur), best = 1e30f; int bi = -1;
        float dxs = dir == 3 ? -1.f : dir == 4 ? 1.f : 0.f, dys = dir == 1 ? -1.f : dir == 2 ? 1.f : 0.f;
        for (size_t i = 0; i < it.size(); i++) {
            if ((int)i == cur) continue;
            float vx = cx((int)i) - ox, vy = cy((int)i) - oy;
            float along = vx * dxs + vy * dys, across = std::fabs(vx * dys) + std::fabs(vy * dxs);
            if (along < 4) continue;
            float score = along + across * 2.5f;
            if (score < best) { best = score; bi = (int)i; }
        }
        if (bi < 0) {   // nothing further that way: wrap round to the far end
            for (size_t i = 0; i < it.size(); i++) {
                if ((int)i == cur) continue;
                float vx = cx((int)i) - ox, vy = cy((int)i) - oy;
                float along = vx * dxs + vy * dys, across = std::fabs(vx * dys) + std::fabs(vy * dxs);
                if (along > -4) continue;
                float score = along + across * 2.5f;   // the most negative "along" = the far end
                if (bi < 0 || score < best) { best = score; bi = (int)i; }
            }
        }
        return bi < 0 ? cur : bi;
    }
    // The keyboard in menus: arrows, Tab / Shift+Tab and Enter move over the menu items the same way
    // (the game's own menus take only the mouse). Only while the items of the page can be read; keys
    // sent by the pad's fallback are not taken for the player's (a moment after one is sent).
    static inline ULONGLONG keysSentAt = 0;
    static void keyNav(pad::MenuOut& o) {
        static const int vks[6] = { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_TAB, VK_RETURN };
        static bool prev[6]; bool now[6];
        for (int i = 0; i < 6; i++) now[i] = (GetAsyncKeyState(vks[i]) & 0x8000) != 0;
        bool fresh = GetTickCount64() - keysSentAt > 150;
        bool any = false; for (int i = 0; i < 6; i++) if (now[i] != prev[i]) any = true;
        if (any && fresh && !o.nav && !o.navStart && !o.select) {
            static std::vector<gui::Item> it;
            if (gui::items(it) && !it.empty()) {
                for (int i = 0; i < 4; i++) if (now[i] && !prev[i]) o.nav = i + 1;
                if (now[4] && !prev[4]) o.nav = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 6 : 5;
                static bool pressed = false;
                if (now[5] && !prev[5]) {
                    POINT cp; GetCursorPos(&cp); if (g_hwnd) ScreenToClient(g_hwnd, &cp);
                    bool onItem = false;
                    for (auto& t : it) if (cp.x >= t.x0 && cp.x <= t.x1 && cp.y >= t.y0 && cp.y <= t.y1) onItem = true;
                    if (onItem) { o.select = 1; pressed = true; } else o.navStart = true;   // Enter first: onto the first item
                }
                if (!now[5] && prev[5] && pressed) { o.select = 2; pressed = false; }
            }
        }
        for (int i = 0; i < 6; i++) prev[i] = now[i];
    }
    static void sendMenu(const pad::MenuOut& o) {
        INPUT in[24]; int n = 0;
        auto mouse = [&](DWORD f, LONG dx, LONG dy, DWORD data) { INPUT i{}; i.type = INPUT_MOUSE; i.mi.dwFlags = f; i.mi.dx = dx; i.mi.dy = dy; i.mi.mouseData = data; in[n++] = i; };
        auto key = [&](WORD vk, bool up) {
            keysSentAt = GetTickCount64();
            INPUT i{}; i.type = INPUT_KEYBOARD; i.ki.wVk = vk; i.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
            i.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0; in[n++] = i;
        };
        if (o.dx || o.dy) { mouse(MOUSEEVENTF_MOVE, (LONG)o.dx, (LONG)o.dy, 0); gui::hideCursor(false); }
        if (o.wheel) mouse(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)(o.wheel * WHEEL_DELTA));
        if (o.leftDown) mouse(MOUSEEVENTF_LEFTDOWN, 0, 0, 0);
        if (o.leftUp) mouse(MOUSEEVENTF_LEFTUP, 0, 0, 0);
        if (o.escape) { key(VK_ESCAPE, false); key(VK_ESCAPE, true); }
        if (o.nav || o.navStart || o.select) {
            static std::vector<gui::Item> it;
            bool have = gui::items(it) && !it.empty() && g_hwnd;
            static int saidN = -2, lines = 0;
            int nn = have ? (int)it.size() : -1;
            if (nn != saidN && lines < 40) { saidN = nn; lines++; logf(have ? "menu navigation: %d items on the page" : "menu navigation: no menu items readable, keys instead", (int)it.size()); }
            if (have) {
                POINT cp; GetCursorPos(&cp); ScreenToClient(g_hwnd, &cp);
                if (o.nav || o.navStart) {
                    int k = pickItem(it, (float)cp.x, (float)cp.y, o.navStart ? 0 : o.nav);
                    if (k >= 0) {
                        POINT p{ (LONG)((it[k].x0 + it[k].x1) * 0.5f), (LONG)((it[k].y0 + it[k].y1) * 0.5f) };
                        ClientToScreen(g_hwnd, &p); SetCursorPos(p.x, p.y);
                        mouse(MOUSEEVENTF_MOVE, 0, 0, 0);       // the game sees the move (hover highlight)
                        gui::hideCursor(true);
                    }
                }
                if (o.select == 1) mouse(MOUSEEVENTF_LEFTDOWN, 0, 0, 0);
                if (o.select == 2) mouse(MOUSEEVENTF_LEFTUP, 0, 0, 0);
            } else {   // keys as before
                if (o.navStart) key(VK_TAB, false), key(VK_TAB, true);
                if (o.nav) {
                    WORD vk = o.nav == 1 ? VK_UP : o.nav == 2 ? VK_DOWN : o.nav == 3 ? VK_LEFT : o.nav == 4 ? VK_RIGHT : VK_TAB;
                    if (o.nav == 6) key(VK_SHIFT, false);
                    key(vk, false); key(vk, true);
                    if (o.nav == 6) key(VK_SHIFT, true);
                }
                if (o.select == 1) key(VK_RETURN, false);
                if (o.select == 2) key(VK_RETURN, true);
            }
        }
        for (int k = 0; k < o.nkeys; k++) {
            const pad::KeyTap& t = o.keys[k];
            if (t.shift) key(VK_SHIFT, false);
            if (t.down) key(t.vk, false);
            if (t.up) key(t.vk, true);
            if (t.shift) key(VK_SHIFT, true);
        }
        if (o.park && g_hwnd) {     // the cursor to the bottom right corner of the game window: no hover highlight
            RECT r; if (GetClientRect(g_hwnd, &r)) { POINT p{ r.right - 2, r.bottom - 2 }; ClientToScreen(g_hwnd, &p); SetCursorPos(p.x, p.y); }
        }
        if (n) SendInput(n, in, sizeof(INPUT));
    }
    STDMETHODIMP GetDeviceData(DWORD, LPDIDEVICEOBJECTDATA, LPDWORD n, DWORD) override { if (n) *n = 0; return DI_OK; }
    STDMETHODIMP SetDataFormat(LPCDIDATAFORMAT f) override { logf("SetDataFormat (data size %lu)", f ? f->dwDataSize : 0); return DI_OK; }
    STDMETHODIMP SetEventNotification(HANDLE) override { return DI_OK; }
    STDMETHODIMP SetCooperativeLevel(HWND, DWORD) override { return DI_OK; }
    STDMETHODIMP GetObjectInfo(LPDIDEVICEOBJECTINSTANCEW, DWORD, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP GetDeviceInfo(LPDIDEVICEINSTANCEW d) override { fillInstance(d); return DI_OK; }
    STDMETHODIMP RunControlPanel(HWND, DWORD) override { return DI_OK; }
    STDMETHODIMP Initialize(HINSTANCE, DWORD, REFGUID) override { return DI_OK; }
    STDMETHODIMP CreateEffect(REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT*, LPUNKNOWN) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP EnumEffects(LPDIENUMEFFECTSCALLBACKW, LPVOID, DWORD) override { return DI_OK; }
    STDMETHODIMP GetEffectInfo(LPDIEFFECTINFOW, REFGUID) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP GetForceFeedbackState(LPDWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP SendForceFeedbackCommand(DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, LPVOID, DWORD) override { return DI_OK; }
    STDMETHODIMP Escape(LPDIEFFESCAPE) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP Poll() override { return DI_OK; }
    STDMETHODIMP SendDeviceData(DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP EnumEffectsInFile(LPCWSTR, LPDIENUMEFFECTSINFILECALLBACK, LPVOID, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP WriteEffectToFile(LPCWSTR, DWORD, LPDIFILEEFFECT, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP BuildActionMap(LPDIACTIONFORMATW, LPCWSTR, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP SetActionMap(LPDIACTIONFORMATW, LPCWSTR, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP GetImageInfo(LPDIDEVICEIMAGEINFOHEADERW) override { return DIERR_UNSUPPORTED; }

    static void fillInstance(LPDIDEVICEINSTANCEW d) {
        if (!d) return;
        DWORD sz = d->dwSize ? d->dwSize : sizeof(DIDEVICEINSTANCEW);
        memset(d, 0, sz); d->dwSize = sz;
        d->guidInstance = GUID_ShrekPad; d->guidProduct = GUID_ShrekPad;
        d->dwDevType = DI8DEVTYPE_GAMEPAD | (DI8DEVTYPEGAMEPAD_STANDARD << 8);
        wcscpy(d->tszInstanceName, L"Shrek Remaster gamepad (XInput)");
        wcscpy(d->tszProductName, L"Shrek Remaster gamepad (XInput)");
    }
};

// ------------------------------------------------------------------ IDirectInput8W wrapper
struct DI8 : IDirectInput8W {
    LONG refs = 1;
    IDirectInput8W* real;
    explicit DI8(IDirectInput8W* r) : real(r) { if (!g_realDI) { g_realDI = r; r->AddRef(); } }
    STDMETHODIMP QueryInterface(REFIID riid, void** p) override {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDirectInput8W)) { *p = this; AddRef(); return S_OK; }
        return real->QueryInterface(riid, p);
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override { LONG r = InterlockedDecrement(&refs); if (!r) { real->Release(); delete this; } return r; }
    STDMETHODIMP CreateDevice(REFGUID g, LPDIRECTINPUTDEVICE8W* dev, LPUNKNOWN outer) override {
        if (IsEqualGUID(g, GUID_ShrekPad)) { *dev = new VPad(); logf("virtual gamepad created"); return DI_OK; }
        HRESULT hr = real->CreateDevice(g, dev, outer);
        logf("game CreateDevice %s -> 0x%08lx", IsEqualGUID(g, GUID_SysMouse) ? "SysMouse" : IsEqualGUID(g, GUID_SysKeyboard) ? "SysKeyboard" : "other", hr);
        return hr;
    }
    STDMETHODIMP EnumDevices(DWORD type, LPDIENUMDEVICESCALLBACKW cb, LPVOID ref, DWORD flags) override {
        // our controller first: the game takes the first game controller it is offered
        bool wantPads = type == DI8DEVCLASS_ALL || type == DI8DEVCLASS_GAMECTRL || type == DI8DEVTYPE_GAMEPAD || type == DI8DEVTYPE_JOYSTICK;
        logf("EnumDevices(type %lu, flags %lu)", type, flags);
        if (g_enabled && (g_xget || g_sonyEnabled) && wantPads) {
            DIDEVICEINSTANCEW d{}; d.dwSize = sizeof d; VPad::fillInstance(&d);
            BOOL r = cb(&d, ref);
            logf("  offered the ShrekPad gamepad -> %s", r == DIENUM_STOP ? "taken" : "skipped, continuing");
            if (r == DIENUM_STOP) return DI_OK;
        }
        return real->EnumDevices(type, cb, ref, flags);
    }
    STDMETHODIMP GetDeviceStatus(REFGUID g) override { return IsEqualGUID(g, GUID_ShrekPad) ? DI_OK : real->GetDeviceStatus(g); }
    STDMETHODIMP RunControlPanel(HWND h, DWORD f) override { return real->RunControlPanel(h, f); }
    STDMETHODIMP Initialize(HINSTANCE h, DWORD v) override { return real->Initialize(h, v); }
    STDMETHODIMP FindDevice(REFGUID a, LPCWSTR b, LPGUID c) override { return real->FindDevice(a, b, c); }
    STDMETHODIMP EnumDevicesBySemantics(LPCWSTR a, LPDIACTIONFORMATW b, LPDIENUMDEVICESBYSEMANTICSCBW c, LPVOID d, DWORD e) override { return real->EnumDevicesBySemantics(a, b, c, d, e); }
    STDMETHODIMP ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK a, LPDICONFIGUREDEVICESPARAMSW b, DWORD c, LPVOID d) override { return real->ConfigureDevices(a, b, c, d); }
};

// ------------------------------------------------------------------ exports
typedef HRESULT(WINAPI* DI8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static HMODULE g_real;
static HMODULE realDll() {
    if (!g_real) {
        wchar_t p[MAX_PATH]; GetSystemDirectoryW(p, MAX_PATH); wcscat(p, L"\\dinput8.dll");
        g_real = LoadLibraryW(p);
    }
    return g_real;
}

static void initOnce();
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    initOnce();
    logf("DirectInput8Create(version %lx, %s)", ver, IsEqualIID(riid, IID_IDirectInput8W) ? "unicode" : IsEqualIID(riid, IID_IDirectInput8A) ? "ANSI" : "other");
    HMODULE m = realDll();
    if (!m) logf("system dinput8.dll not found");
    auto f = m ? (DI8Create_t)GetProcAddress(m, "DirectInput8Create") : nullptr;
    if (!f) return DIERR_GENERIC;
    HRESULT hr = f(inst, ver, riid, out, outer);
    if (FAILED(hr) || !out || !*out) return hr;
    initXInput();
    if (IsEqualIID(riid, IID_IDirectInput8W)) *out = static_cast<IDirectInput8W*>(new DI8((IDirectInput8W*)*out));
    return hr;
}
extern "C" HRESULT WINAPI DllCanUnloadNow() { return S_FALSE; }
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID c, REFIID r, LPVOID* p) {
    typedef HRESULT(WINAPI * F)(REFCLSID, REFIID, LPVOID*);
    HMODULE m = realDll(); F f = m ? (F)GetProcAddress(m, "DllGetClassObject") : nullptr;
    return f ? f(c, r, p) : CLASS_E_CLASSNOTAVAILABLE;
}
extern "C" HRESULT WINAPI DllRegisterServer() { return E_FAIL; }
extern "C" HRESULT WINAPI DllUnregisterServer() { return E_FAIL; }

// what the game will do with the pad: its bindings in Documents\Shrek 2\User.ini and UseJoystick
static void logBindings() {
    wchar_t docs[MAX_PATH] = L"";
    SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs);
    wchar_t user[MAX_PATH], def[MAX_PATH], defu[MAX_PATH];
    swprintf(user, MAX_PATH, L"%ls\\Shrek 2\\User.ini", docs);
    swprintf(def, MAX_PATH, L"%lsDefault.ini", g_dir); swprintf(defu, MAX_PATH, L"%lsDefUser.ini", g_dir);
    wchar_t v[256];
    GetPrivateProfileStringW(L"WinDrv.WindowsClient", L"UseJoystick", L"(none)", v, 256, def);
    logf("Default.ini UseJoystick=%ls", v);
    for (const wchar_t* f : { (const wchar_t*)user, (const wchar_t*)defu }) {
        DWORD attr = GetFileAttributesW(f);
        logf("%ls %s", f, attr == INVALID_FILE_ATTRIBUTES ? "(missing)" : "");
        if (attr == INVALID_FILE_ATTRIBUTES) continue;
        for (const wchar_t* k : { L"Joy1", L"Joy3", L"Joy4", L"Joy8", L"JoyX", L"JoyY", L"JoyR", L"JoyU" }) {
            GetPrivateProfileStringW(L"Engine.Input", k, L"(none)", v, 256, f);
            logf("    %ls=%ls", k, v);
        }
    }
}

static HMODULE g_self;
static void initOnce() {
    static bool done = false; if (done) return; done = true;
    {
        HMODULE h = g_self;
        if (!h) GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&initOnce, &h);
        GetModuleFileNameW(h, g_dir, MAX_PATH);
        wchar_t* s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0;
        loadSettings();
        wchar_t lp[MAX_PATH]; swprintf(lp, MAX_PATH, L"%lsShrekPad.log", g_dir);
        { wchar_t pv[MAX_PATH]; swprintf(pv, MAX_PATH, L"%lsShrekPad.prev.log", g_dir); MoveFileExW(lp, pv, MOVEFILE_REPLACE_EXISTING); }   // the run before stays (bug reports)
        if (iniF(L"Log", 1) != 0) g_log = _wfopen(lp, L"w");
        if (!g_log) { wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t); wcscat(t, L"ShrekPad.log"); if (iniF(L"Log", 1) != 0) g_log = _wfopen(t, L"w"); }
        logf("ShrekPad 1.27 (%ls): triggers %d (R2 %d%% at %d%%, L2 %d%%, Donkey/Puss %d%%) dsx %d mouse x%.2f enabled=%d sony=%d deadzone=%.2f/%.2f camera=%.2f curve=%.2f invertY=%d swapAB=%d",
             g_dir, g_adaptive, g_trR2, g_trR2Point, g_trL2, g_trSmall, (int)g_dsx, g_mouseSens, g_enabled, g_sonyEnabled, g_set.deadzoneLeft, g_set.deadzoneRight, g_set.cameraSpeed, g_set.cameraCurve, g_set.invertY, g_set.swapAB);
        logBindings();
        hookCursor();
    }
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_self = h; DisableThreadLibraryCalls(h); }
    if (reason == DLL_PROCESS_DETACH) {
        if (g_xset && g_xIndex.load() >= 0) { XINPUT_VIBRATION_ v{ 0, 0 }; g_xset((DWORD)g_xIndex.load(), &v); }
        if (g_sony) g_sony->restoreLightbar();
        dsxRelease();
    }
    return TRUE;
}
