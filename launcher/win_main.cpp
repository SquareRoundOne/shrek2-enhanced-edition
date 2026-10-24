// Shrek 2 Enhanced Edition launcher - Windows front end (Win32 window + software renderer from gfx.h).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include "ui.h"
#include "fonts.h"

static app::App g_app;
static gfx::Canvas g_canvas;
static HWND g_wnd;
static float g_scale = 1.f;
static HCURSOR g_cursor;
static int g_lastHover = -2;
static std::wstring g_error;
static bool g_swallowUp = false, g_swallowR = false;

static std::wstring wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); return w;
}

struct WinOS : ls::Platform {
    DWORD gamePid = 0;
    osio::Path root, docs;
    osio::Path gameRoot() override { return root; }
    osio::Path docsDir() override { return docs; }
    void display(int& w, int& h, int& hz) override {
        DEVMODEW dm{}; dm.dmSize = sizeof dm;
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) { w = dm.dmPelsWidth; h = dm.dmPelsHeight; hz = dm.dmDisplayFrequency > 1 ? dm.dmDisplayFrequency : 60; }
    }
    bool gameRunning() override {
        std::wstring exe = (root / "System" / "Game.exe").wstring();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof pe; bool found = false;
        for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
            if (_wcsicmp(pe.szExeFile, L"Game.exe") != 0) continue;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!h) { found = true; break; }        // can't tell: assume ours
            wchar_t buf[MAX_PATH * 2]; DWORD n = MAX_PATH * 2;
            if (QueryFullProcessImageNameW(h, 0, buf, &n) && _wcsicmp(buf, exe.c_str()) == 0) found = true;
            CloseHandle(h);
        }
        CloseHandle(snap); return found;
    }
    std::vector<uint8_t> wavBuf;   // PlaySound plays from memory asynchronously: the buffer must outlive it
    bool playWav(const std::vector<uint8_t>& w) override {
        PlaySoundW(nullptr, nullptr, 0);
        wavBuf = w;
        return PlaySoundW((LPCWSTR)wavBuf.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT) != FALSE;
    }
    bool showFile(const osio::Path& p) override {
        std::wstring arg = L"/select,\"" + p.wstring() + L"\"";
        return (INT_PTR)ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL) > 32;
    }
    osio::Path desktopDir() override {
        wchar_t b[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, b))) return osio::Path(b);
        return docs;
    }
    bool systemEnglish() override {
        LANGID l = PRIMARYLANGID(GetUserDefaultUILanguage());
        return !(l == LANG_RUSSIAN || l == LANG_UKRAINIAN || l == LANG_BELARUSIAN || l == LANG_KAZAK);
    }
    bool launchGame(const std::string& map) override {
        std::wstring exe = (root / "System" / "Game.exe").wstring(), dir = (root / "System").wstring();
        std::wstring cmd = L"\"" + exe + L"\"";
        if (!map.empty()) cmd += L" " + wide(map);
        STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
        AllowSetForegroundWindow(ASFW_ANY);     // the game may take the foreground from us
        if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) return false;
        gamePid = pi.dwProcessId;
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return true;
    }
    bool runScript(const osio::Path& bat) override {
        std::wstring b = bat.wstring(), dir = bat.parent_path().wstring();
        return (INT_PTR)ShellExecuteW(g_wnd, L"open", b.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL) > 32;
    }
};
static WinOS g_os;

static float dpiScale(HWND w) {
    typedef UINT(WINAPI * GetDpiForWindow_t)(HWND);
    static auto f = (GetDpiForWindow_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    UINT dpi = f && w ? f(w) : 0;
    if (!dpi) { HDC dc = GetDC(nullptr); dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc); }
    return dpi / 96.f;
}

static void render() {
    RECT rc; GetClientRect(g_wnd, &rc);
    int w = rc.right, h = rc.bottom; if (w <= 0 || h <= 0) return;
    if (g_canvas.w != w || g_canvas.h != h || g_canvas.scale != g_scale) { g_canvas.scale = g_scale; g_canvas.resize(w, h); g_app.bgCache.reset(); }
    g_app.now = GetTickCount64() / 1000.0;
    g_app.draw(g_canvas);
}

static void paint(HDC dc) {
    render();
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = g_canvas.w; bi.bmiHeader.biHeight = -g_canvas.h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, 0, 0, g_canvas.w, g_canvas.h, 0, 0, 0, g_canvas.h, g_canvas.px.data(), &bi, DIB_RGB_COLORS);
}

static void redraw() { InvalidateRect(g_wnd, nullptr, FALSE); }

static void afterInput() {
    if (g_app.wantPlay) {
        g_app.wantPlay = false;
        if (g_os.gameRunning()) { g_app.model.setStatus("!Игра уже запущена."); g_app.statusTime = GetTickCount64() / 1000.0; }
        else if (g_app.model.ensureLang(),
                 // free look: only for a level started from the launcher, handed to ShrekPad through the environment
                 SetEnvironmentVariableW(L"SHREK_EE_FREELOOK", !g_app.wantMap.empty() && g_app.model.freeLook() ? L"1" : nullptr),
                 // a hidden place: "map@x,y,z" (free look at that place)
                 SetEnvironmentVariableW(L"SHREK_EE_SPOT", g_app.wantMap.find('@') != std::string::npos
                     ? std::wstring(g_app.wantMap.begin() + g_app.wantMap.find('@') + 1, g_app.wantMap.end()).c_str() : nullptr),
                 g_os.launchGame(g_app.wantMap.substr(0, g_app.wantMap.find('@')))) {
            // stay (hidden) until the game window exists, then hand it the foreground: otherwise
            // Windows may keep the focus elsewhere and the game waits on a black screen for a click
            ShowWindow(g_wnd, SW_HIDE);
            for (int i = 0; i < 150; i++) {
                struct F { DWORD pid; HWND h; } f{ g_os.gamePid, nullptr };
                EnumWindows([](HWND h, LPARAM lp) -> BOOL {
                    F* f = (F*)lp; DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
                    if (pid == f->pid && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) { f->h = h; return FALSE; }
                    return TRUE; }, (LPARAM)&f);
                if (f.h) { Sleep(300); SetForegroundWindow(f.h); break; }
                Sleep(100);
            }
            DestroyWindow(g_wnd); return;
        }
        else { g_app.model.setStatus("!Не удалось запустить System\\Game.exe"); g_app.statusTime = GetTickCount64() / 1000.0; }
    }
    if (!g_app.model.status.empty() || g_app.capturing()) SetTimer(g_wnd, 1, 100, nullptr);
    redraw();
}

// virtual key -> Unreal Engine 2 key name (as in User.ini [Engine.Input])
static std::string ueKey(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') return std::string(1, (char)vk);
    if (vk >= '0' && vk <= '9') return std::string(1, (char)vk);
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "NumPad" + std::to_string(vk - VK_NUMPAD0);
    switch (vk) {
    case VK_SPACE: return "Space"; case VK_RETURN: return "Enter"; case VK_TAB: return "Tab"; case VK_BACK: return "Backspace";
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return "Shift";
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "Ctrl";
    case VK_MENU: case VK_LMENU: case VK_RMENU: return "Alt";
    case VK_CAPITAL: return "CapsLock"; case VK_UP: return "Up"; case VK_DOWN: return "Down"; case VK_LEFT: return "Left"; case VK_RIGHT: return "Right";
    case VK_INSERT: return "Insert"; case VK_DELETE: return "Delete"; case VK_HOME: return "Home"; case VK_END: return "End";
    case VK_PRIOR: return "PageUp"; case VK_NEXT: return "PageDown"; case VK_PAUSE: return "Pause";
    case VK_MULTIPLY: return "GreyStar"; case VK_ADD: return "GreyPlus"; case VK_SUBTRACT: return "GreyMinus"; case VK_DIVIDE: return "GreySlash"; case VK_DECIMAL: return "NumPadPeriod";
    case VK_OEM_1: return "Semicolon"; case VK_OEM_PLUS: return "Equals"; case VK_OEM_COMMA: return "Comma"; case VK_OEM_MINUS: return "Minus";
    case VK_OEM_PERIOD: return "Period"; case VK_OEM_2: return "Slash"; case VK_OEM_3: return "Tilde"; case VK_OEM_4: return "LeftBracket";
    case VK_OEM_5: return "Backslash"; case VK_OEM_6: return "RightBracket"; case VK_OEM_7: return "SingleQuote";
    }
    return "";
}
static void captured(const std::string& key) { g_app.captureKey(key); afterInput(); }

static HCURSOR makeCursor() {
    if (!g_app.art.cursor) return LoadCursor(nullptr, IDC_ARROW);
    int s = (int)std::lround(32 * g_scale); s = std::max(24, std::min(96, s));
    gfx::Canvas c; c.scale = s / 64.f; c.resize(s, s);
    std::fill(c.px.begin(), c.px.end(), 0u);
    c.image(*g_app.art.cursor, gfx::Rect(0, 0, 64, 64));
    // DIB section with alpha (premultiplied is what CreateIconIndirect expects for 32-bit colour)
    BITMAPV5HEADER bh{}; bh.bV5Size = sizeof bh; bh.bV5Width = s; bh.bV5Height = -s; bh.bV5Planes = 1; bh.bV5BitCount = 32;
    bh.bV5Compression = BI_BITFIELDS; bh.bV5RedMask = 0x00FF0000; bh.bV5GreenMask = 0x0000FF00; bh.bV5BlueMask = 0x000000FF; bh.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr; HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bh, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color) return LoadCursor(nullptr, IDC_ARROW);
    memcpy(bits, c.px.data(), (size_t)s * s * 4);
    HBITMAP mask = CreateBitmap(s, s, 1, 1, nullptr);
    ICONINFO ii{}; ii.fIcon = FALSE; ii.xHotspot = 0; ii.yHotspot = 0; ii.hbmMask = mask; ii.hbmColor = color;
    HCURSOR cur = CreateIconIndirect(&ii);
    DeleteObject(color); DeleteObject(mask);
    return cur ? cur : LoadCursor(nullptr, IDC_ARROW);
}

static LRESULT CALLBACK wndProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(w, &ps); paint(dc); EndPaint(w, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_NCHITTEST: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(w, &p);
        float lx = p.x / g_scale, ly = p.y / g_scale;
        // drag the window by the table around the book; the pages themselves stay interactive
        if (g_app.overWidget(lx, ly) || g_app.dragging >= 0 || g_app.book().contains(lx, ly)) return HTCLIENT;
        return HTCAPTION;
    }
    case WM_NCLBUTTONDBLCLK: return 0;                    // no maximise on double click
    case WM_SETCURSOR: SetCursor(g_cursor); return TRUE;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT t{ sizeof t, TME_LEAVE, w, 0 }; TrackMouseEvent(&t);
        float x = GET_X_LPARAM(lp) / g_scale, y = GET_Y_LPARAM(lp) / g_scale;
        g_app.mouseMove(x, y);
        int h = g_app.hitAt(x, y);
        // repaint when the hovered widget or the hint changes, or while dragging a slider
        static float lastRowY = -1; float rowY = (float)(int)(y / 8);
        if (h != g_lastHover || g_app.dragging >= 0 || rowY != lastRowY) { g_lastHover = h; lastRowY = rowY; redraw(); }
        return 0;
    }
    case WM_NCMOUSEMOVE:
        if (g_lastHover != -1) { g_app.mouseLeave(); g_lastHover = -1; redraw(); }
        { POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(w, &p); g_app.mouseMove(p.x / g_scale, p.y / g_scale); }
        break;
    case WM_MOUSELEAVE: g_app.mouseLeave(); g_lastHover = -2; redraw(); return 0;
    case WM_LBUTTONDOWN:
        if (g_app.capturing()) { g_swallowUp = true; captured("LeftMouse"); return 0; }
        SetCapture(w); g_app.mouseDown(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale); redraw(); return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        if (g_swallowUp) { g_swallowUp = false; return 0; }
        g_app.mouseUp(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale); afterInput(); return 0;
    case WM_RBUTTONDOWN: if (g_app.capturing()) { g_swallowR = true; captured("RightMouse"); return 0; } break;
    case WM_RBUTTONUP: if (g_swallowR) { g_swallowR = false; return 0; } if (!g_app.capturing()) { g_app.rightClick(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale); afterInput(); } return 0;
    case WM_MBUTTONDOWN: if (g_app.capturing()) { captured("MiddleMouse"); return 0; } break;
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        if (g_app.capturing()) { captured(d > 0 ? "MouseWheelUp" : "MouseWheelDown"); return 0; }
        g_app.wheel(d / 120.f); redraw(); return 0;
    }
    case WM_SYSKEYDOWN:
        if (g_app.capturing()) { std::string k = ueKey(wp); if (!k.empty()) captured(k); return 0; }
        break;
    case WM_SYSCHAR: if (g_app.capturing()) return 0; break;
    case WM_KEYDOWN:
        if (g_app.capturing()) {
            if (wp == VK_ESCAPE) { g_app.cancelCapture(); redraw(); return 0; }
            std::string k = ueKey(wp); if (!k.empty()) captured(k);
            return 0;
        }
        if (wp == VK_ESCAPE) { DestroyWindow(w); return 0; }
        if (wp == VK_RETURN) { g_app.wantPlay = true; g_app.wantMap.clear(); afterInput(); return 0; }
        if (wp == VK_LEFT || wp == VK_RIGHT) { g_app.groupStep(wp == VK_RIGHT ? 1 : -1); redraw(); return 0; }
        if (wp == VK_UP || wp == VK_DOWN) { int n = (int)g_app.model.tabs.size(); g_app.tab = (g_app.tab + (wp == VK_DOWN ? 1 : n - 1)) % n; redraw(); return 0; }
        break;
    case WM_TIMER:
        if (g_app.capturing()) { redraw(); return 0; }   // blinking "нажмите…"
        if (GetTickCount64() / 1000.0 - g_app.statusTime > 6.5) KillTimer(w, 1);
        redraw(); return 0;
    case WM_KILLFOCUS: if (g_app.capturing()) { g_app.cancelCapture(); redraw(); } break;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) { g_app.refresh(); redraw(); }   // a module script may have finished
        break;
    case WM_DPICHANGED: {
        g_scale = HIWORD(wp) / 96.f;
        RECT* r = (RECT*)lp;
        SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        DestroyCursor(g_cursor); g_cursor = makeCursor();
        redraw(); return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

static osio::Path exeDir() {
    wchar_t buf[MAX_PATH * 4]; GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    return osio::Path(buf).parent_path();
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int show) {
    std::wstring cl = cmdLine ? cmdLine : L"";
    bool cliReset = cl.find(L"--lang-reset") != std::wstring::npos, cliApply = cl.find(L"--lang-apply") != std::wstring::npos;
    typedef BOOL(WINAPI * SetCtx_t)(HANDLE);
    if (auto f = (SetCtx_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")) f((HANDLE)-4);  // per-monitor v2
    else SetProcessDPIAware();

    // game folder: next to the launcher, or its parent when the launcher sits in System
    osio::Path d = exeDir();
    if (osio::exists(d / "System" / "Game.exe")) g_os.root = d;
    else if (osio::exists(d / "Game.exe")) g_os.root = d.parent_path();
    else {
        MessageBoxW(nullptr, L"Положите лаунчер в папку игры — туда, где лежит папка System с Game.exe.", L"Shrek 2 Enhanced Edition", MB_ICONWARNING);
        return 1;
    }
    wchar_t docs[MAX_PATH];
    std::string appDir = "Shrek 2";
    { ini::File f; if (f.load(g_os.root / "System" / "Default.ini")) appDir = f.get("Core.System", "AppUserDir", "Shrek 2"); }
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs))) g_os.docs = osio::Path(docs) / wide(appDir);
    else g_os.docs = g_os.root / "System";

    if (cliReset || cliApply) {
        // used by install.ps1: no window; exit code 0 = done
        ls::Model m; m.os = &g_os; m.root = g_os.root; m.sys = g_os.root / "System"; m.docs = g_os.docs;
        m.lm.init(m.root);
        m.lm.setEngineLanguage = [&m](bool en) { return m.writeMain([&](ini::File& f) { f.set("Engine.Engine", "Language", en ? "int" : "Rus"); }); };
        std::string err;
        bool ok;
        if (cliReset) ok = m.lm.reset(err);
        else if (m.lm.configured()) ok = m.lm.ensure(err);
        else {      // first install: Russian voices with the restored line, the language the game already uses
            bool en = ini::lower(m.readMain("Engine.Engine", "Language", "Rus")) == "int";
            ok = m.lm.apply(en ? lang::TEXT_EN : lang::TEXT_RU, lang::VOICE_RU, err);
        }
        if (!ok) { FILE* f = osio::open(g_os.root / "System" / "ShrekRemaster" / "lang_error.txt", "wb"); if (f) { fputs(err.c_str(), f); fclose(f); } }
        return ok ? 0 : 2;
    }
    g_app.init(&g_os, FONT_UI, FONT_UI_N, FONT_TITLE, FONT_TITLE_N);
    g_app.onQuit = []() { DestroyWindow(g_wnd); };
    g_app.onMinimize = []() { ShowWindow(g_wnd, SW_MINIMIZE); };

    WNDCLASSEXW wc{ sizeof wc };
    wc.lpfnWndProc = wndProc; wc.hInstance = inst; wc.lpszClassName = L"ShrekRemasterLauncher";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    std::wstring gameExe = (g_os.root / "System" / "Game.exe").wstring();
    HICON big = nullptr, small = nullptr;
    ExtractIconExW(gameExe.c_str(), 0, &big, &small, 1);
    wc.hIcon = big; wc.hIconSm = small;
    RegisterClassExW(&wc);

    // size: logical 1280x800 scaled by the DPI of the monitor under the cursor, centred there
    POINT pt; GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof mi }; GetMonitorInfoW(mon, &mi);
    typedef HRESULT(WINAPI * GetDpiForMonitor_t)(HMONITOR, int, UINT*, UINT*);
    UINT dx = 96, dy = 96;
    if (HMODULE sh = LoadLibraryW(L"shcore.dll")) if (auto f = (GetDpiForMonitor_t)GetProcAddress(sh, "GetDpiForMonitor")) f(mon, 0, &dx, &dy);
    g_scale = dx / 96.f;
    int ww = (int)(app::W * g_scale), wh = (int)(app::H * g_scale);
    RECT wa = mi.rcWork;
    // shrink on small screens
    float fit = std::min(1.f, std::min((wa.right - wa.left) * 0.95f / ww, (wa.bottom - wa.top) * 0.95f / wh));
    if (fit < 1.f) { g_scale *= fit; ww = (int)(app::W * g_scale); wh = (int)(app::H * g_scale); }
    int x = wa.left + ((wa.right - wa.left) - ww) / 2, y = wa.top + ((wa.bottom - wa.top) - wh) / 2;

    g_wnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Shrek 2 Enhanced Edition", WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
                            x, y, ww, wh, nullptr, nullptr, inst, nullptr);
    int corner = 2;  // DWMWCP_ROUND on Windows 11
    DwmSetWindowAttribute(g_wnd, 33, &corner, sizeof corner);
    g_cursor = makeCursor();
    if (!g_app.art.fromGame) { g_app.model.setStatus("!Не найдены текстуры меню игры (Textures\\storybookanimTX.utx)."); g_app.statusTime = 1e18; }
    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
