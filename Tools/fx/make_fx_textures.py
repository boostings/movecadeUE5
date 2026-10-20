"""Generates the FX source textures in Tools/fx/textures (run with any Python 3 that has numpy).

    python Tools/fx/make_fx_textures.py

Everything is drawn procedurally, so there are no third-party images:
- question.png  the item box "?" glyph: bold white, a warm outline and a soft drop shadow (RGBA),
- star.png      a four-point twinkle with a soft core (grayscale, used as alpha),
- noise.png     tileable fractal value noise (grayscale), for puff erosion and fireball breakup,
- chevron.png   a boost-pad chevron (grayscale), for the pad activation flash.
Tools/ue/make_fx_materials.py imports them into /Game/FX/Textures.
"""
import os
import struct
import zlib

import numpy as np

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "textures")


def write_png(path, img):
    """img: HxW (gray) or HxWx4 (RGBA) array in [0, 1]."""
    a = np.clip(np.round(img * 255.0), 0, 255).astype(np.uint8)
    h, w = a.shape[:2]
    color_type = 0 if a.ndim == 2 else 6
    raw = b"".join(b"\x00" + a[y].tobytes() for y in range(h))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, color_type, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def grid(n):
    """Pixel-centre coordinates in [-1, 1], y down."""
    c = (np.arange(n) + 0.5) / n * 2.0 - 1.0
    return np.meshgrid(c, c)


def question(n=512):
    """Signed distance to a chunky "?" made of an arc, a stem and a dot (in [-1, 1] units), then shaded."""
    x, y = grid(n)
    # Arc: ring of radius r around (0, -0.28), open at the lower left.
    cx, cy, r, w = 0.0, -0.30, 0.36, 0.13
    ang = np.arctan2(y - cy, x - cx)  # y down: -pi/2 is the top
    ring = np.abs(np.hypot(x - cx, y - cy) - r) - w
    # Keep the arc from the left (a0, just below the centre line) over the top to the lower right (a1).
    a0, a1 = np.pi * 0.92, np.pi * 0.30
    ring = np.where((ang < a1) | (ang > a0), ring, 10.0)
    # Round caps at both arc ends.
    caps = []
    for a in (a0, a1):
        px, py = cx + r * np.cos(a), cy + r * np.sin(a)
        caps.append(np.hypot(x - px, y - py) - w)
    # Stem: from the arc's lower-right end down to the middle.
    sx0, sy0 = cx + r * np.cos(a1), cy + r * np.sin(a1)
    sx1, sy1 = 0.0, 0.20
    vx, vy = sx1 - sx0, sy1 - sy0
    t = np.clip(((x - sx0) * vx + (y - sy0) * vy) / (vx * vx + vy * vy), 0.0, 1.0)
    stem = np.hypot(x - (sx0 + t * vx), y - (sy0 + t * vy)) - w
    dot = np.hypot(x, y - 0.66) - w * 1.12
    d = np.minimum.reduce([ring, caps[0], caps[1], stem, dot])
    px = 2.0 / n
    fill = smoothstep(px, -px, d)
    outline = smoothstep(px, -px, d - 0.075)
    dy, dx = int(0.035 * n), int(0.02 * n)
    shifted = np.full_like(d, 10.0)
    shifted[dy:, dx:] = d[:-dy, :-dx]
    shadow = smoothstep(0.10, -0.02, shifted - 0.075)
    # Fill: white with a slight top-to-bottom warm gradient; outline: deep amber.
    top = np.clip((y + 1.0) * 0.5, 0.0, 1.0)
    fr = 1.0
    fg = 1.0 - 0.06 * top
    fb = 0.96 - 0.22 * top
    orr, og, ob = 0.55, 0.22, 0.02
    rgb = np.zeros((n, n, 3))
    rgb[..., 0] = orr * outline + (fr - orr) * fill
    rgb[..., 1] = og * outline + (fg - og) * fill
    rgb[..., 2] = ob * outline + (fb - ob) * fill
    alpha = np.maximum(outline, shadow * 0.45)
    # Premultiplied shadow colour is black, so un-premultiply the outline colour where only shadow shows.
    cov = np.maximum(outline, 1e-4)
    rgb = rgb / cov[..., None] * (outline[..., None] > 0.001)
    return np.dstack([np.clip(rgb, 0, 1), alpha])


def star(n=256):
    x, y = grid(n)
    r = np.hypot(x, y)
    ang = np.arctan2(y, x)
    # Four long rays and four short diagonal ones.
    rays = np.abs(np.cos(2.0 * ang)) ** 60 * np.exp(-r * 3.2) + 0.45 * np.abs(np.cos(2.0 * ang + np.pi / 2)) ** 90 * np.exp(-r * 6.0)
    core = np.exp(-(r / 0.16) ** 2)
    halo = np.exp(-(r / 0.42) ** 2) * 0.35
    v = np.clip(rays * 1.2 + core + halo, 0.0, 1.0) * smoothstep(1.0, 0.85, r)
    return v


def value_noise(n=256, octaves=5, seed=7):
    rng = np.random.default_rng(seed)
    out = np.zeros((n, n))
    amp, total = 1.0, 0.0
    for o in range(octaves):
        cells = 4 * (2 ** o)
        lattice = rng.random((cells, cells))
        coords = (np.arange(n) / n) * cells
        i0 = np.floor(coords).astype(int)
        f = coords - i0
        f = f * f * (3.0 - 2.0 * f)
        i1 = (i0 + 1) % cells
        a = lattice[np.ix_(i0, i0)]
        b = lattice[np.ix_(i0, i1)]
        c = lattice[np.ix_(i1, i0)]
        d = lattice[np.ix_(i1, i1)]
        fy, fx = np.meshgrid(f, f, indexing="ij")
        layer = (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy
        out += layer * amp
        total += amp
        amp *= 0.5
    out /= total
    return (out - out.min()) / (out.max() - out.min())


def chevron(n=256):
    x, y = grid(n)
    # Two stacked chevrons pointing up (-y, the top of the image), each a thick inverted V.
    v = np.zeros_like(x)
    for oy in (-0.55, -0.05):
        d = np.abs((y - oy) - np.abs(x) * 0.9) - 0.16
        band = smoothstep(0.02, -0.02, d) * smoothstep(0.02, -0.02, np.abs(x) - 0.82)
        v = np.maximum(v, band)
    return v


def main():
    os.makedirs(OUT, exist_ok=True)
    write_png(os.path.join(OUT, "question.png"), question())
    write_png(os.path.join(OUT, "star.png"), star())
    write_png(os.path.join(OUT, "noise.png"), value_noise())
    write_png(os.path.join(OUT, "chevron.png"), chevron())
    print("wrote", OUT)


if __name__ == "__main__":
    main()
