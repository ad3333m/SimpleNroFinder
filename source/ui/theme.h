#pragma once

#include "ui/gfx.h"

// A single GitHub-dark theme. All screens draw with the active one through T().
struct Theme {
    Color bg;          // page background
    Color surface;     // cards, lists
    Color surface2;    // raised rows, menus
    Color surface3;    // hover
    Color border;      // card and list separators
    Color header;      // top bar
    Color input;       // search field
    Color text;        // body text
    Color textStrong;  // headings, repo names
    Color textMuted;   // metadata, timestamps
    Color textFaint;   // placeholders
    Color link;        // clickable text
    Color accent;      // primary buttons (GitHub green)
    Color accentText;
    Color warn;        // amber "is this an NRO" prompt
    Color danger;
    Color scrim;       // dialog backdrop
    Color codeBg;
};

const Theme& T();
void InitTheme();
