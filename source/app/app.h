#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "github/api.h"
#include "github/download.h"
#include "github/model.h"
#include "platform/platform.h"
#include "store/catalog.h"
#include "ui/gfx.h"

struct App;

// Overlay drawn on top of everything, grabbing all input while on top.
class Overlay {
public:
    virtual ~Overlay() = default;
    virtual void Update(App& app) = 0;
    virtual void Draw(App& app) = 0;
    bool closed = false;
};

enum class Screen {
    Home,      // category tabs + Search
    Package,   // one App Store package
    Releases,  // a GitHub repo's releases + assets (from Search only)
};

struct Toast {
    std::string text;
    double until = 0;
    bool error = false;
};

// One row of the home list: an App Store package, or (Search only) a GitHub repo.
struct Item {
    int pkg = -1;  // index into App::packages, -1 = GitHub repo
    Repo repo;
};

struct App {
    bool quit = false;
    Screen screen = Screen::Home;
    int redraw = 3;
    double lastDraw = 0;
    std::vector<std::unique_ptr<Overlay>> overlays;
    std::vector<Toast> toasts;

    // ---- catalogue
    std::vector<Package> packages;              // most downloaded first
    std::map<std::string, std::string> installed;  // name -> version
    bool catalogBusy = false;
    std::string catalogError;

    // ---- home
    int tab = 0;  // index into kTabs (app.cpp); the last one is Search
    std::vector<Item> items;
    int sel = 0;
    float scroll = 0;
    std::string searchQuery;
    std::vector<Repo> githubResults;
    bool githubBusy = false;
    std::string githubError;

    // ---- package detail
    int pkgSel = -1;
    float detailScroll = 0;

    // ---- GitHub releases
    Repo repo;
    std::vector<Release> releases;
    int relSel = 0;
    int relAssetSel = 0;
    bool relBusy = false;
    std::string relError;

    void Init();
    void Shutdown();
    bool Frame(const Platform::Input& in, float dt);
    void Draw();

    void LoadCatalog();
    void RebuildItems();
    void RunSearch(const std::string& q);
    void OpenItem(const Item& it);
    void Install(const Package& p);
    bool IsSearch() const;

    void Push(std::unique_ptr<Overlay> o);
    void ShowToast(const std::string& text, bool error = false);
    void AskIsNro(const std::string& name, const std::string& url, uint64_t knownSize = 0);

    void UpdateHome();
    void DrawHome();
    void UpdatePackage();
    void DrawPackage();
    void UpdateReleases();
    void DrawReleases();
    void DrawHeader(const std::string& title, const std::string& subtitle = "");
    void DrawHints(const std::vector<std::pair<std::string, std::string>>& items);
    void DrawToasts();
};

std::unique_ptr<Overlay> MakeNroPrompt(App& app, const std::string& name, const std::string& url, uint64_t knownSize);
std::unique_ptr<Overlay> MakeDownloadProgress(App& app);
