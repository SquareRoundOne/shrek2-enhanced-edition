// rumble.cpp - contextual vibration. ALAudio's PlaySound is hooked (its slot in the audio
// subsystem's virtual table): every sound the game plays arrives with its name, volume, radius and
// position. Sounds that should be felt (hits, explosions, landings, heavy steps, smashes, magic...)
// start a pulse whose strength follows the sound's volume and its distance from the camera; the
// pulses are mixed every frame and handed to ShrekPad (dinput8.dll), which drives the motors of
// Xbox / DualShock 4 / DualSense pads.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cwchar>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>

void shr_log(const char* f, ...);
void subs_onSound(const wchar_t* name);
void subs_onStop(const wchar_t* name);

namespace {
typedef int (__fastcall* PlaySoundFn)(void* self, void* edx, void* actor, int id, void* sound, float x, float y, float z,
                                      float vol, float radius, float pitch, int flags, float f1, float f2);
typedef const wchar_t* (__fastcall* GetNameFn)(void* obj, void* edx);
typedef int (__fastcall* StopSoundFn)(void* self, void* edx, void* actor, void* sound);
typedef void (__cdecl* PadRumbleFn)(float low, float high);
typedef void (__cdecl* PadHeroFn)(int hero);

PlaySoundFn g_orig; StopSoundFn g_origStop;
GetNameFn g_getName;
PadRumbleFn g_pad;
PadHeroFn g_padHero;
bool g_on = true, g_steps = false, g_hooked = false, g_tried = false;
float g_cam[3]; bool g_haveCam = false;

struct Pattern { const char* key; float low, high; int ms; int shape; bool step; };
// shape: 0 = sharp hit (fast decay), 1 = thump (attack then decay), 2 = rumble (flat, fades at the end), 3 = double knock
// first match wins: specific names before general ones
const Pattern PATTERNS[] = {
    { "punch_miss", 0.00f, 0.12f, 60, 0, false }, { "swoosh", 0, 0.08f, 50, 0, false }, { "swish", 0, 0.08f, 50, 0, false },
    { "screen_shake", 0.65f, 0.35f, 1600, 2, false }, { "ground_shake", 1.0f, 0.6f, 650, 1, false },
    { "bellyflop", 0.95f, 0.5f, 420, 1, false },
    { "explode", 1.0f, 0.7f, 600, 1, false }, { "fgm_explode", 1.0f, 0.8f, 900, 1, false }, { "firework", 0.35f, 0.4f, 260, 1, false },
    { "building_smash", 0.95f, 0.6f, 650, 1, false }, { "house_blow_down", 0.9f, 0.5f, 1100, 2, false }, { "strawhut_blow_down", 0.7f, 0.4f, 900, 2, false },
    { "tree_fall", 0.9f, 0.4f, 900, 2, false }, { "tree_uproot", 0.7f, 0.5f, 500, 1, false }, { "tree_hit", 0.9f, 0.5f, 450, 1, false },
    { "boulder_hit", 0.95f, 0.5f, 450, 1, false }, { "boulder_drop", 0.95f, 0.5f, 500, 1, false }, { "boulder_roll", 0.5f, 0.2f, 1200, 2, false },
    { "big_ball", 0.8f, 0.3f, 700, 2, false }, { "floor_crumble", 0.85f, 0.5f, 900, 2, false }, { "bookcase_slam", 1.0f, 0.6f, 500, 1, false },
    { "bookcase_fall", 0.7f, 0.3f, 800, 2, false }, { "gate_smash", 0.9f, 0.6f, 450, 1, false }, { "gate_fall", 0.85f, 0.5f, 500, 1, false },
    { "cow_impact", 0.8f, 0.5f, 380, 1, false }, { "catapult_launch", 0.6f, 0.4f, 450, 1, false }, { "trolley_hit", 0.85f, 0.5f, 450, 1, false },
    { "wheel_drop", 0.6f, 0.3f, 300, 1, false }, { "wheel_hit", 0.5f, 0.4f, 220, 0, false }, { "crate_break", 0.45f, 0.55f, 200, 0, false },
    { "door_castle_large", 0.7f, 0.3f, 600, 2, false }, { "stone_shut", 0.7f, 0.3f, 400, 1, false }, { "stone_slide", 0.35f, 0.15f, 700, 2, false },
    { "big_door_slide", 0.4f, 0.15f, 800, 2, false }, { "metal_elev_st", 0.4f, 0.2f, 300, 1, false }, { "cage_rise", 0.3f, 0.15f, 700, 2, false },
    { "magic_hit", 0.6f, 0.7f, 350, 1, false }, { "magic_", 0.15f, 0.35f, 300, 2, false }, { "transform", 0.3f, 0.5f, 450, 2, false },
    { "giantpumpkin", 0.9f, 0.6f, 600, 1, false }, { "pumpkin_kick", 0.4f, 0.5f, 160, 0, false },
    { "fatknight_foot", 0.75f, 0.2f, 220, 1, false }, { "fatknight_swordhit", 0.7f, 0.6f, 280, 0, false }, { "fatknight_swipe", 0.15f, 0.2f, 120, 0, false },
    { "spider_walk", 0.25f, 0.1f, 120, 1, false }, { "spider_attack", 0.4f, 0.4f, 200, 0, false },
    { "sword_hit", 0.35f, 0.7f, 140, 0, false }, { "grate_slash", 0.3f, 0.6f, 140, 0, false }, { "armor", 0.15f, 0.35f, 100, 0, false },
    { "punching_bag", 0.5f, 0.5f, 180, 0, false }, { "spin_punch", 0.6f, 0.6f, 220, 0, false },
    { "punch", 0.55f, 0.65f, 170, 0, false }, { "kick", 0.55f, 0.6f, 170, 0, false }, { "attack", 0.35f, 0.45f, 160, 0, false },
    { "rat_hit", 0.3f, 0.5f, 120, 0, false }, { "briar_damage", 0.3f, 0.6f, 160, 0, false }, { "hit", 0.45f, 0.6f, 160, 0, false },
    { "bodyfall", 0.55f, 0.3f, 230, 1, false }, { "body_fall", 0.55f, 0.3f, 230, 1, false }, { "faint_drop", 0.6f, 0.3f, 260, 1, false },
    { "faint", 0.4f, 0.2f, 260, 1, false }, { "rat_fall", 0.2f, 0.2f, 120, 0, false },
    { "hay_land", 0.35f, 0.15f, 180, 1, false }, { "landing", 0.45f, 0.25f, 170, 1, false }, { "land", 0.45f, 0.25f, 170, 1, false },
    { "jump_double", 0.0f, 0.18f, 80, 0, false }, { "boing", 0.35f, 0.45f, 220, 3, false },
    { "throw", 0.1f, 0.25f, 90, 0, false }, { "drink_potion", 0.1f, 0.25f, 300, 2, false }, { "potion_break", 0.3f, 0.5f, 200, 0, false },
    { "pickup_coin", 0.0f, 0.15f, 45, 0, false }, { "get_item", 0.0f, 0.2f, 70, 0, false },
    { "f_shrek_", 0.18f, 0.0f, 70, 1, true }, { "f_donkey_", 0.08f, 0.06f, 50, 1, true }, { "f_pib_", 0.0f, 0.06f, 40, 0, true },
    { "f_steed_", 0.2f, 0.05f, 70, 1, true },
};

struct Pulse { float low, high; DWORD start; int ms, shape; };
std::mutex g_mu;
std::vector<Pulse> g_pulses;

float shapeAt(int shape, float t) {          // t in 0..1 -> 0..1
    switch (shape) {
    case 0: return std::exp(-5.f * t);
    case 1: return t < 0.12f ? t / 0.12f : std::exp(-3.5f * (t - 0.12f));
    case 2: return t < 0.1f ? t / 0.1f : t > 0.75f ? (1 - t) / 0.25f : 1.f;
    case 3: { float u = std::fmod(t * 2, 1.f); return std::exp(-6.f * u) * (t < 0.5f ? 1.f : 0.7f); }
    }
    return 0;
}

// the hero the player controls, for ShrekPad's triggers (Donkey and Puss get lighter ones): the
// actor that plays a jump or landing sound right next to the camera (the camera follows the hero;
// the companions are further away). Actor names follow the class: Shrek0, Donkey2, PIB1, Steed0...
void heroFromSound(void* actor, const char* n, float x, float y, float z) {
    if (!g_padHero || !actor || !g_haveCam) return;
    if (!(strstr(n, "jump") || strstr(n, "land")) || strstr(n, "water")) return;
    float dx = x - g_cam[0], dy = y - g_cam[1], dz = z - g_cam[2];
    float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    const wchar_t* an = g_getName(actor, nullptr); if (!an) return;
    char a[64]; size_t i = 0; for (; an[i] && i < sizeof a - 1; i++) a[i] = (char)towlower(an[i]); a[i] = 0;
    static int logged = 0;
    if (logged < 12) { logged++; shr_log("hero sound: %s by %s at %.0f", n, a, d); }
    if (d > 500) return;
    int h = -1;
    if (!strncmp(a, "donkey", 6) || !strncmp(a, "steed", 5)) h = 1;
    else if (!strncmp(a, "pib", 3) || !strncmp(a, "puss", 4)) h = 2;
    else if (!strncmp(a, "shrek", 5)) h = 0;
    static int last = -1;
    if (h >= 0 && h != last) { last = h; g_padHero(h); }
}

int __fastcall hkPlaySound(void* self, void* edx, void* actor, int id, void* sound, float x, float y, float z,
                           float vol, float radius, float pitch, int flags, float f1, float f2) {
    if (sound && g_getName) subs_onSound(g_getName(sound, nullptr));     // voice lines: subtitles (subs.cpp)
    if (sound && g_getName && g_padHero) {
        const wchar_t* wn = g_getName(sound, nullptr);
        if (wn) { char n[96]; size_t i = 0; for (; wn[i] && i < sizeof n - 1; i++) n[i] = (char)towlower(wn[i]); n[i] = 0; heroFromSound(actor, n, x, y, z); }
    }
    if (g_on && sound && g_getName && g_pad) {
        const wchar_t* wn = g_getName(sound, nullptr);
        if (wn) {
            char n[96]; size_t i = 0;
            for (; wn[i] && i < sizeof n - 1; i++) n[i] = (char)towlower(wn[i]);
            n[i] = 0;
            for (auto& p : PATTERNS) {
                if (!strstr(n, p.key)) continue;
                if (p.step && !g_steps) break;
                float a = std::min(1.f, std::max(0.f, vol));
                if (g_haveCam && radius > 1) {     // fade with the distance from the camera
                    float dx = x - g_cam[0], dy = y - g_cam[1], dz = z - g_cam[2];
                    float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                    float r = std::max(radius, 400.f);
                    float f = std::max(0.f, 1.f - d / r);
                    a *= f * f * 0.85f + 0.15f * (d < 600 ? 1.f : 0.f);
                }
                if (a > 0.02f) {
                    std::lock_guard<std::mutex> l(g_mu);
                    if (g_pulses.size() < 32) g_pulses.push_back({ p.low * a, p.high * a, GetTickCount(), p.ms, p.shape });
                }
                break;
            }
        }
    }
    return g_orig(self, edx, actor, id, sound, x, y, z, vol, radius, pitch, flags, f1, f2);
}

int __fastcall hkStopSound(void* self, void* edx, void* actor, void* sound) {
    if (sound && g_getName) subs_onStop(g_getName(sound, nullptr));
    return g_origStop(self, edx, actor, sound);
}

void tryHook() {
    if (g_hooked || g_tried) return;
    HMODULE al = GetModuleHandleW(L"ALAudio.dll"), core = GetModuleHandleW(L"Core.dll");
    HMODULE pad = GetModuleHandleW(L"dinput8.dll");
    if (!al || !core) return;                  // audio not started yet
    g_tried = true;
    g_pad = pad ? (PadRumbleFn)GetProcAddress(pad, "ShrekPadRumble") : nullptr;
    g_padHero = pad ? (PadHeroFn)GetProcAddress(pad, "ShrekPadHero") : nullptr;
    g_getName = (GetNameFn)GetProcAddress(core, "?GetName@UObject@@QBEPBGXZ");
    void* fn = (void*)GetProcAddress(al, "?PlaySoundW@UALAudioSubsystem@@UAEHPAVAActor@@HPAVUSound@@VFVector@@MMMHMM@Z");
    void* fnStop = (void*)GetProcAddress(al, "?StopSound@UALAudioSubsystem@@UAEHPAVAActor@@PAVUSound@@@Z");
    if (!g_pad) shr_log("vibration: ShrekPad without vibration support (sounds still followed for subtitles)");
    if (!g_getName || !fn) { shr_log("vibration: ALAudio PlaySound not found"); return; }
    BYTE* base = (BYTE*)al;
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    int n = 0;
    for (int s = 0; s < nt->FileHeader.NumberOfSections; s++, sec++) {
        if (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
        void** p = (void**)(base + sec->VirtualAddress);
        size_t cnt = sec->Misc.VirtualSize / sizeof(void*);
        for (size_t i = 0; i < cnt; i++) if (p[i] == fn) {
            DWORD old;
            if (VirtualProtect(&p[i], sizeof(void*), PAGE_READWRITE, &old)) {
                g_orig = (PlaySoundFn)fn; p[i] = (void*)hkPlaySound; n++;
                VirtualProtect(&p[i], sizeof(void*), old, &old);
            }
        } else if (fnStop && p[i] == fnStop) {
            DWORD old;
            if (VirtualProtect(&p[i], sizeof(void*), PAGE_READWRITE, &old)) {
                g_origStop = (StopSoundFn)fnStop; p[i] = (void*)hkStopSound;
                VirtualProtect(&p[i], sizeof(void*), old, &old);
            }
        }
    }
    g_hooked = n > 0;
    shr_log(n ? "vibration: listening to the game's sounds (%d slot%s)" : "vibration: PlaySound slot not found", n, n == 1 ? "" : "s");
}
} // namespace

void rumble_config(bool on, bool steps) { g_on = on; g_steps = steps; }
void rumble_camera(const float* pos) { memcpy(g_cam, pos, 12); g_haveCam = true; }

// once per frame (Present)
void rumble_frame() {
    tryHook();
    if (!g_pad || !g_hooked) return;
    static float lastL = -1, lastH = -1;
    float L = 0, H = 0;
    {
        std::lock_guard<std::mutex> l(g_mu);
        DWORD now = GetTickCount();
        for (size_t i = 0; i < g_pulses.size();) {
            Pulse& p = g_pulses[i];
            float t = float(now - p.start) / p.ms;
            if (t >= 1) { g_pulses[i] = g_pulses.back(); g_pulses.pop_back(); continue; }
            float e = shapeAt(p.shape, t);
            // several pulses add up, but softly (no instant saturation)
            L = 1 - (1 - L) * (1 - p.low * e); H = 1 - (1 - H) * (1 - p.high * e);
            i++;
        }
    }
    if (std::fabs(L - lastL) > 0.01f || std::fabs(H - lastH) > 0.01f || ((L == 0) != (lastL == 0))) {
        g_pad(L, H); lastL = L; lastH = H;
    }
}
