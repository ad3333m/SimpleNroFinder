// SimpleNroFinder: popular Switch homebrew from the Homebrew App Store
// catalogue, by category, plus a Search tab that also queries GitHub.
#include "app/app.h"

#include <algorithm>
#include <cmath>

#include "net/async.h"
#include "ui/icons.h"
#include "ui/images.h"
#include "ui/input.h"
#include "ui/text.h"
#include "ui/theme.h"
#include "util/log.h"
#include "util/strings.h"

using namespace Platform;

namespace {

struct Tab {
    const char* label;
    const char* category;  // "" = everything, "*" = whatever the named tabs don't cover
};
const Tab kTabs[] = {{"Featured", ""},       {"Games", "game"},  {"Emulators", "emu"}, {"Tools", "tool"},
                     {"Advanced", "advanced"}, {"More", "*"},    {"Search", nullptr}};
const int kTabCount = sizeof(kTabs) / sizeof(kTabs[0]);

bool InTab(const Tab& t, const std::string& cat) {
    if (!*t.category) return true;
    if (*t.category != '*') return cat == t.category;
    return cat != "game" && cat != "emu" && cat != "tool" && cat != "advanced";
}

std::string Count(int64_t n) {
    if (n >= 1000000) return Str::Format("%.1fM", n / 1e6);
    if (n >= 1000) return Str::Format("%.1fK", n / 1e3);
    return std::to_string(n);
}

const char* CategoryName(const std::string& c) {
    if (c == "game") return "Game";
    if (c == "emu") return "Emulator";
    if (c == "tool") return "Tool";
    if (c == "advanced") return "Advanced";
    if (c == "theme") return "Theme";
    if (c == "concept") return "Concept";
    if (c == "legacy") return "Legacy";
    return "Misc";
}

// Word-wraps s to maxW, keeping its own line breaks.
std::vector<std::string> Wrap(const std::string& s, Face f, int size, float maxW) {
    std::vector<std::string> out;
    for (const std::string& para : Str::Split(s, '\n')) {
        std::string line;
        for (const std::string& word : Str::Split(para, ' ')) {
            std::string next = line.empty() ? word : line + " " + word;
            if (!line.empty() && Text::Width(next, f, size) > maxW) {
                out.push_back(line);
                line = word;
            } else {
                line = next;
            }
        }
        out.push_back(line);
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

void App::Init() { LoadCatalog(); }

void App::Shutdown() { Download::Shutdown(); }

bool App::Frame(const Platform::Input& in, float dt) {
    Ui::BeginFrame(in, dt);
    int pumped = Async::Pump();

    if (!overlays.empty()) {
        overlays.back()->Update(*this);
    } else {
        switch (screen) {
            case Screen::Home: UpdateHome(); break;
            case Screen::Package: UpdatePackage(); break;
            case Screen::Releases: UpdateReleases(); break;
        }
    }
    overlays.erase(
        std::remove_if(overlays.begin(), overlays.end(), [](const std::unique_ptr<Overlay>& o) { return o->closed; }),
        overlays.end());
    const double now = Ui::Time();
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [&](const Toast& t) { return t.until < now; }),
                 toasts.end());

    bool dlActive = false;
    if (auto t = Download::Current()) dlActive = !t->done.load();

    bool want = redraw > 0 || pumped > 0 || Ui::Active() || Ui::Animating() || !toasts.empty() || Images::Busy() ||
                dlActive || now - lastDraw > 0.5;
    if (want) {
        Draw();
        if (redraw > 0) redraw--;
        lastDraw = now;
    }
    Ui::EndFrame(want);
    Images::EndFrame(want);
    return want;
}

void App::Draw() {
    Gfx::Clear(T().bg);
    switch (screen) {
        case Screen::Home: DrawHome(); break;
        case Screen::Package: DrawPackage(); break;
        case Screen::Releases: DrawReleases(); break;
    }
    for (auto& o : overlays) o->Draw(*this);
    DrawToasts();
}

// ---------------------------------------------------------------------------
// data
// ---------------------------------------------------------------------------

bool App::IsSearch() const { return kTabs[tab].category == nullptr; }

void App::LoadCatalog() {
    catalogBusy = true;
    catalogError.clear();
    Catalog::Fetch([this](bool ok, std::vector<Package> list, std::map<std::string, std::string> inst,
                          const std::string& err) {
        catalogBusy = false;
        if (ok) {
            packages = std::move(list);
            installed = std::move(inst);
        } else {
            catalogError = err;
        }
        RebuildItems();
    });
}

void App::RebuildItems() {
    items.clear();
    if (IsSearch()) {
        if (!searchQuery.empty()) {
            const std::string q = Str::Lower(searchQuery);
            for (int i = 0; i < (int)packages.size(); i++) {
                const Package& p = packages[i];
                if (Str::Lower(p.title + " " + p.name + " " + p.author + " " + p.description).find(q) !=
                    std::string::npos)
                    items.push_back({i, {}});
            }
            for (const Repo& r : githubResults) items.push_back({-1, r});
        }
    } else {
        for (int i = 0; i < (int)packages.size(); i++)
            if (InTab(kTabs[tab], packages[i].category)) items.push_back({i, {}});
    }
    sel = std::clamp(sel, 0, std::max(0, (int)items.size() - 1));
    redraw = 3;
}

void App::RunSearch(const std::string& q) {
    searchQuery = Str::Trim(q);
    githubResults.clear();
    githubError.clear();
    sel = 0;
    scroll = 0;
    RebuildItems();
    if (searchQuery.empty()) return;
    githubBusy = true;
    // GitHub results only ever show up here, never in the catalogue tabs.
    const std::string asked = searchQuery;
    Api::SearchRepos(searchQuery + " switch", [this, asked](bool ok, std::vector<Repo> list, const std::string& err) {
        if (asked != searchQuery) return;  // a newer search replaced this one
        githubBusy = false;
        if (ok) githubResults = std::move(list);
        else githubError = err;
        RebuildItems();
    });
}

void App::OpenItem(const Item& it) {
    if (it.pkg >= 0) {
        pkgSel = it.pkg;
        detailScroll = 0;
        screen = Screen::Package;
        redraw = 3;
        return;
    }
    repo = it.repo;
    screen = Screen::Releases;
    relSel = 0;
    relAssetSel = 0;
    releases.clear();
    relError.clear();
    relBusy = true;
    Api::FetchReleases(repo.owner, repo.name, [this](bool ok, std::vector<Release> list, const std::string& err) {
        relBusy = false;
        if (ok) releases = std::move(list);
        else relError = err;
        redraw = 3;
    });
}

void App::Install(const Package& p) {
    Download::StartPackage(p.ZipUrl(), p.name, p.title, p.version, p.size);
    Push(MakeDownloadProgress(*this));
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

void App::Push(std::unique_ptr<Overlay> o) {
    if (o) overlays.push_back(std::move(o));
    Ui::Consume();
    redraw = 3;
}

void App::ShowToast(const std::string& text, bool error) {
    Toast t;
    t.text = text;
    t.error = error;
    t.until = Ui::Time() + (error ? 5.0 : 3.0);
    toasts.push_back(t);
    if (toasts.size() > 3) toasts.erase(toasts.begin());
    redraw = 2;
}

void App::AskIsNro(const std::string& name, const std::string& url, uint64_t knownSize) {
    Push(MakeNroPrompt(*this, name, url, knownSize));
}

void App::DrawHeader(const std::string& title, const std::string& subtitle) {
    const Theme& th = T();
    Gfx::FillRect(Rect{0, 0, (float)Gfx::W, 64}, th.header);
    Gfx::FillRect(Rect{0, 64, (float)Gfx::W, 1}, th.border);
    Icon::Logo(36, 32, 40, 1.0f);
    Text::Draw(title, 68, 18, Face::Bold, 22, th.textStrong, Gfx::W - 300);
    if (!subtitle.empty()) Text::Draw(subtitle, 68, 44, Face::Regular, 14, th.textMuted, Gfx::W - 300);
}

void App::DrawHints(const std::vector<std::pair<std::string, std::string>>& hints) {
    const Theme& th = T();
    const float y = (float)Gfx::H - 32;
    Gfx::FillRect(Rect{0, y - 10, (float)Gfx::W, 42}, th.header);
    Gfx::FillRect(Rect{0, y - 10, (float)Gfx::W, 1}, th.border);
    Icon::Hints((float)Gfx::W - 24, y, hints, th.textMuted, th.surface2, th.textStrong);
}

void App::DrawToasts() {
    if (toasts.empty()) return;
    const Theme& th = T();
    const float w = 460, x = Gfx::W - w - 20;
    float y = 80;
    for (const auto& t : toasts) {
        const float h = 48;
        const Rect r{x, y, w, h};
        const float a = std::clamp((float)(t.until - Ui::Time()) * 3.0f, 0.0f, 1.0f);
        Gfx::Shadow(Rect{r.x, r.y + 4, r.w, r.h}, 12, 18, Color(0, 0, 0, (uint8_t)(120 * a)));
        Gfx::FillRound(r, 10, th.surface2.Alpha(a));
        Gfx::StrokeRound(r, 10, 1, (t.error ? th.danger : th.border).Alpha(a));
        Text::Draw(t.text, r.x + 16, r.y + (h - Text::GetMetrics(Face::Medium, 15).lineHeight) / 2, Face::Medium, 15,
                   (t.error ? th.danger : th.textStrong).Alpha(a), r.w - 32);
        y += h + 10;
    }
}

// ---------------------------------------------------------------------------
// Home: category tabs and Search
// ---------------------------------------------------------------------------

void App::UpdateHome() {
    if (Ui::Pressed(BTN_PLUS)) {
        quit = true;
        return;
    }
    int dt = (Ui::Repeat(BTN_R, 0.4f, 0.14f) ? 1 : 0) - (Ui::Repeat(BTN_L, 0.4f, 0.14f) ? 1 : 0);
    if (Ui::Pressed(BTN_B) && IsSearch()) dt = -tab;  // B leaves Search for Featured
    if (dt && tab + dt >= 0 && tab + dt < kTabCount) {
        tab += dt;
        sel = 0;
        scroll = 0;
        RebuildItems();
    }
    if (Ui::Pressed(BTN_Y) || (IsSearch() && searchQuery.empty() && Ui::Pressed(BTN_A))) {
        std::string out;
        if (Platform::Keyboard("Search homebrew and GitHub", searchQuery, out, 100, false, false)) {
            Ui::Consume();
            tab = kTabCount - 1;
            RunSearch(out);
        }
        return;
    }
    if (Ui::Pressed(BTN_X)) {
        LoadCatalog();
        if (IsSearch() && !searchQuery.empty()) RunSearch(searchQuery);
    }
    if (items.empty()) return;
    int dy = Ui::NavY() + Ui::PageY() * 7;
    if (dy) sel = std::clamp(sel + dy, 0, (int)items.size() - 1);
    float s = Ui::StickScrollDelta();
    if (s != 0) scroll = std::max(0.0f, scroll + s);
    if (Ui::Pressed(BTN_A)) OpenItem(items[sel]);
}

void App::DrawHome() {
    const Theme& th = T();
    DrawHeader("SimpleNroFinder", packages.empty() ? "Popular Switch homebrew"
                                                    : Str::Format("%zu apps from the Homebrew App Store", packages.size()));

    // Tabs
    float x = 40;
    for (int i = 0; i < kTabCount; i++) {
        const bool on = i == tab;
        const float w = Text::Width(kTabs[i].label, Face::SemiBold, 16) + 28;
        const Rect pill{x, 78, w, 34};
        if (on) Gfx::FillRound(pill, 17, th.accent);
        Text::DrawCentered(kTabs[i].label, x + w / 2, 86, Face::SemiBold, 16, on ? th.accentText : th.textMuted);
        x += w + 8;
    }
    if (IsSearch() && !searchQuery.empty())
        Text::DrawRight("\"" + searchQuery + "\"", (float)Gfx::W - 40, 86, Face::Medium, 15, th.text);

    const Rect list{40, 124, (float)Gfx::W - 80, (float)Gfx::H - 124 - 52};
    Gfx::FillRound(list, 12, th.surface);
    Gfx::StrokeRound(list, 12, 1, th.border);
    const float cx = list.x + list.w / 2, cy = list.y + list.h / 2 - 10;

    if (items.empty()) {
        if (catalogBusy || githubBusy) Icon::Spinner(cx, cy, 18, th.textMuted, Ui::Time());
        else if (!catalogError.empty()) Text::DrawCentered(catalogError + "  (X to retry)", cx, cy, Face::Regular, 15, th.danger, list.w - 40);
        else if (IsSearch() && searchQuery.empty()) Text::DrawCentered("Press Y to search the App Store and GitHub", cx, cy, Face::Regular, 15, th.textMuted);
        else Text::DrawCentered(githubError.empty() ? "Nothing found." : "Nothing found. GitHub: " + githubError, cx, cy, Face::Regular, 15, th.textMuted, list.w - 40);
    } else {
        const float rowH = 76;
        const float top = sel * rowH;
        if (top < scroll) scroll = top;
        if (top + rowH > scroll + list.h - 16) scroll = top + rowH - list.h + 16;
        scroll = std::clamp(scroll, 0.0f, std::max(0.0f, items.size() * rowH - list.h + 16));
        Gfx::PushClip(list);
        for (int i = 0; i < (int)items.size(); i++) {
            Rect row{list.x + 8, list.y + 8 + i * rowH - scroll, list.w - 16, rowH - 4};
            if (!Gfx::Visible(row)) continue;
            if (i == sel) {
                Gfx::FillRound(row, 10, th.surface3);
                Gfx::StrokeRound(row, 10, 2, th.accent);
            }
            const Rect icon{row.x + 10, row.y + 8, 96, 56};
            const float tx = icon.Right() + 16, tw = row.w - (tx - row.x) - 150;
            std::string title, sub, meta, badge;
            Color badgeColor = th.textMuted;
            const Images::Img* img = nullptr;
            if (items[i].pkg >= 0) {
                const Package& p = packages[items[i].pkg];
                img = Images::Get(p.IconUrl(), 256);
                title = p.title;
                sub = p.description;
                meta = Str::Format("%s   v%s   %s   \xE2\x86\x93 %s", p.author.c_str(), p.version.c_str(),
                                   CategoryName(p.category), Count(p.downloads).c_str());
                if (p.binary == "none") meta += "   no .nro (sysmodule/overlay)";
                auto it = installed.find(p.name);
                if (it != installed.end()) {
                    const bool update = !it->second.empty() && it->second != p.version;
                    badge = update ? "Update" : "Installed";
                    badgeColor = update ? th.warn : th.accent;
                }
                if (IsSearch() && badge.empty()) badge = "App Store";
            } else {
                const Repo& r = items[i].repo;
                img = r.ownerAvatar.empty() ? nullptr : Images::Get(r.ownerAvatar + "&s=64", 64);
                title = r.fullName;
                sub = r.description;
                meta = Str::Format("\xE2\x98\x85 %s", Count(r.stars).c_str());
                if (!r.language.empty()) meta += "   " + r.language;
                badge = "GitHub";
                badgeColor = th.link;
            }
            Gfx::FillRound(icon, 8, th.surface2);
            if (img && img->tex) {
                if (items[i].pkg >= 0) Gfx::ImageCover(img->tex, img->w, img->h, icon, 8);
                else Gfx::ImageCircle(img->tex, icon.x + icon.w / 2, icon.y + icon.h / 2, 26);
            }
            Text::Draw(title, tx, row.y + 8, Face::SemiBold, 18, th.textStrong, tw);
            Text::Draw(sub, tx, row.y + 32, Face::Regular, 14, th.textMuted, tw);
            Text::Draw(meta, tx, row.y + 52, Face::Regular, 12, th.textFaint, tw);
            if (!badge.empty()) {
                const float bw = Text::Width(badge, Face::SemiBold, 13) + 20;
                const Rect b{row.Right() - bw - 14, row.y + row.h / 2 - 13, bw, 26};
                Gfx::StrokeRound(b, 13, 1.5f, badgeColor);
                Text::DrawCentered(badge, b.x + b.w / 2, b.y + 5, Face::SemiBold, 13, badgeColor);
            }
        }
        Gfx::PopClip();
        if (IsSearch() && githubBusy)
            Text::DrawRight("Searching GitHub...", list.Right() - 16, list.Bottom() - 22, Face::Regular, 12, th.textFaint);
    }

    DrawHints({{"A", "Open"}, {"Y", "Search"}, {"L/R", "Tabs"}, {"ZL/ZR", "Page"}, {"X", "Refresh"}, {"+", "Quit"}});
}

// ---------------------------------------------------------------------------
// Package detail
// ---------------------------------------------------------------------------

void App::UpdatePackage() {
    if (Ui::Pressed(BTN_B)) {
        screen = Screen::Home;
        redraw = 3;
        return;
    }
    const Platform::Input& in = Ui::In();
    if (in.held & BTN_UP) detailScroll = std::max(0.0f, detailScroll - Ui::Dt() * 500);
    if (in.held & BTN_DOWN) detailScroll += Ui::Dt() * 500;
    float s = Ui::StickScrollDelta(1200);
    if (s != 0) detailScroll = std::max(0.0f, detailScroll + s);
    if (Ui::Pressed(BTN_A)) Install(packages[pkgSel]);
}

void App::DrawPackage() {
    const Theme& th = T();
    const Package& p = packages[pkgSel];
    DrawHeader(p.title, "by " + p.author);

    // Left: banner, facts and the install button.
    const Rect left{40, 84, 400, (float)Gfx::H - 84 - 56};
    Gfx::FillRound(left, 12, th.surface);
    Gfx::StrokeRound(left, 12, 1, th.border);
    const Rect banner{left.x + 16, left.y + 16, left.w - 32, (left.w - 32) * 150 / 256};
    Gfx::FillRound(banner, 10, th.surface2);
    if (const Images::Img* img = Images::Get(p.IconUrl(), 512); img && img->tex)
        Gfx::ImageCover(img->tex, img->w, img->h, banner, 10);

    auto it = installed.find(p.name);
    const bool have = it != installed.end();
    const bool update = have && !it->second.empty() && it->second != p.version;
    std::vector<std::pair<std::string, std::string>> facts{
        {"Version", p.version + (update ? "  (you have " + it->second + ")" : "")},
        {"Category", CategoryName(p.category)},
        {"Downloads", Count(p.downloads)},
        {"Size", Str::HumanSize(p.size)},
        {"Updated", p.updated},
        {"License", p.license.empty() ? "-" : p.license},
        {"Launch", p.binary == "none" ? "No .nro (sysmodule / overlay)" : p.binary},
    };
    float y = banner.Bottom() + 16;
    for (const auto& [k, v] : facts) {
        Text::Draw(k, left.x + 20, y, Face::Regular, 14, th.textMuted);
        Text::Draw(v, left.x + 120, y, Face::Medium, 14, th.text, left.w - 140);
        y += 24;
    }
    const Rect btn{left.x + 16, left.Bottom() - 64, left.w - 32, 48};
    Gfx::FillRound(btn, 10, update ? th.warn : th.accent);
    Text::DrawCentered(update ? "Update  A" : have ? "Reinstall  A" : "Install  A", btn.x + btn.w / 2, btn.y + 14,
                       Face::SemiBold, 17, th.accentText);
    Ui::Hot(btn, [this] { Install(packages[pkgSel]); });

    // Right: description, scrollable.
    const Rect right{left.Right() + 12, 84, (float)Gfx::W - left.Right() - 12 - 40, left.h};
    Gfx::FillRound(right, 12, th.surface);
    Gfx::StrokeRound(right, 12, 1, th.border);
    const float lh = Text::GetMetrics(Face::Regular, 15).lineHeight;
    std::vector<std::string> lines = Wrap(p.description, Face::SemiBold, 17, right.w - 48);
    const size_t head = lines.size();
    lines.push_back("");
    for (auto& l : Wrap(p.details.empty() ? "No details." : p.details, Face::Regular, 15, right.w - 48))
        lines.push_back(std::move(l));
    detailScroll = std::min(detailScroll, std::max(0.0f, lines.size() * lh - right.h + 40));
    Gfx::PushClip(right);
    y = right.y + 20 - detailScroll;
    for (size_t i = 0; i < lines.size(); i++, y += lh)
        if (y > right.y - lh && y < right.Bottom())
            Text::Draw(lines[i], right.x + 24, y, i < head ? Face::SemiBold : Face::Regular, i < head ? 17 : 15,
                       i < head ? th.textStrong : th.text);
    Gfx::PopClip();

    DrawHints({{"A", update ? "Update" : have ? "Reinstall" : "Install"}, {"D-Pad", "Scroll"}, {"B", "Back"}});
}

// ---------------------------------------------------------------------------
// GitHub releases (reached from Search)
// ---------------------------------------------------------------------------

void App::UpdateReleases() {
    if (Ui::Pressed(BTN_B)) {
        screen = Screen::Home;
        redraw = 3;
        return;
    }
    if (releases.empty()) return;
    if (Ui::Repeat(BTN_L, 0.35f, 0.14f) || Ui::Repeat(BTN_R, 0.35f, 0.14f)) {
        relSel = std::clamp(relSel + (Ui::Held(BTN_R) ? 1 : -1), 0, (int)releases.size() - 1);
        relAssetSel = 0;
    }
    const int n = std::max(1, (int)releases[relSel].assets.size());
    int dy = Ui::NavY() + Ui::PageY() * 6;
    if (dy) relAssetSel = std::clamp(relAssetSel + dy, 0, n - 1);
    if (Ui::Pressed(BTN_A) && relAssetSel < (int)releases[relSel].assets.size()) {
        const Asset& a = releases[relSel].assets[relAssetSel];
        AskIsNro(a.name, a.url, a.size);
    }
}

void App::DrawReleases() {
    const Theme& th = T();
    DrawHeader(repo.fullName, "GitHub releases");
    const Rect left{40, 84, 360, (float)Gfx::H - 84 - 56};
    const Rect right{left.Right() + 12, 84, (float)Gfx::W - left.Right() - 12 - 40, left.h};
    for (const Rect& r : {left, right}) {
        Gfx::FillRound(r, 12, th.surface);
        Gfx::StrokeRound(r, 12, 1, th.border);
    }
    const float cx = left.x + left.w / 2, cy = left.y + left.h / 2 - 10;
    if (relBusy) {
        Icon::Spinner(cx, cy, 18, th.textMuted, Ui::Time());
    } else if (!relError.empty()) {
        Text::DrawCentered(relError, cx, cy, Face::Regular, 15, th.danger, left.w - 20);
    } else if (releases.empty()) {
        Text::DrawCentered("No releases.", cx, cy, Face::Regular, 15, th.textMuted);
    } else {
        const float rowH = 56;
        const float off = std::max(0.0f, (relSel + 1) * rowH - left.h + 16);
        Gfx::PushClip(left);
        for (int i = 0; i < (int)releases.size(); i++) {
            const Release& r = releases[i];
            Rect row{left.x + 8, left.y + 8 + i * rowH - off, left.w - 16, rowH - 4};
            if (!Gfx::Visible(row)) continue;
            if (i == relSel) Gfx::FillRound(row, 8, th.surface3);
            Text::Draw(r.name.empty() ? r.tag : r.name, row.x + 12, row.y + 8, Face::SemiBold, 15, th.textStrong, row.w - 24);
            Text::Draw(r.tag + (r.prerelease ? "   pre-release" : ""), row.x + 12, row.y + 30, Face::Regular, 12,
                       th.textMuted, row.w - 24);
        }
        Gfx::PopClip();

        const Release& r = releases[relSel];
        Text::Draw(r.name.empty() ? r.tag : r.name, right.x + 16, right.y + 14, Face::Bold, 18, th.textStrong, right.w - 32);
        Text::Draw(Str::Format("%zu file%s", r.assets.size(), r.assets.size() == 1 ? "" : "s"), right.x + 16, right.y + 40, Face::Regular, 13, th.textMuted);
        const float topY = right.y + 68;
        Gfx::FillRect(Rect{right.x + 16, topY - 10, right.w - 32, 1}, th.border);
        const float arowH = 56;
        const float aoff = std::max(0.0f, (relAssetSel + 1) * arowH - (right.Bottom() - topY) + 8);
        Gfx::PushClip(Rect{right.x, topY, right.w, right.Bottom() - topY});
        if (r.assets.empty())
            Text::DrawCentered("No downloadable files.", right.x + right.w / 2, topY + 24, Face::Regular, 14, th.textMuted);
        for (int i = 0; i < (int)r.assets.size(); i++) {
            const Asset& a = r.assets[i];
            Rect row{right.x + 10, topY + i * arowH - aoff, right.w - 20, arowH - 4};
            if (!Gfx::Visible(row)) continue;
            if (i == relAssetSel) {
                Gfx::FillRound(row, 8, th.surface3);
                Gfx::StrokeRound(row, 8, 1, th.accent);
            }
            const bool nro = Str::EndsWith(Str::Lower(a.name), ".nro");
            Icon::File(row.x + 28, row.y + row.h / 2, 20, nro ? th.accent : th.link);
            Text::Draw(a.name, row.x + 56, row.y + 10, Face::Medium, 16, th.textStrong, row.w - 80);
            Text::Draw(Str::Format("%s   %d downloads", Str::HumanSize(a.size).c_str(), a.downloads), row.x + 56,
                       row.y + 32, Face::Regular, 12, th.textMuted);
        }
        Gfx::PopClip();
    }
    DrawHints({{"A", "Download"}, {"L/R", "Release"}, {"B", "Back"}});
}
