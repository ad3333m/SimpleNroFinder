#!/usr/bin/env python3
"""Draw SimpleNroFinder's art: icon.jpg (256x256 NACP tile) and romfs/logo.png
(128x128 header mark). A grid of colourful app tiles on a deep gradient, with a
glass magnifier lifting one of them."""
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
S = 1024  # drawn at 4x, downscaled for smooth edges

TILES = [  # (top colour, bottom colour) per app tile, row-major 3x3
    ((255, 122, 89), (232, 72, 85)), ((255, 196, 61), (247, 148, 29)), ((72, 219, 180), (20, 168, 148)),
    ((99, 179, 255), (58, 115, 235)), ((178, 120, 255), (124, 77, 230)), ((255, 110, 170), (219, 61, 129)),
    ((72, 219, 180), (20, 168, 148)), ((255, 122, 89), (232, 72, 85)), ((99, 179, 255), (58, 115, 235)),
]


def vgrad(w, h, top, bottom):
    g = Image.new("RGB", (1, h))
    for y in range(h):
        t = y / max(1, h - 1)
        g.putpixel((0, y), tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
    return g.resize((w, h))


def rounded(w, h, r):
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, w - 1, h - 1], radius=r, fill=255)
    return m


def shadow(base, box, r, blur, alpha, offset=(0, 18)):
    sh = Image.new("L", base.size, 0)
    x0, y0, x1, y1 = box
    ImageDraw.Draw(sh).rounded_rectangle([x0 + offset[0], y0 + offset[1], x1 + offset[0], y1 + offset[1]], radius=r,
                                         fill=alpha)
    sh = sh.filter(ImageFilter.GaussianBlur(blur))
    base.paste((0, 0, 0), (0, 0), sh)


def tile(base, x, y, size, colours, r):
    shadow(base, (x, y, x + size, y + size), r, 14, 110, (0, 10))
    t = vgrad(size, size, *colours)
    base.paste(t, (x, y), rounded(size, size, r))
    # soft top highlight
    hl = Image.new("L", (size, size), 0)
    ImageDraw.Draw(hl).rounded_rectangle([0, 0, size - 1, size // 2], radius=r, fill=46)
    hl = ImageChops.multiply(hl.filter(ImageFilter.GaussianBlur(18)), rounded(size, size, r))
    base.paste((255, 255, 255), (x, y), hl)


def draw() -> Image.Image:
    img = vgrad(S, S, (28, 32, 58), (10, 12, 24)).convert("RGB")
    # faint teal glow behind the lens
    glow = Image.new("L", (S, S), 0)
    ImageDraw.Draw(glow).ellipse([360, 300, 900, 840], fill=120)
    img.paste((20, 168, 148), (0, 0), glow.filter(ImageFilter.GaussianBlur(120)))

    size, gap = 200, 44
    ox = (S - (3 * size + 2 * gap)) // 2
    oy = ox - 20
    for i, c in enumerate(TILES):
        x, y = ox + (i % 3) * (size + gap), oy + (i // 3) * (size + gap)
        tile(img, x, y, size, c, 52)

    # Magnifier centred on the middle-right tile, lifting it larger through the lens.
    cx, cy, R, ring = 610, 560, 210, 46
    hx, hy = cx + R * 0.72, cy + R * 0.72
    d = ImageDraw.Draw(img)
    sh = Image.new("L", (S, S), 0)
    sd = ImageDraw.Draw(sh)
    sd.ellipse([cx - R - ring // 2, cy - R - ring // 2 + 26, cx + R + ring // 2, cy + R + ring // 2 + 26], fill=150)
    sd.line([(hx, hy + 26), (hx + 160, hy + 186)], fill=150, width=96)
    img.paste((0, 0, 0), (0, 0), sh.filter(ImageFilter.GaussianBlur(26)))

    # lens contents: dark glass with the purple tile magnified
    lens = Image.new("L", (S, S), 0)
    ImageDraw.Draw(lens).ellipse([cx - R, cy - R, cx + R, cy + R], fill=255)
    img.paste((16, 20, 38), (0, 0), lens)
    big = 250
    t = vgrad(big, big, *TILES[4])
    img.paste(t, (cx - big // 2, cy - big // 2), ImageChops.multiply(rounded(big, big, 66), lens.crop(
        (cx - big // 2, cy - big // 2, cx + big // 2, cy + big // 2))))
    # little download arrow on the lifted tile
    ax, ay = cx, cy
    d.line([(ax, ay - 62), (ax, ay + 40)], fill=(255, 255, 255), width=30)
    d.polygon([(ax - 62, ay + 4), (ax + 62, ay + 4), (ax, ay + 70)], fill=(255, 255, 255))
    d.rounded_rectangle([ax - 70, ay + 84, ax + 70, ay + 104], radius=10, fill=(255, 255, 255))
    # glass sheen
    sheen = Image.new("L", (S, S), 0)
    ImageDraw.Draw(sheen).ellipse([cx - R * 0.8, cy - R * 0.9, cx + R * 0.2, cy - R * 0.1], fill=60)
    img.paste((255, 255, 255), (0, 0), ImageChops.multiply(sheen.filter(ImageFilter.GaussianBlur(20)), lens))

    # ring + handle
    d.line([(hx, hy), (hx + 160, hy + 160)], fill=(232, 238, 248), width=86)
    d.line([(hx + 70, hy + 70), (hx + 160, hy + 160)], fill=(20, 168, 148), width=86)
    d.ellipse([hx + 120, hy + 120, hx + 202, hy + 202], fill=(20, 168, 148))
    d.ellipse([cx - R - ring // 2, cy - R - ring // 2, cx + R + ring // 2, cy + R + ring // 2],
              outline=(240, 244, 250), width=ring)
    return img


def main() -> None:
    art = draw()
    art.resize((256, 256), Image.LANCZOS).save(ROOT / "icon.jpg", "JPEG", quality=95)
    logo = art.resize((128, 128), Image.LANCZOS).convert("RGBA")
    logo.putalpha(rounded(128, 128, 28))
    (ROOT / "romfs").mkdir(exist_ok=True)
    logo.save(ROOT / "romfs" / "logo.png", optimize=True)


if __name__ == "__main__":
    main()
