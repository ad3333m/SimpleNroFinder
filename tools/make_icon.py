#!/usr/bin/env python3
"""Draw SimpleNroFinder's art: icon.jpg (256x256 NACP tile) and romfs/logo.png
(128x128 header mark). A teal tile with a white magnifier over "NRO"."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT = "C:/Windows/Fonts/segoeuib.ttf"
TEAL, TEAL_DARK, NAVY, WHITE = (20, 168, 148), (12, 110, 98), (14, 17, 23), (255, 255, 255)


def mark(size: int, text: bool) -> Image.Image:
    s = size * 4  # supersample, then downscale for smooth edges
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for y in range(s):  # vertical teal gradient
        t = y / s
        c = tuple(int(a + (b - a) * t) for a, b in zip(TEAL, TEAL_DARK))
        d.line([(0, y), (s, y)], fill=c + (255,))
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1], radius=s // 5, fill=255)
    img.putalpha(mask)
    cx, cy, r, w = s * 0.44, s * 0.42, s * 0.25, s * 0.07
    d.line([(cx + r * 0.7, cy + r * 0.7), (s * 0.80, s * 0.80)], fill=WHITE, width=int(w * 1.5))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=WHITE, width=int(w))
    if text:
        f = ImageFont.truetype(FONT, int(r * 0.62))
        d.text((cx, cy), "NRO", font=f, fill=WHITE, anchor="mm")
    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    tile = Image.new("RGB", (256, 256), NAVY)
    m = mark(256, True)
    tile.paste(m, (0, 0), m)
    tile.save(ROOT / "icon.jpg", "JPEG", quality=94)
    (ROOT / "romfs").mkdir(exist_ok=True)
    mark(128, False).save(ROOT / "romfs" / "logo.png", optimize=True)


if __name__ == "__main__":
    main()
