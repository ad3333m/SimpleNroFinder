// Two overlays:
//   NroPrompt       - "Is this an NRO?" confirmation before every download.
//   DownloadProgress - live progress for an in-flight download, cancel on B.
#include "app/app.h"

#include <algorithm>
#include <cmath>

#include "github/download.h"
#include "platform/platform.h"
#include "ui/gfx.h"
#include "ui/icons.h"
#include "ui/input.h"
#include "ui/text.h"
#include "ui/theme.h"
#include "util/strings.h"

using namespace Platform;

namespace {

void Scrim() { Gfx::FillRect(Rect{0, 0, (float)Gfx::W, (float)Gfx::H}, T().scrim); }

void Card(const Rect& r, Color border) {
    Gfx::Shadow(Rect{r.x, r.y + 8, r.w, r.h}, 16, 30, Color(0, 0, 0, 160));
    Gfx::FillRound(r, 16, T().surface);
    Gfx::StrokeRound(r, 16, 1, border);
}

// ---------------------------------------------------------------------------
// "Is this an NRO?"
// ---------------------------------------------------------------------------
class NroPrompt : public Overlay {
public:
    NroPrompt(const std::string& name, const std::string& url, uint64_t size)
        : name_(name), url_(url), size_(size) {
        isZip_ = Str::EndsWith(Str::Lower(name_), ".zip");
    }

    void Update(App& app) override {
        if (Ui::Pressed(BTN_B)) {
            closed = true;
            return;
        }
        int dx = Ui::NavX();
        if (dx) sel_ = dx > 0 ? 1 : 0;
        if (Ui::Pressed(BTN_A)) Choose(app, sel_);
    }

    void Choose(App& app, int which) {
        closed = true;
        if (which == 1) {
            // Start the download to sdmc:/switch/<basename>. For a zip the
            // file lands there first, then every .nro inside is extracted
            // beside it and the zip is deleted.
            std::string base = name_;
            size_t slash = base.find_last_of("/\\");
            if (slash != std::string::npos) base = base.substr(slash + 1);
            if (base.empty()) base = isZip_ ? "download.zip" : "download.bin";
            const std::string dest = Platform::DownloadDir() + base;
            Download::Start(url_, dest, base, size_, Platform::DownloadDir());
            app.Push(MakeDownloadProgress(app));
        } else {
            app.ShowToast("Download cancelled.");
        }
    }

    void Draw(App& app) override {
        (void)app;
        const Theme& th = T();
        Scrim();
        Ui::Block(Rect{0, 0, (float)Gfx::W, (float)Gfx::H}, [this] { closed = true; });

        const Rect card{(Gfx::W - 640) / 2.0f, (Gfx::H - 340) / 2.0f, 640, 340};
        Card(card, th.warn);
        Ui::Block(card);

        // Big amber warning icon.
        Gfx::FillCircle(card.x + 54, card.y + 68, 24, th.warn);
        Text::DrawCentered("?", card.x + 54, card.y + 68 - 18, Face::Bold, 32, th.bg);

        Text::Draw(isZip_ ? "Is there an NRO inside?" : "Is this an NRO?", card.x + 100, card.y + 44, Face::Bold, 24,
                   th.textStrong);
        Text::Draw(name_, card.x + 100, card.y + 80, Face::Mono, 15, th.text, card.w - 120);

        // Explanation.
        const char* body = isZip_
            ? "Yes downloads the zip, finds every .nro inside and extracts them to\n"
              "sdmc:/switch/ so hbmenu can see them. The zip itself is removed.\n"
              "No cancels and nothing is written to the SD card."
            : "Yes saves it to sdmc:/switch/ so hbmenu can see it.\n"
              "No cancels and nothing is written to the SD card.";
        Text::Draw(body, card.x + 32, card.y + 128, Face::Regular, 15, th.textMuted, card.w - 64);
        if (size_) Text::Draw("Size: " + Str::HumanSize(size_), card.x + 32, card.y + 200, Face::Regular, 14,
                              th.textFaint);

        const Rect no{card.x + 32, card.Bottom() - 72, 280, 48};
        const Rect yes{card.Right() - 32 - 280, card.Bottom() - 72, 280, 48};
        Gfx::FillRound(no, 10, sel_ == 0 ? th.surface3 : th.surface2);
        if (sel_ == 0) Gfx::StrokeRound(no, 10, 2, th.danger);
        Gfx::FillRound(yes, 10, sel_ == 1 ? th.accent : Mix(th.accent, th.surface, 0.5f));
        if (sel_ == 1) Gfx::StrokeRound(yes, 10, 2, th.textStrong);
        Text::DrawCentered("No, cancel", no.x + no.w / 2, no.y + 15, Face::SemiBold, 16, th.text);
        Text::DrawCentered("Yes, download", yes.x + yes.w / 2, yes.y + 15, Face::SemiBold, 16, th.accentText);
        Ui::Hot(no, [this, &app] { Choose(app, 0); });
        Ui::Hot(yes, [this, &app] { Choose(app, 1); });
    }

private:
    std::string name_, url_;
    uint64_t size_;
    bool isZip_ = false;
    int sel_ = 1;  // "Yes" is the default
};

// ---------------------------------------------------------------------------
// Download progress
// ---------------------------------------------------------------------------
class DownloadProgress : public Overlay {
public:
    DownloadProgress() {}

    void Update(App& app) override {
        auto t = Download::Current();
        if (!t) {
            closed = true;
            return;
        }
        if (t->done.load()) {
            if (!reportedDone_) {
                reportedDone_ = true;
                if (t->success && !t->pkgName.empty()) {
                    app.installed[t->pkgName] = t->pkgVersion;
                    app.ShowToast("Installed " + t->displayName);
                } else if (t->success) {
                    if (!t->extracted.empty()) {
                        app.ShowToast(Str::Format("Extracted %zu nro%s to %s", t->extracted.size(),
                                                  t->extracted.size() == 1 ? "" : "s", t->destDir.c_str()));
                    } else {
                        app.ShowToast("Saved to " + t->path);
                    }
                } else {
                    app.ShowToast(t->error.empty() ? "Download failed" : t->error, true);
                }
                dismissAt_ = Ui::Time() + 1.2;
            }
            if (Ui::Pressed(BTN_A) || Ui::Pressed(BTN_B) || Ui::Time() > dismissAt_) closed = true;
            return;
        }
        if (Ui::Pressed(BTN_B)) {
            Download::CancelCurrent();
            app.ShowToast("Cancelling...");
        }
    }

    void Draw(App& app) override {
        (void)app;
        auto t = Download::Current();
        if (!t) return;
        const Theme& th = T();
        Scrim();
        Ui::Block(Rect{0, 0, (float)Gfx::W, (float)Gfx::H}, [] {});

        const Rect card{(Gfx::W - 640) / 2.0f, (Gfx::H - 260) / 2.0f, 640, 260};
        Card(card, t->done.load() ? (t->success ? th.accent : th.danger) : th.border);

        if (t->done.load() && t->success) {
            Icon::Check(card.x + 54, card.y + 56, 32, th.accent);
            Text::Draw(t->pkgName.empty() ? "Downloaded" : "Installed", card.x + 100, card.y + 36, Face::Bold, 22,
                       th.textStrong);
            Text::Draw(t->displayName, card.x + 100, card.y + 68, Face::Mono, 14, th.text, card.w - 120);
            if (!t->pkgName.empty()) {
                Text::Draw(t->extracted.empty() ? "Runs in the background (sysmodule / overlay). Reboot to load it."
                                                : "sdmc:/" + t->extracted[0],
                           card.x + 32, card.y + 118, Face::Regular, 14, th.textMuted, card.w - 64);
                if (!t->extracted.empty())
                    Text::Draw("Launch it from hbmenu.", card.x + 32, card.y + 142, Face::Regular, 14, th.textFaint);
            } else if (!t->extracted.empty()) {
                std::string line = Str::Format("Extracted %zu nro file%s from the zip to %s",
                                               t->extracted.size(), t->extracted.size() == 1 ? "" : "s",
                                               t->destDir.c_str());
                Text::Draw(line, card.x + 32, card.y + 118, Face::Regular, 14, th.textMuted, card.w - 64);
                std::string list;
                for (size_t i = 0; i < t->extracted.size() && i < 3; i++) {
                    if (!list.empty()) list += ",  ";
                    list += t->extracted[i];
                }
                if (t->extracted.size() > 3) list += ",  +" + std::to_string(t->extracted.size() - 3) + " more";
                Text::Draw(list, card.x + 32, card.y + 142, Face::Mono, 13, th.textFaint, card.w - 64);
            } else {
                Text::Draw("Saved to " + t->path, card.x + 32, card.y + 118, Face::Regular, 14, th.textMuted,
                           card.w - 64);
                Text::Draw("Launch it from hbmenu.", card.x + 32, card.y + 142, Face::Regular, 14, th.textFaint);
            }
        } else if (t->done.load()) {
            Icon::Close(card.x + 54, card.y + 56, 28, th.danger);
            Text::Draw("Download failed", card.x + 100, card.y + 36, Face::Bold, 22, th.textStrong);
            Text::Draw(t->displayName, card.x + 100, card.y + 68, Face::Mono, 14, th.text, card.w - 120);
            Text::Draw(t->error, card.x + 32, card.y + 118, Face::Regular, 14, th.danger, card.w - 64);
        } else {
            Icon::Spinner(card.x + 54, card.y + 56, 20, th.accent, Ui::Time());
            const char* title = t->stage.load() == 1 ? (t->pkgName.empty() ? "Extracting nros from zip..." : "Installing...")
                                                    : "Downloading";
            Text::Draw(title, card.x + 100, card.y + 36, Face::Bold, 22, th.textStrong);
            Text::Draw(t->displayName, card.x + 100, card.y + 68, Face::Mono, 14, th.text, card.w - 120);

            uint64_t rec = t->received.load();
            uint64_t tot = t->total.load();
            float frac = tot ? std::min(1.0f, (float)rec / (float)tot) : 0.0f;
            const Rect bar{card.x + 32, card.y + 140, card.w - 64, 10};
            Gfx::FillRound(bar, 5, th.surface3);
            if (frac > 0.01f) Gfx::FillRound(Rect{bar.x, bar.y, bar.w * frac, bar.h}, 5, th.accent);
            else if (tot == 0) {
                // Indeterminate shimmer.
                float t2 = std::fmod(Ui::Time(), 1.5) / 1.5f;
                Gfx::FillRound(Rect{bar.x + (bar.w - 120) * t2, bar.y, 120, bar.h}, 5, th.accent);
            }
            std::string line;
            if (tot) line = Str::HumanSize(rec) + " / " + Str::HumanSize(tot);
            else line = Str::HumanSize(rec);
            Text::Draw(line, card.x + 32, card.y + 164, Face::Regular, 13, th.textMuted);
        }

        const Rect btn{card.x + card.w - 160 - 32, card.Bottom() - 60, 160, 44};
        Gfx::FillRound(btn, 10, th.surface2);
        Gfx::StrokeRound(btn, 10, 1, th.border);
        const char* label = t->done.load() ? "OK  A" : "Cancel  B";
        Text::DrawCentered(label, btn.x + btn.w / 2, btn.y + 12, Face::SemiBold, 16, th.text);
    }

private:
    bool reportedDone_ = false;
    double dismissAt_ = 1e18;
};

}  // namespace

std::unique_ptr<Overlay> MakeNroPrompt(App&, const std::string& name, const std::string& url, uint64_t knownSize) {
    return std::make_unique<NroPrompt>(name, url, knownSize);
}

std::unique_ptr<Overlay> MakeDownloadProgress(App&) { return std::make_unique<DownloadProgress>(); }
