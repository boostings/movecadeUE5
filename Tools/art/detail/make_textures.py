"""Generates the vehicle detail textures in Tools/art/detail/textures (any Python 3 with numpy).

    python Tools/art/detail/make_textures.py

Everything is drawn procedurally from fixed seeds, so there are no third-party images and a rerun gives the
same bytes. Normal maps are DirectX style (green down, what UE expects), 8-bit, tangent space.

  flake_n      metallic-paint flakes: a jittered cell field, each flake tilted at random (paint, UV1)
  carbon_c     2x2 twill carbon weave, colour (UV1)
  carbon_n     the same weave's normal map (UV1)
  brushed_n    brushed-metal streaks, normal (UV1)
  brushed_r    the streaks as a roughness mask (linear, UV1)
  cast_n       fine sand-cast noise for engine blocks and calipers (UV1)
  seat_n       quilted seat channels with stitch rows (UV0, metre box projection)
  seat_c       the same quilt's colour mask: stitches light, channels shaded (UV0)
  tyre_n       tyre sidewall rings and tread sipes; U wraps the tyre, V runs bead to bead (UV0)
  lens_n       a prismatic headlamp lens: concentric Fresnel rings over a cell grid (UV0, 0..1 on the lens)
  roundel_c    the racing roundel: a white disc, a dark ring and an original star-bolt emblem (UV0)
  plastic_n    soft orange-peel noise for trim plastics and rubber (UV1)
"""
import os
import struct
import zlib

import numpy as np

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "textures")


def write_png(path, img):
    """img: HxW (grey), HxWx3 (RGB) or HxWx4 (RGBA) in [0, 1]."""
    a = np.clip(np.round(img * 255.0), 0, 255).astype(np.uint8)
    h, w = a.shape[:2]
    color_type = 0 if a.ndim == 2 else {3: 2, 4: 6}[a.shape[2]]
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
    y, x = np.mgrid[0:n, 0:n].astype(np.float64)
    return (x + 0.5) / n, (y + 0.5) / n


def value_noise(n, cells, rng, octaves=1, persistence=0.5):
    """Tileable fractal value noise in [0, 1]."""
    u, v = grid(n)
    total = np.zeros((n, n))
    amp, norm = 1.0, 0.0
    c = cells
    for _ in range(octaves):
        lattice = rng.random((c, c))
        x, y = u * c, v * c
        x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
        fx, fy = x - x0, y - y0
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        a = lattice[y0 % c, x0 % c]
        b = lattice[y0 % c, (x0 + 1) % c]
        cc = lattice[(y0 + 1) % c, x0 % c]
        d = lattice[(y0 + 1) % c, (x0 + 1) % c]
        total += amp * ((a * (1 - fx) + b * fx) * (1 - fy) + (cc * (1 - fx) + d * fx) * fy)
        norm += amp
        amp *= persistence
        c *= 2
    return total / norm


def height_to_normal(h, strength):
    """Tileable central differences, DirectX convention (what UE expects): rows run down the image, and a
    slope rising toward the bottom of the image tilts the normal toward the top (green below 0.5)."""
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5
    nx, ny, nz = -dx * strength, -dy * strength, np.ones_like(h)
    return pack_normal(nx, ny, nz)


def pack_normal(nx, ny, nz):
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.stack([nx / length * 0.5 + 0.5, ny / length * 0.5 + 0.5, nz / length * 0.5 + 0.5], axis=-1)


# --------------------------------------------------------------------------------------------------
def flake():
    """Jittered flakes, 48 per side: each is a cell of one random tilt, so the clear coat sparkles."""
    n, cells = 512, 48
    rng = np.random.default_rng(1101)
    u, v = grid(n)
    jitter = rng.random((cells, cells, 2)) * 0.8 + 0.1
    tilt = rng.normal(0.0, 0.35, (cells, cells, 2))
    x, y = u * cells, v * cells
    ix, iy = np.floor(x).astype(int), np.floor(y).astype(int)
    best = np.full((n, n), 9.0)
    nx, ny = np.zeros((n, n)), np.zeros((n, n))
    # Nearest jittered point among the 3x3 neighbours (Voronoi): flakes are irregular polygons.
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            cx, cy = (ix + ox) % cells, (iy + oy) % cells
            px = ix + ox + jitter[cy, cx, 0]
            py = iy + oy + jitter[cy, cx, 1]
            d = (x - px) ** 2 + (y - py) ** 2
            closer = d < best
            best = np.where(closer, d, best)
            nx = np.where(closer, tilt[cy, cx, 0], nx)
            ny = np.where(closer, tilt[cy, cx, 1], ny)
    return pack_normal(nx, ny, np.ones((n, n)))


def carbon():
    """2x2 twill: tows alternate over two, under two, stepping one each row; 8 tows per tile."""
    n, tows = 512, 8
    rng = np.random.default_rng(2202)
    u, v = grid(n)
    x, y = u * tows, v * tows
    ix, iy = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - ix, y - iy
    warp_on_top = ((ix + iy) % 4) < 2
    # Each tow is a rounded ridge across its width, with fine fibres along its length.
    ridge_w = np.sin(np.pi * fx) ** 0.6  # warp tow runs along v: its profile is across u
    ridge_f = np.sin(np.pi * fy) ** 0.6
    fibres_w = value_noise(n, 64, rng, 2) * 0.15
    fibres_f = value_noise(n, 64, rng, 2) * 0.15
    h = np.where(warp_on_top, ridge_w + fibres_w * np.sin(np.pi * fx), ridge_f + fibres_f * np.sin(np.pi * fy))
    # Dip the tow ends where it dives under the crossing tow.
    along = np.where(warp_on_top, fy, fx)
    h *= 0.75 + 0.25 * np.sin(np.pi * np.clip(along, 0, 1)) ** 0.5
    normal = height_to_normal(h, 6.0)
    sheen = np.where(warp_on_top, 0.55 + 0.45 * ridge_w, 0.35 + 0.35 * ridge_f)
    base = 0.035 + 0.05 * sheen + 0.012 * value_noise(n, 16, rng, 3)
    colour = np.stack([base * 0.95, base * 0.98, base * 1.06], axis=-1)
    return colour, normal


def brushed():
    n = 512
    rng = np.random.default_rng(3303)
    # Streaks along u: noise stretched 1:64.
    streak = np.zeros((n, n))
    for rows, amp in ((512, 0.5), (256, 0.3), (128, 0.2)):
        lane = rng.random(rows)
        lane = np.repeat(lane, n // rows)
        drift = value_noise(n, 4, rng, 2)
        streak += amp * (lane[:, None] * 0.8 + drift * 0.2)
    normal = height_to_normal(streak, 1.4)
    rough = 0.75 + 0.25 * (streak - streak.min()) / (streak.max() - streak.min())
    return normal, rough


def cast():
    rng = np.random.default_rng(4404)
    h = value_noise(256, 32, rng, 4, 0.55)
    return height_to_normal(h, 3.0)


def plastic():
    rng = np.random.default_rng(5505)
    h = value_noise(256, 24, rng, 3, 0.5)
    return height_to_normal(h, 1.2)


def seat():
    """Horizontal channels, 4 per tile (a 25 cm tile at the default 4x scale), each with a stitch row."""
    n, channels = 512, 4
    u, v = grid(n)
    y = v * channels
    f = y - np.floor(y)
    pad = np.sin(np.pi * f) ** 0.35  # puffy channels
    seam = smoothstep(0.0, 0.06, f) * smoothstep(0.0, 0.06, 1.0 - f)
    h = pad * seam
    # Stitches: dashes 6 mm long just inside each seam, on both sides.
    dash = (np.floor(u * 64) % 2) == 0
    stitch_band = (np.abs(f - 0.12) < 0.018) | (np.abs(f - 0.88) < 0.018)
    stitches = (dash & stitch_band).astype(np.float64)
    h = h - stitches * 0.08
    normal = height_to_normal(h, 5.0)
    shade = 0.82 + 0.18 * pad * seam
    mask = np.clip(shade, 0, 1)
    colour = np.stack([mask, mask, mask], axis=-1)
    colour = np.where(stitches[:, :, None] > 0, np.array([1.0, 0.86, 0.45]), colour)  # warm contrast thread
    return colour, normal


def tyre():
    """U: 32 repeats around the tyre carry fine sipes on the crown; V: bead (0) .. crown (0.5) .. bead (1)."""
    n = 512
    u, v = grid(n)
    side = np.abs(v - 0.5) * 2.0  # 0 at the crown, 1 at the bead
    # Sidewall: concentric rings and a band of radial ribs.
    rings = np.where(side > 0.55, 0.5 + 0.5 * np.sin(side * 140.0), 0.5)
    ribs = np.where((side > 0.62) & (side < 0.78), (np.sin(u * np.pi * 2 * 96) > 0.6) * 0.6, 0.0)
    # Crown sipes: thin zig-zag cuts.
    zig = np.abs(((u * 4.0 + side * 1.5) % 1.0) - 0.5)
    sipes = np.where(side < 0.35, (zig < 0.035) * -0.8, 0.0)
    h = rings * 0.25 + ribs * 0.4 + sipes
    return height_to_normal(h, 4.0)


def lens():
    n = 256
    u, v = grid(n)
    r = np.sqrt((u - 0.5) ** 2 + (v - 0.5) ** 2) * 2.0
    fresnel = (r * 9.0) % 1.0  # sawtooth rings
    cells = (np.sin(u * np.pi * 2 * 10) * np.sin(v * np.pi * 2 * 10)) * 0.25
    h = fresnel * 0.6 + cells * (r > 0.45)
    return height_to_normal(h, 2.5)


def roundel():
    """An original emblem: a five-point star with a lightning notch, on a white disc with a dark ring."""
    n = 512
    u, v = grid(n)
    x, y = (u - 0.5) * 2.0, (0.5 - v) * 2.0
    r = np.sqrt(x * x + y * y)
    a = np.arctan2(y, x)
    ring = r > 0.80  # out to the decal's edge (the decal disc spans r <= 1)
    disc = r <= 0.80
    # Star: radius modulated between inner and outer at 5 points (point up).
    k = 5
    t = ((a - np.pi / 2) % (2 * np.pi / k)) / (2 * np.pi / k)
    tri = np.abs(t - 0.5) * 2.0  # 1 at the points, 0 between
    star_r = 0.30 + 0.36 * tri ** 1.6
    star = r < star_r
    # A diagonal bolt cut through the star.
    s = (x * 0.8 + y * 0.6)
    bolt = (np.abs(s - 0.06 * np.sign(y + 0.05)) < 0.045) & (r < 0.6)
    emblem = star & ~bolt
    white = np.array([0.96, 0.95, 0.92])
    dark = np.array([0.06, 0.07, 0.12])
    gold = np.array([1.0, 0.72, 0.12])
    img = np.ones((n, n, 3)) * white
    img = np.where(ring[:, :, None], dark, img)
    img = np.where((disc & emblem)[:, :, None], gold, img)
    # Antialias the ring edges a little.
    edge = smoothstep(0.0, 0.006, np.abs(r - 0.80))
    img = img * (0.75 + 0.25 * edge[:, :, None])
    # A thin light pinstripe inside the ring.
    pin = np.abs(r - 0.90) < 0.012
    img = np.where(pin[:, :, None], np.array([0.85, 0.85, 0.88]), img)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    write_png(os.path.join(OUT, "flake_n.png"), flake())
    c, nrm = carbon()
    write_png(os.path.join(OUT, "carbon_c.png"), c)
    write_png(os.path.join(OUT, "carbon_n.png"), nrm)
    nrm, rough = brushed()
    write_png(os.path.join(OUT, "brushed_n.png"), nrm)
    write_png(os.path.join(OUT, "brushed_r.png"), rough)
    write_png(os.path.join(OUT, "cast_n.png"), cast())
    write_png(os.path.join(OUT, "plastic_n.png"), plastic())
    c, nrm = seat()
    write_png(os.path.join(OUT, "seat_c.png"), c)
    write_png(os.path.join(OUT, "seat_n.png"), nrm)
    write_png(os.path.join(OUT, "tyre_n.png"), tyre())
    write_png(os.path.join(OUT, "lens_n.png"), lens())
    write_png(os.path.join(OUT, "roundel_c.png"), roundel())
    print("wrote", sorted(os.listdir(OUT)))


if __name__ == "__main__":
    main()
