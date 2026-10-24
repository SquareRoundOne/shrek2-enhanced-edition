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
static int g_trR2 = 10, g_trR2Point = 0, g_trL2 = 5;    // force of R2 / L2 (%), where the R2 click starts (% of the travel, 0: no click, smooth)
static bool g_dsx = true;                        // ... also through DSX
static std::atomic<int> g_rumLow{ 0 }, g_rumHigh{ 0 };   // 0..255
static wchar_t g_dir[MAX_PATH];
static FILE* g_log;
static pad::Settings g_set;
static bool g_enabled = true;
static bool g_sonyEnabled = true, g_lightbar = true, g_cursorOn = true, g_rawMouse = true;
static float g_mouseSens = 1.5f;
static uint8_t g_lbR = 40, g_lbG = 170, g_lbB = 20;

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
    g_cursorOn = iniF(L"GameCursor", 1) != 0;
    g_rawMouse = iniF(L"RawMouse", 1) != 0;
    g_mouseSens = std::min(10.f, std::max(0.1f, iniF(L"MouseSensitivity", 1.5f)));
    g_lbR = (uint8_t)iniF(L"LightbarR", g_lbR); g_lbG = (uint8_t)iniF(L"LightbarG", g_lbG); g_lbB = (uint8_t)iniF(L"LightbarB", g_lbB);
    g_rumbleStrength = std::min(1.f, std::max(0.f, iniF(L"Rumble", 70) / 100.f));
    g_adaptive = iniF(L"AdaptiveTriggers", 1) != 0 ? 1 : 0;
    g_trR2 = std::min(100, std::max(0, (int)iniF(L"TriggerR2", 10)));
    g_trR2Point = std::min(90, std::max(0, (int)iniF(L"TriggerR2Point", 0)));
    g_trL2 = std::min(100, std::max(0, (int)iniF(L"TriggerL2", 5)));
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
static void trigFx(int mode, sony::TriggerFx& l, sony::TriggerFx& r) {
    l = sony::fxOff(); r = sony::fxOff();
    if (mode <= 0) return;
    if (g_trR2 > 0) {
        if (g_trR2Point > 0) {   // R2: a section of resistance - the click - from TriggerR2Point on
            int st = g_trR2Point * 255 / 100, en = std::min(255, st + 0x30);
            r = sony::fxSection((uint8_t)st, (uint8_t)en, (uint8_t)(g_trR2 * 255 / 100));
        } else r = sony::fxResist(0x10, (uint8_t)(g_trR2 * 255 / 100));   // smooth: the same light resistance all the way
    }
    if (g_trL2 > 0) l = sony::fxResist(0x20, (uint8_t)(g_trL2 * 255 / 100));
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
    int rf = mode > 0 ? (g_trR2 * 8 + 50) / 100 : 0, lf = mode > 0 ? (g_trL2 * 8 + 50) / 100 : 0;
    if (mode > 0 && g_trR2 > 0 && !rf) rf = 1;
    if (mode > 0 && g_trL2 > 0 && !lf) lf = 1;
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
    g_trigWant = want;
    static int dsxSent = -1; static ULONGLONG dsxAt = 0;
    if (g_dsx && g_adaptive > 0 && !g_sonyDirect) {
        ULONGLONG now = GetTickCount64();
        if (want != dsxSent || now - dsxAt > 3000) { dsxSend(want); dsxSent = want; dsxAt = now; }   // again now and then: DSX may start later
    }
}

// Called by shrz.dll (the renderer) every frame with the vibration the game's sounds ask for.
// Only while the game window has the focus: no buzzing in the background.
static HWND g_hwnd;
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
            int reports = 0; uint8_t firstId = 0; int sentLow = 0, sentHigh = 0, sentTrig = -1, offAgain = 0;
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
static int WINAPI myShowCursor(BOOL show) {
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
static LRESULT CALLBACK msgHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == PM_REMOVE) {
        MSG* m = (MSG*)lp;
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
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        float dt = last.QuadPart ? (float)(now.QuadPart - last.QuadPart) / freq.QuadPart : 0.f;
        last = now; dt = std::min(std::max(dt, 0.f), 0.1f);

        pad::XState x = g_enabled ? readPad() : pad::XState();
        if (x.connected != loggedPad) { logf(x.connected ? "controller connected" : "controller disconnected"); loggedPad = x.connected; }
        bool fg = gameInForeground();
        if (fg != lastFg) { logf(fg ? "game active" : "game not active"); if (!fg) logForeground(); lastFg = fg; }
        if (!fg) x = pad::XState();
        bool inMenu = fg && cursorShown();
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
            sendMenu(o);
        } else {
            menu.leave();
            map.toJoy(x, j, dt > 0 ? dt : 1.f / 60.f);
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
    static void sendMenu(const pad::MenuOut& o) {
        INPUT in[24]; int n = 0;
        auto mouse = [&](DWORD f, LONG dx, LONG dy, DWORD data) { INPUT i{}; i.type = INPUT_MOUSE; i.mi.dwFlags = f; i.mi.dx = dx; i.mi.dy = dy; i.mi.mouseData = data; in[n++] = i; };
        auto key = [&](WORD vk, bool up) {
            INPUT i{}; i.type = INPUT_KEYBOARD; i.ki.wVk = vk; i.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
            i.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0; in[n++] = i;
        };
        if (o.dx || o.dy) mouse(MOUSEEVENTF_MOVE, (LONG)o.dx, (LONG)o.dy, 0);
        if (o.wheel) mouse(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)(o.wheel * WHEEL_DELTA));
        if (o.leftDown) mouse(MOUSEEVENTF_LEFTDOWN, 0, 0, 0);
        if (o.leftUp) mouse(MOUSEEVENTF_LEFTUP, 0, 0, 0);
        if (o.escape) { key(VK_ESCAPE, false); key(VK_ESCAPE, true); }
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
        if (iniF(L"Log", 1) != 0) g_log = _wfopen(lp, L"w");
        if (!g_log) { wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t); wcscat(t, L"ShrekPad.log"); if (iniF(L"Log", 1) != 0) g_log = _wfopen(t, L"w"); }
        logf("ShrekPad 1.18 (%ls): triggers %d (R2 %d%% at %d%%, L2 %d%%) dsx %d mouse x%.2f enabled=%d sony=%d deadzone=%.2f/%.2f camera=%.2f curve=%.2f invertY=%d swapAB=%d",
             g_dir, g_adaptive, g_trR2, g_trR2Point, g_trL2, (int)g_dsx, g_mouseSens, g_enabled, g_sonyEnabled, g_set.deadzoneLeft, g_set.deadzoneRight, g_set.cameraSpeed, g_set.cameraCurve, g_set.invertY, g_set.swapAB);
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
