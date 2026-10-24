// Launcher UI: an open storybook like the game's own menu. Left page: logo, sections, «Играть».
// Right page: settings of the chosen section. All art is decoded from the game's packages.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include <cmath>
#include <thread>
#include <map>
#include "gfx.h"
#include "pkg.h"
#include "settings.h"

namespace app {

using namespace gfx;

static const float W = 1280, H = 800;           // logical window size

struct Art {
    ImageP wood, leather, plain, menuPage, heartsPage, titlePage, titleEn, shield, shieldHi, frame, frameHi, box, boxHi, check, arrow, cursor;
    std::map<std::string, ImageP> icons;           // level pictures (and their coloured _hili)
    bool fromGame = false;

    static ImageP load(upkg::Pkg& p, const char* name) {
        std::vector<uint8_t> rgba; int w, h, aw = 0, ah = 0;
        if (!upkg::decodeTexture(p, name, rgba, w, h, &aw, &ah)) return nullptr;
        // HD texture (texture kit): the layout below uses the authored size, so it is brought back
        // to it by averaging (alpha-weighted)
        if (aw > 0 && ah > 0 && (w > aw || h > ah) && w % aw == 0 && h % ah == 0) {
            int fx = w / aw, fy = h / ah; std::vector<uint8_t> o((size_t)aw * ah * 4);
            for (int y = 0; y < ah; y++) for (int x = 0; x < aw; x++) {
                float c[3] = { 0, 0, 0 }, a = 0;
                for (int j = 0; j < fy; j++) for (int i = 0; i < fx; i++) {
                    const uint8_t* q = &rgba[((size_t)(y * fy + j) * w + (x * fx + i)) * 4]; float qa = q[3] / 255.f;
                    for (int k = 0; k < 3; k++) c[k] += q[k] * qa;
                    a += qa;
                }
                uint8_t* d = &o[((size_t)y * aw + x) * 4];
                for (int k = 0; k < 3; k++) d[k] = (uint8_t)std::lround(a > 0 ? c[k] / a : 0);
                d[3] = (uint8_t)std::lround(a / (fx * fy) * 255.f);
            }
            rgba.swap(o); w = aw; h = ah;
        }
        return Image::fromRGBA(rgba.data(), w, h);
    }
    // the top mip of a texture from the English overlay (lang\tex_en.bin)
    static ImageP loadPatch(const lang::TexEnt& e) {
        int fmt = 0;
        for (auto& p : e.props) if (p.name == "Format" && !p.raw.empty()) fmt = p.raw[0];
        try {
            lang::Rd r(e.blob, 0); if (r.ci() <= 0) return nullptr;
            r.i32(); int cnt = r.ci(); size_t at = r.p; r.p += cnt; int w = r.i32(), h = r.i32();
            if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || at + cnt > e.blob.size()) return nullptr;
            std::vector<uint8_t> rgba;
            if (!upkg::decodePixels(fmt, &e.blob[at], (size_t)cnt, w, h, rgba)) return nullptr;
            return Image::fromRGBA(rgba.data(), w, h);
        } catch (...) { return nullptr; }
    }
    void loadFrom(const osio::Path& root) {
        osio::Path tex = root / "Textures", L = root / "System" / "ShrekRemaster" / "lang";
        upkg::Pkg a, b;
        if (osio::readFile(tex / "storybookanimTX.utx", a.d)) {
            // with English signs on, the package carries the English pages: look at the original part
            ini::File st; std::string v; lang::Overlay::Rec rec;
            if (st.load(L / "state.ini") && !(v = st.get("Overlay", "Textures/storybookanimTX.utx", "")).empty() &&
                lang::Overlay::parseRec(v, rec) && rec.size == a.d.size()) { a.d.resize((size_t)rec.base); memcpy(a.d.data(), rec.hdr.data(), 64); }
            if (a.parse()) {
                ImageP mb = load(a, "menu_button"), mbh = load(a, "menu_button_hili");
                if (mb) shield = mb->crop(24, 0, 68, 64);
                if (mbh) shieldHi = mbh->crop(24, 0, 68, 64);
                frame = load(a, "yesno"); frameHi = load(a, "yesno_hili");
                box = load(a, "options_button"); boxHi = load(a, "options_button_hili");
                check = load(a, "check_tx"); arrow = load(a, "back_button"); cursor = load(a, "Cursor");
                plain = load(a, "blankpage2"); menuPage = load(a, "blank_page"); heartsPage = load(a, "design_page"); titlePage = load(a, "Title_Page");
                for (auto& l : ls::LEVELS) for (std::string n : { std::string(l.icon), std::string(l.icon) + "_hili" })
                    if (!icons.count(n)) if (ImageP im = load(a, n.c_str())) icons[n] = im;
            }
        }
        lang::TexPatch tp;
        if (tp.load(L / "tex_en.bin"))
            for (auto& pk : tp.pkgs) if (lang::lower(pk.first) == "textures/storybookanimtx.utx")
                for (auto& e : pk.second) if (e.full == "Title_Page") titleEn = loadPatch(e);
        if (!titleEn) titleEn = titlePage;
        if (b.load(tex / "Storybook_TX.utx")) { wood = load(b, "Interior_1"); leather = load(b, "book_cover"); }
        fromGame = plain && menuPage && shield && frame;
    }
};

struct Hit { Rect r; std::function<void()> click; std::function<void(float)> drag; int cursor = 0; std::function<void()> rclick; };

struct App {
    ls::Model model;
    Art art;
    Font ui, title;
    int tab = 0;
    float mx = -1, my = -1;                  // mouse, logical
    int pressed = -1, dragging = -1;
    std::vector<Hit> hits;
    std::string hint;
    ImageP bgCache; float bgScale = 0; bool bgEn = false;
    std::function<void()> onQuit, onMinimize;
    bool wantPlay = false; std::string wantMap;
    double statusTime = 0, now = 0;
    float scroll = 0, scrollMax = 0; int scrollTab = -1;   // right page
    Rect listRect;
    std::string capId; int capSlot = -1;                     // key capture for a BIND row

    // colours of the game's menu text
    Color ink = Color::hex(0x3b2a12), inkSoft = Color::hex(0x5d4523), inkHi = Color::hex(0x14100a), inkDim = Color::hex(0x8a7650);
    Color red = Color::hex(0x8a1a10), blue = Color::hex(0x1d3a8a);

    void init(ls::Platform* os, const uint8_t* uiTtf, size_t uiN, const uint8_t* tTtf, size_t tN) {
        ui.load(uiTtf, uiN, 0.86f);
        title.load(tTtf, tN, 1.0f);
        model.init(os);
        art.loadFrom(model.root);
    }

    // ------------------------------------------------------------------ layout constants
    Rect book() const { return Rect(78, 44, 1124, 712); }
    Rect leftPage() const { return Rect(100, 62, 536, 676); }
    Rect rightPage() const { return Rect(644, 62, 536, 676); }
    Rect logoRect() const { Rect L = leftPage(); float lw = 250, lh = lw * 332.f / 424.f; return Rect(L.x + (L.w - lw) / 2, L.y + 118, lw, lh); }

    // ------------------------------------------------------------------ static background
    void drawBackground(Canvas& c) {
        // wooden table
        if (art.wood) c.tile(*art.wood, Rect(0, 0, W, H), 300, 300, 0.75f);
        else c.fill(Rect(0, 0, W, H), Color::hex(0x1b1510));
        // vignette
        for (int i = 0; i < 6; i++) c.shadow(Rect(-60.f + i * 10, -60.f + i * 10, W + 120 - i * 20, H + 120 - i * 20), 0, 0, Color(0, 0, 0, 0));
        Rect bk = book();
        c.shadow(Rect(bk.x + 6, bk.y + 14, bk.w, bk.h), 22, 34, Color(0, 0, 0, 0.75f));
        // leather cover
        if (art.leather) {
            Rect oldClip = c.setClip(bk);
            Rect src(30, 30, 360, 450);
            c.image(*art.leather, src, Rect(bk.x, bk.y, bk.w / 2, bk.h), 1, 0, 0.9f);
            c.image(*art.leather, src, Rect(bk.x + bk.w / 2, bk.y, bk.w / 2, bk.h), 1, 0, 0.9f, true);
            c.clip = oldClip;
            // rounded corners: paint the table back in the corners
        } else c.roundRect(bk, 18, Color::hex(0x5a2e16));
        c.roundRect(bk, 18, Color(0, 0, 0, 0));   // no-op keeps API symmetric
        // page stacks (a few sheets peeking out)
        Rect L = leftPage(), R = rightPage();
        for (int i = 3; i >= 1; i--) {
            float o = i * 3.f;
            Color pc = Color::hex(0xcdb27a).alpha(1);
            c.roundRect(Rect(L.x - o, L.y + o * 0.6f, L.w + o, L.h), 3, Color::hex(0x6b5634).alpha(0.5f));
            c.roundRect(Rect(L.x - o + 1, L.y + o * 0.6f, L.w + o - 1, L.h - 1), 3, pc);
            c.roundRect(Rect(R.x, R.y + o * 0.6f, R.w + o, R.h), 3, Color::hex(0x6b5634).alpha(0.5f));
            c.roundRect(Rect(R.x, R.y + o * 0.6f, R.w + o - 1, R.h - 1), 3, pc);
        }
        drawPage(c, L, true);
        drawPage(c, R, false);
        // spine shading
        float sx = (L.r() + R.x) / 2;
        for (int i = 0; i < 40; i++) {
            float t = i / 40.f, a = 0.38f * (1 - t) * (1 - t);
            c.fill(Rect(sx + i, R.y, 1, R.h), Color(0.18f, 0.1f, 0.02f, a));
            c.fill(Rect(sx - 1 - i, L.y, 1, L.h), Color(0.18f, 0.1f, 0.02f, a * 1.1f));
        }
        c.fill(Rect(L.r(), L.y, R.x - L.r(), L.h), Color::hex(0x2a1a0c));
    }
    void drawPage(Canvas& c, Rect p, bool left) {
        if (art.plain) c.image(*art.plain, Rect(4, 4, 504, 504), p, 1, 0, 1.0f);
        else c.fill(p, Color::hex(0xd9bf86));
        // decorative bands keep their proportions: crop from the 512x512 page textures
        float k = p.w / 512.f;
        auto band = [&](Image& im, Rect src, float y) {
            Rect d(p.x + src.x * k, y, src.w * k, src.h * k);
            c.shadow(Rect(d.x + 1.5f, d.y + 2.5f, d.w, d.h), 1, 4, Color(0.25f, 0.15f, 0.05f, 0.35f));
            c.image(im, src, d);
        };
        if (left && art.heartsPage) band(*art.heartsPage, Rect(38, 11, 442, 79), p.y + 14);
        else if (!left && art.menuPage) {
            band(*art.menuPage, Rect(29, 21, 438, 65), p.y + 18);
            band(*art.menuPage, Rect(29, 424, 438, 67), p.b() - 67 * k - 20);
        }
        ImageP tp = model.uiEn ? art.titleEn : art.titlePage;
        if (left && tp) {
            Rect lg = logoRect();
            c.shadow(Rect(lg.x + 2, lg.y + 3, lg.w, lg.h), 2, 6, Color(0.25f, 0.15f, 0.05f, 0.35f));
            c.image(*tp, Rect(42, 93, 424, 332), lg);
        }
        // page edge darkening
        c.shadow(Rect(p.x, p.y, p.w, p.h), 2, 0, Color(0, 0, 0, 0));
    }

    // ------------------------------------------------------------------ widgets
    bool hovered(Rect r) const { return r.contains(mx, my); }
    void addHit(Rect r, std::function<void()> f) { hits.push_back({ r, f, nullptr }); }

    void shieldItem(Canvas& c, Rect r, const std::string& label, bool selected, float fs = 22.f) {
        bool hov = hovered(r);
        ImageP sh = (selected || hov) && art.shieldHi ? art.shieldHi : art.shield;
        float sw = fs < 21 ? 34.f : 40.f, shh = fs < 21 ? 32.f : 38.f;
        if (sh) c.image(*sh, Rect(r.x, r.y + (r.h - shh) / 2, sw, shh));
        Color col = selected ? inkHi : hov ? inkHi : ink;
        float tx = r.x + sw + 12;
        c.text(ui, label, tx, r.y + (r.h - fs) / 2 - 1, fs, col);
        if (selected) {
            float tw = c.textWidth(ui, label, fs);
            c.fill(Rect(tx, r.y + r.h - 6, tw, 2), red.alpha(0.8f));
        }
    }
    // framed button (the game's yes/no frame)
    bool frameButton(Canvas& c, Rect r, const std::string& label, float size, std::function<void()> f, bool enabled = true, Font* font = nullptr) {
        bool hov = enabled && hovered(r);
        ImageP fr = hov && art.frameHi ? art.frameHi : art.frame;
        if (fr) c.nine(*fr, r, 40, 16, 40, 16, std::min(30.f, r.h * 0.6f), r.h * 0.25f, std::min(30.f, r.h * 0.6f), r.h * 0.25f, enabled ? 1 : 0.5f, enabled ? 0 : 0.7f);
        else c.roundRect(r, 6, hov ? Color::hex(0xe8d29c) : Color::hex(0xd8c08a));
        Font& fo = font ? *font : ui;
        float th = size;
        c.text(fo, label, r.x, r.y + (r.h - th * 1.12f) / 2, th, enabled ? (hov ? inkHi : ink) : inkDim, CENTER, r.w);
        if (enabled) addHit(r, f);
        return hov;
    }
    void arrowBtn(Canvas& c, Rect r, bool right, bool enabled, std::function<void()> f) {
        bool hov = enabled && hovered(Rect(r.x - 4, r.y - 6, r.w + 8, r.h + 12));
        Color col = !enabled ? inkDim.alpha(0.45f) : hov ? red : ink;
        float cy = r.y + r.h / 2, h2 = r.h * 0.42f, w = r.h * 0.62f, x0 = r.x + (r.w - w) / 2;
        if (right) c.polygon({ { x0, cy - h2 }, { x0 + w, cy }, { x0, cy + h2 } }, col);
        else c.polygon({ { x0 + w, cy - h2 }, { x0, cy }, { x0 + w, cy + h2 } }, col);
        if (enabled) addHit(Rect(r.x - 4, r.y - 6, r.w + 8, r.h + 12), f);
    }

    // ------------------------------------------------------------------ frame
    void draw(Canvas& c) {
        hits.clear(); hint.clear();
        if (!bgCache || bgScale != c.scale || bgCache->w != c.w || bgCache->h != c.h || bgEn != model.uiEn) {
            bgEn = model.uiEn;
            // the static book is the expensive part: render horizontal bands in parallel
            for (auto im : { art.wood, art.leather, art.plain, art.menuPage, art.heartsPage, art.titlePage, art.titleEn }) if (im) im->level(4);
            int n = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
            std::vector<std::thread> th; std::vector<Canvas> parts(n);
            for (int i = 0; i < n; i++) {
                int y0 = c.h * i / n, y1 = c.h * (i + 1) / n;
                th.emplace_back([this, &parts, &c, i, y0, y1]() {
                    Canvas& p = parts[i]; p.scale = c.scale; p.w = c.w; p.h = c.h; p.px.assign((size_t)c.w * c.h, 0xFF000000);
                    p.clip = Rect(0, (float)y0, (float)c.w, (float)(y1 - y0));
                    drawBackground(p);
                });
            }
            for (auto& t : th) t.join();
            for (int i = 0; i < n; i++) {
                int y0 = c.h * i / n, y1 = c.h * (i + 1) / n;
                std::copy(parts[i].px.begin() + (size_t)y0 * c.w, parts[i].px.begin() + (size_t)y1 * c.w, c.px.begin() + (size_t)y0 * c.w);
            }
            bgCache = std::make_shared<Image>(); bgCache->w = c.w; bgCache->h = c.h; bgCache->px = c.px; bgScale = c.scale;
        } else c.px = bgCache->px;

        drawLeft(c);
        drawRight(c);
        drawChrome(c);
    }

    void drawChrome(Canvas& c) {
        // window buttons on the table, top-right
        Rect cl(W - 46, 6, 36, 30), mn(W - 86, 6, 36, 30);
        auto btn = [&](Rect r, const char* s, std::function<void()> f) {
            bool hov = hovered(r);
            if (hov) c.roundRect(r, 6, Color(1, 0.9f, 0.7f, 0.18f));
            c.text(ui, s, r.x, r.y + 2, 22, hov ? Color::hex(0xf3e2b8) : Color::hex(0xc9b48a), CENTER, r.w);
            addHit(r, f);
        };
        btn(mn, "–", [this]() { if (onMinimize) onMinimize(); });
        {   // launcher language: RU · EN
            Rect lr(W - 172, 6, 80, 30);
            bool hov = hovered(lr);
            if (hov) c.roundRect(lr, 6, Color(1, 0.9f, 0.7f, 0.18f));
            Color on = Color::hex(0xf3e2b8), off = Color::hex(0x8d7a58);
            float x = lr.x + 12;
            x += c.text(ui, "RU", x, lr.y + 5, 17, model.uiEn ? off : on);
            x += c.text(ui, " · ", x, lr.y + 5, 17, off);
            c.text(ui, "EN", x, lr.y + 5, 17, model.uiEn ? on : off);
            addHit(lr, [this]() { int t = tab; model.setUiLang(!model.uiEn); tab = t; });
        }
        btn(cl, "×", [this]() { if (onQuit) onQuit(); });
        c.text(ui, model.uiEn ? "Shrek 2 Enhanced Edition" : "Шрек 2: Улучшенное Издание", 16, 10, 16, Color::hex(0xb9a37a));
    }

    void drawLeft(Canvas& c) {
        Rect L = leftPage();
        Rect lg = logoRect();
        c.text(title, model.uiEn ? "Enhanced Edition" : "Улучшенное Издание", L.x, lg.b() + 2, 26, red, CENTER, L.w);
        // the list: one item per tab, tabs of a group (Управление) as one item
        std::vector<int> items;
        for (int i = 0; i < (int)model.tabs.size(); i++) {
            const std::string& g = model.tabs[i].group;
            bool dup = false; if (!g.empty()) for (int j : items) if (model.tabs[j].group == g) dup = true;
            if (!dup) items.push_back(i);
        }
        auto itemTitle = [&](int i) -> const std::string& { return model.tabs[i].group.empty() ? model.tabs[i].title : model.tabs[i].group; };
        auto itemOn = [&](int i) { return i == tab || (!model.tabs[i].group.empty() && tab < (int)model.tabs.size() && model.tabs[tab].group == model.tabs[i].group); };
        int nt = (int)items.size();
        if (nt <= 8) {
            float step = nt > 7 ? 28.f : nt > 6 ? 31.f : 35.f, y = lg.b() + (nt > 7 ? 32 : nt > 6 ? 36 : 44), x = L.x + 150;
            for (int n = 0; n < nt; n++) {
                int i = items[n];
                Rect r(x, y + n * step, 260, step - 1);
                shieldItem(c, r, itemTitle(i), itemOn(i));
                int ti = i; addHit(r, [this, ti]() { if (!itemOnTab(ti)) tab = ti; capId.clear(); });
            }
        } else {   // two columns: the first half on the left, the rest on the right
            int rows = (nt + 1) / 2;
            float step = 34.f, y = lg.b() + 40, colW = (L.w - 60) / 2;
            for (int n = 0; n < nt; n++) {
                int i = items[n];
                int col = n / rows, row = n % rows;
                Rect r(L.x + 30 + col * colW, y + row * step, colW - 8, step - 1);
                shieldItem(c, r, itemTitle(i), itemOn(i), 19.f);
                int ti = i; addHit(r, [this, ti]() { if (!itemOnTab(ti)) tab = ti; capId.clear(); });
            }
        }
        Rect play(L.x + (L.w - 280) / 2, L.b() - 92, 280, 54);
        frameButton(c, play, model.tr("ИГРАТЬ"), 28, [this]() { wantPlay = true; wantMap.clear(); }, true, &title);
        if (!model.status.empty() && now - statusTime < 6) {
            float a = std::min(1.0, 6 - (now - statusTime));
            c.text(ui, model.status, L.x, L.b() - 32, 15, (model.statusError ? red : Color::hex(0x3d6a1a)).alpha(a), CENTER, L.w);
        }
    }

    // the current tab belongs to the group of tab i (clicking the group's item keeps the page shown)
    bool itemOnTab(int i) const {
        return tab < (int)model.tabs.size() && !model.tabs[i].group.empty() && model.tabs[tab].group == model.tabs[i].group;
    }
    // the other pages of the current tab's group: -1 previous, +1 next (wraps round)
    void groupStep(int d) {
        if (tab >= (int)model.tabs.size() || model.tabs[tab].group.empty()) return;
        std::vector<int> g; for (int i = 0; i < (int)model.tabs.size(); i++) if (model.tabs[i].group == model.tabs[tab].group) g.push_back(i);
        int k = (int)(std::find(g.begin(), g.end(), tab) - g.begin());
        tab = g[(k + d + (int)g.size()) % g.size()]; capId.clear();
    }
    static int u8len(const std::string& s) { int n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++; return n; }
    float rowHeight(const ls::Row& r) const {
        if (r.kind == ls::PARA) {   // about 52 characters to a line of the right page at 16 px
            float ph = r.paraH > 0 ? r.paraH : std::max(1, (u8len(r.text) + 51) / 52) * 21.f;
            return (r.label.empty() ? 6.f : 28.f) + ph + 12.f;
        }
        if (r.kind == ls::LINE) {
            float ph = r.paraH > 0 ? r.paraH : std::max(1, (u8len(r.text) + 45) / 46) * 21.f;
            return (r.label.empty() ? 4.f : 24.f) + ph + 14.f;
        }
        return r.kind == ls::KEYS ? 30.f * r.keys.size() + 8 : r.kind == ls::MODULE ? 86.f : r.kind == ls::HEADER ? 42.f : r.kind == ls::BIND ? 40.f : r.kind == ls::LEVEL ? 78.f : 44.f;
    }
    void drawRight(Canvas& c) {
        Rect R = rightPage();
        if (tab >= (int)model.tabs.size()) return;
        ls::Tab& t = model.tabs[tab];
        if (scrollTab != tab) { scroll = 0; scrollTab = tab; }
        float k = R.w / 512.f;
        c.text(title, t.heading, R.x, R.y + 6 + 80 * k + 10, 34, ink, CENTER, R.w);
        if (!t.group.empty()) {   // arrows to the other pages of the group (Клавиатура и мышь / Геймпад)
            float hy0 = R.y + 6 + 80 * k + 10;
            for (int d = -1; d <= 1; d += 2) {
                Rect a(d < 0 ? R.x + 40 : R.r() - 40 - 44, hy0 - 2, 44, 44);
                bool hv = a.contains(mx, my);
                c.text(ui, d < 0 ? "\xE2\x97\x80" : "\xE2\x96\xB6", a.x, a.y + 6, 28, hv ? red : ink, CENTER, a.w);
                addHit(a, [this, d]() { groupStep(d); });
            }
        }
        float hy = R.b() - 82 * k - 6 - 78;
        listRect = Rect(R.x + 24, R.y + 146, R.w - 48, hy - 8 - (R.y + 146));
        {   // paragraphs: their real height at this page width
            float lx0 = R.x + 46, cx0 = R.x + 290, cw0 = R.r() - 50 - cx0;
            for (auto& r : t.rows) if (r.kind == ls::PARA) r.paraH = c.paragraph(ui, r.text, lx0 - 6, 0, cx0 + cw0 - lx0 + 6, 17, inkHi, 1.22f, false);
            for (auto& r : t.rows) if (r.kind == ls::LINE) r.paraH = c.paragraph(title, r.text, lx0 + 30, 0, cx0 + cw0 - lx0 - 24, 18, inkHi, 1.2f, false);
        }
        float total = 0; for (auto& r : t.rows) total += rowHeight(r);
        scrollMax = std::max(0.f, total - listRect.h + 6);
        scroll = std::clamp(scroll, 0.f, scrollMax);
        float lx = R.x + 46, cx = R.x + 290, cw = R.r() - 50 - cx;
        Rect oldClip = c.setClip(listRect);
        float y = listRect.y + 4 - scroll;
        size_t hitsBefore = hits.size();
        for (size_t i = 0; i < t.rows.size(); i++) {
            ls::Row& r = t.rows[i];
            float rh = rowHeight(r);
            if (y + rh > listRect.y - 2 && y < listRect.b() + 2) {
                Rect rowR(R.x + 30, y - 4, R.w - 60, rh);
                if (hovered(rowR) && listRect.contains(mx, my) && !r.hint.empty()) hint = r.hint;
                drawRow(c, r, lx, cx, cw, y, rh);
            }
            y += rh;
        }
        c.clip = oldClip;
        // widgets scrolled out of the visible part must not take clicks
        for (size_t i = hitsBefore; i < hits.size(); i++) {
            Rect& h = hits[i].r;
            float y0 = std::max(h.y, listRect.y), y1 = std::min(h.b(), listRect.b());
            if (y1 <= y0) h = Rect(-100, -100, 0, 0); else { h.h = y1 - y0; h.y = y0; }
        }
        if (scrollMax > 0) {
            // soft fades at the cut edges and a thin scroll bar
            Color pg = Color::hex(0xd9bf86);
            for (int i = 0; i < 14; i++) {
                float a = (1 - i / 14.f) * 0.85f;
                if (scroll > 1) c.fill(Rect(listRect.x, listRect.y + i, listRect.w, 1), pg.alpha(a * 0.6f));
                if (scroll < scrollMax - 1) c.fill(Rect(listRect.x, listRect.b() - 1 - i, listRect.w, 1), pg.alpha(a * 0.6f));
            }
            float bx = R.r() - 34, th = std::max(36.f, listRect.h * listRect.h / (listRect.h + scrollMax));
            float ty = listRect.y + (listRect.h - th) * (scroll / scrollMax);
            c.roundRect(Rect(bx, listRect.y, 4, listRect.h), 2, inkSoft.alpha(0.18f));
            c.roundRect(Rect(bx - 1, ty, 6, th), 3, inkSoft.alpha(0.6f));
            Hit h; h.r = Rect(bx - 8, listRect.y, 20, listRect.h);
            float ly = listRect.y, lh = listRect.h;
            h.drag = [this, ly, lh, th](float) { float tt = std::clamp((my - ly - th / 2) / std::max(1.f, lh - th), 0.f, 1.f); scroll = tt * scrollMax; };
            h.click = []() {};
            hits.push_back(h);
        }
        // hint above the bottom band
        std::string hs = hint.empty() ? defaultHint() : hint;
        if (!capId.empty()) hs = model.uiEn ? "Press a key or mouse button for \"" + capLabel() + "\". Esc cancels." : "Нажмите клавишу или кнопку мыши для «" + capLabel() + "». Esc — отмена.";
        c.paragraph(ui, hs, R.x + 52, hy, R.w - 104, 16, inkSoft, 1.3f);
    }
    std::string capLabel() {
        if (tab < (int)model.tabs.size()) for (auto& r : model.tabs[tab].rows) if (r.id == capId) return r.label;
        return "";
    }
    std::string defaultHint() {
        if (tab < (int)model.tabs.size() && model.tabs[tab].title == model.tr("Модули")) return model.tr("Модули ставятся и удаляются отдельно. Каждый хранит оригинальные файлы и умеет вернуть их обратно.");
        if (tab < (int)model.tabs.size() && model.tabs[tab].title == model.tr("Уровни"))
            for (auto& r : model.tabs[tab].rows) if (r.kind == ls::LEVEL) return r.hint;
        if (scrollMax > 0) return model.tr("Наведите на настройку, чтобы прочитать подсказку. Список прокручивается колесом мыши. Изменения сохраняются сразу.");
        return model.tr("Наведите на настройку, чтобы прочитать подсказку. Изменения сохраняются сразу.");
    }
    void wheel(float steps) {
        if (!capId.empty()) return;
        if (listRect.contains(mx, my) || rightPage().contains(mx, my)) scroll = std::clamp(scroll - steps * 44.f * 1.5f, 0.f, scrollMax);
    }
    bool capturing() const { return !capId.empty(); }
    // a key or mouse button (engine name) for the row being captured
    void captureKey(const std::string& key) {
        std::string id = capId; int slot = capSlot; capId.clear();
        if (key.empty() || tab >= (int)model.tabs.size()) return;
        for (auto& r : model.tabs[tab].rows) if (r.id == id) {
            if (model.setBind(r, slot, key)) {} statusTime = now;
            int keepTab = tab; float sc = scroll; model.reload(); tab = keepTab; scroll = sc;
            return;
        }
    }
    void cancelCapture() { capId.clear(); }

    void changed(ls::Row& r) {
        bool ok = model.change(r);
        statusTime = now;
        (void)ok;
        std::string id = r.id; int keepTab = tab; float sc = scroll;
        model.reload();
        tab = keepTab; scroll = sc;
        if (!ok) { /* reload restored the real on-disk value */ }
        (void)id;
    }

    void drawRow(Canvas& c, ls::Row& r, float lx, float cx, float cw, float y, float rh) {
        Color lc = r.enabled ? ink : inkDim;
        if (r.kind == ls::HEADER) {
            c.text(title, r.label, lx - 6, y + 10, 24, ink);
            float tw = c.textWidth(title, r.label, 24);
            c.fill(Rect(lx + tw + 6, y + 26, cx + cw - lx - tw - 6, 1.5f), inkSoft.alpha(0.4f));
            return;
        }
        if (r.kind == ls::PARA) {
            float yy = y;
            if (!r.label.empty()) { c.text(ui, r.label, lx - 6, yy + 2, 17, inkSoft); yy += 26; }
            c.paragraph(ui, r.text, lx - 6, yy, cx + cw - lx + 6, 17, inkHi, 1.22f);
            return;
        }
        if (r.kind == ls::LINE) {   // a voice line: play mark, speaker, text; click to listen
            Rect row(lx - 16, y - 2, cx + cw - lx + 26, rh - 6);
            bool hov = r.enabled && hovered(row) && listRect.contains(mx, my);
            if (hov) c.roundRect(row, 8, Color(1, 0.96f, 0.82f, 0.45f));
            float ty = r.label.empty() ? y + 4 : y + 24;   // under a speaker's header the name is not repeated
            c.text(ui, "\xE2\x96\xB6", lx - 6, ty - 2, 18, hov ? red : r.enabled ? inkSoft : inkDim);
            if (!r.label.empty()) c.text(ui, r.label, lx + 30, y + 2, 15, hov ? red : inkSoft);
            c.paragraph(title, r.text, lx + 30, ty, cx + cw - lx - 24, 18, hov ? inkHi : r.enabled ? ink : inkDim, 1.2f);
            std::string id = r.id.substr(4);
            if (r.enabled) addHit(row, [this, id]() { std::string m = model.playLine(id); if (!m.empty()) { model.setStatus(m); statusTime = now; } });
            return;
        }
        if (r.kind == ls::LEVEL) {
            Rect row(lx - 16, y - 2, cx + cw - lx + 26, 74);
            bool hov = r.enabled && hovered(row) && listRect.contains(mx, my);
            if (hov) c.roundRect(row, 8, Color(1, 0.96f, 0.82f, 0.45f));
            Rect ic(lx - 8, y + 1, 68, 68);
            auto it = art.icons.find(r.icon + (hov ? "_hili" : ""));
            if (it == art.icons.end()) it = art.icons.find(r.icon);
            c.shadow(Rect(ic.x + 1.5f, ic.y + 2.5f, ic.w, ic.h), 1, 4, Color(0.25f, 0.15f, 0.05f, 0.35f));
            if (it != art.icons.end()) c.image(*it->second, ic); else c.roundRect(ic, 4, inkSoft.alpha(0.3f));
            c.text(ui, r.label, ic.r() + 16, y + 12, 15, r.enabled ? (hov ? red : inkSoft) : inkDim);
            c.text(title, r.sub, ic.r() + 16, y + 32, 22, r.enabled ? (hov ? inkHi : ink) : inkDim);
            if (r.enabled) { std::string map = r.id.substr(4); addHit(row, [this, map]() { wantPlay = true; wantMap = map; }); }
            return;
        }
        if (!r.badge.empty()) {
            // gamepad button as printed on the controller
            float bw = r.badgeRound ? 26.f : std::max(30.f, c.textWidth(ui, r.badge, 13) + 14);
            Rect bd(lx - 2, y + 5, bw, 26);
            auto col = [](uint32_t v) { return Color::hex(v); };
            c.shadow(Rect(bd.x + 1, bd.y + 1.5f, bd.w, bd.h), 1, 3, Color(0, 0, 0, 0.25f));
            c.roundRect(bd, r.badgeRound ? 13.f : 7.f, col(r.badgeBg));
            c.text(ui, r.badge, bd.x, bd.y + (r.badgeRound ? 3.5f : 5.5f), r.badgeRound ? 16.f : 13.f, col(r.badgeFg), CENTER, bd.w);
            c.text(ui, r.label, lx + bw + 10, y + 8, 18, lc);
        } else if (r.kind != ls::KEYS && r.kind != ls::MODULE) c.text(ui, r.label, lx, y + 8, 18, lc);
        if (r.kind == ls::CHOICE) {
            Rect a(cx, y + 6, 30, 24), b(cx + cw - 30, y + 6, 30, 24);
            bool en = r.enabled;
            arrowBtn(c, a, false, en && r.value > 0, [this, &r]() { r.value--; changed(r); });
            arrowBtn(c, b, true, en && r.value + 1 < (int)r.options.size(), [this, &r]() { r.value++; changed(r); });
            std::string v = r.enabled ? r.options[r.value] : r.disabledNote;
            c.text(ui, v, cx + 34, y + 8, 19, en ? inkHi : inkDim, CENTER, cw - 68);
            if (en) addHit(Rect(cx + 34, y, cw - 68, 36), [this, &r]() { r.value = (r.value + 1) % (int)r.options.size(); changed(r); });
        } else if (r.kind == ls::TOGGLE && !r.enabled) {
            c.text(ui, r.disabledNote, cx, y + 8, 17, inkDim, CENTER, cw);
        } else if (r.kind == ls::TOGGLE) {
            Rect b(cx + cw / 2 - 16, y + 2, 32, 32);
            bool hov = hovered(Rect(cx, y, cw, 36));
            if (art.box) c.image(hov && art.boxHi ? *art.boxHi : *art.box, b);
            else c.roundRect(b, 5, Color::hex(0x8a6a30));
            if (r.value && art.check) c.image(*art.check, Rect(6, 6, 55, 50), Rect(b.x + 3, b.y - 3, 32, 29));
            c.text(ui, model.tr(r.value ? "Вкл" : "Выкл"), b.r() + 12, y + 8, 17, inkSoft);
            addHit(Rect(cx, y, cw, 36), [this, &r]() { r.value = !r.value; changed(r); });
        } else if (r.kind == ls::SLIDER) {
            Rect tr(cx + 8, y + 17, cw - 78, 4);
            bool en = r.enabled;
            c.roundRect(tr, 2, inkSoft.alpha(en ? 0.7f : 0.3f));
            float t = (float)(r.value - r.vmin) / (r.vmax - r.vmin);
            c.roundRect(Rect(tr.x, tr.y, tr.w * t, tr.h), 2, (en ? red : inkDim).alpha(en ? 0.85f : 0.5f));
            Rect kn(tr.x + tr.w * t - 10, y + 9, 20, 20);
            bool hov = en && (hovered(Rect(cx, y, cw - 60, 36)) || dragging >= 0);
            c.roundRect(kn, 10, !en ? Color::hex(0xcdb98e) : hov ? Color::hex(0xe9cf8a) : Color::hex(0xd9b86a));
            c.roundRect(kn.inset(3), 7, en ? ink : inkDim);
            std::string vs;
            if (!en && !r.disabledNote.empty()) vs = "";
            else if (r.div == 1) vs = std::to_string(r.value) + r.unit;
            else { char b[32]; snprintf(b, sizeof b, r.div == 10 ? "%.1f" : "%.2f", (double)r.value / r.div); vs = b + r.unit; }
            c.text(ui, vs, tr.r() + 16, y + 8, 19, en ? inkHi : inkDim);
            if (!en) { if (!r.disabledNote.empty()) c.text(ui, r.disabledNote, cx, y + 26, 13, inkDim, CENTER, cw); return; }
            Hit h; h.r = Rect(cx, y, cw - 56, 36);
            float x0 = tr.x, w0 = tr.w;
            h.drag = [this, &r, x0, w0](float px) {
                float tt = std::clamp((px - x0) / w0, 0.f, 1.f);
                int v = r.vmin + (int)std::lround(tt * (r.vmax - r.vmin) / r.step) * r.step;
                r.value = v;
            };
            h.click = [this, &r]() { changed(r); };
            hits.push_back(h);
        } else if (r.kind == ls::BIND) {
            float gw = 8, w2 = (cw + 10 - gw) / 2;
            for (int sl = 0; sl < 2; sl++) {
                Rect cell(cx - 10 + sl * (w2 + gw), y + 2, w2, 30);
                bool cap = capId == r.id && capSlot == sl;
                bool hov = hovered(cell);
                std::string k = sl < (int)r.bound.size() ? model.tr(ls::keyLabel(r.bound[sl])) : "";
                c.roundRect(cell, 6, cap ? red.alpha(0.16f) : hov ? Color(1, 0.95f, 0.8f, 0.55f) : Color(1, 0.97f, 0.88f, 0.28f));
                c.roundRect(Rect(cell.x, cell.b() - 2, cell.w, 2), 1, (cap ? red : inkSoft).alpha(cap ? 0.8f : 0.35f));
                std::string txt = cap ? (fmod(now, 1.0) < 0.6 ? model.tr("нажмите…") : "") : k.empty() ? "—" : k;
                c.text(ui, txt, cell.x, y + 7, 17, cap ? red : k.empty() ? inkDim : inkHi, CENTER, cell.w);
                Hit h; h.r = cell; std::string id = r.id; int slot = sl;
                h.click = [this, id, slot]() { capId = id; capSlot = slot; };
                h.rclick = [this, &r, slot]() { if (slot < (int)r.bound.size()) { model.setBind(r, slot, ""); statusTime = now; int kt = tab; float sc = scroll; model.reload(); tab = kt; scroll = sc; } };
                hits.push_back(h);
            }
        } else if (r.kind == ls::INFO) {
            c.text(ui, r.text, cx, y + 8, 19, inkHi, CENTER, cw);
        } else if (r.kind == ls::KEYS) {
            float yy = y + 2;
            for (auto& kv : r.keys) {
                c.text(ui, kv.first, lx + 20, yy, 17, ink);
                c.text(ui, kv.second, cx - 60, yy, 17, inkSoft);
                yy += 30;
            }
        } else if (r.kind == ls::MODULE) {
            c.text(title, r.label, lx, y + 2, 22, ink);
            bool good = r.text.rfind("установлен", 0) == 0;
            c.text(ui, r.text, lx + 180, y + 6, 17, good ? Color::hex(0x3d6a1a) : inkSoft);
            float bx = lx;
            for (auto& a : r.actions) {
                float bw = std::max(130.f, c.textWidth(ui, a.label, 17) + 76);
                auto run = a.run;
                frameButton(c, Rect(bx, y + 36, bw, 38), a.label, 17, [this, run]() { model.setStatus(run()); statusTime = now; });
                bx += bw + 10;
            }
        }
    }

    // ------------------------------------------------------------------ input
    int hitAt(float x, float y) const {
        for (int i = (int)hits.size() - 1; i >= 0; i--) if (hits[i].r.contains(x, y)) return i;
        return -1;
    }
    bool overWidget(float x, float y) const { return hitAt(x, y) >= 0; }
    void mouseMove(float x, float y) {
        mx = x; my = y;
        if (dragging >= 0 && dragging < (int)hits.size() && hits[dragging].drag) hits[dragging].drag(x);
    }
    void mouseDown(float x, float y) {
        mx = x; my = y; pressed = hitAt(x, y);
        if (pressed >= 0 && hits[pressed].drag) { dragging = pressed; hits[pressed].drag(x); }
    }
    void mouseUp(float x, float y) {
        mx = x; my = y;
        int h = hitAt(x, y);
        if (dragging >= 0) { if (hits[dragging].click) hits[dragging].click(); dragging = -1; pressed = -1; return; }
        if (h >= 0 && h == pressed && hits[h].click) hits[h].click();
        pressed = -1;
    }
    void mouseLeave() { mx = my = -1; }
    void rightClick(float x, float y) {
        mx = x; my = y; int h = hitAt(x, y);
        if (h >= 0 && hits[h].rclick) hits[h].rclick();
    }
    void refresh() { int t = tab; model.reload(); tab = t; }
};

}  // namespace app
