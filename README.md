# SimpleNroFinder

A Nintendo Switch homebrew `.nro` for finding and installing other homebrew.

- **Featured / Games / Emulators / Tools / Advanced / More** — the ~500 apps in the
  [Homebrew App Store](https://hb-app.store) catalogue, most downloaded first.
  A installs: the package unpacks onto the SD card exactly like the App Store does
  (and is recorded in `switch/appstore/.get/packages/`, so both apps agree on
  what's installed and what has an update).
- **Search** (Y) — matches from that catalogue plus GitHub repositories. GitHub
  results only ever appear in Search. Open one to pick a release file; every
  GitHub download asks "Is this an NRO?" and lands in `sdmc:/switch/`.

Controls: L/R tabs, A open/install, Y search, X refresh, B back, + quit.

## Build

Built by GitHub Actions in the `devkitpro/devkita64` container (see
`.github/workflows/build.yml`); the `latest` release carries `SimpleNroFinder.nro`.
Locally: `make` with devkitA64 and the portlibs listed in the workflow.
`tools/make_icon.py` redraws `icon.jpg` and `romfs/logo.png`.
