#!/usr/bin/env python3
"""Rebuild TIMBRE brand assets with a TAMBRA pixel-art wordmark.

Reads design/brand/wordmark.json (5x7 glyphs, whole-number scale, nearest-
neighbour only) and composites the word onto copies of the existing burst
art so og-image.png and timbre-hero.gif keep palette, size, and GIF timing.

Favicons have no wordmark (waveform mark). They are re-exported unchanged
from the committed sources so the pipeline still produces every AC path.

Usage (from repo root):
    python3 design/brand/render.py
"""
from __future__ import annotations

import json
import shutil
from pathlib import Path

from PIL import Image, ImageDraw, PngImagePlugin

ROOT = Path(__file__).resolve().parents[2]
BRAND = Path(__file__).resolve().parent
REFERENCE = BRAND / "reference"
PUBLIC = ROOT / "ui" / "public"
DOCS = ROOT / "docs"


def parse_hex(value: str) -> tuple[int, int, int, int]:
    h = value.strip().lstrip("#")
    if len(h) == 6:
        h += "ff"
    r, g, b, a = (int(h[i : i + 2], 16) for i in (0, 2, 4, 6))
    return r, g, b, a


def load_spec() -> dict:
    return json.loads((BRAND / "wordmark.json").read_text(encoding="utf-8"))


def render_wordmark(spec: dict) -> Image.Image:
    """TAMBRA as an RGBA strip, upscaled with Image.NEAREST only."""
    scale = int(spec["scale"])
    spacing = int(spec["letter_spacing"])
    glyphs = spec["letters"]
    word = spec["word"]
    gradient = [parse_hex(c) for c in spec["gradient"]]
    rows = len(next(iter(glyphs.values())))
    cols_per = len(next(iter(glyphs.values()))[0])
    width = len(word) * cols_per + spacing * (len(word) - 1)
    small = Image.new("RGBA", (width, rows), (0, 0, 0, 0))
    px = small.load()
    x = 0
    for ch in word:
        grid = glyphs[ch]
        if len(grid) != rows or any(len(r) != cols_per for r in grid):
            raise SystemExit(f"glyph {ch!r} must be {cols_per}x{rows}")
        for gy, row in enumerate(grid):
            color = gradient[min(gy, len(gradient) - 1)]
            for gx, cell in enumerate(row):
                if cell != ".":
                    px[x + gx, gy] = color
        x += cols_per + spacing
    return small.resize((width * scale, rows * scale), Image.NEAREST)


def letter_mask(src: Image.Image, bbox: tuple[int, int, int, int], bg: tuple[int, int, int, int]) -> list[tuple[int, int]]:
    """Pixels in bbox that look like the old TIMBRE wordmark (not bg / sparks)."""
    x0, y0, x1, y1 = bbox
    px = src.load()
    br, bg_, bb, _ = bg
    hits = []
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b, a = px[x, y]
            if a < 200:
                continue
            # skip near-background navy
            if abs(r - br) < 18 and abs(g - bg_) < 18 and abs(b - bb) < 28:
                continue
            lum = r + g + b
            # wordmark is saturated yellow/pink/violet, not dim scanlines
            if lum < 140:
                continue
            chroma = max(r, g, b) - min(r, g, b)
            if chroma < 40:
                continue
            hits.append((x, y))
    return hits


def erase_and_blit(base: Image.Image, mask: list[tuple[int, int]], word: Image.Image, origin: tuple[int, int], bg: tuple[int, int, int, int]) -> Image.Image:
    out = base.copy()
    draw = ImageDraw.Draw(out)
    for x, y in mask:
        draw.point((x, y), fill=bg)
    out.alpha_composite(word, dest=origin)
    return out


def save_png(img: Image.Image, path: Path) -> None:
    img.save(path, format="PNG", pnginfo=PngImagePlugin.PngInfo())


def render_og(spec: dict, word: Image.Image, mask: list[tuple[int, int]], bg: tuple[int, int, int, int]) -> None:
    src = Image.open(REFERENCE / "og-image.png").convert("RGBA")
    origin = tuple(spec["origin"])
    out = erase_and_blit(src, mask, word, origin, bg)
    save_png(out, PUBLIC / "og-image.png")
    print(f"wrote {PUBLIC / 'og-image.png'} {out.size}")


def render_gif(spec: dict, word: Image.Image, mask: list[tuple[int, int]], bg: tuple[int, int, int, int]) -> None:
    src_path = REFERENCE / "timbre-hero.gif"
    gif = Image.open(src_path)
    origin = tuple(spec["origin"])
    frames: list[Image.Image] = []
    durations: list[int] = []
    n = getattr(gif, "n_frames", 1)
    black = (0, 0, 0, 255)
    for i in range(n):
        gif.seek(i)
        durations.append(int(gif.info.get("duration", 100)))
        fr = gif.convert("RGBA")
        # GIF splash is true black; OG navy fill would stamp a rectangle.
        frames.append(erase_and_blit(fr, mask, word, origin, black))
    # Quantize via adaptive palette from the first composited frame; keep
    # 3.6s loop (36 x 100ms on the source).
    rgb_frames = [fr.convert("RGB") for fr in frames]
    pal = rgb_frames[0].convert("P", palette=Image.ADAPTIVE, colors=255)
    out_frames = [pal]
    for fr in rgb_frames[1:]:
        out_frames.append(fr.quantize(palette=pal, dither=Image.NONE))
    dest = PUBLIC / "timbre-hero.gif"
    out_frames[0].save(
        dest,
        format="GIF",
        save_all=True,
        append_images=out_frames[1:],
        duration=durations,
        loop=0,
        optimize=False,
        disposal=2,
    )
    shutil.copyfile(dest, DOCS / "timbre-hero.gif")
    total_ms = sum(durations)
    print(f"wrote {dest} {n} frames {total_ms}ms; copied docs/timbre-hero.gif")


def reexport_favicons() -> None:
    for name in ("favicon.png", "favicon-64.png"):
        src = REFERENCE / name
        dest = PUBLIC / name
        shutil.copyfile(src, dest)
        img = Image.open(dest)
        print(f"copied {dest} {img.size}")


def main() -> int:
    spec = load_spec()
    bg = parse_hex(spec["bg"])
    word = render_wordmark(spec)
    origin = tuple(spec["origin"])
    # Mask from the current og-image (TIMBRE letters) before we overwrite it.
    og = Image.open(REFERENCE / "og-image.png").convert("RGBA")
    mask = letter_mask(og, tuple(spec["erase_bbox"]), bg)
    if len(mask) < 200:
        raise SystemExit(f"wordmark mask too small ({len(mask)} px) — check erase_bbox")
    render_og(spec, word, mask, bg)
    render_gif(spec, word, mask, bg)
    reexport_favicons()
    print(f"wordmark {word.size} at {origin}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
