// Launcher settings model: reads the current state from the game's files and applies changes
// directly to them (Default.ini / User.ini in Documents / DefUser.ini / dxvk.conf / D3DDrv.dll /
// Sounds). Every setting is described by a Row; the UI only draws rows and calls change().
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <array>
#include "ini.h"
#include "osio.h"
#include "lang.h"
#include "i18n.h"
#include "dlgplay.h"
#include "bugreport.h"

namespace ls {

using osio::Path;

struct Platform {
    virtual ~Platform() {}
    virtual Path gameRoot() = 0;              // folder with System\Game.exe
    virtual Path docsDir() = 0;               // Documents\Shrek 2
    virtual void display(int& w, int& h, int& hz) = 0;
    virtual bool gameRunning() = 0;
    virtual bool launchGame(const std::string& map = "") = 0;   // map: start straight on this level
    virtual bool runScript(const Path& bat) = 0;   // opens a console window running a .bat
    virtual bool systemEnglish() { return false; }  // first start: launcher language from Windows
    virtual bool playWav(const std::vector<uint8_t>&) { return false; }   // a voice line (Скрытый контент)
    virtual bool showFile(const Path&) { return false; }   // an Explorer window with the file selected (bug report)
    virtual Path desktopDir() { return docsDir(); }
};

enum Kind { CHOICE, TOGGLE, SLIDER, INFO, MODULE, KEYS, HEADER, BIND, LEVEL, PARA, LINE };   // LINE: a voice line to listen to (label speaker, text the line)   // PARA: a heading (label) over a paragraph (text)

struct Action { std::string label; std::function<std::string()> run; };

struct Row {
    std::string id, label, hint;
    Kind kind = CHOICE;
    std::vector<std::string> options;      // CHOICE
    int value = 0;                         // CHOICE index / TOGGLE 0-1 / SLIDER value
    int vmin = 0, vmax = 100, step = 1;    // SLIDER
    std::string text;                      // INFO / MODULE status
    bool enabled = true;
    std::string disabledNote;
    std::vector<Action> actions;           // MODULE buttons
    std::vector<std::pair<std::string, std::string>> keys;   // KEYS table
    int div = 1; std::string unit;         // SLIDER: shown value = value / div, then unit
    std::vector<std::string> bound;        // BIND: engine key names (up to two)
    int act = -1;                          // BIND: index in BIND_ACTS
    std::string badge; uint32_t badgeBg = 0, badgeFg = 0xFFFFFF; bool badgeRound = true;   // gamepad button picture
    std::string icon, sub;                 // LEVEL: storybookanimTX picture, level name (label = chapter)
    float paraH = 0;                       // PARA: measured height of the paragraph (set while drawing)
};

struct Tab { std::string title, heading, group; std::vector<Row> rows; };   // group: tabs shown as one item of the list, switched by arrows over the page

static const char* TEXDETAIL_KEYS[] = { "TextureDetailInterface", "TextureDetailTerrain", "TextureDetailWeaponSkin",
                                        "TextureDetailPlayerSkin", "TextureDetailWorld", "TextureDetailRenderMap" };

// ------------------------------------------------------------------ input tables
inline std::string normCmd(const std::string& s) {
    std::string o; bool sp = false;
    for (char c : s) { if (c == ' ' || c == '\t') { sp = !o.empty(); continue; } if (sp) { o += ' '; sp = false; } o += (char)std::tolower((unsigned char)c); }
    return o;
}
inline std::vector<std::string> splitCmd(const std::string& v) {
    std::vector<std::string> out; size_t a = 0;
    while (a <= v.size()) { size_t b = v.find('|', a); if (b == std::string::npos) b = v.size(); std::string t = ini::trim(v.substr(a, b - a)); if (!t.empty()) out.push_back(t); a = b + 1; }
    return out;
}
struct BindAct { const char* label; const char* cmd; std::vector<const char*> match; const char* hint; };
static const std::vector<BindAct> BIND_ACTS = {
    { "Вперёд", "MoveForward", { "moveforward", "axis abasey speed=+1200.0", "axis aarrowup speedbase=0.7" }, "" },
    { "Назад", "MoveBackward", { "movebackward", "axis abasey speed=-1200.0", "axis aarrowup speedbase=0.7 invert=-1" }, "" },
    { "Шаг влево", "StrafeLeft", { "strafeleft" }, "" },
    { "Шаг вправо", "StrafeRight", { "straferight" }, "" },
    { "Поворот влево", "Axis aArrowRight SpeedBase=0.9 Invert=-1", { "turnleft", "axis aarrowright speedbase=0.9 invert=-1" }, "Поворот камеры с клавиатуры." },
    { "Поворот вправо", "Axis aArrowRight SpeedBase=0.9 Invert=1", { "turnright", "axis aarrowright speedbase=0.9 invert=1" }, "Поворот камеры с клавиатуры." },
    { "Прыжок", "Jump", { "jump" }, "Второе нажатие в воздухе — двойной прыжок." },
    { "Удар", "Fire", { "fire" }, "Двойное нажатие — двойной удар, в прыжке — атака в прыжке." },
    { "Действие", "Use", { "use" }, "Подобрать, поговорить, дёрнуть рычаг." },
    { "Меню зелий", "StopMoviePlayback", { "stopmovieplayback" }, "Открывает выбор зелья (в игре эта команда называется StopMoviePlayback)." },
    { "Следующее зелье", "SelectPotion 100", { "selectpotion 100" }, "Выбирает следующее зелье из тех, что есть; значок на экране меняется, зелье не применяется." },
    { "Предыдущее зелье", "SelectPotion 101", { "selectpotion 101" }, "Выбирает предыдущее зелье из тех, что есть." },
    { "Применить зелье", "SelectPotion 102", { "selectpotion 102" }, "Выпить или бросить выбранное зелье (как кнопка «Применить» в меню зелий)." },
    { "Пропуск сцены", "BypassCutscene", { "bypasscutscene" }, "Пропускает катсцену на движке." },
    { "Идти шагом", "Walking", { "walking" }, "Пока клавиша зажата, персонаж идёт, а не бежит." },
    { "Применить предмет", "UseCarriedInventoryItem", { "usecarriedinventoryitem" }, "Бросить или применить то, что персонаж держит в руках." },
    { "Камера за спину", "CenterView", { "centerview" }, "" },
};
inline bool partIsAct(const std::string& part, const BindAct& a) {
    std::string n = normCmd(part); for (auto m : a.match) if (n == m) return true; return false;
}
// engine key name -> what the player sees
inline std::string keyLabel(const std::string& k) {
    static const std::pair<const char*, const char*> M[] = {
        { "leftmouse", "ЛКМ" }, { "rightmouse", "ПКМ" }, { "middlemouse", "Колёсико" }, { "mousewheelup", "Колесо ↑" }, { "mousewheeldown", "Колесо ↓" },
        { "space", "Пробел" }, { "enter", "Enter" }, { "up", "↑" }, { "down", "↓" }, { "left", "←" }, { "right", "→" },
        { "ctrl", "Ctrl" }, { "shift", "Shift" }, { "alt", "Alt" }, { "escape", "Esc" }, { "backspace", "Backspace" }, { "capslock", "Caps Lock" },
        { "pageup", "Page Up" }, { "pagedown", "Page Down" }, { "home", "Home" }, { "end", "End" }, { "insert", "Insert" }, { "delete", "Delete" }, { "tab", "Tab" },
        { "greystar", "Num *" }, { "greyplus", "Num +" }, { "greyminus", "Num −" }, { "greyslash", "Num /" }, { "numpadperiod", "Num ." },
        { "semicolon", ";" }, { "equals", "=" }, { "comma", "," }, { "minus", "-" }, { "period", "." }, { "slash", "/" }, { "tilde", "~" },
        { "leftbracket", "[" }, { "backslash", "\\" }, { "rightbracket", "]" }, { "singlequote", "'" }, { "pause", "Pause" },
    };
    std::string l = ini::lower(k);
    for (auto& m : M) if (l == m.first) return m.second;
    if (l.rfind("numpad", 0) == 0 && l.size() == 7) return std::string("Num ") + l[6];
    if (l.size() == 1) return std::string(1, (char)std::toupper((unsigned char)l[0]));
    return k;
}
// gamepad buttons (ShrekPad: Joy1..Joy12 buttons, Joy13..16 the D-pad), named and drawn the way
// the chosen controller family prints them
enum PadStyle { PAD_XBOX = 0, PAD_PS = 1, PAD_NINTENDO = 2 };
struct PadFace { const char* name; const char* badge; uint32_t bg, fg; bool round; };
struct PadBtn { const char* key; PadFace face[3]; };
static const PadBtn PAD_BTNS[] = {
    { "Joy1", { { "A", "A", 0x5DBB3A, 0xFFFFFF, true }, { "Крест", "✕", 0x2B2B2B, 0x8DB4F0, true }, { "B", "B", 0x3A3A3A, 0xFFFFFF, true } } },
    { "Joy2", { { "B", "B", 0xD8352A, 0xFFFFFF, true }, { "Круг", "○", 0x2B2B2B, 0xF06A6E, true }, { "A", "A", 0x3A3A3A, 0xFFFFFF, true } } },
    { "Joy3", { { "X", "X", 0x2E7FD8, 0xFFFFFF, true }, { "Квадрат", "□", 0x2B2B2B, 0xE38BD3, true }, { "Y", "Y", 0x3A3A3A, 0xFFFFFF, true } } },
    { "Joy4", { { "Y", "Y", 0xE8B321, 0x2B2B2B, true }, { "Треугольник", "△", 0x2B2B2B, 0x4FD3AE, true }, { "X", "X", 0x3A3A3A, 0xFFFFFF, true } } },
    { "Joy5", { { "LB", "LB", 0x3A3A3A, 0xFFFFFF, false }, { "L1", "L1", 0x2B2B2B, 0xFFFFFF, false }, { "L", "L", 0x3A3A3A, 0xFFFFFF, false } } },
    { "Joy6", { { "RB", "RB", 0x3A3A3A, 0xFFFFFF, false }, { "R1", "R1", 0x2B2B2B, 0xFFFFFF, false }, { "R", "R", 0x3A3A3A, 0xFFFFFF, false } } },
    { "Joy11", { { "LT", "LT", 0x3A3A3A, 0xFFFFFF, false }, { "L2", "L2", 0x2B2B2B, 0xFFFFFF, false }, { "ZL", "ZL", 0x3A3A3A, 0xFFFFFF, false } } },
    { "Joy12", { { "RT", "RT", 0x3A3A3A, 0xFFFFFF, false }, { "R2", "R2", 0x2B2B2B, 0xFFFFFF, false }, { "ZR", "ZR", 0x3A3A3A, 0xFFFFFF, false } } },
    { "Joy9", { { "Нажатие левого стика", "LS", 0x3A3A3A, 0xFFFFFF, true }, { "L3", "L3", 0x2B2B2B, 0xFFFFFF, true }, { "Нажатие левого стика", "LS", 0x3A3A3A, 0xFFFFFF, true } } },
    { "Joy10", { { "Нажатие правого стика", "RS", 0x3A3A3A, 0xFFFFFF, true }, { "R3", "R3", 0x2B2B2B, 0xFFFFFF, true }, { "Нажатие правого стика", "RS", 0x3A3A3A, 0xFFFFFF, true } } },
    { "Joy7", { { "View", "▤", 0x3A3A3A, 0xFFFFFF, true }, { "Share / Create", "▤", 0x2B2B2B, 0xFFFFFF, false }, { "Кнопка −", "−", 0x3A3A3A, 0xFFFFFF, true } } },
    { "Joy13", { { "Крестовина ↑", "↑", 0x3A3A3A, 0xFFFFFF, false }, { "Крестовина ↑", "↑", 0x2B2B2B, 0xFFFFFF, false }, { "Крестовина ↑", "↑", 0x3A3A3A, 0xFFFFFF, false } } },
    { "Joy14", { { "Крестовина →", "→", 0x3A3A3A, 0xFFFFFF, false }, { "Крестовина →", "→", 0x2B2B2B, 0xFFFFFF, false }, { "Крестовина →", "→", 0x3A3A3A, 0xFFFFFF, false } } },
    { "Joy15", { { "Крестовина ↓", "↓", 0x3A3A3A, 0xFFFFFF, false }, { "Крестовина ↓", "↓", 0x2B2B2B, 0xFFFFFF, false }, { "Крестовина ↓", "↓", 0x3A3A3A, 0xFFFFFF, false } } },
    { "Joy16", { { "Крестовина ←", "←", 0x3A3A3A, 0xFFFFFF, false }, { "Крестовина ←", "←", 0x2B2B2B, 0xFFFFFF, false }, { "Крестовина ←", "←", 0x3A3A3A, 0xFFFFFF, false } } },
};
static const char* PAD_START[3] = { "Menu", "Options", "+" };
// subtitle colours (EnhancedEdition.ini [Subtitles] TextColor / NameColor), in the order of the choices
static const char* SUB_TEXT[5] = { "FFFFFF", "FFE14D", "B6F27A", "9AD8FF", "FFC48A" };
// subtitle fonts (EnhancedEdition.ini [Subtitles] Font): the one built into shrz.dll, Windows' Palatino (the English game's serif face), Tahoma Bold, Verdana Bold, Georgia Bold
static const char* SUB_FONT[5] = { "builtin", "pala.ttf", "tahomabd.ttf", "verdanab.ttf", "georgiab.ttf" };
static const char* SUB_NAME[6] = { "char", "FFD54A", "8EE05A", "6CC8FF", "FF9A3D", "FFFFFF" };
// voice lines nobody hears in the game (found in no scene, bump set, map or script, and not put back
// by this edition's BumpSet.int), grouped by speaker
static const char* CUT_LINES[] = {
    "pc_shk_beanstalkintro_2",
    "pc_shk_bumpline_5",
    "pc_shk_bumpline_71",
    "pc_shk_bumpline_72",
    "pc_shk_bumpline_585",
    "pc_shk_bumpline_798",
    "pc_shk_bumpline_855",
    "pc_shk_bumpline_1225",
    "pc_shk_bumpline_1810",
    "pc_shk_bumpline_1817",
    "pc_shk_bumpline_1826",
    "pc_shk_bumpline_1847",
    "pc_shk_bumpline_1849",
    "pc_shk_bumpline_1850",
    "pc_shk_bumpline_1881",
    "pc_shk_bumpline_1884",
    "pc_shk_bumpline_1893",
    "pc_shk_bumpline_1904",
    "pc_shk_bumpline_1909",
    "pc_shk_bumpline_1919",
    "pc_shk_bumpline_1934",
    "pc_shk_bumpline_1939",
    "pc_shk_bumpline_1943",
    "pc_shk_bumpline_1972",
    "pc_shk_bumpline_1977",
    "pc_shk_bumpline_1982",
    "pc_shk_bumpline_2673",
    "pc_shk_bumpline_2687",
    "pc_shk_bumpline_2768",
    "pc_shk_bumpline_2776",
    "pc_shk_bumpline_2855",
    "pc_shk_fgmdrivethru1_4",
    "pc_shk_giantpumpkin_2",
    "pc_shk_giantpumpkin_4",
    "pc_shk_giantpumpkin_10",
    "pc_shk_giantpumpkin_12",
    "pc_shk_giantpumpkin_16",
    "pc_shk_meetpib_15",
    "pc_shk_mongotutoral_18",
    "pc_shk_poisonappintro_2",
    "pc_shk_potionfail_123",
    "pc_shk_shrekmissing_21",
    "pc_dnk_bumpline_22",
    "pc_dnk_bumpline_754",
    "pc_dnk_bumpline_790",
    "pc_dnk_bumpline_1767",
    "pc_dnk_bumpline_2023",
    "pc_dnk_bumpline_2058",
    "pc_dnk_bumpline_2060",
    "pc_dnk_bumpline_2061",
    "pc_dnk_bumpline_2073",
    "pc_dnk_bumpline_2108",
    "pc_dnk_bumpline_2158",
    "pc_dnk_bumpline_2196",
    "pc_dnk_bumpline_2205",
    "pc_dnk_bumpline_2239",
    "pc_dnk_bumpline_2240",
    "pc_dnk_bumpline_2274",
    "pc_dnk_bumpline_2560",
    "pc_dnk_bumpline_2577",
    "pc_dnk_bumpline_2692",
    "pc_dnk_bumpline_2772",
    "pc_dnk_carhijack_84",
    "pc_dnk_cherrytree_2",
    "pc_dnk_cherrytree_4",
    "pc_dnk_fgmdrivethru1_2",
    "pc_dnk_fgmdrivethru1_6",
    "pc_dnk_fgmsoffice_75",
    "pc_dnk_healthcoinshud_2",
    "pc_dnk_healthcoinshud_4",
    "pc_dnk_healthcoinshud_6",
    "pc_dnk_healthcoinshud_8",
    "pc_dnk_introjumpatack_2",
    "pc_dnk_pickupwheel_2",
    "pc_dnk_shrekmissing_23",
    "pc_dnk_snapdragintro_6",
    "pc_dnk_snapdragintro_8",
    "pc_dnk_swampbouncy_4a",
    "pc_dnk_swamphealth_2",
    "pc_dnk_swamphealth_6",
    "pc_dnk_swampintro3_43",
    "pc_dnk_swampintro3_67",
    "pc_dnk_swampintro3_69",
    "pc_dnk_swampintro3_76",
    "pc_dnk_swampmove_3",
    "pc_dnk_swampmove_6",
    "pc_dnk_swampposter2_2",
    "pc_dnk_swampposter2_20",
    "pc_dnk_swinginglog_2",
    "pc_dnk_wheelback_93",
    "pc_dnk_wheelback_95",
    "pc_dnk_wheelback_97",
    "pc_dnk_wheelback_99",
    "pc_pib_bumpline_549",
    "pc_pib_bumpline_2287",
    "pc_pib_bumpline_2289",
    "pc_pib_bumpline_2296",
    "pc_pib_bumpline_2302",
    "pc_pib_bumpline_2303",
    "pc_pib_bumpline_2309",
    "pc_pib_bumpline_2315",
    "pc_pib_bumpline_2316",
    "pc_pib_bumpline_2338",
    "pc_pib_bumpline_2342",
    "pc_pib_bumpline_2349",
    "pc_pib_bumpline_2375",
    "pc_pib_bumpline_2379",
    "pc_pib_bumpline_2381",
    "pc_pib_bumpline_2383",
    "pc_pib_bumpline_2384",
    "pc_pib_bumpline_2781",
    "pc_pib_bumpline_2799",
    "pc_pib_bumpline_3039",
    "pc_pib_introfirevents_2",
    "pc_pib_introfirevents_6",
    "pc_pib_introlasers_6",
    "pc_pib_pibpotion_20",
    "pc_pib_potionfail_117",
    "pc_pib_potionfail_120",
    "pc_nar_bumpline_2451",
    "pc_nar_bumpline_2453",
    "pc_nar_bumpline_2455",
    "pc_nar_bumpline_2456",
    "pc_nar_bumpline_2457",
    "pc_nar_bumpline_2459",
    "pc_nar_bumpline_2460",
    "pc_nar_bumpline_2461",
    "pc_nar_bumpline_2463",
    "pc_nar_bumpline_2465",
    "pc_nar_bumpline_2466",
    "pc_nar_bumpline_2467",
    "pc_nar_bumpline_2468",
    "pc_nar_bumpline_2475",
    "pc_nar_bumpline_2478",
    "pc_nar_bumpline_2480",
    "pc_nar_bumpline_2566",
    "pc_gbm_bumpline_2408",
    "pc_knt_bumpline_2983",
    "pc_knt_catattack_2",
    "pc_elf_elffaint_2",
    "pc_elf_elffaint_10",
    "pc_elf_elffaint_28",
    "pc_elf_elffaint_30",
    "pc_bnd_banditfaint_10",
    "pc_bnd_carhijack_57",
    "pc_pst_bumpline_2661",
    "pc_pst_bumpline_2662",
    "pc_pst_bumpline_2663",
    "pc_pst_bumpline_2664",
    "pc_pst_bumpline_2665",
    "pc_pst_bumpline_2724",
    "pc_pst_citfaint_2",
    "pc_fem_bumpline_555",
    "pc_fem_bumpline_557",
    "pc_fem_bumpline_558",
    "pc_fem_bumpline_559",
    "pc_fem_bumpline_560",
    "pc_fem_bumpline_561",
    "pc_fem_bumpline_2703",
    "pc_fem_bumpline_2710",
    "pc_fem_bumpline_2712",
    "pc_fem_bumpline_2713",
    "pc_fem_bumpline_2714",
    "pc_fem_bumpline_2715",
    "pc_fem_bumpline_2750",
    "pc_guy_bumpline_2844",
    "pc_guy_bumpline_2850",
    "pc_guy_bumpline_3054",
    "pc_guy_bumpline_3055",
    "pc_bgy_bumpline_3288",
    "pc_bgy_bumpline_3343",
    "pc_bgy_bumpline_3349",
    "pc_bgy_bumpline_3350",
    "pc_sgy_bumpline_3222",
    "pc_sgy_bumpline_3342",
    "pc_sgy_bumpline_3348",
    "pc_sgy_bumpline_3351",
    "pc_tgy_bumpline_3178",
    "pc_tgy_bumpline_3341",
    "pc_tgy_bumpline_3347",
    "pc_tgy_bumpline_3352",
    "pc_roy_swampsave_10",
    "pc_bbs_bumpline_2787",
};
// lines recorded only for the English edition (lang\voice_en.uax), unused there too: no text in the
// game files, so written down from the recordings by speech recognition, with a translation
struct EnLine { const char* id; const char* en; const char* ru; };
static const EnLine EN_LINES[] = {
    { "pc_shk_bumpline_6", "*sigh*", "(вздыхает)" },
    { "pc_shk_bumpline_607", "Almost got 'em all!", "Почти все собрал!" },
    { "pc_shk_bumpline_650", "Pfft.", "Пф-ф." },
    { "pc_shk_bumpline_866", "Hey, you're burning me!", "Эй, ты меня обжигаешь!" },
    { "pc_shk_bumpline_1089C", "Ow!", "Ай!" },
    { "pc_shk_bumpline_1556", "I have to do a jump attack to pull that switch: hit the right mouse button to jump, and then while I'm in the air, hit the left mouse button.", "Чтобы дёрнуть этот рычаг, нужен удар в прыжке: правой кнопкой мыши прыгаешь, а в воздухе жмёшь левую." },
    { "pc_shk_bumpline_1559", "Oh, I'll have to jump attack to trigger that.", "Ох, придётся ударить в прыжке, чтобы это сработало." },
    { "PC_SHK_BUMPLINE_1645", "This way! The window!", "Сюда! В окно!" },
    { "pc_shk_bumpline_1761", "This is no way to say hello.", "Так не здороваются." },
    { "pc_shk_bumpline_1831", "Hooray for us!", "Ура нам!" },
    { "pc_shk_bumpline_1837", "Uh, yeah.", "Ну, да." },
    { "pc_shk_bumpline_1853", "Don't even joke about that, Donkey.", "Даже не шути так, Осёл." },
    { "pc_shk_bumpline_1880A", "Oof!", "Уф!" },
    { "pc_shk_bumpline_1880B", "No!", "Нет!" },
    { "pc_shk_bumpline_1967A", "Owww!", "А-а-ай!" },
    { "pc_shk_bumpline_1967B", "Oh!", "Ох!" },
    { "pc_shk_bumpline_1967C", "Ow!", "Ай!" },
    { "pc_shk_bumpline_1968A", "*grunts*", "(кряхтит)" },
    { "pc_shk_bumpline_1968C", "*coughing*", "(кашляет)" },
    { "pc_shk_bumpline_1968D", "*a cry*", "(вскрик)" },
    { "pc_shk_bumpline_2570", "No one can stop me now.", "Теперь меня никто не остановит." },
    { "pc_dnk_bumpline_96", "No, Shrek! I said hit your left mouse button!", "Нет, Шрек! Я же сказал: левую кнопку мыши!" },
    { "pc_dnk_bumpline_158", "When you gonna start fighting, Shrek?", "Когда ты уже начнёшь драться, Шрек?" },
    { "pc_dnk_bumpline_326", "Shrek, you need to combo punch the bandit that's holding the wheel to knock him out!", "Шрек, разбойника с колесом надо вырубить серией ударов!" },
    { "pc_dnk_bumpline_454", "I think I found a great pumpkin!", "Кажется, я нашёл отличную тыкву!" },
    { "pc_dnk_bumpline_558", "What the...", "Что за..." },
    { "pc_dnk_bumpline_587", "Ooh, I almost got it.", "У-у, почти достал." },
    { "pc_dnk_bumpline_779", "I get first dibs on where to sleep when we get to the barn.", "Чур, в сарае я первый выбираю, где спать." },
    { "pc_dnk_bumpline_791", "Oh, you think you special with your high-tech hoo-ha.", "Ой, думаешь, ты особенный со своими навороченными штучками." },
    { "pc_dnk_bumpline_816", "Man, go find somebody else who's about to kiss, alright?", "Слушай, найди кого-нибудь другого, кто собирается целоваться, ладно?" },
    { "pc_dnk_bumpline_817", "This one's mine.", "Это моё." },
    { "pc_dnk_bumpline_1219", "I believe I can fly.", "Я верю, что могу летать." },
    { "pc_dnk_bumpline_1468", "Who let the dogs out?", "Кто выпустил собак?" },
    { "pc_dnk_bumpline_2010", "To trigger that switch, I need to do a jump attack. So click the right mouse button to make me jump. And then while I'm in the air, press the left mouse button. And watch the donkey fly!", "Чтобы сработал этот рычаг, мне нужен удар в прыжке. Щёлкни правой кнопкой мыши, чтобы я подпрыгнул, а пока я в воздухе — левой. И смотри, как летает осёл!" },
    { "pc_dnk_bumpline_2243", "Shrek, you have to fight back. That's how fighting works.", "Шрек, надо давать сдачи. Так драки и устроены." },
    { "pc_dnk_bumpline_2581", "Pull that lever, Shrek, and then the bridge will come down. I figured that out all by myself.", "Дёрни рычаг, Шрек, и мост опустится. Я сам до этого додумался." },
    { "pc_dnk_SwampBouncy_2", "To jump on the lily pad, hit the Control key or your right mouse button while you're running forward.", "Чтобы прыгнуть на кувшинку, нажми на бегу Ctrl или правую кнопку мыши." },
    { "pc_dnk_SwampMove_4", "Walking through water really kind of slows things down. It'd be way faster if you just jump across the lily pad. So come on, hop to it.", "По воде идти — сплошная морока. Куда быстрее перепрыгнуть по кувшинкам. Давай, прыг-скок." },
    { "pc_dnk_SwampSave_2", "Oh yeah, see Tinkerbell flying around over there? Run up to her to save your game. Really sparks your sense of wonder, don't it?", "Ах да, видишь, там летает Динь-Динь? Подбеги к ней, чтобы сохранить игру. Прямо чудеса, правда?" },
    { "pc_dnk_SwampSave_4", "Just run up to Tinkerbell to save your game.", "Просто подбеги к Динь-Динь, чтобы сохранить игру." },
    { "pc_pib_bumpline_800", "Shrek, why do you put up with this stupid donkey?", "Шрек, зачем ты терпишь этого глупого осла?" },
    { "pc_pib_bumpline_811", "Shrek, I think what you are doing for Fiona is very romantic.", "Шрек, по-моему, то, что ты делаешь ради Фионы, очень романтично." },
    { "pc_pib_bumpline_1576", "Enough of your asinine remarks.", "Хватит твоих ослиных замечаний." },
    { "pc_pib_bumpline_1597", "I bet I could crawl under some of these unscathed.", "Готов поспорить, под некоторыми из них я проползу невредимым." },
    { "pc_pib_bumpline_1717", "I wonder if there is a poster around here somewhere.", "Интересно, нет ли здесь где-нибудь плаката." },
    { "pc_pib_bumpline_2295", "Hmm, a booster lies near. I can feel it in my claws.", "Хм, где-то рядом усилитель. Чую когтями." },
    { "pc_pib_bumpline_2378", "Bah! These woods are dark and full of terrors.", "Ба! Этот лес тёмен и полон ужасов." },
    { "pc_pib_MeetPIB_39", "I am not here to argue with a donkey.", "Я здесь не для того, чтобы спорить с ослом." },
    { "pc_fin_bumpline_64", "My husband's gonna flatten all of you.", "Мой муж вас всех в лепёшку раскатает." },
    { "pc_fgm_bumpline_2424", "You're in over your head, ogre.", "Ты зарвался, людоед." },
    { "pc_fgm_bumpline_2425", "I'm going to enjoy this.", "Я получу от этого удовольствие." },
    { "pc_nar_bumpline_1276", "Tom Thumb, wanted for giving the finger.", "Мальчик-с-пальчик — разыскивается за неприличный жест пальцем." },
    { "pc_nar_bumpline_1282", "The Emperor, wanted for indecent exposure.", "Император — разыскивается за непристойное обнажение." },
    { "pc_nar_bumpline_1284", "Little Bo Peep, wanted for animal neglect.", "Пастушка Бо-Пип — разыскивается за небрежение к животным." },
    { "pc_knt_bumpline_2727", "Thou art lovely, earth, in a summer's day.", "Прекрасна ты, земля, летним днём." },
    { "pc_knt_bumpline_2978", "Was there a noble steed here a second ago, or am I losing my marbles?", "Тут только что был благородный скакун или я схожу с ума?" },
    { "pc_elf_bumpline_158", "Time for a smackdown!", "Время для взбучки!" },
    { "pc_elf_bumpline_1780", "Alert! Alert! Ass in the house! This is not an ass-related drill!", "Тревога! Тревога! В здании осёл! Это не учебная ослиная тревога!" },
    { "pc_pst_bumpline_1083", "Take that!", "Получай!" },
    { "pc_pst_bumpline_1547", "Begone, foul beast. This land is for foul people only.", "Прочь, мерзкое чудище. Эта земля — только для мерзких людей." },
    { "pc_pst_citifaint_18", "Ugh.", "Ух." },
    { "pc_psc_bumpline_261", "Arrgh!", "А-а-ргх!" },
    { "pc_psc_bumpline_268", "Mommy!", "Мама!" },
    { "pc_psc_bumpline_270", "Run away! Run away!", "Бежим! Бежим!" },
    { "pc_psc_bumpline_275", "Hide the children!", "Прячьте детей!" },
    { "pc_psc_bumpline_277", "You idiot!", "Идиот!" },
    { "pc_psc_bumpline_284", "Attack?", "В атаку?" },
    { "pc_psc_bumpline_307", "Stab him in the gut!", "Пырни его в брюхо!" },
    { "pc_psc_bumpline_317", "Show a little spine now!", "Ну-ка, покажи характер!" },
    { "pc_psc_bumpline_1431", "Attack?", "В атаку?" },
    { "pc_psc_bumpline_1516", "Eeyore! Eeyore! Eeyore!", "Иа! Иа! Иа!" },
    { "pc_psc_bumpline_1752", "Here's pulp in your eye!", "Получай мякотью в глаз!" },
    { "pc_psc_bumpline_2520", "Oh?", "О?" },
    { "pc_bgy_bumpline_3300", "Hit your space bar to bring up your potion menu.", "Чтобы открыть меню зелий, нажмите пробел." },
    { "pc_sgy_bumpline_3301", "Hit your space bar to bring up your potion menu.", "Чтобы открыть меню зелий, нажмите пробел." },
};
inline const char* speakerName(const std::string& id) {
    static const std::pair<const char*, const char*> T[] = { { "shk", "Шрек" }, { "dnk", "Осёл" }, { "pib", "Кот в сапогах" }, { "fin", "Фиона" },
        { "fgm", "Крёстная Фея" }, { "nar", "Рассказчик" }, { "fem", "Горожанка" }, { "psw", "Крестьянка" }, { "bnd", "Разбойник" },
        { "kng", "Король" }, { "gbm", "Пряник" }, { "ggm", "Монго" }, { "knt", "Рыцарь" }, { "elf", "Эльф" }, { "pst", "Крестьянин" },
        { "psb", "Мальчишка" }, { "psg", "Девчонка" }, { "psc", "Узник" }, { "psp", "Узник" }, { "guy", "Продавец" }, { "bgy", "Продавец" },
        { "sgy", "Продавец" }, { "tgy", "Продавец" }, { "roy", "Рой" }, { "tra", "Экскурсовод" }, { "bbs", "Голос из уборной" }, { "lep", "Голос из уборной" } };
    std::string w = id.size() > 6 ? ini::lower(id.substr(3, 3)) : "";
    for (auto& t : T) if (w == t.first) return t.second;
    return "";
}
// levels for "Уровни": map, chapter, name, picture in storybookanimTX (the same as on the save slots)
struct LevelDef { const char* map; const char* chapter; const char* name; const char* icon; };
static const LevelDef LEVELS[] = {
    { "1_Shreks_Swamp.unr", "Глава 1", "Болото Шрека", "1_Shreks_Swamp" },
    { "2_Carriage_Hijack.unr", "Глава 2", "Нападение на карету", "2_Carraige_Hijack" },
    { "3_The_Hunt_Part1.unr", "Глава 3", "Охота, часть 1", "3_The_Hunt" },
    { "3_The_Hunt_Part2.unr", "Глава 3", "Охота, часть 2", "3_The_Hunt" },
    { "3_The_Hunt_Part3.unr", "Глава 3", "Охота, часть 3", "3_The_Hunt" },
    { "3_The_Hunt_Part4.unr", "Глава 3", "Охота, часть 4", "3_The_Hunt" },
    { "4_FGM_Office.unr", "Глава 4", "Контора Крёстной Феи", "4_FGM_PIB" },
    { "4_FGM_PIB.unr", "Глава 4", "Фабрика зелий: Кот в сапогах", "4_FGM_PIB" },
    { "5_FGM_Donkey.unr", "Глава 5", "Фабрика зелий: Осёл", "5_FGM_Donkey" },
    { "6_Hamlet.unr", "Глава 6", "Деревня", "6_Hamlet" },
    { "6_Hamlet_Mine.unr", "Глава 6", "Шахта гномов", "6_Hamlet" },
    { "6_Hamlet_End.unr", "Глава 6", "Деревня: финал", "6_Hamlet" },
    { "7_Prison_Donkey.unr", "Глава 7", "Тюрьма: Осёл", "7_Prison_Donkey" },
    { "8_Prison_PIB.unr", "Глава 8", "Тюрьма: Кот в сапогах", "8_Prison_PIB" },
    { "9_Prison_Shrek.unr", "Глава 9", "Тюрьма: Шрек", "9_Prison_Shrek" },
    { "10_Castle_Siege.unr", "Глава 10", "Осада замка", "10_Castle_Seige" },
    { "11_FGM_Battle.unr", "Глава 11", "Битва с Крёстной Феей", "11_FGM_Battle" },
    { "Beanstalk_bonus.unr", "Бонус", "Бобовый стебель", "giant" },
    { "Beanstalk_bonus_dawn.unr", "Бонус", "Бобовый стебель: рассвет", "giant" },
    { "Beanstalk_bonus_knight.unr", "Бонус", "Бобовый стебель: рыцари", "giant" },
};
struct PadAct { const char* label; const char* cmd; };
static const PadAct PAD_ACTS[] = {
    { "Прыжок", "Jump | BypassCutscene" }, { "Удар", "Fire" }, { "Действие", "Use" }, { "Меню зелий", "StopMoviePlayback" },
    { "Следующее зелье", "SelectPotion 100" }, { "Предыдущее зелье", "SelectPotion 101" }, { "Применить зелье", "SelectPotion 102" },
    { "Применить предмет", "UseCarriedInventoryItem" }, { "Камера за спину", "CenterView" }, { "Идти шагом", "Walking" },
    { "Пропуск сцены", "BypassCutscene" },
    { "Зелье 1", "SelectPotion 0" }, { "Зелье 2", "SelectPotion 1" }, { "Зелье 3", "SelectPotion 2" }, { "Зелье 4", "SelectPotion 3" },
    { "Зелье 5", "SelectPotion 4" }, { "Зелье 6", "SelectPotion 5" }, { "Ничего", "" },
};
static const int PAD_ACT_N = sizeof(PAD_ACTS) / sizeof(PAD_ACTS[0]);
struct LightColor { const char* label; int r, g, b; };
static const LightColor LIGHTS[] = { { "Не менять", -1, -1, -1 }, { "Зелёная", 40, 170, 20 }, { "Синяя", 20, 60, 220 }, { "Красная", 220, 20, 20 },
                                     { "Фиолетовая", 140, 30, 200 }, { "Оранжевая", 230, 100, 0 }, { "Белая", 200, 200, 200 } };

struct Model {
    Platform* os = nullptr;
    Path root, sys, docs;
    std::vector<Tab> tabs;
    std::string status;                    // last message for the user
    bool statusError = false;
    int monW = 1920, monH = 1080, monHz = 60;
    bool uiEn = false;                     // launcher language (the game's own language is on the Language tab)
    std::string tr(const std::string& s) const { return uiEn ? i18n::tr(s) : s; }
    Path launcherIni() { return sys / "ShrekRemaster" / "launcher.ini"; }
    void setUiLang(bool en) {
        uiEn = en;
        ini::File f; if (!f.load(launcherIni())) { f.path = launcherIni(); f.loaded = true; f.crlf = true; }
        f.set("Launcher", "Language", en ? "en" : "ru"); f.dirty = true;
        osio::mkdirs(launcherIni().parent_path()); f.save();
        status.clear();
        reload();
    }
    bool dxvk = false, msaaPatch = false;
    std::vector<int> fpsValues;

    // ------------------------------------------------------------ files
    std::vector<Path> mainInis() {
        std::vector<Path> v{ sys / "Default.ini" };
        for (Path p : { sys / "Game.ini", sys / "Shrek2.ini", docs / "Default.ini", docs / "Game.ini", docs / "Shrek2.ini" })
            if (osio::exists(p)) v.push_back(p);
        return v;
    }
    std::vector<Path> userInis() {
        std::vector<Path> v{ sys / "DefUser.ini" };
        for (Path p : { sys / "User.ini", docs / "User.ini" }) if (osio::exists(p)) v.push_back(p);
        return v;
    }
    Path userIniForRead() { Path d = docs / "User.ini"; return osio::exists(d) ? d : sys / "DefUser.ini"; }

    std::string readMain(const std::string& sec, const std::string& key, const std::string& def) {
        ini::File f; if (!f.load(sys / "Default.ini")) return def; return f.get(sec, key, def);
    }
    std::string readUser(const std::string& sec, const std::string& key, const std::string& def) {
        ini::File f; if (!f.load(userIniForRead())) return def; return f.get(sec, key, def);
    }
    bool writeInis(const std::vector<Path>& files, std::function<void(ini::File&)> fn) {
        bool ok = true;
        for (auto& p : files) {
            ini::File f; if (!f.load(p)) { if (p.filename() == "Default.ini" || p.filename() == "DefUser.ini") ok = false; continue; }
            fn(f); if (!f.save()) ok = false;
        }
        return ok;
    }
    bool writeMain(std::function<void(ini::File&)> fn) { return writeInis(mainInis(), fn); }
    bool writeUser(std::function<void(ini::File&)> fn) { return writeInis(userInis(), fn); }

    // dxvk.conf: "key = value" lines, ours are tagged by the "# ShrekRemaster" block
    std::string dxvkGet(const std::string& key) {
        ini::File f; if (!f.load(sys / "dxvk.conf")) return "";
        for (auto& l : f.lines) {
            std::string t = ini::trim(l); if (t.empty() || t[0] == '#') continue;
            size_t e = t.find('='); if (e == std::string::npos) continue;
            if (ini::trim(t.substr(0, e)) == key) return ini::trim(t.substr(e + 1));
        }
        return "";
    }
    bool dxvkSet(const std::string& key, const std::string& val) {   // empty val removes
        ini::File f; Path p = sys / "dxvk.conf";
        if (!f.load(p)) { f.path = p; f.loaded = true; f.crlf = true; }
        bool found = false;
        for (size_t i = f.lines.size(); i-- > 0;) {
            std::string t = ini::trim(f.lines[i]); if (t.empty() || t[0] == '#') continue;
            size_t e = t.find('='); if (e == std::string::npos) continue;
            if (ini::trim(t.substr(0, e)) == key) {
                if (val.empty()) f.lines.erase(f.lines.begin() + i);
                else { f.lines[i] = key + " = " + val; found = true; }
                f.dirty = true;
            }
        }
        if (!val.empty() && !found) {
            bool tag = false; for (auto& l : f.lines) if (l.find("ShrekRemaster") != std::string::npos) tag = true;
            if (!tag) f.lines.push_back("# ShrekRemaster");
            f.lines.push_back(key + " = " + val); f.dirty = true;
        }
        return f.save();
    }

    // ShrekRender.ini (shrz.dll: lighting, relief, sharpening) and alsoft.ini (OpenAL Soft)
    Path renderIni() { return sys / "ShrekRender.ini"; }
    std::string readRender(const std::string& sec, const std::string& key, const std::string& def) {
        ini::File f; if (!f.load(renderIni())) return def; return f.get(sec, key, def);
    }
    bool writeKeys(const Path& p, const std::vector<std::array<std::string, 3>>& kv) {
        ini::File f; if (!f.load(p)) { f.path = p; f.loaded = true; f.crlf = true; }
        for (auto& e : kv) f.set(e[0], e[1], e[2]);
        f.dirty = true; return f.save();
    }
    // System\EnhancedEdition.ini: what ShrekPad applies in the game (cheats)
    Path eeIni() { return sys / "EnhancedEdition.ini"; }
    std::string readEE(const std::string& sec, const std::string& key, const std::string& def) {
        ini::File f; if (!f.load(eeIni())) return def; return f.get(sec, key, def);
    }
    std::string readLauncher(const std::string& sec, const std::string& key, const std::string& def) {
        ini::File f; if (!f.load(launcherIni())) return def; return f.get(sec, key, def);
    }
    bool freeLook() { return readLauncher("Levels", "FreeLook", "0") != "0"; }
    // the game's own credits (HpMenu credits_1..N: the role, then the names) and ours
    void buildCredits(Tab& cr) {
        auto para = [&](const std::string& h, const std::string& t) { Row r; r.id = "cr"; r.kind = PARA; r.label = h; r.text = t; cr.rows.push_back(r); };
        auto head = [&](const std::string& h) { Row r; r.id = "crh"; r.kind = HEADER; r.label = h; cr.rows.push_back(r); };
        head("Улучшенное Издание");
        para("Фанатский патч", "Shrek 2 Enhanced Edition (Шрек 2: Улучшенное Издание). Идея, тестирование и руководство — SquareRoundOne (автор сборки); программирование — Claude (Anthropic). Этот материал не сделан и не поддерживается Activision.");
        para("Использовано", "DXVK (zlib) — рендер через Vulkan; OpenAL Soft (LGPL) — звук; Real-ESRGAN (BSD-3) — HD-текстуры; AudioSR (MIT) и Apollo (CC BY-SA 4.0) — HD-звук; аудиодекодер Bink по FFmpeg (LGPL); AMD FidelityFX CAS (MIT) — резкость; stb (public domain); rgbcx (MIT); шрифты DejaVu и Lora (свободные лицензии).");
        // game credits from the localisation the launcher speaks (Russian file for the Russian UI)
        Path f = sys / (uiEn ? "HpMenu.int" : "HpMenu.rus");
        if (!osio::exists(f)) f = sys / "HpMenu.int";
        std::vector<uint8_t> d; std::string t;
        if (osio::readFile(f, d)) {
            if (d.size() >= 2 && d[0] == 0xFF && d[1] == 0xFE) {   // UTF-16
                for (size_t i = 2; i + 1 < d.size(); i += 2) {
                    unsigned c = d[i] | (d[i + 1] << 8);
                    if (c < 0x80) t += (char)c; else if (c < 0x800) { t += (char)(0xC0 | (c >> 6)); t += (char)(0x80 | (c & 0x3F)); }
                    else { t += (char)(0xE0 | (c >> 12)); t += (char)(0x80 | ((c >> 6) & 0x3F)); t += (char)(0x80 | (c & 0x3F)); }
                }
            } else {   // Windows-1251
                for (uint8_t c : d) {
                    if (c < 0x80) t += (char)c;
                    else {
                        unsigned u = c >= 0xC0 ? 0x410 + (c - 0xC0) : c == 0xA8 ? 0x401 : c == 0xB8 ? 0x451 : c == 0xB9 ? 0x2116 : c == 0x96 ? 0x2013 : c == 0x97 ? 0x2014 : c == 0xA9 ? 0xA9 : 0xFFFD;
                        t += (char)(0xC0 | (u >> 6)); if (u >= 0x800) { t.back() = (char)(0xE0 | (u >> 12)); t += (char)(0x80 | ((u >> 6) & 0x3F)); }
                        t += (char)(0x80 | (u & 0x3F));
                    }
                }
            }
        }
        std::vector<std::pair<int, std::string>> lines;
        size_t p = 0;
        while (p < t.size()) {
            size_t e = t.find('\n', p); if (e == std::string::npos) e = t.size();
            std::string l = t.substr(p, e - p); p = e + 1;
            while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
            if (l.rfind("credits_", 0) == 0) {
                size_t eq = l.find('='); if (eq == std::string::npos) continue;
                int n = atoi(l.c_str() + 8); std::string v = l.substr(eq + 1);
                for (auto& ch : v) if (ch == '\t') ch = ' ';
                lines.push_back({ n, v });
            }
        }
        std::sort(lines.begin(), lines.end());
        if (!lines.empty()) head(uiEn ? "Shrek 2 (2004)" : "Шрек 2 (2004)");
        for (auto& l : lines) {
            // the role ends where the first Latin name starts after Cyrillic words (Russian file)
            std::string v = l.second, role, names = v;
            bool cyr = false;
            for (size_t i = 0; i < v.size(); i++) {
                unsigned char c = (unsigned char)v[i];
                if (c == 0xD0 || c == 0xD1) cyr = true;
                else if (cyr && c >= 'A' && c <= 'Z' && i > 0 && v[i - 1] == ' ' && i + 1 < v.size() && v[i + 1] >= 'a' && v[i + 1] <= 'z') { role = v.substr(0, i - 1); names = v.substr(i); break; }
            }
            para(role, names);
        }
    }
    Path alsoftIni() { return sys / "alsoft.ini"; }
    std::string readAlsoft(const std::string& sec, const std::string& key, const std::string& def) {
        ini::File f; if (!f.load(alsoftIni())) return def; return f.get(sec, key, def);
    }
    std::string readAudio(const std::string& key, const std::string& def) { return readMain("ALAudio.ALAudioSubsystem", key, def); }
    bool writeAudio(const std::vector<std::pair<std::string, std::string>>& kv) {
        return writeMain([&](ini::File& f) { for (auto& p : kv) f.set("ALAudio.ALAudioSubsystem", p.first, p.second); });
    }
    // output: 0 Windows, 1 headphones (HRTF), 2 stereo, 3 quad, 4 5.1, 5 7.1
    bool writeAlsoft(int output, bool eax) {
        ini::File f; Path p = alsoftIni();
        if (!f.load(p)) { f.path = p; f.loaded = true; f.crlf = true; f.lines = { "# Shrek 2 Enhanced Edition: OpenAL Soft settings (the launcher's Sound tab writes them)" }; }
        const char* ch[] = { "", "stereo", "stereo", "quad", "surround51", "surround71" };
        auto setOrDrop = [&](const char* sec, const char* key, const std::string& v) {
            if (!v.empty()) { f.set(sec, key, v); return; }
            size_t a, b; std::string k, vv;
            if (f.sectionRange(sec, a, b)) for (size_t i = b; i-- > a;) if (ini::File::keyOf(f.lines[i], k, vv) && ini::lower(k) == key) f.lines.erase(f.lines.begin() + i);
        };
        setOrDrop("general", "channels", ch[std::clamp(output, 0, 5)]);
        f.set("general", "stereo-mode", output == 1 ? "headphones" : "speakers");
        f.set("general", "hrtf", output == 1 ? "true" : "false");
        f.set("general", "frequency", "48000");
        f.set("general", "resampler", "bsinc24");
        f.set("general", "sources", "256");
        f.set("general", "output-limiter", "true");
        f.set("eax", "enable", eax ? "true" : "false");
        f.dirty = true; return f.save();
    }
    static int fxLevel(double v, double a, double b) { return v <= 0.01 ? 0 : v < a ? 1 : v < b ? 2 : 3; }
    static int sharpenLevel(double v) { return v <= 0.01 ? 0 : v < 0.35 ? 1 : v < 0.6 ? 2 : 3; }

    // MSAA level lives in D3DDrv.dll after the marker "SHRMSAA:"
    long msaaOffset(std::vector<uint8_t>& d) {
        if (!osio::readFile(sys / "D3DDrv.dll", d)) return -1;
        static const char m[] = "SHRMSAA:";
        for (size_t i = 0; i + 12 <= d.size(); i++) if (!memcmp(&d[i], m, 8)) return (long)(i + 8);
        return -1;
    }

    static bool fileContains(const Path& p, const char* s) {
        std::vector<uint8_t> d; if (!osio::readFile(p, d)) return false;
        size_t n = strlen(s);
        for (size_t i = 0; i + n <= d.size(); i++) if (!memcmp(&d[i], s, n)) return true;
        return false;
    }

    // ------------------------------------------------------------ build
    Row* find(const std::string& id) {
        for (auto& t : tabs) for (auto& r : t.rows) if (r.id == id) return &r;
        return nullptr;
    }
    static int indexOf(const std::vector<int>& v, int x) { for (size_t i = 0; i < v.size(); i++) if (v[i] == x) return (int)i; return -1; }

    void init(Platform* p) {
        os = p; root = os->gameRoot(); sys = root / "System"; docs = os->docsDir();
        os->display(monW, monH, monHz);
        { ini::File f; if (f.load(launcherIni())) uiEn = ini::lower(f.get("Launcher", "Language", "ru")) == "en"; else uiEn = os->systemEnglish(); }
        lm.init(root);
        lm.setEngineLanguage = [this](bool en) { return writeMain([&](ini::File& f) { f.set("Engine.Engine", "Language", en ? "int" : "Rus"); }); };
        ensureLang();
        reload();
    }

    void reload() {
        dxvk = fileContains(sys / "d3d9.dll", "dxvk") || fileContains(sys / "d3d9.dll", "DXVK");
        std::vector<uint8_t> dd; long mo = msaaOffset(dd); msaaPatch = mo >= 0;
        tabs.clear();

        // ---------------- Экран
        Tab scr; scr.title = "Экран"; scr.heading = "Экран";
        {
            Row r; r.id = "monitor"; r.kind = INFO; r.label = "Монитор";
            r.text = std::to_string(monW) + " × " + std::to_string(monH) + ", " + std::to_string(monHz) + " Гц";
            r.hint = "Игра запускается в окне без рамки во весь экран, в разрешении монитора. Alt+Tab переключает мгновенно.";
            scr.rows.push_back(r);
        }
        {
            Row r; r.id = "fps"; r.label = "Ограничение кадров";
            fpsValues = { 0, 60, 120 };
            if (monHz >= 100) fpsValues.push_back(monHz - 3);
            if (indexOf(fpsValues, monHz) < 0) fpsValues.push_back(monHz);
            for (int v : fpsValues) {
                if (v == 0) r.options.push_back("Нет");
                else if (monHz >= 100 && v == monHz - 3) r.options.push_back(std::to_string(v) + " (G-Sync)");
                else if (v == monHz) r.options.push_back(std::to_string(v) + " (монитор)");
                else r.options.push_back(std::to_string(v));
            }
            int cur = atoi(dxvkGet("d3d9.maxFrameRate").c_str());
            int i = indexOf(fpsValues, cur);
            if (i < 0 && cur > 0) { fpsValues.push_back(cur); r.options.push_back(std::to_string(cur)); i = (int)fpsValues.size() - 1; }
            r.value = i < 0 ? 0 : i;
            r.enabled = dxvk; r.disabledNote = "нужен DXVK";
            r.hint = "С G-Sync/FreeSync лучше держать на 3 кадра ниже частоты монитора: так кадры всегда попадают в окно синхронизации.";
            scr.rows.push_back(r);
        }
        {
            Row r; r.id = "vsync"; r.label = "Вертикальная синхронизация";
            r.options = { "Как в игре", "Вкл", "Выкл" };
            std::string v = dxvkGet("d3d9.presentInterval");
            r.value = v.empty() ? 0 : (v == "0" ? 2 : 1);
            r.enabled = dxvk; r.disabledNote = "нужен DXVK";
            r.hint = "С G-Sync обычно выключают, оставляя ограничение кадров. Без G-Sync включите, чтобы не было разрывов кадра.";
            scr.rows.push_back(r);
        }
        {
            int cur = (int)(atof(readUser("Engine.PlayerController", "DefaultFOV", "95").c_str()) + 0.5);
            if (cur < 75 || cur > 110) cur = 95;
            {
                Row r; r.id = "fov_preset"; r.label = "Угол обзора"; r.options = { "Старый (85)", "Современный (95)", "Новый (105)", "Свой" };
                r.value = cur == 85 ? 0 : cur == 95 ? 1 : cur == 105 ? 2 : 3;
                r.hint = "Современный (95) — по умолчанию. Старый (85) — авторский угол 2004 года, Новый (105) — самый широкий. Патч сам расширяет угол под широкий экран (Hor+). В игре Insert переключает их по кругу. Меню всегда показываются при 85.";
                scr.rows.push_back(r);
            }
            Row r; r.id = "fov"; r.label = "Свой угол"; r.kind = SLIDER; r.vmin = 75; r.vmax = 110;
            r.value = cur;
            r.hint = "Любое значение от 75 до 110 вместо готовых.";
            scr.rows.push_back(r);
        }
        {
            Row r; r.id = "bright"; r.label = "Яркость"; r.kind = SLIDER; r.vmin = 0; r.vmax = 100;
            r.value = (int)(atof(readMain("WinDrv.WindowsClient", "Brightness", "0.5").c_str()) * 100 + 0.5);
            r.hint = "50 — как задумано. То же значение меняет ползунок яркости в настройках самой игры.";
            scr.rows.push_back(r);
        }
        tabs.push_back(scr);

        // ---------------- Графика
        Tab gr; gr.title = "Графика"; gr.heading = "Графика";
        {
            Row r; r.id = "msaa"; r.label = "Сглаживание (MSAA)"; r.options = { "Выкл", "2x", "4x", "8x" };
            int v = 0; if (mo >= 0) memcpy(&v, &dd[mo], 4);
            r.value = v >= 8 ? 3 : v >= 4 ? 2 : v >= 2 ? 1 : 0;
            r.enabled = msaaPatch; r.disabledNote = "нужен основной патч";
            r.hint = "Сглаживает края геометрии. 4x — хороший баланс; 8x почти не нагружает современные видеокарты.";
            gr.rows.push_back(r);
        }
        bool shrz = osio::exists(sys / "shrz.dll");
        {
            Row r; r.id = "ssaa"; r.label = "Суперсэмплинг"; r.kind = TOGGLE;
            r.value = ini::lower(dxvkGet("d3d9.forceSampleRateShading")) == "true";
            Row* m = nullptr; for (auto& x : gr.rows) if (x.id == "msaa") m = &x;
            r.enabled = dxvk && m && m->value > 0; r.disabledNote = dxvk ? "нужно MSAA" : "нужен DXVK";
            r.hint = "Каждый пиксель считается по всем выборкам MSAA, а не только края: гладкие листва, трава, решётки и текстуры вдали. Нагрузка на видеокарту растёт во столько раз, сколько выборок MSAA.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "foliageaa"; r.label = "Сглаживание листвы"; r.kind = TOGGLE;
            r.value = readRender("Image", "FoliageAA", "0") != "0";
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "Края листьев, травы и решёток сглаживаются так же, как края моделей (alpha-to-coverage). Работает вместе со сглаживанием MSAA.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "pplight"; r.label = "Попиксельное освещение"; r.kind = TOGGLE;
            r.value = readRender("Lighting", "PerPixel", "1") != "0";
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "Свет игры считается для каждой точки, а не только в вершинах моделей: плавные пятна от фонарей и факелов. В игре: F3 — вкл/выкл, F4 — показать только свет.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "relief"; r.label = "Рельеф поверхностей"; r.options = { "Выкл", "Слабый", "Обычный" };
            r.value = std::clamp(atoi(readRender("Lighting", "Relief", "2").c_str()), 0, 2);
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "Объём из самих текстур: швы камней, доски, складки. Нужно попиксельное освещение. В игре F5 перебирает уровни.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "relief_world"; r.label = "Рельеф на стенах и земле"; r.kind = TOGGLE;
            r.value = readRender("Lighting", "ReliefWorld", "1") != "0";
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "Рельеф и на поверхностях с заранее рассчитанным светом (стены, земля, скалы), а не только на персонажах и предметах.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "lmfilter"; r.label = "Плавный запечённый свет"; r.kind = TOGGLE;
            r.value = readRender("Lighting", "LightmapFilter", "1") != "0";
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "Карты освещения уровней (свет и тени, рассчитанные заранее) сглаживаются бикубически: мягкие переходы вместо ступенек и ромбиков. Нужно попиксельное освещение.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "finedetail"; r.label = "Мелкие детали вблизи"; r.options = { "Выкл", "Слабые", "Обычные" };
            double v = atof(readRender("Image", "FineDetail", "0.35").c_str());
            r.value = v <= 0.01 ? 0 : v < 0.3 ? 1 : 2;
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "Там, где разработчики растянули одну текстуру на большую стену, скалу или землю, вблизи добавляется мелкое зерно без рисунка: меньше «каши». Персонажей не касается. Нужно попиксельное освещение.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "aerial"; r.label = "Воздушная перспектива"; r.options = { "Выкл", "Слабая", "Обычная" };
            double v = atof(readRender("Image", "AerialPerspective", "0.35").c_str());
            r.value = v <= 0.01 ? 0 : v < 0.3 ? 1 : 2;
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "Дальние планы чуть бледнее и голубее, как в настоящем воздухе: появляется глубина, задники не спорят с тем, что рядом. Нужно попиксельное освещение.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "ssao"; r.label = "Затенение в углах (SSAO)"; r.options = { "Выкл", "Слабое", "Среднее" };
            r.value = std::min(2, fxLevel(atof(readRender("Lighting", "AmbientOcclusion", "1").c_str()), 0.8, 1.25));
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "Мягкая тень там, где поверхности сходятся: углы комнат, под столами, у корней деревьев, складки одежды. Нужно попиксельное освещение. В игре F6 — показать только затенение / всё выкл.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "charshadows"; r.label = "Тени персонажей"; r.options = { "Выкл", "Светлые", "Обычные" };
            r.value = std::min(2, fxLevel(atof(readRender("Lighting", "CharacterShadows", "0.6").c_str()), 0.5, 0.75));
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "Настоящие тени героев от солнца (или сверху, если солнца нет) на землю, стены и друг на друга. Нужно попиксельное освещение. Круглые тени игры можно выключить в её настройках.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "worldsh"; r.label = "Тени мира от солнца"; r.options = { "Выкл", "Низкие", "Высокие" };
            { double v = atof(readRender("Lighting", "WorldShadows", "1").c_str()); r.value = v <= 0.01 ? 0 : v < 0.75 ? 1 : 2; }
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "Тени от солнца — домов, деревьев с листвой, скал, крыш, мостов — на землю, стены и героев: зашёл в тень дома — потемнел, наполовину в тени — наполовину. Тень запоминается, когда предмет хоть раз был на экране. «Низкие» — для слабых видеокарт: меньше памяти, простые края. Работает вместе с «Тенями персонажей».";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "water"; r.label = "Отражения на воде"; r.options = { "Выкл", "Слабые", "Обычные" };
            double v = atof(readRender("Image", "WaterReflections", "0.6").c_str());
            r.value = v <= 0.01 ? 0 : v < 0.45 ? 1 : 2;
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "Вода (ровные поверхности с движущейся текстурой) отражает то, что видно на экране: берега, деревья, героев, небо. Чем положе взгляд, тем сильнее. Нужно «Затенение в углах» (SSAO).";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "rays"; r.label = "Лучи солнца"; r.options = { "Выкл", "Мягкие", "Яркие" };
            double v = atof(readRender("Image", "SunRays", "0.5").c_str());
            r.value = v <= 0.01 ? 0 : v < 0.7 ? 1 : 2;
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "Когда солнце в кадре, от него сквозь листву, окна и щели расходятся лучи света. Только на уровнях с солнцем.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "bloom"; r.label = "Свечение ярких мест"; r.kind = TOGGLE;
            r.value = atof(readRender("Image", "Bloom", "0").c_str()) > 0.01;
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "Bloom: яркий свет — окна, небо, огонь, волшебство — мягко светится по краям.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "sharpen"; r.label = "Повышение резкости"; r.options = { "Выкл", "Слабое", "Среднее" };
            r.value = std::min(2, sharpenLevel(atof(readRender("Image", "Sharpen", "0").c_str())));
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "AMD FidelityFX CAS: возвращает чёткость, которую съедают сглаживание и фильтрация. В игре F1 — вкл/выкл.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "camfade"; r.label = "Прозрачность перед камерой"; r.kind = TOGGLE;
            r.value = ini::lower(readUser("ShGame.ShrekController", "bDoOpacityForCamera", "True")) != "false";
            r.hint = "Деревья, стены и прочее, что встаёт между героем и камерой, становятся полупрозрачными — так задумано в игре. Выключите, если мешают просвечивающие силуэты (предметы тогда просто загораживают героя).";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "aniso"; r.label = "Анизотропная фильтрация"; r.options = { "Выкл", "2x", "4x", "8x", "16x" };
            int v = atoi(readMain("D3DDrv.D3DRenderDevice", "LevelOfAnisotropy", "1").c_str());
            r.value = v >= 16 ? 4 : v >= 8 ? 3 : v >= 4 ? 2 : v >= 2 ? 1 : 0;
            r.hint = "Чёткость текстур пола и стен под углом. На современных видеокартах 16x ничего не стоит.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "texdetail"; r.label = "Детализация текстур"; r.options = { "Обычная", "Высокая", "Ультра" };
            std::string v = ini::lower(readMain("WinDrv.WindowsClient", "TextureDetailWorld", "Normal"));
            r.value = v == "ultrahigh" ? 2 : (v == "high" || v == "veryhigh") ? 1 : 0;
            r.hint = "«Ультра» загружает текстуры в полном размере, без уменьшения. Нужна для HD-текстур.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "shadows"; r.label = "Тени"; r.options = { "Выкл", "Простые", "Полные" };
            bool no = ini::lower(readMain("WinDrv.WindowsClient", "NoShadows", "false")) == "true";
            bool simple = ini::lower(readMain("WinDrv.WindowsClient", "SimpleShadows", "false")) == "true";
            r.value = no ? 0 : simple ? 1 : 2;
            r.hint = "Полные — тени по форме персонажей, простые — круглые пятна под ногами.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "decals"; r.label = "Декали"; r.kind = TOGGLE;
            r.value = ini::lower(readMain("WinDrv.WindowsClient", "Decals", "True")) == "true";
            r.hint = "Следы, пятна и отметины на поверхностях.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "coronas"; r.label = "Блики источников света"; r.kind = TOGGLE;
            r.value = ini::lower(readMain("WinDrv.WindowsClient", "Coronas", "True")) == "true";
            r.hint = "Ореолы вокруг фонарей, факелов и окон.";
            gr.rows.push_back(r);
        }
        {
            Row r; r.id = "autodetail"; r.label = "Снижать детали при низком FPS"; r.kind = TOGGLE;
            r.value = atof(readMain("WinDrv.WindowsClient", "MinDesiredFrameRate", "35").c_str()) > 0;
            r.hint = "Движок 2004 года сам убирает детали, если кадров меньше 35. На современном железе это не нужно — лучше выключить.";
            gr.rows.push_back(r);
        }
        {   // preset: the 2005 picture, or the player's own settings (any change of a row below makes it "custom")
            Row r; r.id = "gpreset"; r.label = "Пресет графики"; r.options = { "Оригинал 2004", "Для слабых ПК", "Настраиваемый" };
            auto is = [&](auto& t) { for (auto& x : gr.rows) for (auto& o : t) if (x.id == o.first && x.enabled && x.value != o.second) return false; return true; };
            r.value = is(PRESET_ORIGINAL) ? 0 : is(PRESET_WEAK) ? 1 : 2;
            r.enabled = shrz && dxvk; r.disabledNote = shrz ? "нужен DXVK" : "нужен основной патч";
            r.hint = "«Оригинал 2004» — картинка как в 2005 году: всё, что добавляет патч, выключено (сглаживание, новое освещение, рельеф, затенение, тени, свечение, резкость, детали), а настройки игры — как были: без анизотропной фильтрации, обычная детализация текстур. «Для слабых ПК» — улучшения, которые почти ничего не стоят: попиксельный свет, мягкие тени с низким качеством, резкость, сглаживание 2x; без затенения в углах, отражений, лучей и свечения. «Настраиваемый» возвращает ваши настройки; меняется сам, как только вы меняете что-то ниже.";
            gr.rows.insert(gr.rows.begin(), r);
        }
        tabs.push_back(gr);

        // ---------------- Звук
        Tab sn; sn.title = "Звук"; sn.heading = "Звук";
        bool oal = osio::exists(sys / "OpenAL32.dll");
        bool newSnd = oal && ini::lower(readAudio("UseDefaultDriver", "True")) == "false";
        {
            Row r; r.id = "snd_engine"; r.label = "Звуковой движок"; r.options = { "Новый", "Оригинал" };
            r.value = newSnd ? 0 : 1;
            r.enabled = oal; r.disabledNote = "нужен основной патч";
            r.hint = "Новый — OpenAL Soft: объёмный звук в наушниках, настоящие 5.1/7.1, эхо и реверберация по зонам уровня, чистый ресемплинг. Оригинал — библиотека Creative 2004 года, как было.";
            sn.rows.push_back(r);
        }
        {
            Row r; r.id = "snd_out"; r.label = "Вывод звука"; r.options = { "Как в Windows", "Наушники", "Стерео", "Квадро", "5.1", "7.1" };
            std::string ch = ini::lower(readAlsoft("general", "channels", "")), hrtf = ini::lower(readAlsoft("general", "hrtf", ""));
            r.value = hrtf == "true" ? 1 : ch == "stereo" ? 2 : ch == "quad" ? 3 : ch == "surround51" ? 4 : ch == "surround71" ? 5 : 0;
            r.enabled = newSnd; r.disabledNote = "с новым движком";
            r.hint = "«Как в Windows» берёт схему колонок из настроек Windows (для ресивера обычно 7.1 — он сам разложит на ваши колонки). В наушниках выберите «Наушники»: звуки будут слышны сзади, сверху, вдалеке.";
            sn.rows.push_back(r);
        }
        {
            Row r; r.id = "snd_eax"; r.label = "Эффекты среды"; r.kind = TOGGLE;
            r.value = ini::lower(readAudio("UseEAX", "False")) == "true";
            r.enabled = newSnd; r.disabledNote = "с новым движком";
            r.hint = "Эхо и реверберация, заданные в уровнях (EAX): гулкие пещеры и залы, глухой лес. Раньше работало только на звуковых картах Creative.";
            sn.rows.push_back(r);
        }
        {
            Row r; r.id = "snd_3d"; r.label = "Объёмное позиционирование"; r.kind = TOGGLE;
            r.value = ini::lower(readAudio("Use3DSound", "False")) == "true";
            r.hint = "Звуки располагаются в пространстве вокруг камеры, а не только между левым и правым каналом.";
            sn.rows.push_back(r);
        }
        {
            Row r; r.id = "snd_ch"; r.label = "Одновременных звуков"; r.options = { "32", "64", "128" };
            int v = atoi(readAudio("Channels", "32").c_str());
            r.value = v >= 128 ? 2 : v >= 64 ? 1 : 0;
            r.hint = "Сколько звуков может играть сразу. 32 — как в 2004 году: в шумных сценах звуки обрывались.";
            sn.rows.push_back(r);
        }
        {
            Row r; r.id = "snd_music"; r.label = "Громкость музыки"; r.kind = SLIDER; r.vmin = 0; r.vmax = 100; r.unit = "%";
            r.value = std::clamp((int)std::lround(atof(readAudio("MusicVolume", "0.3").c_str()) * 100), 0, 100);
            r.hint = "То же, что ползунок музыки в настройках самой игры.";
            sn.rows.push_back(r);
        }
        {
            Row r; r.id = "snd_fx"; r.label = "Громкость звуков"; r.kind = SLIDER; r.vmin = 0; r.vmax = 100; r.unit = "%";
            r.value = std::clamp((int)std::lround(atof(readAudio("SoundVolume", "1.0").c_str()) * 100), 0, 100);
            r.hint = "Эффекты и голоса.";
            sn.rows.push_back(r);
        }
        tabs.push_back(sn);

        // ---------------- Клавиатура и мышь
        Tab ct; ct.title = "Клавиатура и мышь"; ct.heading = "Клавиатура и мышь"; ct.group = "Управление";
        auto binds = readBinds();
        {
            Row r; r.id = "layout"; r.label = "Раскладка"; r.options = { "Оригинал", "Современная" };
            int p = presetOf(binds);
            if (p < 0) { r.options.push_back("Своя"); r.value = 2; } else r.value = p;
            r.hint = "Готовые наборы. Современная: прыжок на пробеле, колесо мыши выбирает зелье, правая кнопка его применяет, Q — меню зелий. Ниже любую клавишу можно переназначить.";
            ct.rows.push_back(r);
        }
        for (size_t i = 0; i < BIND_ACTS.size(); i++) {
            Row r; r.id = "bind" + std::to_string(i); r.kind = BIND; r.label = BIND_ACTS[i].label; r.act = (int)i;
            for (auto& kv : binds) {
                if (r.bound.size() >= 2) break;
                for (auto& part : splitCmd(kv.second)) if (partIsAct(part, BIND_ACTS[i])) { r.bound.push_back(kv.first); break; }
            }
            r.hint = std::string(BIND_ACTS[i].hint) + (BIND_ACTS[i].hint[0] ? " " : "") +
                     "Щёлкните по ячейке и нажмите клавишу или кнопку мыши; Esc — отмена, правый щелчок по ячейке — очистить.";
            ct.rows.push_back(r);
        }
        {
            Row r; r.kind = HEADER; r.label = "Мышь"; ct.rows.push_back(r);
        }
        bool pad = osio::exists(padIni());
        {
            Row r; r.id = "rawmouse"; r.label = "Прямой ввод"; r.kind = TOGGLE;
            r.value = readPad("RawMouse", "1") != "0";
            r.enabled = pad; r.disabledNote = "нужен основной патч";
            r.hint = "Движение мыши читается прямо с датчика: без ускорения Windows и без влияния скорости указателя. Выключите, если мышь ведёт себя странно.";
            ct.rows.push_back(r);
        }
        {
            Row r; r.id = "mousesens"; r.label = "Скорость мыши"; r.kind = SLIDER; r.vmin = 2; r.vmax = 50; r.div = 10;
            r.value = (int)std::lround(atof(readPad("MouseSensitivity", "1.5").c_str()) * 10);
            r.value = std::clamp(r.value, r.vmin, r.vmax);
            r.enabled = pad && readPad("RawMouse", "1") != "0"; r.disabledNote = "при прямом вводе";
            r.hint = "Множитель к отсчётам датчика мыши при прямом вводе. 1.5 — примерно как скорость курсора Windows по умолчанию.";
            ct.rows.push_back(r);
        }
        {
            Row r; r.id = "invert"; r.label = "Инверсия по вертикали"; r.kind = TOGGLE;
            r.value = ini::lower(readUser("Engine.PlayerInput", "bInvertMouse", "False")) == "true";
            r.hint = "Мышь вперёд — камера смотрит вниз, как в авиасимуляторах.";
            ct.rows.push_back(r);
        }
        {
            Row r; r.id = "smooth"; r.label = "Сглаживание мыши"; r.kind = TOGGLE;
            r.value = atoi(readUser("Engine.PlayerInput", "MouseSmoothingMode", "0").c_str()) != 0;
            r.hint = "Сглаживание движка 2004 года повторяло прошлое движение в кадрах без данных от мыши. С прямым вводом оно только добавляет задержку — лучше выключить.";
            ct.rows.push_back(r);
        }
        {
            Row r; r.id = "gamecursor"; r.label = "Курсор из игры"; r.kind = TOGGLE;
            r.value = readPad("GameCursor", "1") != "0";
            r.enabled = pad; r.disabledNote = "нужен основной патч";
            r.hint = "В меню — зелёная стрелка из игры вместо курсора Windows.";
            ct.rows.push_back(r);
        }
        tabs.push_back(ct);

        // ---------------- Геймпад
        Tab gp; gp.title = "Геймпад"; gp.heading = "Геймпад"; gp.group = "Управление";
        { std::string st = ini::lower(readPad("ButtonStyle", "xbox")); padStyle = st == "ps" ? PAD_PS : st == "nintendo" ? PAD_NINTENDO : PAD_XBOX; }
        {
            auto tog = [&](const char* id, const char* label, const char* key, const char* def, const char* hint) {
                Row r; r.id = id; r.label = label; r.kind = TOGGLE; r.value = readPad(key, def) != "0";
                r.enabled = pad; r.disabledNote = "нужен основной патч"; r.hint = hint; gp.rows.push_back(r);
            };
            auto sld = [&](const char* id, const char* label, const char* key, const char* def, int vmin, int vmax, int div, const char* unit, int mul, const char* hint) {
                Row r; r.id = id; r.label = label; r.kind = SLIDER; r.vmin = vmin; r.vmax = vmax; r.div = div; r.unit = unit;
                r.value = std::clamp((int)std::lround(atof(readPad(key, def).c_str()) * mul), vmin, vmax);
                r.enabled = pad; r.disabledNote = "нужен основной патч"; r.hint = hint; gp.rows.push_back(r);
            };
            {
                Row r; r.id = "pad_style"; r.label = "Значки кнопок"; r.options = { "Xbox", "PlayStation", "Nintendo" };
                r.value = padStyle; r.enabled = pad; r.disabledNote = "нужен основной патч";
                r.hint = "Как подписывать кнопки здесь, в лаунчере: как на геймпаде Xbox, PlayStation или Nintendo.";
                gp.rows.push_back(r);
            }
            tog("pad_on", "Геймпад", "Enabled", "1", "Xbox и другие XInput-геймпады, DualShock 4 и DualSense — напрямую, без Steam и DS4Windows. Меняется при следующем запуске игры.");
            sld("pad_cam", "Скорость камеры", "CameraSensitivity", "1.3", 3, 25, 10, "", 10, "Во сколько раз правый стик поворачивает камеру быстрее стрелок на клавиатуре.");
            sld("pad_curve", "Кривая правого стика", "CameraCurve", "1.6", 10, 30, 10, "", 10, "1.0 — линейно. Больше — точнее при малом отклонении стика, быстрее при полном.");
            sld("pad_dzl", "Мёртвая зона левого", "DeadzoneLeft", "0.20", 5, 40, 1, "%", 100, "Доля хода стика, которая игнорируется. Увеличьте, если персонаж сам идёт при отпущенном стике.");
            sld("pad_dzr", "Мёртвая зона правого", "DeadzoneRight", "0.18", 5, 40, 1, "%", 100, "Увеличьте, если камера сама медленно поворачивается.");
            sld("pad_trig", "Порог курков", "TriggerThreshold", "0.15", 5, 60, 1, "%", 100, "С какого нажатия курок считается нажатым.");
            tog("pad_inv", "Инверсия камеры", "InvertY", "0", "Стик вперёд — камера смотрит вниз.");
            sld("pad_rumble", "Вибрация", "Rumble", "70", 0, 100, 1, "%", 1, "Удары, взрывы, приземления, топот великанов — по звукам игры, с учётом громкости и расстояния. 0 — выключить. Xbox, DualShock 4, DualSense (и через Steam/DSX).");
            tog("pad_rumsteps", "Вибрация от шагов", "RumbleSteps", "0", "Лёгкие толчки от шагов персонажа.");
            {
                Row r; r.id = "pad_lbmode"; r.label = "Световая панель"; r.options = { "Зелёная", "По здоровью", "По герою", "Герой и здоровье" };
                r.value = std::clamp(atoi(readPad("LightbarMode", "3").c_str()), 0, 3);
                r.enabled = pad; r.disabledNote = "нужен основной патч";
                r.hint = "DualShock 4 и DualSense. «По здоровью»: зелёная — жёлтая — красная. «По герою»: Шрек зелёный, Осёл серый, Кот оранжевый. «Герой и здоровье»: цвет героя, от половины здоровья краснеет; меньше четверти — мигает. Меняется при следующем запуске игры.";
                gp.rows.push_back(r);
            }
            tog("pad_adapt", "Адаптивные курки", "AdaptiveTriggers", "1", "DualSense: курки с сопротивлением — только в игре, в меню они свободны. Работает напрямую и через DSX. Меняется при следующем запуске игры.");
            sld("pad_r2", "Курок R2 (удар): сила", "TriggerR2", "10", 0, 100, 1, "%", 1, "Насколько туго нажимается R2. 0 — без сопротивления.");
            sld("pad_r2pt", "Курок R2: щелчок", "TriggerR2Point", "0", 0, 90, 1, "%", 1, "0 — без щелчка: R2 нажимается плавно по всему ходу. Больше нуля — на этой части хода курка появляется щелчок.");
            sld("pad_l2", "Курок L2 (шаг): сила", "TriggerL2", "5", 0, 100, 1, "%", 1, "Лёгкое сопротивление на L2 по всему ходу. 0 — без сопротивления.");
            sld("pad_small", "Курки за Осла и Кота", "TriggerSmallHeroes", "40", 0, 100, 1, "%", 1, "Какая доля силы курков остаётся, когда вы играете за Осла или Кота: они легче Шрека. 100 — как у Шрека, 0 — без сопротивления.");
            tog("pad_dsx", "Курки через DSX", "DSX", "1", "Если DualSense подключён через DSX (DualSenseX), эффекты курков передаются ему. Выключите, если DSX должен управлять курками сам.");
            {
                std::string a = PAD_BTNS[0].face[padStyle].badge, b = PAD_BTNS[1].face[padStyle].badge;
                std::string lbl = "Поменять " + a + " и " + b;
                tog("pad_swap", lbl.c_str(), "SwapAB", "0", "Подтверждение правой кнопкой, отмена — нижней (как принято на Nintendo и в японских играх).");
            }
            sld("pad_cursor", "Курсор в меню", "CursorSpeed", "1.0", 3, 30, 10, "", 10, "Скорость курсора в меню (правый стик) при полном отклонении (высот экрана в секунду).");
            tog("pad_sony", "DualShock / DualSense", "Sony", "1", "Читать геймпады PlayStation напрямую (USB и Bluetooth). Выключите, если пользуетесь Steam Input или DS4Windows.");
            {
                Row r; r.id = "pad_light"; r.label = "Подсветка PlayStation";
                for (auto& l : LIGHTS) r.options.push_back(l.label);
                r.value = 0;
                if (readPad("Lightbar", "1") != "0") {
                    int R = atoi(readPad("LightbarR", "40").c_str()), G = atoi(readPad("LightbarG", "170").c_str()), B = atoi(readPad("LightbarB", "20").c_str());
                    r.value = -1;
                    for (size_t i = 1; i < sizeof(LIGHTS) / sizeof(LIGHTS[0]); i++) if (LIGHTS[i].r == R && LIGHTS[i].g == G && LIGHTS[i].b == B) r.value = (int)i;
                    if (r.value < 0) { r.options.push_back("Своя (" + std::to_string(R) + "," + std::to_string(G) + "," + std::to_string(B) + ")"); r.value = (int)r.options.size() - 1; }
                }
                r.enabled = pad; r.disabledNote = "нужен основной патч";
                r.hint = "Цвет световой панели DualShock 4 / DualSense во время игры.";
                gp.rows.push_back(r);
            }
        }
        {
            Row r; r.kind = HEADER; r.label = "Кнопки"; gp.rows.push_back(r);
        }
        for (auto& b : PAD_BTNS) {
            const PadFace& f = b.face[padStyle];
            Row r; r.id = std::string("joy:") + b.key; r.label = f.name;
            r.badge = f.badge; r.badgeBg = f.bg; r.badgeFg = f.fg; r.badgeRound = f.round;
            for (int i = 0; i < PAD_ACT_N; i++) r.options.push_back(PAD_ACTS[i].label);
            std::string cur = "";
            for (auto& kv : binds) if (ini::lower(kv.first) == ini::lower(b.key)) cur = kv.second;
            std::string nc = normCmd(cur); r.value = -1;
            for (int i = 0; i < PAD_ACT_N; i++) if (normCmd(PAD_ACTS[i].cmd) == nc) r.value = i;
            if (r.value < 0) { r.options.push_back("Своё: " + cur); r.value = (int)r.options.size() - 1; }
            bool swap = readPad("SwapAB", "0") != "0";
            const char* ok = PAD_BTNS[swap ? 1 : 0].face[padStyle].badge; const char* back = PAD_BTNS[swap ? 0 : 1].face[padStyle].badge;
            r.hint = std::string(PAD_START[padStyle]) + " всегда открывает меню. В меню левый стик и крестовина переходят по пунктам (LB / RB — тоже), " + ok + " — выбрать, " + back + " — назад, курсор спрятан (если где-то нужен — правый стик).";
            gp.rows.push_back(r);
        }
        tabs.push_back(gp);

        // ---------------- Язык
        Tab lg; lg.title = "Язык"; lg.heading = "Язык";
        {
            Row r; r.id = "text"; r.label = "Текст и субтитры"; r.options = { "Русский (2005)", "Новый перевод", "English" };
            bool eng = ini::lower(readMain("Engine.Engine", "Language", "Rus")) == "int";
            r.value = lm.text(eng ? lang::TEXT_EN : lang::TEXT_RU);
            if (r.value == lang::TEXT_EN && !eng) r.value = lang::TEXT_RU;
            if (r.value != lang::TEXT_EN && eng) r.value = lang::TEXT_EN;
            r.hint = "«Русский (2005)» — субтитры локализации 2005 года. «Новый перевод» — субтитры, заново переведённые с английского ближе к оригиналу; меню и подсказки остаются из издания 2005. English — весь текст игры и английские надписи на текстурах.";
            lg.rows.push_back(r);
        }
        {
            Row r; r.id = "voice"; r.label = "Озвучка"; r.options = { "Русская", "Английская" };
            r.value = lm.configured() ? lm.voice() : 0;
            r.enabled = lm.haveEnglishVoice(); r.disabledNote = "нет английской";
            r.hint = r.enabled ? "Голоса персонажей и рекламный ролик Крёстной Феи. Выбирается отдельно от текста: например, английская озвучка с русскими субтитрами нового перевода."
                               : "Английская озвучка — отдельный архив Shrek2_EnhancedEdition_EnglishVoice: распакуйте его в папку игры, и выбор станет доступен.";
            lg.rows.push_back(r);
        }
        {
            Row r; r.id = "texinfo"; r.kind = INFO; r.label = "Надписи на текстурах";
            Row* t = nullptr; for (auto& x : lg.rows) if (x.id == "text") t = &x;
            bool texEn = t && t->value == lang::TEXT_EN && lm.haveEnglishTextures();
            r.text = texEn ? "английские" : "русские";
            r.hint = "Вывески, плакаты «Разыскивается», экраны загрузки и страницы книги следуют за языком текста.";
            lg.rows.push_back(r);
        }
        tabs.push_back(lg);


        // ---------------- Игра: settings of the game itself and cheats
        Tab gm; gm.title = "Игра"; gm.heading = "Игра";
        {
            Row r; r.id = "intro"; r.label = "Логотипы при запуске"; r.kind = TOGGLE;
            r.value = ini::lower(readMain("URL", "LocalMap", "SH2_Preamble.unr")) != "book_frontend.unr";
            r.hint = "Выключите, чтобы сразу попадать в меню-книгу без роликов Activision, KnowWonder и DreamWorks.";
            gm.rows.push_back(r);
        }
        {
            Row r; r.id = "camsnap"; r.label = "Камера сама встаёт за спину"; r.kind = TOGGLE;
            r.value = ini::lower(readUser("ShGame.SHHeroPawn", "SaveCameraNoSnapRotation", "False")) != "true";
            r.hint = "Когда герой бежит, камера сама разворачивается ему за спину. Выключите, если хотите держать камеру только мышью или правым стиком. То же, что «Автовыравнивание» в настройках игры.";
            gm.rows.push_back(r);
        }
        {
            Row r; r.kind = HEADER; r.label = "Субтитры"; gm.rows.push_back(r);
        }
        {
            Row r; r.id = "sub_on"; r.label = "Субтитры"; r.kind = TOGGLE; r.value = readEE("Subtitles", "Enabled", "1") != "0";
            r.enabled = pad; r.disabledNote = "нужен основной патч";
            r.hint = "Выключено — субтитров нет совсем, ни новых, ни игровых.";
            gm.rows.push_back(r);
        }
        {
            Row r; r.id = "sub_which"; r.label = "Какие реплики"; r.options = { "Все", "Только сюжетные" };
            r.value = readEE("Subtitles", "StoryOnly", "0") != "0" ? 1 : 0;
            r.enabled = pad; r.disabledNote = "нужен основной патч";
            r.hint = "«Все» — сцены и всё, что говорят на ходу: реплики героев, разговоры с жителями. «Только сюжетные» — только реплики сцен.";
            gm.rows.push_back(r);
        }
        {
            Row r; r.id = "sub_mode"; r.label = "Вид"; r.options = { "Как в игре", "Без фона в игре", "Без чёрных полос" };
            r.value = std::clamp(atoi(readEE("Subtitles", "Mode", "1").c_str()), 0, 2);
            r.enabled = pad; r.disabledNote = "нужен основной патч";
            r.hint = "Субтитры ко всем репликам — и в сценах, и во время игры, — с чёрной обводкой и именем говорящего. «Как в игре» — субтитры самой игры. «Без фона в игре» — во время игры разговоры идут без чёрных полос, в сценах полосы остаются, а субтитры стоят посередине нижней полосы. «Без чёрных полос» — полос нет и в сценах. Меняется при следующем запуске игры.";
            gm.rows.push_back(r);
        }
        if (readEE("Subtitles", "Mode", "1") != "0") {
            {
                Row r; r.id = "sub_names"; r.label = "Имя говорящего"; r.kind = TOGGLE; r.value = readEE("Subtitles", "Names", "1") != "0";
                r.enabled = pad; r.hint = "Перед репликой — кто её говорит: «Осёл: …».";
                gm.rows.push_back(r);
            }
            {
                Row r; r.id = "sub_font"; r.label = "Шрифт"; r.options = { "Прежний (DejaVu)", "Palatino (как в английской)", "Tahoma Bold", "Verdana Bold", "Georgia Bold" };
                std::string v = readEE("Subtitles", "Font", "builtin"); r.value = 0;
                for (int i = 0; i < 5; i++) if (ini::lower(v) == ini::lower(SUB_FONT[i])) r.value = i;
                r.enabled = pad; r.hint = "Гарнитура субтитров. «Прежний» — шрифт, встроенный в игру; Palatino — того же вида, что в английской версии; остальные берутся из Windows. Действует со следующего запуска игры.";
                gm.rows.push_back(r);
            }
            {
                Row r; r.id = "sub_text"; r.label = "Цвет текста"; r.options = { "Белый", "Жёлтый", "Светло-зелёный", "Голубой", "Персиковый" };
                std::string v = readEE("Subtitles", "TextColor", "FFFFFF"); r.value = 0;
                for (int i = 0; i < 5; i++) if (ini::lower(v) == ini::lower(SUB_TEXT[i])) r.value = i;
                r.enabled = pad; r.hint = "Цвет самих реплик.";
                gm.rows.push_back(r);
            }
            {
                Row r; r.id = "sub_name"; r.label = "Цвет имени"; r.options = { "Цвет персонажа", "Золотой", "Зелёный", "Голубой", "Оранжевый", "Белый" };
                std::string v = readEE("Subtitles", "NameColor", "char"); r.value = 0;
                for (int i = 0; i < 6; i++) if (ini::lower(v) == ini::lower(SUB_NAME[i])) r.value = i;
                r.enabled = pad; r.hint = "Цвет имени того, кто говорит. «Цвет персонажа»: у каждого свой — Шрек зелёный, Осёл серый, Кот рыжий, Фиона бирюзовая, Крёстная Фея голубая, Пряник коричневый и так далее.";
                gm.rows.push_back(r);
            }
            {
                Row r; r.id = "sub_size"; r.label = "Размер"; r.kind = SLIDER; r.vmin = 70; r.vmax = 150; r.step = 5; r.unit = "%";
                r.value = std::clamp(atoi(readEE("Subtitles", "Size", "100").c_str()), 70, 150);
                r.enabled = pad; r.hint = "Размер букв субтитров.";
                gm.rows.push_back(r);
            }
        }
        {
            Row r; r.kind = HEADER; r.label = "Разное"; gm.rows.push_back(r);
        }
        {
            Row r; r.id = "photoinfo"; r.kind = INFO; r.label = "Фоторежим"; r.text = "F9 / View + Y";
            r.hint = "В игре F9 (на геймпаде View/Share + Y): мир замирает, интерфейс и субтитры прячутся, камера летает свободно. WASD, стрелки или левый стик — движение по взгляду, мышь или правый стик — поворот, пробел/E/RT — вверх, C/Q/Ctrl/LT — вниз, Shift или бамперы — быстрее. F9 ещё раз — обратно в игру.";
            gm.rows.push_back(r);
        }
        {
            Row r; r.id = "speedrun"; r.label = "Таймер для спидрана"; r.kind = TOGGLE; r.value = readEE("Game", "SpeedrunTimer", "0") != "0";
            r.enabled = pad; r.disabledNote = "нужен основной патч";
            r.hint = "В правом верхнем углу — время уровня и всего прохождения. Загрузки, открытое меню и меню-книга не считаются; новое прохождение начинается с новой игры. Итоги уровней пишутся в ShrekRender.log.";
            gm.rows.push_back(r);
        }
        {
            Row r; r.id = "devmenu"; r.label = "Меню разработчика"; r.kind = TOGGLE; r.value = readEE("Game", "DevMenu", "0") != "0";
            r.enabled = pad; r.disabledNote = "нужен основной патч";
            r.hint = "Включает отладочный режим, оставленный разработчиками (то же, что «set SHGame.Version bDebugEnabled True»). В игре F7 открывает их меню: переход на любой уровень и закладки; F2 — отладочные окна. Может менять поведение игры — для обычного прохождения выключите. Меняется при следующем запуске игры.";
            gm.rows.push_back(r);
        }
        {
            Row r; r.id = "cheats"; r.label = "Читы"; r.kind = TOGGLE;
            r.value = readEE("Cheats", "Enabled", "0") != "0";
            r.enabled = pad; r.disabledNote = "нужен основной патч";
            r.hint = "Включает настройки ниже. Они действуют с начала каждого уровня; выключите галочку — игра снова обычная. Сохранения при этом не портятся, но монеты и зелья, полученные так, в них останутся.";
            gm.rows.push_back(r);
        }
        if (readEE("Cheats", "Enabled", "0") != "0" && pad) {
            {
                Row r; r.id = "ch_health"; r.label = "Бесконечное здоровье"; r.kind = TOGGLE;
                r.value = readEE("Cheats", "EndlessHealth", "0") != "0";
                r.hint = "Здоровье героя всё время полное. Упасть в пропасть всё равно можно.";
                gm.rows.push_back(r);
            }
            {
                Row r; r.id = "ch_coins"; r.label = "Монеты в начале уровня"; r.options = { "Нет", "+50", "+100", "+500", "+1000" };
                int v = atoi(readEE("Cheats", "CoinsPerLevel", "0").c_str());
                r.value = v >= 1000 ? 4 : v >= 500 ? 3 : v >= 100 ? 2 : v >= 50 ? 1 : 0;
                r.hint = "Столько монет добавляется при входе на каждый уровень.";
                gm.rows.push_back(r);
            }
            {
                Row r; r.id = "ch_potions"; r.label = "Зелья в начале уровня"; r.options = { "Нет", "+1", "+3", "+5", "+10" };
                int v = atoi(readEE("Cheats", "PotionsPerLevel", "0").c_str());
                r.value = v >= 10 ? 4 : v >= 5 ? 3 : v >= 3 ? 2 : v >= 1 ? 1 : 0;
                r.hint = "Столько штук каждого из девяти зелий добавляется при входе на каждый уровень.";
                gm.rows.push_back(r);
            }
            {
                Row r; r.id = "ch_speed"; r.label = "Скорость игры"; r.options = { "50%", "75%", "100%", "125%", "150%", "200%" };
                int v = atoi(readEE("Cheats", "GameSpeed", "100").c_str());
                static const int sp[] = { 50, 75, 100, 125, 150, 200 };
                r.value = 2; for (int i = 0; i < 6; i++) if (v == sp[i]) r.value = i;
                r.hint = "Замедление или ускорение всего: героя, врагов, анимаций и звука.";
                gm.rows.push_back(r);
            }
        }
        {
            Row r; r.kind = HEADER; r.label = "Сообщить об ошибке"; gm.rows.push_back(r);
        }
        {
            Row r; r.kind = PARA;
            r.text = "Увидели ошибку — нажмите в игре F12 (геймпад: View/Share + LB): сохранятся снимок экрана и отметка в журналах с уровнем и местом. Потом здесь соберите отчёт — архив со снимками, журналами и настройками появится на рабочем столе, его и пришлите.";
            gm.rows.push_back(r);
        }
        {
            Row r; r.id = "bugreport"; r.kind = MODULE; r.label = "Отчёт об ошибке";
            int marks = 0; Path bd = sys / "BugReports";
            { std::error_code ec; for (auto& e : osio::fs::directory_iterator(bd, ec)) if (e.path().extension() == ".png") marks++; }
            r.text = marks ? std::string(uiEn ? "marks: " : "отметок: ") + std::to_string(marks) : "отметок нет";
            r.hint = "В отчёт входят журналы игры (System\\*.log), её настройки, версия патча и снимки отметок. Ничего личного, кроме имени папки игры.";
            r.actions.push_back({ "Собрать отчёт", [this]() { return makeReport(); } });
            if (marks) r.actions.push_back({ "Очистить отметки", [this, bd]() {
                std::error_code ec; for (auto& e : osio::fs::directory_iterator(bd, ec)) if (e.path().extension() == ".png" || e.path().filename() == "marks.txt") osio::fs::remove(e.path(), ec);
                return std::string("Отметки удалены"); } });
            gm.rows.push_back(r);
        }
        tabs.push_back(gm);

        // ---------------- Уровни
        Tab lv; lv.title = "Уровни"; lv.heading = "Уровни";
        {
            Row r; r.id = "freelook"; r.label = "Свободный осмотр"; r.kind = TOGGLE;
            r.value = readLauncher("Levels", "FreeLook", "0") != "0";
            r.enabled = pad; r.disabledNote = "нужен основной патч";
            r.hint = "Уровень, выбранный ниже, откроется для осмотра. Когда герой в ваших руках, F8 или нажатие обоих стиков включает и выключает полёт сквозь стены: движение — WASD, стрелки или левый стик по направлению камеры; вверх — пробел, E, A или правый курок; вниз — C, Q, Ctrl, B или левый курок; быстрее — Shift или бамперы. Здоровье не кончается. Только для этого запуска; обычная игра — кнопкой «Играть».";
            lv.rows.push_back(r);
        }
        for (auto& l : LEVELS) {
            Row r; r.id = std::string("map:") + l.map; r.kind = LEVEL; r.label = l.chapter; r.sub = l.name; r.icon = l.icon;
            r.enabled = osio::exists(root / "Maps" / l.map);
            r.hint = "Щёлкните, чтобы начать игру прямо с этого уровня. Сохранение пойдёт в ячейку, которую вы выбирали в меню игры последней, — не перезапишите своё прохождение.";
            lv.rows.push_back(r);
        }
        tabs.push_back(lv);

        // ---------------- Скрытый контент: то, что есть в файлах игры, но игроку не показывается
        Tab hs; hs.title = "Скрытый контент"; hs.heading = "Скрытый контент";
                {
            Row r; r.kind = PARA;
            r.text = "Места, которые есть в файлах уровней, но в игре недоступны: декорации, на которых снимали ролики, и площадки сцен. Уровень открывается как обычно; как только закончится вступительная сцена (её можно пропустить прыжком), герой сам переносится в это место и дальше ходит. Летать — F8 или оба стика, как в «Уровни» → «Свободный осмотр». Места из уровней, где есть только сцены, открываются в фоторежиме (F9 — выйти).";
            hs.rows.push_back(r);
        }
        struct Spot { const char* map; const char* at; const char* icon; const char* label; const char* sub; const char* hint; };
        static const Spot SPOTS[] = {
            { "1_Shreks_Swamp.unr", "-2734,-3238,-790", "1_Shreks_Swamp", "Болото Шрека", "Старое болото у дома",
              "Площадка вступления и финала главы (в карте — «старое вступление»): дом и уборная Шрека. Сюда игра приводит только в сценах." },
            { "1_Shreks_Swamp.unr", "-15600,-9391,-900", "1_Shreks_Swamp", "Болото Шрека", "Съёмки рекламы Крёстной Феи",
              "Цветные комнаты с Крёстной Феей, её логотипом, крестьянами и зельями, с камерами и метками сцены «Com_». На них сняли ролик FGMcommercial.bik, который игра показывает у плаката Феи; живой сцены в игре нет." },
            { "1_Shreks_Swamp.unr", "-11100,-6650,-930", "1_Shreks_Swamp", "Болото Шрека", "Таверна «Отравленное яблоко»",
              "91 предмет мебели таверны, 11 стопок монет и 3 мешка с деньгами — настоящие, их можно собрать. Похоже на остаток вырезанного бонусного уровня: в озвучке есть реплика Шрека «Get all the coins you can!» с именем PoisonAppIntro — так же, как у бонусного «Бобового стебля»." },
            { "2_Carriage_Hijack.unr", "-13482,-1360,810,photo", "2_Carraige_Hijack", "Нападение на карету", "Внутри кареты",
              "Отдельно собранный салон кареты для сцены, где герои едут внутри. Открывается в фоторежиме, когда уровень загрузится." },
            { "4_FGM_Office.unr", "-60,60,-300,photo16384", "4_FGM_PIB", "Контора Крёстной Феи", "Приёмная Феи",
              "Уровень из одних сцен — приход к Крёстной Фее: очередь посетителей и её стол, где она встречает Шрека. Открывается в фоторежиме, когда сцена начнётся; F9 — продолжить сцену." },
            { "4_FGM_Office.unr", "1290,1420,2530,photo", "4_FGM_PIB", "Контора Крёстной Феи", "Коридор конторы",
              "Уровень из одних сцен — приход к Крёстной Фее. Открывается в фоторежиме у коридора, где Кот и Осёл разбираются с эльфами; F9 — продолжить сцену." },
            { "9_Prison_Shrek.unr", "-18375,5181,240", "9_Prison_Shrek", "Тюрьма: Шрек", "Двор с Монго",
              "Площадка финальной сцены главы: ворота замка и великан Монго (огромный пряник), который приходит за Шреком." },
        };
        for (auto& sp : SPOTS) {
            Row r; r.id = std::string("map:") + sp.map + "@" + sp.at; r.kind = LEVEL; r.label = sp.label; r.sub = sp.sub; r.icon = sp.icon;
            r.enabled = pad && osio::exists(root / "Maps" / sp.map);
            r.disabledNote = "нужен основной патч";
            r.hint = sp.hint;
            hs.rows.push_back(r);
        }
        // voice lines: the text of the active language from the game's own dialogue file
        std::map<std::string, std::string> dlg;
        {
            bool eng = ini::lower(readMain("Engine.Engine", "Language", "Rus")) == "int";
            std::vector<uint8_t> b;
            if ((osio::readFile(sys / (eng ? "HpDialog.int" : "HpDialog.Rus"), b) || osio::readFile(sys / (eng ? "HpDialog.Rus" : "HpDialog.int"), b)) && b.size() > 2 && b[0] == 0xFF && b[1] == 0xFE) {
                std::string line;
                for (size_t i = 2; i + 1 < b.size(); i += 2) {
                    uint32_t c = b[i] | (b[i + 1] << 8);
                    if (c == '\n' || c == '\r') {
                        size_t eq = line.find('=');
                        if (eq != std::string::npos && line.size() > 3 && line[0] == 'p') {
                            std::string v = line.substr(eq + 1);
                            while (!v.empty() && v[0] == '[') { size_t q = v.find(']'); if (q == std::string::npos) break; v.erase(0, q + 1); }
                            while (!v.empty() && v.back() == ' ') v.pop_back();
                            dlg[ini::lower(line.substr(0, eq))] = v;
                        }
                        line.clear(); continue;
                    }
                    if (c < 0x80) line += (char)c; else if (c < 0x800) { line += (char)(0xC0 | (c >> 6)); line += (char)(0x80 | (c & 0x3F)); }
                    else { line += (char)(0xE0 | (c >> 12)); line += (char)(0x80 | ((c >> 6) & 0x3F)); line += (char)(0x80 | (c & 0x3F)); }
                }
            }
        }
        hs.title = "Места"; hs.heading = "Скрытые места"; hs.group = "Скрытый контент";
        tabs.push_back(hs);
        // the lines, by speaker: those of the installed voices (Russian text from the game's files),
        // then those recorded only in English (written down from the recording, with a translation)
        Tab cl; cl.title = "Вырезанные реплики"; cl.heading = "Вырезанные реплики"; cl.group = "Скрытый контент";
        {
            Row r; r.kind = PARA;
            r.text = "Записанные реплики, которые игра нигде не проигрывает: их нет ни в одной сцене, ни в наборах фраз, ни в картах, ни в скриптах. Те, что подходят к месту, это издание вернуло в игру (154 реплики героев и жителей, плюс недостающие в обеих озвучках); здесь — остальные: подсказки об управлении, комментарии к местам, которых нет в игре, вступления к бонусным уровням. Помеченные «английская запись» есть только в английском издании: текст записан с озвучки, слушать их можно с установленной английской озвучкой.";
            cl.rows.push_back(r);
        }
        {
            std::vector<std::string> order; std::map<std::string, std::vector<Row>> by;
            auto add = [&](const std::string& who0, Row r) {
                std::string who = who0.empty() ? "Прочие" : who0;
                if (!by.count(who)) order.push_back(who);
                by[who].push_back(r);
            };
            for (const char* id : CUT_LINES) {
                Row r; r.id = std::string("snd:") + id; r.kind = LINE;
                auto it = dlg.find(ini::lower(id)); r.text = it != dlg.end() ? it->second : id;
                r.hint = "Щёлкните, чтобы послушать (звучит установленная озвучка).";
                add(speakerName(id), r);
            }
            // the English-only ones go after the speaker's own, or in the speaker order of the list
            std::vector<std::string> enOrder;
            for (const EnLine& e : EN_LINES) {
                Row r; r.id = std::string("snd:en:") + e.id; r.kind = LINE;
                r.label = "английская запись";
                r.text = uiEn ? e.en : e.ru;
                r.enabled = lm.haveEnglishVoice(); r.disabledNote = "нужна английская озвучка";
                r.hint = uiEn ? std::string("Recorded only for the English edition. Click to listen.")
                              : std::string("Только в английском издании. Оригинал: «") + e.en + "». Щёлкните, чтобы послушать.";
                if (!lm.haveEnglishVoice()) r.hint += uiEn ? " Needs the English voices (the Language tab)." : " Нужна английская озвучка (вкладка «Язык»).";
                std::string who = speakerName(e.id); if (who.empty()) who = "Прочие";
                if (!by.count(who)) { order.push_back(who); }
                by[who].push_back(r);
            }
            for (auto& who : order) {
                Row h; h.kind = HEADER; h.label = who; cl.rows.push_back(h);
                for (auto& r : by[who]) cl.rows.push_back(r);
            }
        }
        tabs.push_back(cl);

        // ---------------- Модули
        Tab md; md.title = "Модули"; md.heading = "Модули";
        md.rows.push_back(moduleMain());
        md.rows.push_back(moduleVideos());
        md.rows.push_back(moduleTextures());
        md.rows.push_back(moduleAudio());
        tabs.push_back(md);

        // ---------------- Титры
        Tab cr; cr.title = "Титры"; cr.heading = "Титры";
        buildCredits(cr);
        tabs.push_back(cr);
        if (uiEn) translateRows();
    }
    void translateRows() {
        for (auto& t : tabs) {
            t.title = tr(t.title); t.heading = tr(t.heading); if (!t.group.empty()) t.group = tr(t.group);
            for (auto& r : t.rows) {
                r.label = tr(r.label); r.hint = tr(r.hint); if (r.kind != LINE) r.text = tr(r.text); r.disabledNote = tr(r.disabledNote); r.sub = tr(r.sub);
                for (auto& o : r.options) o = tr(o);
                for (auto& a : r.actions) a.label = tr(a.label);
                for (auto& k : r.keys) { k.first = tr(k.first); k.second = tr(k.second); }
            }
        }
    }

    // ------------------------------------------------------------ input
    Path padIni() { return sys / "ShrekPad.ini"; }
    std::string readPad(const std::string& key, const std::string& def) {
        ini::File f; if (!f.load(padIni())) return def; return f.get("ShrekPad", key, def);
    }
    bool writePad(const std::vector<std::pair<std::string, std::string>>& kv) {
        ini::File f; if (!f.load(padIni())) return false;
        for (auto& p : kv) f.set("ShrekPad", p.first, p.second);
        return f.save();
    }
    // [Engine.Input] of the player's User.ini, in file order (key, command)
    std::vector<std::pair<std::string, std::string>> readBinds() {
        std::vector<std::pair<std::string, std::string>> out;
        ini::File f; if (!f.load(userIniForRead())) return out;
        size_t a, b; if (!f.sectionRange("Engine.Input", a, b)) return out;
        std::string k, v;
        for (size_t i = a; i < b; i++) if (ini::File::keyOf(f.lines[i], k, v) && k.rfind("Aliases", 0) != 0) out.push_back({ k, ini::trim(v) });
        return out;
    }
    static std::string bindOf(const std::vector<std::pair<std::string, std::string>>& b, const std::string& key) {
        for (auto& kv : b) if (ini::lower(kv.first) == ini::lower(key)) return kv.second;
        return "";
    }
    // the keys of the two presets (everything else in [Engine.Input] is left alone)
    static std::vector<std::pair<std::string, std::string>> preset(bool modern) {
        return { { "W", "MoveForward" }, { "S", "MoveBackward" }, { "A", "StrafeLeft" }, { "D", "StrafeRight" },
                 { "Up", "Axis aArrowUp SpeedBase=0.7" }, { "Down", "Axis aArrowUp SpeedBase=0.7 Invert=-1" },
                 { "Left", "Axis aArrowRight SpeedBase=0.9 Invert=-1" }, { "Right", "Axis aArrowRight SpeedBase=0.9 Invert=1" },
                 { "LeftMouse", "Fire" }, { "RightMouse", modern ? "SelectPotion 102" : "Jump" }, { "Ctrl", modern ? "" : "Jump" },     // modern: Space jumps, Ctrl stays free (the light keys are F1, F3-F6)
                 // modern: the wheel picks a potion, the right button uses it, Q opens the potion menu
                 { "MouseWheelUp", modern ? "SelectPotion 101" : "NextSkill | CameraSpeedUp" },
                 { "MouseWheelDown", modern ? "SelectPotion 100" : "PrevSkill | CameraSpeedDown" },
                 { "Q", modern ? "StopMoviePlayback" : "WeaponZoom" },
                 { "Space", modern ? "Jump | BypassCutscene" : "BypassCutscene | StopMoviePlayback" },
                 { "Enter", "Use | Fire" }, { "F", "Use" }, { "U", "UseCarriedInventoryItem" }, { "End", "CenterView" }, { "Home", "CenterView" } };
    }
    // which preset the managed bindings match (-1: own)
    int presetOf(const std::vector<std::pair<std::string, std::string>>& b) {
        for (int m = 0; m < 2; m++) {
            bool same = true;
            for (auto& kv : preset(m == 1)) {
                std::string cur = bindOf(b, kv.first);
                std::vector<std::string> want = splitCmd(kv.second), have = splitCmd(cur);
                // compare only the parts the launcher manages (End also carries editor commands)
                std::vector<std::string> h2;
                for (auto& p : have) for (auto& a : BIND_ACTS) if (partIsAct(p, a)) { h2.push_back(normCmd(p)); break; }
                std::vector<std::string> w2; for (auto& p : want) for (auto& a : BIND_ACTS) if (partIsAct(p, a)) { w2.push_back(normCmd(p)); break; }
                if (h2 != w2) { same = false; break; }
            }
            if (same) {     // and no other key carries a managed action
                for (auto& kv : b) {
                    bool inPreset = false; for (auto& p : preset(m == 1)) if (ini::lower(p.first) == ini::lower(kv.first)) inPreset = true;
                    if (inPreset || ini::lower(kv.first).rfind("joy", 0) == 0) continue;
                    for (auto& p : splitCmd(kv.second)) for (auto& a : BIND_ACTS) if (partIsAct(p, a)) same = false;
                }
            }
            if (same) return m;
        }
        return -1;
    }
    bool applyPreset(bool modern) {
        auto b = readBinds();
        std::vector<std::pair<std::string, std::string>> upd;
        auto ps = preset(modern);
        for (auto& kv : b) {        // strip managed actions from all other keyboard/mouse keys
            std::string lk = ini::lower(kv.first);
            if (lk.rfind("joy", 0) == 0 || lk == "mousex" || lk == "mousey") continue;
            bool inPreset = false; for (auto& p : ps) if (ini::lower(p.first) == lk) inPreset = true;
            if (inPreset) continue;
            std::vector<std::string> keep; bool changed = false;
            for (auto& p : splitCmd(kv.second)) { bool m = false; for (auto& a : BIND_ACTS) if (partIsAct(p, a)) m = true; if (m) changed = true; else keep.push_back(p); }
            if (changed) { std::string v; for (auto& k : keep) v += (v.empty() ? "" : " | ") + k; upd.push_back({ kv.first, v }); }
        }
        for (auto& p : ps) upd.push_back(p);
        return writeUser([&](ini::File& f) { for (auto& u : upd) f.set("Engine.Input", u.first, u.second); });
    }
    // bind key `key` (engine name) into slot `slot` of action row r; empty key clears the slot
    bool setBind(const Row& r, int slot, const std::string& key) {
        if (r.act < 0 || r.act >= (int)BIND_ACTS.size()) return false;
        const BindAct& A = BIND_ACTS[r.act];
        if (ini::lower(key) == "escape") { setStatus("!Esc занята меню игры."); return false; }
        auto b = readBinds();
        std::vector<std::pair<std::string, std::string>> upd;
        std::string old = slot < (int)r.bound.size() ? r.bound[slot] : "";
        auto strip = [&](const std::string& k) {
            std::string v; for (auto& p : splitCmd(bindOf(b, k))) if (!partIsAct(p, A)) v += (v.empty() ? "" : " | ") + p;
            return v;
        };
        if (!old.empty() && ini::lower(old) != ini::lower(key)) upd.push_back({ old, strip(old) });
        std::string note;
        if (!key.empty()) {
            std::string cur = bindOf(b, key);
            for (auto& p : splitCmd(cur)) for (auto& a : BIND_ACTS) if (&a != &A && partIsAct(p, a)) note += (note.empty() ? "" : ", ") + std::string("«") + a.label + "»";
            upd.push_back({ key, A.cmd });
        }
        bool ok = writeUser([&](ini::File& f) { for (auto& u : upd) f.set("Engine.Input", u.first, u.second); });
        if (ok) setStatus(note.empty() ? "Сохранено" : keyLabel(key) + " снята с " + note);
        return ok;
    }

    // ------------------------------------------------------------ language
    int padStyle = PAD_XBOX;
    lang::Manager lm;
    bool texHold = false;     // a texture-module script was started from here: don't touch Textures until the next start
    // ------------------------------------------------------------ bug report
    std::string makeReport() {
        bugreport::Zip z;
        std::string info = "Shrek 2 Enhanced Edition bug report\r\n";
        info += "patch: " + versionOf(sys / "ShrekRemaster" / "version.txt") + "\r\n";
        for (auto& t : tabs) for (auto& r : t.rows) if (r.kind == MODULE && r.id != "bugreport") info += r.label + ": " + r.text + "\r\n";
        { time_t t = time(nullptr); char b[64]; strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", localtime(&t)); info += std::string("made: ") + b + "\r\n"; }
        z.add("report.txt", std::vector<uint8_t>(info.begin(), info.end()));
        auto addFile = [&](const Path& p, const std::string& name) {
            std::vector<uint8_t> d; if (osio::size(p) > (64 << 20) || !osio::readFile(p, d)) return;
            z.add(name, d);
        };
        std::error_code ec;
        for (auto& e : osio::fs::directory_iterator(sys, ec)) {
            std::string ext = ini::lower(osio::u8str(e.path().extension())), fn = osio::u8str(e.path().filename());
            if (ext == ".log" || ext == ".ini" || ini::lower(fn) == "dxvk.conf" || ini::lower(fn) == "shrekrender_frame.txt") addFile(e.path(), "System/" + fn);
        }
        for (const char* f : { "version.txt", "hints_keys.txt", "launcher.ini", "lang/state.ini" }) addFile(sys / "ShrekRemaster" / f, std::string("System/ShrekRemaster/") + f);
        addFile(docs / "User.ini", "Documents/User.ini");
        // the marks: the newest 12 screenshots and the list
        std::vector<Path> shots;
        for (auto& e : osio::fs::directory_iterator(sys / "BugReports", ec)) if (e.path().extension() == ".png") shots.push_back(e.path());
        std::sort(shots.begin(), shots.end());
        if (shots.size() > 12) shots.erase(shots.begin(), shots.end() - 12);
        for (auto& p : shots) addFile(p, "BugReports/" + osio::u8str(p.filename()));
        addFile(sys / "BugReports" / "marks.txt", "BugReports/marks.txt");
        time_t t = time(nullptr); char b[64]; strftime(b, sizeof b, "Shrek2EE_report_%Y%m%d_%H%M%S.zip", localtime(&t));
        Path out = os->desktopDir() / b;
        auto data = z.finish();
        if (!osio::writeFile(out, data)) { out = sys / "BugReports" / b; osio::mkdirs(out.parent_path()); if (!osio::writeFile(out, data)) return uiEn ? "!Could not write the report" : "!Не удалось записать отчёт"; }
        os->showFile(out);
        return std::string(uiEn ? "Report saved: " : "Отчёт сохранён: ") + osio::u8str(out.filename()) + " (" + std::to_string(z.count) + (uiEn ? " files)" : " файлов)");
    }

    // ------------------------------------------------------------ control hints (shrz.dll subtitles)
    // The game's hints name the 2004 keys ("click the left mouse button"). shrz.dll swaps in the
    // player's own buttons (ShrekRemaster\hints.txt has the reworded lines); here, before each start
    // of the game, the buttons themselves: ShrekRemaster\hints_keys.txt, UTF-8, "K|P.<lang>.<name>=text"
    // (K keyboard, P gamepad), K.Default=1 while the keyboard has the game's classic layout.
    void writeHints() {
        auto b = readBinds();
        auto keysFor = [&](int act, bool joy) {
            std::vector<std::string> out;
            for (auto& kv : b) {
                bool isJoy = ini::lower(kv.first).rfind("joy", 0) == 0;
                if (isJoy != joy || ini::lower(kv.first) == "mousex" || ini::lower(kv.first) == "mousey") continue;
                for (auto& part : splitCmd(kv.second)) if (partIsAct(part, BIND_ACTS[act])) { out.push_back(kv.first); break; }
            }
            return out;
        };
        auto actIdx = [](const char* cmd) { for (size_t i = 0; i < BIND_ACTS.size(); i++) if (!strcmp(BIND_ACTS[i].cmd, cmd)) return (int)i; return -1; };
        int aFire = actIdx("Fire"), aJump = actIdx("Jump"), aPot = actIdx("StopMoviePlayback"), aUseP = actIdx("SelectPotion 102"), aFwd = 0, aBack = 1, aLeft = 2, aRight = 3;
        auto first = [&](int act, bool joy) { auto v = keysFor(act, joy); return v.empty() ? std::string() : v[0]; };
        // a key as the object of "press": accusative in Russian
        auto kRu = [&](const std::string& k) -> std::string {
            std::string l = ini::lower(k);
            if (l == "leftmouse") return "левую кнопку мыши"; if (l == "rightmouse") return "правую кнопку мыши";
            if (l == "middlemouse") return "колёсико мыши"; if (l == "space") return "пробел";
            if (l == "up") return "стрелку вперёд"; if (l == "down") return "стрелку вниз"; if (l == "left") return "стрелку влево"; if (l == "right") return "стрелку вправо";
            return "клавишу " + keyLabel(k);
        };
        auto kEn = [&](const std::string& k) -> std::string {
            std::string l = ini::lower(k);
            if (l == "leftmouse") return "your left mouse button"; if (l == "rightmouse") return "your right mouse button";
            if (l == "middlemouse") return "your mouse wheel"; if (l == "space") return "your space bar";
            if (l == "up") return "the up arrow"; if (l == "down") return "the down arrow"; if (l == "left") return "the left arrow"; if (l == "right") return "the right arrow";
            if (l == "escape") return "Escape";
            return "the " + keyLabel(k) + " key";
        };
        auto kShort = [&](const std::string& k) -> std::string {
            std::string l = ini::lower(k);
            if (l == "leftmouse") return "ЛКМ"; if (l == "rightmouse") return "ПКМ";
            return keyLabel(k);
        };
        auto padFace = [&](const std::string& k) -> const PadFace* { for (auto& p : PAD_BTNS) if (ini::lower(p.key) == ini::lower(k)) return &p.face[padStyle]; return nullptr; };
        auto pRu = [&](const std::string& k) -> std::string {
            const PadFace* f = padFace(k); if (!f) return k;
            std::string n = f->name;
            if (n == "Крест" || n == "Круг" || n == "Квадрат" || n == "Треугольник") return ini::lower(n) == n ? n : std::string(n == "Крест" ? "крест" : n == "Круг" ? "круг" : n == "Квадрат" ? "квадрат" : "треугольник");
            if (n.rfind("Крестовина", 0) == 0) return "крестовину " + n.substr(strlen("Крестовина "));
            if (n.rfind("Нажатие", 0) == 0) return n == "Нажатие левого стика" ? "левый стик (нажатием)" : "правый стик (нажатием)";
            return "кнопку " + std::string(f->badge);
        };
        auto pEn = [&](const std::string& k) -> std::string {
            const PadFace* f = padFace(k); if (!f) return k;
            std::string n = f->name;
            if (n == "Крест") return "Cross"; if (n == "Круг") return "Circle"; if (n == "Квадрат") return "Square"; if (n == "Треугольник") return "Triangle";
            if (n.rfind("Крестовина", 0) == 0) return "the D-pad";
            if (n.rfind("Нажатие", 0) == 0) return n == "Нажатие левого стика" ? "the left stick (click)" : "the right stick (click)";
            return "the " + std::string(f->badge) + " button";
        };
        auto pShort = [&](const std::string& k) -> std::string {
            const PadFace* f = padFace(k); if (!f) return k;
            std::string n = f->name;
            if (n == "Крест" || n == "Круг" || n == "Квадрат" || n == "Треугольник") return n;
            return f->badge;
        };
        std::string out = "# written by the launcher before each start of the game (see hints.txt)\n";
        auto put = [&](const std::string& k, const std::string& v) { out += k + "=" + v + "\n"; };
        put("K.Default", presetOf(b) == 0 ? "1" : "0");
        put("PadStyle", padStyle == PAD_PS ? "ps" : padStyle == PAD_NINTENDO ? "nintendo" : "xbox");
        struct A { const char* name; int act; };
        for (A a : { A{ "Fire", aFire }, A{ "Jump", aJump }, A{ "Potions", aPot }, A{ "UsePotion", aUseP } }) {
            if (a.act < 0) continue;
            std::string k = first(a.act, false), j = first(a.act, true);
            if (!k.empty()) { put(std::string("K.ru.") + a.name, kRu(k)); put(std::string("K.en.") + a.name, kEn(k)); }
            if (!j.empty()) { put(std::string("P.ru.") + a.name, pRu(j)); put(std::string("P.en.") + a.name, pEn(j)); }
            if (!strcmp(a.name, "Fire")) {
                if (!k.empty()) { put("K.ru.FireName", kShort(k)); put("K.en.FireName", kShort(k)); }
                if (!j.empty()) { put("P.ru.FireName", pShort(j)); put("P.en.FireName", pShort(j)); }
            }
        }
        put("K.ru.Menu", "Esc"); put("K.en.Menu", "Escape");
        put("P.ru.Menu", std::string("кнопку ") + PAD_START[padStyle]); put("P.en.Menu", std::string("the ") + PAD_START[padStyle] + " button");
        // movement
        std::string f = first(aFwd, false), bk = first(aBack, false), l = first(aLeft, false), r = first(aRight, false);
        bool arrows = ini::lower(f) == "up";
        if (!f.empty()) { put("K.ru.PushFwd", "нажми " + kRu(f)); put("K.ru.HoldFwd", "зажми " + kRu(f)); put("K.en.PushFwd", "press " + kEn(f)); put("K.en.HoldFwd", "hold " + kEn(f)); }
        if (!bk.empty()) { put("K.ru.PushDown", "нажми " + kRu(bk)); put("K.en.PushDown", "press " + kEn(bk)); }
        if (arrows) { put("K.ru.MoveLR", "стрелками влево и вправо"); put("K.en.MoveLR", "your left and right arrows"); put("K.ru.MoveKeys", "клавиши со стрелками"); put("K.en.MoveKeys", "the arrow keys"); }
        else if (!l.empty() && !r.empty()) {
            put("K.ru.MoveLR", "клавишами " + keyLabel(l) + " и " + keyLabel(r)); put("K.en.MoveLR", "the " + keyLabel(l) + " and " + keyLabel(r) + " keys");
            if (!f.empty() && !bk.empty()) { put("K.ru.MoveKeys", "клавиши " + keyLabel(f) + ", " + keyLabel(l) + ", " + keyLabel(bk) + " и " + keyLabel(r)); put("K.en.MoveKeys", "the " + keyLabel(f) + ", " + keyLabel(l) + ", " + keyLabel(bk) + " and " + keyLabel(r) + " keys"); }
        }
        put("P.ru.PushFwd", "наклони левый стик вперёд"); put("P.ru.HoldFwd", "держи левый стик наклонённым вперёд"); put("P.ru.PushDown", "наклони левый стик вниз");
        put("P.ru.MoveLR", "левым стиком"); put("P.ru.MoveKeys", "левый стик");
        put("P.en.PushFwd", "push the left stick forward"); put("P.en.HoldFwd", "hold the left stick forward"); put("P.en.PushDown", "push the left stick down");
        put("P.en.MoveLR", "the left stick"); put("P.en.MoveKeys", "the left stick");
        osio::writeFile(sys / "ShrekRemaster" / "hints_keys.txt", std::vector<uint8_t>(out.begin(), out.end()));
    }
    bool ensureLang(bool textures = true) {
        writeHints();
        if (!lm.configured() || os->gameRunning()) return true;
        if (!textures || texHold) return true;
        std::string err;
        if (!lm.ensure(err)) { setStatus("!Язык: " + err); return false; }
        return true;
    }

    // ------------------------------------------------------------ modules
    static std::string versionOf(const Path& f) {
        std::vector<uint8_t> d; if (!osio::readFile(f, d)) return "";
        std::string s(d.begin(), d.end()); return ini::trim(s);
    }
    Row moduleMain() {
        Row r; r.id = "mod_main"; r.kind = MODULE; r.label = "Основной патч";
        Path dir = sys / "ShrekRemaster";
        bool have = osio::exists(dir / "install.ps1");
        bool inst = msaaPatch && osio::exists(sys / "shrz.dll");
        std::string ver = versionOf(dir / "version.txt");
        r.text = !have ? "не найден" : inst ? ("установлен" + (ver.empty() ? std::string() : ", версия " + ver)) : "не установлен";
        r.hint = "Движок и скрипты: окно без рамки, широкий экран, HD-шрифты, MSAA, 4 ГБ памяти, исправления.";
        if (have) {
            Path i = sys / "Install-EnhancedEdition.bat", u = sys / "Uninstall-EnhancedEdition.bat";
            if (!osio::exists(i)) { i = sys / "Install-ShrekRemaster.bat"; u = sys / "Uninstall-ShrekRemaster.bat"; }   // before 0.9.31
            r.actions.push_back({ inst ? "Переустановить" : "Установить", [this, i]() { return script(i); } });
            if (inst) r.actions.push_back({ "Удалить", [this, u]() { return script(u); } });
        }
        return r;
    }
    Row moduleVideos() {
        Row r; r.id = "mod_vid"; r.kind = MODULE; r.label = "HD-видео";
        Path dir = sys / "ShrekRemasterVideos";
        bool have = osio::exists(dir / "install.ps1");
        bool inst = false;
        if (have) {
            std::error_code ec;
            for (auto& e : osio::fs::directory_iterator(dir / "files", ec)) {
                Path m = root / "Movies" / e.path().filename();
                if (osio::size(m) == osio::size(e.path()) && osio::size(m) > 0) { inst = true; break; }
            }
        }
        bool bundled = !have && osio::exists(sys / "ShrekRemaster" / "packs.lst");   // put in by the all-in-one Setup
        r.text = bundled ? "установлен (в составе установщика)" : !have ? "не скачан" : inst ? "установлен" : "не установлен";
        r.hint = "Заставки и ролики в 1440p (на 4K-мониторе показываются в 2160p).";
        if (have) {
            Path i = sys / "Install-RemasterVideos.bat", u = sys / "Uninstall-RemasterVideos.bat";
            r.actions.push_back({ inst ? "Переустановить" : "Установить", [this, i]() { return script(i); } });
            if (inst) r.actions.push_back({ "Удалить", [this, u]() { return script(u); } });
        }
        return r;
    }
    Row moduleTextures() {
        Row r; r.id = "mod_tex"; r.kind = MODULE; r.label = "HD-текстуры";
        Path dir = sys / "ShrekRemasterTextures";
        bool have = osio::exists(dir / "make.ps1");
        bool inst = false;
        if (have) {
            std::error_code ec;
            for (auto& e : osio::fs::directory_iterator(dir / "files", ec)) {
                Path t = root / "Textures" / e.path().filename();
                if (osio::size(t) == osio::size(e.path()) && osio::size(t) > 0) { inst = true; break; }
            }
        }
        bool bundled = !have && osio::exists(sys / "ShrekRemaster" / "packs.lst");
        r.text = bundled ? "установлены (в составе установщика)" : !have ? "не скачан" : inst ? "установлены" : "не установлены";
        r.hint = "Текстуры мира и интерфейса (HUD, меню, книга, постеры), увеличенные нейросетью. «Создать» — апскейл, «Упаковать» — из готовых картинок (chaiNNer).";
        if (have) {
            Path mk = sys / "Make-HDTextures.bat", pk = sys / "Pack-HDTextures.bat", u = sys / "Uninstall-HDTextures.bat";
            r.actions.push_back({ "Создать", [this, mk]() { return texScript(mk); } });
            if (osio::exists(pk)) r.actions.push_back({ "Упаковать", [this, pk]() { return texScript(pk); } });
            if (inst) r.actions.push_back({ "Удалить", [this, u]() { return texScript(u); } });
        }
        return r;
    }
    Row moduleAudio() {
        Row r; r.id = "mod_aud"; r.kind = MODULE; r.label = "HD-звук";
        Path dir = sys / "ShrekRemasterAudio";
        bool have = osio::exists(dir / "make.ps1");
        bool inst = false;
        if (have) {
            // built packages: files\sfx (Sounds\*.uax, System\AmbientCreatures.u), files\voice (the voice
            // packs in ShrekRemaster\lang), files\*.uax of the first version of the kit; installed when
            // the game's copy is the same size
            auto same = [](const Path& a, const Path& b) { return osio::size(a) > 0 && osio::size(a) == osio::size(b); };
            std::error_code ec;
            for (auto& e : osio::fs::directory_iterator(dir / "files", ec)) {
                Path f = e.path(), n = f.filename();
                if (same(f, root / "Sounds" / n)) { inst = true; break; }
            }
            if (!inst) for (auto& e : osio::fs::directory_iterator(dir / "files" / "sfx", ec)) {
                Path f = e.path(), n = f.filename();
                if (same(f, root / "Sounds" / n) || same(f, sys / n)) { inst = true; break; }
            }
            if (!inst) for (auto& e : osio::fs::directory_iterator(dir / "files" / "voice", ec)) {
                Path f = e.path(), n = f.filename();
                if (same(f, sys / "ShrekRemaster" / "lang" / n)) { inst = true; break; }
            }
        }
        bool bundled = !have && osio::exists(sys / "ShrekRemaster" / "packs.lst");
        r.text = bundled ? "установлен (в составе установщика)" : !have ? "не скачан" : inst ? "установлен" : "не установлен";
        r.hint = "Весь звук игры нейросетями на видеокарте: эффекты и озвучка (AudioSR, до 48 кГц), музыка (Apollo, стерео). «Создать» — всё само (первый раз скачивает ~6 ГБ, озвучка — несколько часов), «Упаковать» — из своих готовых WAV.";
        if (have) {
            Path mk = sys / "Make-HDAudio.bat", pk = sys / "Pack-HDAudio.bat", u = sys / "Uninstall-HDAudio.bat";
            if (osio::exists(mk)) r.actions.push_back({ "Создать", [this, mk]() { return script(mk); } });
            r.actions.push_back({ "Упаковать", [this, pk]() { return script(pk); } });
            if (inst) r.actions.push_back({ "Удалить", [this, u]() { return script(u); } });
        }
        return r;
    }
    // the texture kit reads the packages: give it the original (Russian) ones and leave them alone
    // until the launcher starts again
    std::string texScript(const Path& bat) {
        if (os->gameRunning()) return "!Закройте игру перед установкой.";
        if (lm.configured()) {
            ini::File st; lm.load(st); std::string err;
            if (!lm.applyTextures(false, st, err)) return "!Не удалось вернуть оригинальные текстуры: " + err;
            st.dirty = true; st.save();
        }
        texHold = true;
        return script(bat);
    }
    // a voice line of "Скрытый контент" -> the speakers (Sounds\AllDialog.uax: the voice pack in use)
    std::string playLine(const std::string& id) {
        std::vector<uint8_t> w; std::string err;
        bool en = id.compare(0, 3, "en:") == 0;
        Path pkg = en ? sys / "ShrekRemaster" / "lang" / "voice_en.uax" : root / "Sounds" / "AllDialog.uax";
        if (!dlgplay::lineWav(pkg, en ? id.substr(3) : id, w, err)) return "!" + err;
        return os->playWav(w) ? "" : "!Не удалось проиграть звук";
    }
    std::string script(const Path& bat) {
        if (os->gameRunning()) return "!Закройте игру перед установкой.";
        if (!osio::exists(bat)) return "!Не найден " + osio::u8str(bat.filename());
        return os->runScript(bat) ? "Запущено в отдельном окне. Когда оно закончит, статус обновится." : "!Не удалось запустить " + osio::u8str(bat.filename());
    }

    // ------------------------------------------------------------ graphics preset
    // rows the preset sets, with their 2005 values; the player's values are kept in launcher.ini
    inline static const std::pair<const char*, int> PRESET_ORIGINAL[] = {
        { "msaa", 0 }, { "ssaa", 0 }, { "foliageaa", 0 }, { "pplight", 0 }, { "relief", 0 }, { "relief_world", 0 }, { "lmfilter", 0 }, { "finedetail", 0 }, { "aerial", 0 },
        { "ssao", 0 }, { "charshadows", 0 }, { "worldsh", 0 }, { "water", 0 }, { "rays", 0 }, { "bloom", 0 }, { "sharpen", 0 },
        // the game's own settings as the 2005 Default.ini had them: no anisotropic filtering, normal
        // texture detail (the textures shrunk on load), full shadows, decals, coronas, detail cut below 35 fps
        { "aniso", 0 }, { "texdetail", 0 }, { "shadows", 2 }, { "decals", 1 }, { "coronas", 1 }, { "autodetail", 1 }, { "camfade", 1 } };
    inline static const std::pair<const char*, int> PRESET_DEFAULT[] = {    // first switch to "custom" without saved values
        { "msaa", 2 }, { "ssaa", 0 }, { "foliageaa", 0 }, { "pplight", 1 }, { "relief", 2 }, { "relief_world", 1 }, { "lmfilter", 1 }, { "finedetail", 1 }, { "aerial", 2 },
        { "ssao", 2 }, { "charshadows", 2 }, { "worldsh", 2 }, { "water", 2 }, { "rays", 1 }, { "bloom", 0 }, { "sharpen", 2 },
        { "aniso", 4 }, { "texdetail", 2 }, { "shadows", 2 }, { "decals", 1 }, { "coronas", 1 }, { "autodetail", 0 }, { "camfade", 1 }};
    // "for weak computers": the look of the edition at a low cost - no multisampling beyond 2x, no ambient
    // occlusion (and so no water reflections), no rays or bloom, the level's shadows at the low setting
    inline static const std::pair<const char*, int> PRESET_WEAK[] = {
        { "msaa", 1 }, { "ssaa", 0 }, { "foliageaa", 0 }, { "pplight", 1 }, { "relief", 1 }, { "relief_world", 0 }, { "lmfilter", 1 }, { "finedetail", 0 }, { "aerial", 1 },
        { "ssao", 0 }, { "charshadows", 1 }, { "worldsh", 1 }, { "water", 0 }, { "rays", 0 }, { "bloom", 0 }, { "sharpen", 1 },
        { "aniso", 3 }, { "texdetail", 1 }, { "shadows", 2 }, { "decals", 1 }, { "coronas", 1 }, { "autodetail", 0 }, { "camfade", 1 } };
    template <size_t N> bool presetIs(const std::pair<const char*, int> (&t)[N]) {
        for (auto& o : t) if (Row* x = find(o.first)) if (x->enabled && x->value != o.second) return false;
        return true;
    }
    int presetNow() { return presetIs(PRESET_ORIGINAL) ? 0 : presetIs(PRESET_WEAK) ? 1 : 2; }
    bool applyGfxPreset(int mode, int from) {
        if (mode != 2 && os->gameRunning()) { setStatus("!Закройте игру, чтобы сменить пресет."); return false; }
        ini::File f; if (!f.load(launcherIni())) { f.path = launcherIni(); f.loaded = true; f.crlf = true; }
        bool ok = true;
        if (from == 2 && mode != 2) {   // leaving the player's own settings: remember them first
            // "shadows" was kept in the old order (0 full .. 2 off) before 0.9.52: the new order under its own key
            for (auto& o : PRESET_DEFAULT) if (Row* x = find(o.first)) f.set("CustomGraphics", strcmp(o.first, "shadows") ? o.first : "shadows_v2", std::to_string(x->value));
            f.dirty = true; osio::mkdirs(launcherIni().parent_path()); f.save();
        }
        if (mode == 0) { for (auto& o : PRESET_ORIGINAL) if (Row* x = find(o.first)) if (x->enabled && x->value != o.second) { x->value = o.second; ok = change(*x) && ok; } }
        else if (mode == 1) { for (auto& o : PRESET_WEAK) if (Row* x = find(o.first)) if (x->enabled && x->value != o.second) { x->value = o.second; ok = change(*x) && ok; } }
        else {
            for (auto& o : PRESET_DEFAULT) if (Row* x = find(o.first)) {
                bool sh = !strcmp(o.first, "shadows");
                std::string v = f.get("CustomGraphics", sh ? "shadows_v2" : o.first, "");
                if (sh && v.empty()) { std::string old = f.get("CustomGraphics", "shadows", ""); if (!old.empty()) v = std::to_string(2 - std::clamp(atoi(old.c_str()), 0, 2)); }
                int nv = v.empty() ? o.second : atoi(v.c_str());
                if (x->enabled && x->value != nv) { x->value = nv; ok = change(*x) && ok; }
            }
        }
        return ok;
    }

    // ------------------------------------------------------------ apply
    void setStatus(const std::string& s0) {
        std::string s = tr(s0);
        if (!s.empty() && s[0] == '!') { status = s.substr(1); statusError = true; } else { status = s; statusError = false; }
    }

    // returns false (and sets status) on failure; the caller reloads the rows afterwards
    bool change(Row& r) {
        bool ok = true;
        if (r.id == "gpreset") { int from = presetNow(); return applyGfxPreset(r.value, from); }
        auto fmt = [](const char* f, double v) { char b[64]; snprintf(b, sizeof b, f, v); return std::string(b); };
        if (r.id == "fps") {
            int v = fpsValues[r.value];
            ok = dxvkSet("d3d9.maxFrameRate", v ? std::to_string(v) : "");
        } else if (r.id == "vsync") {
            ok = dxvkSet("d3d9.presentInterval", r.value == 0 ? "" : r.value == 1 ? "1" : "0");
        } else if (r.id == "fov_preset") {
            if (r.value < 3) {
                int v = r.value == 0 ? 85 : r.value == 1 ? 95 : 105;
                std::string sv = fmt("%.6f", (double)v);
                ok = writeUser([&](ini::File& f) { f.set("Engine.PlayerController", "DefaultFOV", sv); f.set("Engine.PlayerController", "DesiredFOV", sv); });
                if (Row* sl = find("fov")) sl->value = v;
            }
        } else if (r.id == "fov") {
            if (Row* pr = find("fov_preset")) pr->value = r.value == 85 ? 0 : r.value == 95 ? 1 : r.value == 105 ? 2 : 3;
            std::string v = fmt("%.6f", (double)r.value);
            ok = writeUser([&](ini::File& f) { f.set("Engine.PlayerController", "DefaultFOV", v); f.set("Engine.PlayerController", "DesiredFOV", v); });
        } else if (r.id == "bright") {
            std::string v = fmt("%.6f", r.value / 100.0);
            ok = writeMain([&](ini::File& f) { f.set("WinDrv.WindowsClient", "Brightness", v); });
        } else if (r.id == "intro") {
            ok = writeMain([&](ini::File& f) { f.set("URL", "LocalMap", r.value ? "SH2_Preamble.unr" : "Book_FrontEnd.unr"); });
        } else if (r.id == "msaa") {
            if (os->gameRunning()) { setStatus("!Закройте игру, чтобы сменить сглаживание."); return false; }
            std::vector<uint8_t> d; long o = msaaOffset(d);
            int lv[] = { 0, 2, 4, 8 }; int v = lv[r.value];
            if (o < 0) ok = false; else { memcpy(&d[o], &v, 4); ok = osio::writeFile(sys / "D3DDrv.dll", d); }
        } else if (r.id == "ssaa") {
            ok = dxvkSet("d3d9.forceSampleRateShading", r.value ? "True" : "");
        } else if (r.id == "pplight") {
            ok = writeKeys(renderIni(), { { "Lighting", "PerPixel", r.value ? "1" : "0" } });
        } else if (r.id == "relief") {
            ok = writeKeys(renderIni(), { { "Lighting", "Relief", std::to_string(r.value) } });
        } else if (r.id == "relief_world") {
            ok = writeKeys(renderIni(), { { "Lighting", "ReliefWorld", r.value ? "1" : "0" } });
        } else if (r.id == "finedetail") {
            const char* v[] = { "0", "0.2", "0.35" };
            ok = writeKeys(renderIni(), { { "Image", "FineDetail", v[std::clamp(r.value, 0, 2)] } });
        } else if (r.id == "foliageaa") {
            ok = writeKeys(renderIni(), { { "Image", "FoliageAA", r.value ? "1" : "0" } });
        } else if (r.id == "aerial") {
            const char* v[] = { "0", "0.2", "0.35" };
            ok = writeKeys(renderIni(), { { "Image", "AerialPerspective", v[std::clamp(r.value, 0, 2)] } });
        } else if (r.id == "lmfilter") {
            ok = writeKeys(renderIni(), { { "Lighting", "LightmapFilter", r.value ? "1" : "0" } });
        } else if (r.id == "ssao") {
            const char* v[] = { "0", "0.6", "1", "1.5" };
            ok = writeKeys(renderIni(), { { "Lighting", "AmbientOcclusion", v[std::clamp(r.value, 0, 3)] } });
        } else if (r.id == "charshadows") {
            const char* v[] = { "0", "0.4", "0.6", "0.85" };
            ok = writeKeys(renderIni(), { { "Lighting", "CharacterShadows", v[std::clamp(r.value, 0, 3)] } });
        } else if (r.id == "worldsh") {
            const char* v[] = { "0", "0.5", "1" };
            ok = writeKeys(renderIni(), { { "Lighting", "WorldShadows", v[std::clamp(r.value, 0, 2)] } });
        } else if (r.id == "water") {
            const char* v[] = { "0", "0.35", "0.6" };
            ok = writeKeys(renderIni(), { { "Image", "WaterReflections", v[std::clamp(r.value, 0, 2)] } });
        } else if (r.id == "rays") {
            const char* v[] = { "0", "0.5", "0.85" };
            ok = writeKeys(renderIni(), { { "Image", "SunRays", v[std::clamp(r.value, 0, 2)] } });
        } else if (r.id == "bloom") {
            ok = writeKeys(renderIni(), { { "Image", "Bloom", r.value ? "0.3" : "0" } });
        } else if (r.id == "sharpen") {
            const char* v[] = { "0", "0.25", "0.45", "0.7" };
            ok = writeKeys(renderIni(), { { "Image", "Sharpen", v[std::clamp(r.value, 0, 3)] } });
        } else if (r.id == "snd_engine") {
            ok = writeAudio({ { "UseDefaultDriver", r.value == 0 ? "False" : "True" } });
            if (r.value == 0 && !osio::exists(alsoftIni())) ok = writeAlsoft(0, true) && ok;
        } else if (r.id == "snd_out") {
            Row* e = find("snd_eax");
            ok = writeAlsoft(r.value, e ? e->value != 0 : true);
        } else if (r.id == "snd_eax") {
            ok = writeAudio({ { "UseEAX", r.value ? "True" : "False" } });
            ok = writeKeys(alsoftIni(), { { "eax", "enable", r.value ? "true" : "false" } }) && ok;
        } else if (r.id == "snd_3d") {
            ok = writeAudio({ { "Use3DSound", r.value ? "True" : "False" } });
        } else if (r.id == "snd_ch") {
            const char* v[] = { "32", "64", "128" };
            ok = writeAudio({ { "Channels", v[std::clamp(r.value, 0, 2)] } });
        } else if (r.id == "snd_music") {
            ok = writeAudio({ { "MusicVolume", fmt("%.2f", r.value / 100.0) } });
        } else if (r.id == "snd_fx") {
            ok = writeAudio({ { "SoundVolume", fmt("%.2f", r.value / 100.0) } });
        } else if (r.id == "pad_rumble") { ok = writePad({ { "Rumble", std::to_string(r.value) } });
        } else if (r.id == "pad_rumsteps") { ok = writePad({ { "RumbleSteps", r.value ? "1" : "0" } });
        } else if (r.id == "pad_lbmode") { ok = writePad({ { "LightbarMode", std::to_string(std::clamp(r.value, 0, 3)) } });
        } else if (r.id == "pad_adapt") { ok = writePad({ { "AdaptiveTriggers", r.value ? "1" : "0" } });
        } else if (r.id == "pad_r2") { ok = writePad({ { "TriggerR2", std::to_string(r.value) } });
        } else if (r.id == "pad_r2pt") { ok = writePad({ { "TriggerR2Point", std::to_string(r.value) } });
        } else if (r.id == "pad_l2") { ok = writePad({ { "TriggerL2", std::to_string(r.value) } });
        } else if (r.id == "pad_small") { ok = writePad({ { "TriggerSmallHeroes", std::to_string(r.value) } });
        } else if (r.id == "pad_dsx") { ok = writePad({ { "DSX", r.value ? "1" : "0" } });
        } else if (r.id == "aniso") {
            int lv[] = { 1, 2, 4, 8, 16 }; int v = lv[r.value];
            ok = writeMain([&](ini::File& f) { f.set("D3DDrv.D3DRenderDevice", "LevelOfAnisotropy", std::to_string(v)); });
            if (dxvk || osio::exists(sys / "dxvk.conf")) ok = dxvkSet("d3d9.samplerAnisotropy", v > 1 ? std::to_string(v) : "") && ok;
        } else if (r.id == "texdetail") {
            const char* v = r.value == 2 ? "UltraHigh" : r.value == 1 ? "High" : "Normal";
            ok = writeMain([&](ini::File& f) { for (auto k : TEXDETAIL_KEYS) f.set("WinDrv.WindowsClient", k, v); });
        } else if (r.id == "shadows") {
            ok = writeMain([&](ini::File& f) {
                f.set("WinDrv.WindowsClient", "NoShadows", r.value == 0 ? "True" : "False");
                f.set("WinDrv.WindowsClient", "SimpleShadows", r.value == 1 ? "True" : "False");
            });
        } else if (r.id == "decals") {
            ok = writeMain([&](ini::File& f) { f.set("WinDrv.WindowsClient", "Decals", r.value ? "True" : "False"); });
        } else if (r.id == "coronas") {
            ok = writeMain([&](ini::File& f) { f.set("WinDrv.WindowsClient", "Coronas", r.value ? "True" : "False"); });
        } else if (r.id == "autodetail") {
            ok = writeMain([&](ini::File& f) { f.set("WinDrv.WindowsClient", "MinDesiredFrameRate", r.value ? "35.000000" : "0.000000"); });
        } else if (r.id == "layout") {
            if (r.value > 1) return true;
            ok = applyPreset(r.value == 1);
        } else if (r.id == "cheats") {
            ok = writeKeys(eeIni(), { { "Cheats", "Enabled", r.value ? "1" : "0" } });
        } else if (r.id == "ch_health") {
            ok = writeKeys(eeIni(), { { "Cheats", "EndlessHealth", r.value ? "1" : "0" } });
        } else if (r.id == "ch_coins") {
            const char* v[] = { "0", "50", "100", "500", "1000" };
            ok = writeKeys(eeIni(), { { "Cheats", "CoinsPerLevel", v[std::clamp(r.value, 0, 4)] } });
        } else if (r.id == "ch_potions") {
            const char* v[] = { "0", "1", "3", "5", "10" };
            ok = writeKeys(eeIni(), { { "Cheats", "PotionsPerLevel", v[std::clamp(r.value, 0, 4)] } });
        } else if (r.id == "sub_on") {
            ok = writeKeys(eeIni(), { { "Subtitles", "Enabled", r.value ? "1" : "0" } });
        } else if (r.id == "sub_which") {
            ok = writeKeys(eeIni(), { { "Subtitles", "StoryOnly", r.value ? "1" : "0" } });
        } else if (r.id == "sub_mode") {
            ok = writeKeys(eeIni(), { { "Subtitles", "Mode", std::to_string(std::clamp(r.value, 0, 2)) } });
        } else if (r.id == "sub_names") {
            ok = writeKeys(eeIni(), { { "Subtitles", "Names", r.value ? "1" : "0" } });
        } else if (r.id == "sub_font") {
            ok = writeKeys(eeIni(), { { "Subtitles", "Font", SUB_FONT[std::clamp(r.value, 0, 4)] } });
        } else if (r.id == "sub_text") {
            ok = writeKeys(eeIni(), { { "Subtitles", "TextColor", SUB_TEXT[std::clamp(r.value, 0, 4)] } });
        } else if (r.id == "sub_name") {
            ok = writeKeys(eeIni(), { { "Subtitles", "NameColor", SUB_NAME[std::clamp(r.value, 0, 5)] } });
        } else if (r.id == "sub_size") {
            ok = writeKeys(eeIni(), { { "Subtitles", "Size", std::to_string(std::clamp(r.value, 70, 150)) } });
        } else if (r.id == "speedrun") {
            ok = writeKeys(eeIni(), { { "Game", "SpeedrunTimer", r.value ? "1" : "0" } });
        } else if (r.id == "devmenu") {
            ok = writeKeys(eeIni(), { { "Game", "DevMenu", r.value ? "1" : "0" } });
        } else if (r.id == "ch_speed") {
            const char* v[] = { "50", "75", "100", "125", "150", "200" };
            ok = writeKeys(eeIni(), { { "Cheats", "GameSpeed", v[std::clamp(r.value, 0, 5)] } });
        } else if (r.id == "freelook") {
            ok = writeKeys(launcherIni(), { { "Levels", "FreeLook", r.value ? "1" : "0" } });
        } else if (r.id == "camsnap") {
            ok = writeUser([&](ini::File& f) { f.set("ShGame.SHHeroPawn", "SaveCameraNoSnapRotation", r.value ? "False" : "True"); });
        } else if (r.id == "invert") {
            ok = writeUser([&](ini::File& f) { f.set("Engine.PlayerInput", "bInvertMouse", r.value ? "True" : "False"); });
        } else if (r.id == "camfade") {
            ok = writeUser([&](ini::File& f) { f.set("ShGame.ShrekController", "bDoOpacityForCamera", r.value ? "True" : "False"); });
        } else if (r.id == "smooth") {
            ok = writeUser([&](ini::File& f) { f.set("Engine.PlayerInput", "MouseSmoothingMode", r.value ? "1" : "0"); });
        } else if (r.id == "rawmouse") {
            ok = writePad({ { "RawMouse", r.value ? "1" : "0" } });
        } else if (r.id == "gamecursor") {
            ok = writePad({ { "GameCursor", r.value ? "1" : "0" } });
        } else if (r.id == "mousesens") {
            ok = writePad({ { "MouseSensitivity", fmt("%.1f", r.value / 10.0) } });
        } else if (r.id == "pad_style") {
            const char* v[] = { "xbox", "ps", "nintendo" };
            ok = writePad({ { "ButtonStyle", v[std::clamp(r.value, 0, 2)] } });
        } else if (r.id == "pad_on") { ok = writePad({ { "Enabled", r.value ? "1" : "0" } });
        } else if (r.id == "pad_inv") { ok = writePad({ { "InvertY", r.value ? "1" : "0" } });
        } else if (r.id == "pad_swap") { ok = writePad({ { "SwapAB", r.value ? "1" : "0" } });
        } else if (r.id == "pad_sony") { ok = writePad({ { "Sony", r.value ? "1" : "0" } });
        } else if (r.id == "pad_cam") { ok = writePad({ { "CameraSensitivity", fmt("%.1f", r.value / 10.0) } });
        } else if (r.id == "pad_curve") { ok = writePad({ { "CameraCurve", fmt("%.1f", r.value / 10.0) } });
        } else if (r.id == "pad_cursor") { ok = writePad({ { "CursorSpeed", fmt("%.1f", r.value / 10.0) } });
        } else if (r.id == "pad_dzl") { ok = writePad({ { "DeadzoneLeft", fmt("%.2f", r.value / 100.0) } });
        } else if (r.id == "pad_dzr") { ok = writePad({ { "DeadzoneRight", fmt("%.2f", r.value / 100.0) } });
        } else if (r.id == "pad_trig") { ok = writePad({ { "TriggerThreshold", fmt("%.2f", r.value / 100.0) } });
        } else if (r.id == "pad_light") {
            if (r.value >= (int)(sizeof(LIGHTS) / sizeof(LIGHTS[0]))) return true;
            const LightColor& l = LIGHTS[r.value];
            if (l.r < 0) ok = writePad({ { "Lightbar", "0" } });
            else ok = writePad({ { "Lightbar", "1" }, { "LightbarR", std::to_string(l.r) }, { "LightbarG", std::to_string(l.g) }, { "LightbarB", std::to_string(l.b) } });
        } else if (r.id.rfind("joy:", 0) == 0) {
            if (r.value >= PAD_ACT_N) return true;
            std::string key = r.id.substr(4), cmd = PAD_ACTS[r.value].cmd;
            ok = writeUser([&](ini::File& f) { f.set("Engine.Input", key, cmd); });
        } else if (r.id == "text" || r.id == "voice") {
            if (os->gameRunning()) { setStatus("!Закройте игру, чтобы сменить язык."); return false; }
            Row* t = find("text"); Row* v = find("voice");
            int tv = t ? t->value : 0, vv = v ? v->value : 0;
            std::string err;
            if (!lm.apply(tv, vv, err)) { setStatus("!Язык: " + err); return false; }
        }
        if (!ok) { if (!statusError) setStatus("!Не удалось записать настройки. Возможно, игра запущена или нет прав на папку."); return false; }
        setStatus("Сохранено");
        return true;
    }

};

}  // namespace ls
