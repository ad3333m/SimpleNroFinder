#include <SDL.h>

#include <cstdlib>

#include "app/app.h"
#include "net/async.h"
#include "net/http.h"
#include "platform/platform.h"
#include "ui/gfx.h"
#include "ui/images.h"
#include "ui/text.h"
#include "ui/theme.h"
#include "util/log.h"

int main(int, char**) {
    Platform::Init();
    Log::Init(Platform::DataDir() + "log.txt");
    LOG("SimpleNroFinder starting (%s)", Platform::AppletMode() ? "applet" : "application");
    Http::GlobalInit();
    Async::Start();
    InitTheme();

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* win = SDL_CreateWindow("SimpleNroFinder", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, Gfx::W, Gfx::H, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);  // headless CI previews
    if (!win || !ren) {
        LOG("SDL setup failed: %s", SDL_GetError());
        return 1;
    }
    Gfx::Init(ren);
    if (!Text::Init()) LOG("fonts missing");
    Images::Init(Platform::DataDir() + "cache/");

    App app;
    app.Init();
#ifndef __SWITCH__
    // CI preview: SNF_SHOT=out.bmp [SNF_SEARCH=query] [SNF_OPEN=1] renders one
    // screen after the network has had time to answer, saves it and quits.
    const char* shot = getenv("SNF_SHOT");
    const char* search = getenv("SNF_SEARCH");
    if (search) {
        app.tab = 6;  // Search
        app.RunSearch(search);
    }
    bool opened = !getenv("SNF_OPEN");
    const uint64_t started = SDL_GetTicks64();
#endif
    uint64_t last = SDL_GetPerformanceCounter();
    const double freq = (double)SDL_GetPerformanceFrequency();
    while (Platform::Running() && !app.quit) {
        Platform::Input in = Platform::PollInput();
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = (float)((now - last) / freq);
        last = now;
#ifndef __SWITCH__
        if (shot && !opened && SDL_GetTicks64() - started > 10000 && !app.items.empty()) {
            app.OpenItem(app.items[0]);
            opened = true;
        }
        if (shot && SDL_GetTicks64() - started > 18000) {
            app.redraw = 1;
            app.Frame(in, dt);
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, Gfx::W, Gfx::H, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
            SDL_SaveBMP(s, shot);
            SDL_FreeSurface(s);
            break;
        }
#endif
        if (app.Frame(in, dt)) SDL_RenderPresent(ren);
        else SDL_Delay(16);
    }
    LOG("SimpleNroFinder exiting");
    Http::AbortAll();
    app.Shutdown();
    Async::Stop();
    Images::Shutdown();
    Text::Shutdown();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    Http::GlobalCleanup();
    Platform::Shutdown();
    return 0;
}
