// prof.cpp - sampling profiler for the game thread. A thread of its own (on another core) stops
// the game thread about 1000 times a second for a few microseconds, notes where it was, and every
// 10 seconds writes System\ShrekRender_profile.txt: share of time per module and the hottest
// places, named after the nearest exported function (UE2's dlls export most of their functions).
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <array>

void shr_log(const char* f, ...);

namespace {
struct Mod { std::wstring name; BYTE* base = nullptr; DWORD size = 0; std::vector<std::pair<DWORD, std::string>> exports; };
std::map<BYTE*, Mod> g_mods;
HANDLE g_game = nullptr;
std::wstring g_out;

Mod* moduleOf(DWORD addr) {
    HMODULE h = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(ULONG_PTR)addr, &h) || !h) return nullptr;
    auto it = g_mods.find((BYTE*)h);
    if (it != g_mods.end()) return &it->second;
    Mod m; m.base = (BYTE*)h;
    wchar_t p[MAX_PATH]; GetModuleFileNameW(h, p, MAX_PATH);
    const wchar_t* s = wcsrchr(p, L'\\'); m.name = s ? s + 1 : p;
    auto nt = (IMAGE_NT_HEADERS*)(m.base + ((IMAGE_DOS_HEADER*)m.base)->e_lfanew);
    m.size = nt->OptionalHeader.SizeOfImage;
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress && dir.Size) {
        auto ex = (IMAGE_EXPORT_DIRECTORY*)(m.base + dir.VirtualAddress);
        auto funcs = (DWORD*)(m.base + ex->AddressOfFunctions);
        auto names = (DWORD*)(m.base + ex->AddressOfNames);
        auto ords = (WORD*)(m.base + ex->AddressOfNameOrdinals);
        for (DWORD i = 0; i < ex->NumberOfNames; i++) {
            DWORD rva = funcs[ords[i]];
            if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size) continue;   // forwarder
            m.exports.push_back({ rva, (const char*)(m.base + names[i]) });
        }
        std::sort(m.exports.begin(), m.exports.end());
    }
    return &(g_mods[(BYTE*)h] = std::move(m));
}

std::string narrow(const std::wstring& w) { std::string s; for (wchar_t c : w) s += (char)(c < 128 ? c : '?'); return s; }

DWORD WINAPI worker(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    timeBeginPeriod(1);
    std::unordered_map<DWORD, unsigned> hits;      // address -> samples
    std::map<std::array<DWORD, 4>, unsigned> chains;
    unsigned total = 0; DWORD lastWrite = GetTickCount();
    for (;;) {
        Sleep(1);
        if (SuspendThread(g_game) == (DWORD)-1) break;
        CONTEXT c{}; c.ContextFlags = CONTEXT_CONTROL;
        BOOL ok = GetThreadContext(g_game, &c);
        ResumeThread(g_game);
        if (!ok) continue;
        hits[c.Eip]++; total++;
        {   // call chain through the frame pointers (exact for functions that keep one, like appMemcpy)
            std::array<DWORD, 4> ch{ c.Eip, 0, 0, 0 };
            DWORD bp = c.Ebp;
            for (int i = 1; i < 4 && bp; i++) {
                DWORD fr[2]; SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(ULONG_PTR)bp, fr, 8, &got) || got != 8) break;
                ch[i] = fr[1];
                if (fr[0] <= bp) break;
                bp = fr[0];
            }
            chains[ch]++;
        }
        if (GetTickCount() - lastWrite < 10000) continue;
        lastWrite = GetTickCount();
        // aggregate per module, per function (nearest export) and per 64-byte block
        std::map<std::string, unsigned> perMod, perFn, perBlock;
        for (auto& h : hits) {
            Mod* m = moduleOf(h.first);
            if (!m) { perMod["(no module)"] += h.second; continue; }
            DWORD rva = h.first - (DWORD)(ULONG_PTR)m->base;
            std::string mn = narrow(m->name);
            perMod[mn] += h.second;
            std::string fn = mn + "!?";
            auto it = std::upper_bound(m->exports.begin(), m->exports.end(), std::make_pair(rva, std::string("\x7f")));
            if (it != m->exports.begin()) { --it; char b[32]; snprintf(b, sizeof b, "+0x%lx", rva - it->first); fn = mn + "!" + it->second + b; fn = fn.substr(0, fn.find('+')); }
            perFn[fn] += h.second;
            char b[64]; snprintf(b, sizeof b, "%s+0x%06lx", mn.c_str(), rva & ~63u); perBlock[b] += h.second;
        }
        FILE* f = _wfopen(g_out.c_str(), L"w");
        if (!f) continue;
        auto dump = [&](const char* title, std::map<std::string, unsigned>& mp, size_t n) {
            std::vector<std::pair<unsigned, std::string>> v;
            for (auto& e : mp) v.push_back({ e.second, e.first });
            std::sort(v.rbegin(), v.rend());
            fprintf(f, "== %s\n", title);
            for (size_t i = 0; i < v.size() && i < n; i++) fprintf(f, "%6.2f%%  %s\n", 100.0 * v[i].first / total, v[i].second.c_str());
        };
        fprintf(f, "%u samples of the game thread (about 1 per ms)\n", total);
        auto where = [&](DWORD a) -> std::string {
            if (!a) return "-";
            Mod* m = moduleOf(a);
            if (!m) { char b[16]; snprintf(b, sizeof b, "%08lx", a); return b; }
            DWORD rva = a - (DWORD)(ULONG_PTR)m->base;
            char b[300]; std::string fn = "?";
            auto it = std::upper_bound(m->exports.begin(), m->exports.end(), std::make_pair(rva, std::string("\x7f")));
            if (it != m->exports.begin()) { --it; fn = it->second.substr(0, 120); }
            snprintf(b, sizeof b, "%s+0x%06lx (%s)", narrow(m->name).c_str(), rva, fn.c_str());
            return b;
        };
        dump("modules", perMod, 30);
        dump("functions (nearest export)", perFn, 60);
        dump("64-byte blocks", perBlock, 60);
        {   // call chains, merged per 64-byte block of each level
            std::map<std::array<DWORD, 4>, unsigned> merged;
            for (auto& e : chains) { auto k = e.first; for (auto& a : k) a &= ~63u; merged[k] += e.second; }
            std::vector<std::pair<unsigned, std::array<DWORD, 4>>> v;
            for (auto& e : merged) v.push_back({ e.second, e.first });
            std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
            fprintf(f, "== call chains (place <- caller <- caller's caller ...)\n");
            for (size_t i = 0; i < v.size() && i < 40; i++) {
                fprintf(f, "%6.2f%%  %s", 100.0 * v[i].first / total, where(v[i].second[0]).c_str());
                for (int j = 1; j < 4 && v[i].second[j]; j++) fprintf(f, "\n            <- %s", where(v[i].second[j]).c_str());
                fprintf(f, "\n");
            }
        }
        fclose(f);
    }
    return 0;
}
} // namespace

void prof_start(const wchar_t* dir) {
    if (g_game) return;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_game,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) { g_game = nullptr; return; }
    g_out = std::wstring(dir) + L"ShrekRender_profile.txt";
    HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    if (t) { CloseHandle(t); shr_log("profiler: sampling the game thread, results in ShrekRender_profile.txt every 10 s"); }
}
