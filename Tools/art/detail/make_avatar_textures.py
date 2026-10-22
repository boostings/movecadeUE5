"""Generates the avatar face detail textures in Tools/art/detail/textures (any Python 3 with numpy).

    python Tools/art/detail/make_avatar_textures.py

Drawn procedurally from fixed seeds like the vehicle maps (make_textures.py, whose PNG writer and noise this
reuses), so there are no third-party images and a rerun gives the same bytes. Imported by
Tools/ue/import_karts.py (the avatarmaterials stage) into /Game/Avatars/Materials/Textures.

  skin_pores_n   tileable skin micro-normal: jittered pores over soft micro-bumps and faint crossing lines
                 (DirectX style, tiled many times over the face UVs; the master fades it with distance)
  iris_c         one iris, centred in the square: R the stroma's radial fibres and crypts (luminance, 0.5
                 mean), G the collarette mask (the warmer ring round the pupil), B the limbal darkening
"""
import os

import numpy as np

from make_textures import OUT, grid, height_to_normal, smoothstep, value_noise, write_png


def skin_pores():
    """Pores: shallow round wells on a jittered grid (Voronoi-jittered, two sizes), over fractal micro-bumps and
    two faint families of fine crossing lines (the skin's own grain). Tileable."""
    n, cells = 512, 44
    rng = np.random.default_rng(5150)
    u, v = grid(n)
    h = np.zeros((n, n))
    for count, depth, radius in ((cells, 1.0, 0.22), (cells // 2 + 3, 0.6, 0.3)):
        jitter = rng.random((count, count, 2)) * 0.7 + 0.15
        size = rng.uniform(0.6, 1.3, (count, count))
        x, y = u * count, v * count
        ix, iy = np.floor(x).astype(int), np.floor(y).astype(int)
        for oy in (-1, 0, 1):
            for ox in (-1, 0, 1):
                cx, cy = (ix + ox) % count, (iy + oy) % count
                d2 = (x - (ix + ox + jitter[cy, cx, 0])) ** 2 + (y - (iy + oy + jitter[cy, cx, 1])) ** 2
                r = radius * size[cy, cx]
                h -= depth * np.exp(-d2 / (r * r))
    bumps = value_noise(n, 32, rng, octaves=3, persistence=0.55) - 0.5
    lines = np.zeros((n, n))
    # Integer wave vectors, so whole periods fit the tile and the lines wrap without a seam.
    for kx, ky in ((41, 56), (-49, 30)):
        wobble = value_noise(n, 8, rng, octaves=2) * 3.0
        phase = u * kx + v * ky + wobble
        lines += smoothstep(0.92, 1.0, np.cos(phase * np.pi * 2.0)) * smoothstep(0.4, 0.6, value_noise(n, 6, rng))
    height = h * 0.9 + bumps * 0.5 - lines * 0.18
    return height_to_normal(height, 2.6)


def iris():
    """A stroma of radial fibres (noise stretched along the radius, wavy), darker crypts, a raised collarette ring
    at about 40 % of the radius, and the darker limbus at the rim. Outside the disc is the plain mean value."""
    n = 256
    rng = np.random.default_rng(7331)
    u, v = grid(n)
    x, y = u * 2.0 - 1.0, v * 2.0 - 1.0
    r = np.sqrt(x * x + y * y)
    theta = np.arctan2(y, x)
    # Polar noise: sample a tileable noise field at (angle, radius) so fibres run outward.
    fine = value_noise(n, 64, rng, octaves=2)
    coarse = value_noise(n, 12, rng, octaves=2)
    a = ((theta / (2.0 * np.pi)) % 1.0)
    ia = np.clip((a * n).astype(int), 0, n - 1)
    ir = np.clip((r * 0.18 * n).astype(int), 0, n - 1)
    wave = np.sin(r * 9.0 + coarse[ia, ir] * 6.0) * 0.012
    ia2 = np.clip((((a + wave) % 1.0) * n).astype(int), 0, n - 1)
    fibres = fine[ir, ia2] * 0.65 + coarse[ir // 3, ia2] * 0.35
    # Crypts and the collarette stay faint: strong rings read as a target, not an eye.
    crypts = smoothstep(0.62, 0.8, value_noise(n, 20, rng, octaves=2)) * smoothstep(0.3, 0.5, r) * (1.0 - smoothstep(0.75, 0.9, r))
    collarette = np.exp(-((r - 0.42) / 0.1) ** 2)
    stroma = 0.5 + (fibres - 0.5) * 1.5 + collarette * 0.06 - crypts * 0.12
    stroma = np.clip(stroma, 0.0, 1.0)
    inner = 1.0 - smoothstep(0.32, 0.55, r)
    limbus = smoothstep(0.78, 1.0, r)
    inside = r <= 1.0
    out = np.stack([np.where(inside, stroma, 0.5), np.where(inside, inner, 0.0), np.where(inside, limbus, 1.0)], axis=-1)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, make in (("skin_pores_n", skin_pores), ("iris_c", iris)):
        write_png(os.path.join(OUT, f"{name}.png"), make())
        print(f"wrote {name}.png")


if __name__ == "__main__":
    main()
