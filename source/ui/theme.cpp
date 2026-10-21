#include "ui/theme.h"

namespace {

Theme g_theme;

Color H(uint32_t rgb, uint8_t a = 255) { return Color::Hex(rgb, a); }

}  // namespace

const Theme& T() { return g_theme; }

void InitTheme() {
    Theme& t = g_theme;
    // Own palette: deep navy with a teal accent.
    t.bg = H(0x0E1117);
    t.surface = H(0x161A22);
    t.surface2 = H(0x1C212B);
    t.surface3 = H(0x262D3A);
    t.border = H(0x2C3341);
    t.header = H(0x090B10);
    t.input = H(0x0E1117);
    t.text = H(0xCDD5E0);
    t.textStrong = H(0xF4F7FB);
    t.textMuted = H(0x8C96A6);
    t.textFaint = H(0x6A7383);
    t.link = H(0x5CC8FF);
    t.accent = H(0x14A894);
    t.accentText = H(0xFFFFFF);
    t.warn = H(0xE0A526);
    t.danger = H(0xF25A5A);
    t.scrim = H(0x05070A, 190);
    t.codeBg = H(0x090B10);
}
