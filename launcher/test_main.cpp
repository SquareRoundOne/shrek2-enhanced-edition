// Linux test harness: renders the launcher into PNG screenshots against a mock game folder.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "ui.h"
#include "fonts.h"
#include <cstdio>

struct MockOS : ls::Platform {
    osio::Path r, d;
    int scripts = 0;
    osio::Path gameRoot() override { return r; }
    osio::Path docsDir() override { return d; }
    void display(int& w, int& h, int& hz) override { w = 3840; h = 2160; hz = 144; }
    bool gameRunning() override { return false; }
    bool launchGame(const std::string& m) override { printf("launch %s\n", m.c_str()); return true; }
    bool runScript(const osio::Path& b) override { printf("runScript %s\n", b.string().c_str()); scripts++; return true; }
};

static void shot(app::App& a, float scale, const std::string& fn) {
    gfx::Canvas c; c.scale = scale; c.resize((int)(app::W * scale), (int)(app::H * scale));
    a.draw(c);
    std::vector<uint8_t> rgb((size_t)c.w * c.h * 3);
    for (size_t i = 0; i < (size_t)c.w * c.h; i++) { uint32_t p = c.px[i]; rgb[i * 3] = (p >> 16) & 255; rgb[i * 3 + 1] = (p >> 8) & 255; rgb[i * 3 + 2] = p & 255; }
    stbi_write_png(fn.c_str(), c.w, c.h, 3, rgb.data(), c.w * 3);
}
static void click(app::App& a, std::function<bool(const app::Hit&)> pick) {
    gfx::Canvas c; c.scale = 1; c.resize(1280, 800); a.draw(c);
    for (auto& h : a.hits) if (pick(h)) { float x = h.r.x + h.r.w / 2, y = h.r.y + h.r.h / 2; a.mouseMove(x, y); a.mouseDown(x, y); a.mouseUp(x, y); return; }
    printf("click: nothing found\n");
}

int main(int argc, char** argv) {
    MockOS os; os.r = argv[1]; os.d = argv[2];
    std::string out = argc > 3 ? argv[3] : ".";
    app::App a; a.init(&os, FONT_UI, FONT_UI_N, FONT_TITLE, FONT_TITLE_N);
    printf("art from game: %d tabs %zu\n", a.art.fromGame, a.model.tabs.size());
    float s = argc > 4 ? atof(argv[4]) : 1.0f;
    for (int t = 0; t < (int)a.model.tabs.size(); t++) {
        a.tab = t; a.mx = 900; a.my = 260;
        shot(a, s, out + "/tab" + std::to_string(t) + ".png");
        if (a.scrollMax > 0) { a.scroll = 1e9; shot(a, s, out + "/tab" + std::to_string(t) + "_end.png"); a.scroll = 0; }
    }
    // keyboard: rebind "Прыжок" slot 2 to Q, then switch the preset
    a.tab = 3; a.scroll = 0;
    a.mx = -1; a.my = -1;
    click(a, [&](const app::Hit& h) { return h.r.x > 1000 && h.r.y > 330 && h.r.y < 600 && h.rclick != nullptr; });
    printf("capturing: %d (%s)\n", a.capturing(), a.capId.c_str());
    shot(a, s, out + "/cap.png");
    a.captureKey("Q");
    printf("status: %s\n", a.model.status.c_str());
    shot(a, s, out + "/cap_done.png");
    // language: English text + English voice through the model, as the arrows would
    a.tab = 5;
    if (auto* r = a.model.find("text")) { r->value = 2; a.changed(*r); }
    printf("lang status: %s\n", a.model.status.c_str());
    if (auto* r = a.model.find("voice")) { r->value = 1; a.changed(*r); }
    printf("voice status: %s\n", a.model.status.c_str());
    a.tab = 5; shot(a, s, out + "/lang_en.png");
    if (auto* r = a.model.find("text")) { r->value = 1; a.changed(*r); }
    printf("text new: %s\n", a.model.status.c_str());
    a.tab = 5; shot(a, s, out + "/lang_new.png");
    // English launcher
    a.model.setUiLang(true);
    for (int t = 0; t < (int)a.model.tabs.size(); t++) { a.tab = t; a.mx = 900; a.my = 300; shot(a, s, out + "/en" + std::to_string(t) + ".png"); }
    a.model.setUiLang(false);
    // level click
    a.tab = 6; a.scroll = 0;
    click(a, [&](const app::Hit& h) { return h.r.x < 800 && h.r.x > 640 && h.r.y > 300; });
    printf("want map: %s\n", a.wantMap.c_str());
    a.tab = 6; a.mx = 900; a.my = 320; shot(a, s, out + "/levels.png");
    printf("playLine: [%s]\n", a.model.playLine("pc_dnk_CherryTree_2").c_str());
    printf("playLine en: [%s]\n", a.model.playLine("en:pc_dnk_bumpline_1219").c_str());
    { auto* g = a.model.find("gpreset"); if (g) { printf("preset now %d\n", a.model.presetNow()); g->value = 1; a.model.change(*g); printf("preset weak -> %d, worldsh=%d ssao=%d\n", a.model.presetNow(), a.model.find("worldsh")->value, a.model.find("ssao")->value); g->value = 2; a.model.change(*g); printf("preset back -> %d ssao=%d\n", a.model.presetNow(), a.model.find("ssao")->value); } }
    a.tab = 9; a.scrollTab = 9; a.scroll = 2350; shot(a, s, out + "/cut_mid.png"); a.scroll = 0;
    printf("report: [%s]\n", a.model.makeReport().c_str());
    a.tab = 3; a.groupStep(1); a.mx = 0; a.my = 0; shot(a, s, out + "/group_next.png"); a.groupStep(1); shot(a, s, out + "/group_wrap.png");
    // gamepad style PlayStation
    if (auto* r = a.model.find("pad_style")) { r->value = 1; a.changed(*r); }
    a.tab = 4; a.scroll = 1e9; shot(a, s, out + "/pad_ps.png");
    if (auto* r = a.model.find("pad_style")) { r->value = 2; a.changed(*r); }
    a.tab = 4; a.scroll = 1e9; shot(a, s, out + "/pad_nin.png");
    if (auto* r = a.model.find("pad_style")) { r->value = 0; a.changed(*r); }
    // sound: new engine, headphones, effects, 128 voices; graphics: supersampling, relief, sharpening
    for (auto id : { "snd_engine" }) if (auto* r = a.model.find(id)) { r->value = 0; a.changed(*r); printf("%s: %s\n", id, a.model.status.c_str()); }
    if (auto* r = a.model.find("snd_out")) { r->value = 1; a.changed(*r); printf("snd_out: %s\n", a.model.status.c_str()); }
    if (auto* r = a.model.find("snd_eax")) { r->value = 1; a.changed(*r); }
    if (auto* r = a.model.find("snd_ch")) { r->value = 2; a.changed(*r); }
    if (auto* r = a.model.find("ssaa")) { printf("ssaa enabled %d\n", r->enabled); r->value = 1; a.changed(*r); }
    if (auto* r = a.model.find("relief")) { r->value = 1; a.changed(*r); }
    if (auto* r = a.model.find("sharpen")) { r->value = 2; a.changed(*r); }
    if (auto* r = a.model.find("pad_rumble")) { r->value = 55; a.changed(*r); }
    a.tab = 2; a.scroll = 0; shot(a, s, out + "/sound_after.png");
    a.tab = 1; a.scroll = 0; shot(a, s, out + "/gfx_after.png");
    return 0;
}
