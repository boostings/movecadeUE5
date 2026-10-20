"""Procedural wheels: a tyre with real tread blocks and sidewall, a spoked rim, hub, lug nuts, a centre cap with
the roundel emblem, and a brake disc behind the spokes.

Contract (KartPawn::ApplyVehicle, KartFxComponent::RefreshAnchors): each wheel mesh spins about its bounding-box
centre, and the FX take the radius as max(extent Y, extent Z). So every wheel is rotationally periodic with an
even number of repeats (its box stays centred on the hub), the tread reaches the original radius, and nothing
pokes out past the tyre's own sidewalls.
"""
import math

import bmesh
from mathutils import Matrix

import kit
from kit import TAU, V


class WheelStyle:
    def __init__(self, **kw):
        self.rim = kw.get("rim", 0.62)               # rim radius / tyre radius
        self.tread = kw.get("tread", "blocks")       # blocks | lugs | road | moto
        self.tread_depth = kw.get("tread_depth", 0.03)
        self.blocks = kw.get("blocks", 24)           # per row; even
        self.spokes = kw.get("spokes", 6)
        self.spoke_style = kw.get("spoke_style", "twin")  # twin | single | y | mesh
        self.lugs = kw.get("lugs", 5)
        self.beadlock = kw.get("beadlock", False)
        self.segments = kw.get("segments", 64)       # multiple of 4
        self.disc = kw.get("disc", True)
        self.disc_side = kw.get("disc_side", -1)     # -1 inboard, +1 just behind the spokes (moto)


def _tread_block(part, M, x0, x1, s_len, h0, h1, theta, r_base, skew=0.0, bevel=0.006):
    """One rubber block between axial x0..x1, s_len long around the tyre, h0..h1 above r_base, wrapped onto
    the carcass (its faces curve with the tyre), then placed by M."""
    res = bmesh.ops.create_cube(part.bm, size=1.0)
    verts = res["verts"]
    for v in verts:
        x = x0 + (v.co.x + 0.5) * (x1 - x0)
        s = v.co.y * s_len + skew * (x - (x0 + x1) / 2)
        h = h0 + (v.co.z + 0.5) * (h1 - h0)
        v.co = V(x, s, h)
    if bevel > 0:
        edges = list({e for v in verts for e in v.link_edges})
        out = bmesh.ops.bevel(part.bm, geom=verts + edges, offset=bevel, offset_type="OFFSET", segments=1,
                              profile=0.5, affect="EDGES", clamp_overlap=True)
        verts = [v for v in set(verts) | set(out.get("verts", [])) if v.is_valid]
    for v in verts:
        x, s, h = v.co
        a = theta + s / r_base
        r = r_base + h
        v.co = V(x, r * math.cos(a), r * math.sin(a))
    bmesh.ops.transform(part.bm, matrix=M, verts=verts)
    faces = part.faces_of(verts)
    if M.determinant() < 0:
        bmesh.ops.reverse_faces(part.bm, faces=faces)
    kit.box_uv(faces, part.uv0, 8.0)
    return part.finish(faces, "m_Tyre")


def _carcass_profile(hw, rr, Rc, tread):
    """The tyre's cross-section (axial x, radius), walked inboard -> outboard so its outside is visible."""
    side_h = Rc - rr
    if tread == "moto":
        base = rr + side_h * 0.45
        prof = [(-hw * 0.8, rr - 0.01), (-hw * 0.95, rr + side_h * 0.2)]
        n = 12
        for k in range(n + 1):
            phi = math.pi * k / n
            prof.append((-hw * math.cos(phi), base + (Rc - base) * math.sin(phi)))
        prof += [(hw * 0.95, rr + side_h * 0.2), (hw * 0.8, rr - 0.01)]
        return prof
    sh = min(0.035, side_h * 0.3, hw * 0.35)  # shoulder radius
    left = [(-hw * 0.80, rr - 0.01), (-hw * 0.93, rr + side_h * 0.18), (-hw, rr + side_h * 0.5),
            (-hw, Rc - sh * 1.4)]
    for k in range(5):
        a = (math.pi / 2) * k / 4
        left.append((-hw + sh - sh * math.cos(a), Rc - sh + sh * math.sin(a)))
    if tread == "road":
        for gx in (-0.5 * hw,):
            left += [(gx - 0.012, Rc), (gx - 0.008, Rc - 0.008), (gx + 0.008, Rc - 0.008), (gx + 0.012, Rc)]
        left += [(-0.012, Rc), (-0.008, Rc - 0.008)]
    right = [(-x, r) for (x, r) in reversed(left)]
    return left + right


def build_wheel(part, centre, radius, width, outward, style):
    """Writes a whole wheel into `part` (an emptied wheel node). outward: +1 if the wheel's outer face is +X."""
    R, hw, s = radius, width / 2, style
    rr = R * s.rim
    td = s.tread_depth if s.tread in ("blocks", "lugs") else 0.0
    Rc = R - td
    seg = s.segments
    M = Matrix.Translation(centre) @ Matrix.Diagonal((outward, 1.0, 1.0, 1.0))
    start = len(part.new_faces)

    # ---- Tyre.
    kit.lathe(part, "m_Tyre", _carcass_profile(hw, rr, Rc, s.tread), seg, M, u_repeat=8.0)
    if s.tread == "blocks":
        n = s.blocks
        pitch = TAU * Rc / n
        for row, (x0, x1) in enumerate(((-hw * 0.86, -hw * 0.06), (hw * 0.06, hw * 0.86))):
            for i in range(n):
                theta = TAU * i / n + (math.pi / n if row else 0.0)
                _tread_block(part, M, x0, x1, pitch * 0.5, -0.02, td, theta, Rc,
                             skew=0.55 if row == 0 else -0.55, bevel=min(0.006, td * 0.3))
    elif s.tread == "lugs":
        n = s.blocks
        pitch = TAU * Rc / n
        for i in range(n):
            theta = TAU * i / n
            side = 1 if i % 2 == 0 else -1
            x0, x1 = (-hw * 0.05, hw * 0.96) if side > 0 else (-hw * 0.96, hw * 0.05)
            _tread_block(part, M, x0, x1, pitch * 0.42, -0.07, td, theta, Rc, skew=0.25 * side,
                         bevel=min(0.012, td * 0.3))
            _tread_block(part, M, -hw * 0.16, hw * 0.16, pitch * 0.2, -0.02, td * 0.8, theta + math.pi / n, Rc,
                         bevel=0.006)

    # ---- Rim barrel: the inner surface seen through the spokes, the outboard lip and flange.
    lip = hw * 0.84
    barrel = [(lip - 0.03, rr + 0.004), (lip - 0.012, rr + 0.016), (lip + 0.004, rr + 0.012),
              (lip + 0.006, rr - 0.004), (lip, rr - 0.022), (lip - 0.012, rr - 0.03), (lip - 0.03, rr - 0.014),
              (-hw * 0.78, rr - 0.014), (-hw * 0.78, rr - 0.004)]
    kit.lathe(part, "m_Rim", barrel, seg, M)
    if s.beadlock:
        ring_r = rr + 0.004
        bx = lip - 0.004
        kit.lathe(part, "m_Accent", [(bx, ring_r - 0.024), (bx + 0.014, ring_r - 0.02), (bx + 0.016, ring_r + 0.026),
                                    (bx, ring_r + 0.03)], seg, M)
        for i in range(16):
            a = TAU * i / 16
            p = V(bx + 0.018, ring_r * math.cos(a), ring_r * math.sin(a))
            kit.cylinder(part, "m_Chrome", 0.008, 0.01, M @ Matrix.Translation(p), segments=6, bevel=0.003)

    # ---- Spokes, from the hub out to the barrel, dished toward the outer face.
    hub_r = rr * 0.30
    face_x = lip - 0.035
    spoke_t = max(0.016, min(0.03, rr * 0.09))
    n = s.spokes
    for i in range(n):
        a = TAU * i / n + math.pi / n
        if s.spoke_style == "twin":
            angles, w = (a - 0.1, a + 0.1), rr * 0.09
        elif s.spoke_style == "mesh":
            angles, w = (a,), rr * 0.07
        elif s.spoke_style == "y":
            angles, w = (a,), rr * 0.15
        else:
            angles, w = (a,), rr * 0.17
        for b in angles:
            d = V(0, math.cos(b), math.sin(b))
            t = V(0, -math.sin(b), math.cos(b))
            inner = V(face_x + 0.012, 0, 0) + d * hub_r * 0.8
            reach = 0.62 if s.spoke_style == "y" else 1.0
            outer = V(face_x + 0.012 - 0.016 * reach, 0, 0) + d * (hub_r * 0.8 + (rr - 0.016 - hub_r * 0.8) * reach)
            kit.box(part, "m_Rim", (spoke_t, (outer - inner).length, w), M @ kit.frame((inner + outer) / 2, y=outer - inner, z=t),
                    bevel=spoke_t * 0.35, segments=2)
            if s.spoke_style == "y":
                for sgn in (-1, 1):
                    fork = d * math.cos(0.2) + t * sgn * math.sin(0.2)
                    o2 = V(face_x - 0.004, 0, 0) + fork * (rr - 0.016)
                    i2 = outer - d * 0.01
                    kit.box(part, "m_Rim", (spoke_t * 0.9, (o2 - i2).length, w * 0.55),
                            M @ kit.frame((o2 + i2) / 2, y=o2 - i2, z=V(1, 0, 0).cross(o2 - i2)),
                            bevel=spoke_t * 0.3, segments=2)
        if s.spoke_style == "mesh":
            a2 = a + TAU / n
            dirs = (V(0, math.cos(a), math.sin(a)), V(0, math.cos(a2), math.sin(a2)))
            for (da, db) in ((dirs[0], dirs[1]), (dirs[1], dirs[0])):
                pa = V(face_x - 0.002, 0, 0) + da * (rr * 0.6)
                pb = V(face_x - 0.006, 0, 0) + db * (rr * 0.9)
                kit.box(part, "m_Rim", (spoke_t * 0.7, (pb - pa).length, rr * 0.045),
                        M @ kit.frame((pa + pb) / 2, y=pb - pa, z=V(1, 0, 0).cross(pb - pa)),
                        bevel=spoke_t * 0.2, segments=1)

    # ---- Hub, lug nuts and the centre cap with the emblem.
    kit.cylinder(part, "m_Hub", hub_r, 0.05, M @ Matrix.Translation(V(face_x - 0.005, 0, 0)), segments=24, bevel=0.008)
    for i in range(s.lugs):
        a = TAU * i / s.lugs + math.pi / 2
        p = V(face_x + 0.022, hub_r * 0.62 * math.cos(a), hub_r * 0.62 * math.sin(a))
        kit.cylinder(part, "m_Chrome", hub_r * 0.13, 0.016, M @ Matrix.Translation(p), segments=6, bevel=0.002)
    cap_r = hub_r * 0.42
    cap_x = face_x + 0.02
    kit.sphere(part, "m_Chrome", cap_r, M @ Matrix.Translation(V(cap_x, 0, 0)), segments=16, rings=6, squash=0.45)
    emblem = M @ Matrix.Translation(V(cap_x + cap_r * 0.45 + 0.0015, 0, 0))
    if outward < 0:
        emblem = emblem @ Matrix.Diagonal((1.0, -1.0, 1.0, 1.0))  # keep the emblem reading the right way round
    kit.disc(part, "m_Roundel", cap_r * 0.8, emblem, segments=20)

    # ---- Brake disc with vent slots, and its hat back to the hub.
    if s.disc:
        dx = -hw * 0.25 if s.disc_side < 0 else face_x - 0.03
        d_in, d_out = rr * 0.42, rr * 0.80
        th = 0.012 if R > 0.25 else 0.006
        kit.lathe(part, "m_Disc", [(dx - th, d_in), (dx - th, d_out), (dx + th, d_out), (dx + th, d_in),
                                  (dx - th, d_in)], seg // 2, M)
        face = dx + th + 0.0006 if s.disc_side < 0 else dx - th - 0.0006
        for i in range(12):
            a = TAU * i / 12
            d = V(0, math.cos(a), math.sin(a))
            t = V(0, -math.sin(a), math.cos(a))
            p = V(face, 0, 0) + d * (d_in + d_out) / 2
            kit.box(part, "m_Hub", (0.0015, (d_out - d_in) * 0.7, 0.006), M @ kit.frame(p, y=d + t * 0.35, z=t))
        if s.disc_side < 0:
            kit.lathe(part, "m_Hub", [(dx + th, d_in + 0.004), (face_x - 0.03, hub_r * 0.95), (face_x - 0.03, 0.0)],
                      seg // 2, M)
    return [f for f in part.new_faces[start:] if f.is_valid]
