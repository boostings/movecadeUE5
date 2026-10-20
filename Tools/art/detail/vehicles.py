"""Per-vehicle detail passes. Each takes {node name: object} in the model frame (kit.py: +X left, -Y front,
+Z up, contact patch at z = -0.49) and returns report info.

Contracts kept (KartPawn.cpp ApplyVehicle / Mounts, KartFxComponent.cpp RefreshAnchors):
  * node names: body, trim, booster (kart), d_wheel / handlebar, wheel_0..3, the `seat` empty;
  * the body's only material is m_Body (the per-racer paint, slot 0);
  * the grip meshes (d_wheel, handlebar) are untouched, so the hands still land on the rim;
  * wheels are rebuilt centred on the original hub with the original radius and width;
  * exhaust tips sit exactly where RefreshAnchors measures the exhausts from the body's bounds.
"""
import math

import bmesh
from mathutils import Matrix, Vector

import kit
import wheels
from kit import TAU, V


def wheel_nodes(objs):
    return sorted(n for n in objs if n.startswith("wheel_"))


def wheel_frames(objs):
    out = {}
    for name in wheel_nodes(objs):
        mn, mx = kit.mesh_bounds(objs[name])
        out[name] = ((mn + mx) / 2, (mx - mn) / 2)
    return out


def rebuild_wheels(objs, style_for):
    hubs = {}
    for name in wheel_nodes(objs):
        obj = objs[name]
        mn, mx = kit.mesh_bounds(obj)
        centre, ext = (mn + mx) / 2, (mx - mn) / 2
        radius, width = max(ext.y, ext.z), ext.x * 2
        outward = 1.0 if centre.x > 1e-3 else -1.0 if centre.x < -1e-3 else 1.0
        part = kit.Part(obj)
        bmesh.ops.delete(part.bm, geom=list(part.bm.verts), context="VERTS")
        obj.data.materials.clear()
        wheels.build_wheel(part, centre, radius, width, outward, style_for(name, radius, width))
        kit.finish_object(part, sharp_deg=50.0)
        nmn, nmx = kit.mesh_bounds(obj)
        nc, ne = (nmn + nmx) / 2, (nmx - nmn) / 2
        hubs[name] = {"hub": [round(c, 4) for c in centre], "radius": round(radius, 4), "width": round(width, 4),
                      "new_hub": [round(c, 4) for c in nc], "new_radius": round(max(ne.y, ne.z), 4),
                      "new_width": round(ne.x * 2, 4)}
        drift = (nc - centre).length
        if drift > 0.004 or abs(max(ne.y, ne.z) - radius) > 0.004:
            print(f"DETAIL WARNING {name}: hub moved {drift * 100:.2f} cm, radius {radius:.3f} -> {max(ne.y, ne.z):.3f}")
    return hubs


def near_wheel(frames, c, s, slack=0.03):
    """True for a trim piece that is part of a static rim: centred on a hub, inside the tyre's width."""
    for centre, ext in frames.values():
        if abs(c.y - centre.y) < slack and abs(c.z - centre.z) < slack and abs(c.x - centre.x) <= ext.x + 0.005 \
                and max(s.y, s.z) < ext.z * 2 + 0.01:
            return True
    return False


def exhaust_anchors(body):
    """KartFxComponent::RefreshAnchors without a booster, in this frame: x = c.x +- e.x * 0.32,
    4 cm behind the body's back (UE min Y is our max y), z = min.z + e.z * 0.6 (e = half extents)."""
    mn, mx = kit.mesh_bounds(body)
    c, e = (mn + mx) / 2, (mx - mn) / 2
    return [V(c.x + sgn * e.x * 0.32, mx.y + 0.04, mn.z + e.z * 0.6) for sgn in (1, -1)]


def exhaust_tip(part, anchor, radius, length, tilt=V(0, 1, 0), shield=None, protrude=-0.02):
    """A chrome tip whose open end is `protrude` past the FX anchor (by default 2 cm short of it, so the
    flames start just outside the pipe), with a dark interior and an optional heat-shield sleeve."""
    axis = tilt.normalized()
    end = anchor + axis * protrude
    start = end - axis * length
    mid = (start + end) / 2
    m = kit.frame(mid, x=axis, z=V(0, 0, 1) if abs(axis.z) < 0.9 else V(1, 0, 0))
    h = length / 2
    r = radius
    # Outer wall, rolled lip, and the inside: one lathe walked so each surface faces out of the metal.
    prof = [(-h, r * 0.92), (h - 0.012, r), (h - 0.002, r * 1.02), (h, r * 0.96), (h - 0.004, r * 0.84),
            (h - 0.06, r * 0.80)]
    kit.lathe(part, "m_Chrome", prof, 24, m)
    kit.cylinder(part, "m_Hub", r * 0.8, 0.01, m @ Matrix.Translation(V(h - 0.06, 0, 0)), segments=20)
    if shield:
        kit.cylinder(part, shield, r * 1.25, length * 0.55, m @ Matrix.Translation(V(-h * 0.35, 0, 0)), segments=24,
                     bevel=0.01)


def lamp(part, centre, normal, radius, mat="m_Lamp", bezel="m_Chrome", depth=0.03, dome=0.35):
    """A headlamp or tail lamp: chrome bezel ring, a domed emissive lens with the prismatic pattern (UV0
    planar), facing `normal`."""
    n = normal.normalized()
    up = V(0, 0, 1) if abs(n.z) < 0.9 else V(0, -1, 0)
    m = kit.frame(centre, x=n, z=up)
    kit.torus(part, bezel, radius * 1.02, radius * 0.14, m, segments=28, sides=8)
    kit.cylinder(part, bezel, radius * 1.05, depth, m @ Matrix.Translation(V(-depth / 2, 0, 0)), segments=28,
                 bevel=0.004)
    faces = kit.sphere(part, mat, radius, m @ Matrix.Translation(V(0.002, 0, 0)), segments=24, rings=8, squash=dome)
    # Keep only the front half of the squashed sphere as the lens (the back hides in the bezel).
    kit.planar_uv(faces, part.uv0, centre, (m.to_3x3() @ V(0, 1, 0)).normalized(),
                  (m.to_3x3() @ V(0, 0, 1)).normalized(), radius * 2)


def round_off(objs, names, width, segments=2, angle=35.0):
    for n in names:
        if n in objs:
            part = kit.Part(objs[n])
            kit.bevel_hard_edges(part, width, segments, angle)
            kit.finish_object(part)


def chain(part, front, rear, r_front, r_rear, pitch=0.022):
    """A roller chain loop in the YZ plane at x = front.x around two sprockets (links alternate plates)."""
    x = front.x
    a2, b2 = V(0, front.y, front.z), V(0, rear.y, rear.z)
    d = b2 - a2
    L = d.length
    u = d / L
    # Outer tangents for two circles (radii r_front, r_rear).
    beta = math.asin(max(-1.0, min(1.0, (r_rear - r_front) / L)))
    n = V(0, -u.z, u.y)  # perpendicular in the YZ plane (up-ish when the chain runs +y)
    path = []

    def ccw(centre, r, a0, a1, steps):
        out = []
        for i in range(steps + 1):
            a = a0 + (a1 - a0) * i / steps
            out.append(centre + (u * math.cos(a) + n * math.sin(a)) * r)
        return out

    top = math.pi / 2 - beta
    # Around the rear sprocket from the top tangent to the bottom one, then round the front back to the top.
    path = ccw(b2, r_rear, top, -top, 18) + ccw(a2, r_front, -top, -(2 * math.pi) + top, 12)
    path.append(path[0])
    # Resample at the chain pitch.
    lengths = [0.0]
    for i in range(1, len(path)):
        lengths.append(lengths[-1] + (path[i] - path[i - 1]).length)
    total = lengths[-1]
    count = max(8, int(total / pitch))
    j = 0
    for k in range(count):
        s = total * k / count
        while j < len(lengths) - 2 and lengths[j + 1] < s:
            j += 1
        t = (s - lengths[j]) / max(lengths[j + 1] - lengths[j], 1e-9)
        p = path[j].lerp(path[j + 1], t)
        tangent = (path[j + 1] - path[j]).normalized()
        centre = V(x, p.y, p.z)
        w = 0.022 if k % 2 == 0 else 0.016
        kit.box(part, "m_Chain", (w, pitch * 1.05, 0.012), kit.frame(centre, y=tangent, x=V(1, 0, 0)), bevel=0.002,
                segments=1)


# --------------------------------------------------------------------------------------------------
# Kart
# --------------------------------------------------------------------------------------------------
def booster_mouths(booster):
    """The two cannon mouths: centre, axis (out of the mouth) and radius, from the rearmost vertex rings."""
    # The booster node is tilted 16 degrees (mounts.json), so the cannons point back and up.
    tilt = 2 * math.asin(0.13917)
    axis = V(0, math.cos(tilt), math.sin(tilt))
    out = []
    for side in (1, -1):
        vs = [v.co.copy() for v in booster.data.vertices if v.co.x * side > 0.33 and v.co.y > 0.7]
        top = max(v.dot(axis) for v in vs)
        ring = [v for v in vs if v.dot(axis) > top - 0.015]
        centre = sum(ring, Vector()) / len(ring)
        radius = max((v - centre - axis * (v - centre).dot(axis)).length for v in ring)
        out.append((centre, axis, radius))
    return out


def build_kart(objs):
    style = lambda name, r, w: wheels.WheelStyle(rim=0.58, tread="blocks", tread_depth=0.03, blocks=20, spokes=6,
                                                 spoke_style="twin", lugs=5, segments=48)
    hubs = rebuild_wheels(objs, style)
    body = objs["body"]
    # The kart has no trim node: every non-paint addition goes on a new one (KartPawn's Mounts: bTrim).
    trim_obj = kit.new_object("trim")
    trim = kit.Part(trim_obj)
    body_part = kit.Part(body)
    tree = kit.bvh(body_part)
    body_part.bm.free()

    # Headlamps on the lower nose's front face, a rubber front bumper with chrome mounts, a carbon splitter.
    for sgn in (1, -1):
        loc, n = kit.hit(tree, V(0.36 * sgn, -2.6, -0.16), V(0, 1, 0))
        if loc is not None and loc.y < -1.2:
            lamp(trim, loc + n * 0.01, (n + V(0, -0.6, 0)).normalized(), 0.05)
    bumper = [V(0.56, -1.56, -0.29), V(0.50, -1.80, -0.29), V(-0.50, -1.80, -0.29), V(-0.56, -1.56, -0.29)]
    kit.tube(trim, "m_Rubber", bumper, 0.03, sides=14, bend=0.12)
    for sgn in (1, -1):
        kit.tube(trim, "m_Chrome", [V(0.30 * sgn, -1.79, -0.29), V(0.30 * sgn, -1.54, -0.29)], 0.016, sides=10)
    kit.box(trim, "m_Carbon", (1.0, 0.26, 0.016), Matrix.Translation(V(0, -1.53, -0.383)), bevel=0.007)
    for sgn in (1, -1):
        kit.box(trim, "m_Carbon", (0.016, 0.24, 0.06), Matrix.Translation(V(0.50 * sgn, -1.53, -0.36)), bevel=0.005)

    # Rear diffuser strakes under the back of the chassis.
    kit.box(trim, "m_Carbon", (0.66, 0.26, 0.02), Matrix.Translation(V(0, 1.0, -0.395)), bevel=0.006)
    for x in (-0.24, -0.08, 0.08, 0.24):
        kit.box(trim, "m_Carbon", (0.016, 0.24, 0.08), Matrix.Translation(V(x, 1.0, -0.355)), bevel=0.005)

    # Booster cannons: a chrome lip round each mouth and a glowing core inside (the FX flames start here).
    mouths = booster_mouths(objs["booster"])
    for (c, axis, r) in mouths:
        m = kit.frame(c, x=axis, z=V(0, 0, 1))
        kit.torus(trim, "m_Chrome", r * 0.80, r * 0.07, m @ Matrix.Translation(V(0.004, 0, 0)), segments=32, sides=8)
        kit.cylinder(trim, "m_Glow", r * 0.58, 0.01, m @ Matrix.Translation(V(0.003, 0, 0)), segments=24)
        kit.torus(trim, "m_Hub", r * 0.62, r * 0.045, m @ Matrix.Translation(V(0.007, 0, 0)), segments=24, sides=6)

    # Roundels: the nose and both side pods.
    kit.decal(trim, "m_Roundel", tree, V(0, -1.36, 1.5), V(0, 0, -1), 0.13, up=V(0, -1, 0), rings=4)
    for sgn in (1, -1):
        kit.decal(trim, "m_Roundel", tree, V(1.6 * sgn, -0.10, -0.08), V(-sgn, 0, 0), 0.085, rings=3)

    # Front coil-over dampers between the axle stubs and the chassis.
    for sgn in (1, -1):
        base = V(0.40 * sgn, -0.70, -0.17)
        top = V(0.30 * sgn, -0.58, 0.05)
        axis = top - base
        kit.helix(trim, "m_Spring", base + axis.normalized() * 0.03, axis, 0.034, 0.008, 6, axis.length - 0.06)
        kit.tube(trim, "m_Chrome", [base, top], 0.012, sides=8)
        kit.cylinder(trim, "m_Hub", 0.022, axis.length * 0.45,
                     kit.frame(base + axis * 0.7, x=axis, z=V(0, 0, 1)), segments=12, bevel=0.004)
    kit.finish_object(trim)
    return {"wheels": hubs, "booster_mouths": [[round(x, 3) for x in c] for c, _, _ in mouths]}


# --------------------------------------------------------------------------------------------------
# Moto
# --------------------------------------------------------------------------------------------------
def build_moto(objs):
    frames = wheel_frames(objs)
    style = lambda name, r, w: wheels.WheelStyle(rim=0.62, tread="moto", spokes=5, spoke_style="y", lugs=0,
                                                 disc_side=1, segments=48)
    hubs = rebuild_wheels(objs, style)
    body, trim_obj = objs["body"], objs["trim"]
    trim = kit.Part(trim_obj)
    removed = kit.delete_islands(trim, lambda mats, c, s: near_wheel(frames, c, s))
    # The seat slab becomes a stitched cushion; the side covers and engine get premium finishes.
    kit.restyle_islands(trim, lambda mats, c, s: "m_Accent" in mats and abs(c.y - 0.30) < 0.02 and c.z > 0.4, "m_Seat")
    kit.restyle_islands(trim, lambda mats, c, s: "m_Accent" in mats and abs(c.y - 0.08) < 0.02 and c.z < 0.0, "m_Engine")
    kit.restyle_islands(trim, lambda mats, c, s: "m_Accent" in mats and abs(c.y + 0.16) < 0.02 and abs(c.z - 0.14) < 0.02, "m_Engine")
    kit.bevel_hard_edges(trim, 0.008, 2)

    body_part = kit.Part(body)
    kit.bevel_hard_edges(body_part, 0.014, 3)
    # A headlight fairing (paint) with a tinted screen, so the bike reads as a sport-tourer.
    fair = kit.frame(V(0, -0.715, 0.655), y=V(0, -1, 0.28), z=V(0, 0.28, 1))
    kit.box(body_part, "m_Body", (0.30, 0.20, 0.27), fair, bevel=0.07, segments=4)
    kit.finish_object(body_part)
    anchors = exhaust_anchors(body)  # from the final body, as the FX will measure it

    tree_b = kit.bvh(kit.Part(body))
    lamp(trim, V(0, -0.83, 0.645), V(0, -1, 0.12), 0.07)
    for sgn in (1, -1):
        lamp(trim, V(0.12 * sgn, -0.80, 0.53), V(0.3 * sgn, -1, 0), 0.022, mat="m_Amber", depth=0.02)
        lamp(trim, V(0.11 * sgn, 1.10, 0.36), V(0.3 * sgn, 1, 0), 0.02, mat="m_Amber", depth=0.02)
    lamp(trim, V(0, 0.885, 0.43), V(0, 1, 0.2), 0.035, mat="m_Tail")

    # Under-tail twin exhaust, exactly on the FX anchors, in a carbon heat shield.
    for a in anchors:
        exhaust_tip(trim, a, 0.032, 0.30, shield="m_Carbon")
    kit.box(trim, "m_Carbon", (0.20, 0.30, 0.09), Matrix.Translation(V(0, anchors[0].y - 0.22, anchors[0].z + 0.01)),
            bevel=0.03, segments=3)

    # Drive chain from the gearbox sprocket to the rear sprocket (left side, x = -0.15).
    kit.cylinder(trim, "m_Chrome", 0.045, 0.02, Matrix.Translation(V(-0.15, 0.22, -0.11)), segments=16, bevel=0.004)
    chain(trim, V(-0.15, 0.22, -0.11), V(-0.15, 0.85, -0.13), 0.05, 0.115)

    # Cylinder head cover, spark plug caps and an air filter pod.
    kit.box(trim, "m_Chrome", (0.26, 0.24, 0.05), Matrix.Translation(V(0, -0.16, 0.235)), bevel=0.018, segments=3)
    for sgn in (1, -1):
        kit.cylinder(trim, "m_Rubber", 0.016, 0.06, kit.frame(V(0.15 * sgn, -0.16, 0.235), x=V(sgn, 0, 0.3),
                                                              z=V(0, 0, 1)), segments=10, bevel=0.005)
    kit.cylinder(trim, "m_Carbon", 0.065, 0.11, kit.frame(V(0.17, 0.02, 0.17), x=V(1, 0.2, 0), z=V(0, 0, 1)),
                 segments=20, bevel=0.02)
    # Fork springs over the stanchions (the fork tubes stay chrome).
    for sgn in (1, -1):
        base, top = V(0.09 * sgn, -0.88, 0.02), V(0.09 * sgn, -0.72, 0.42)
        kit.helix(trim, "m_Spring", base, top - base, 0.03, 0.006, 7, (top - base).length)
    # Number boards on the side covers.
    for sgn in (1, -1):
        kit.decal(trim, "m_Roundel", tree_b, V(0.6 * sgn, 0.42, 0.22), V(-sgn, 0, 0), 0.07, rings=3)
    kit.finish_object(trim)
    return {"wheels": hubs, "removed_static_rim_faces": removed, "exhaust_anchors": [[round(x, 3) for x in a] for a in anchors]}


# --------------------------------------------------------------------------------------------------
# Coupe
# --------------------------------------------------------------------------------------------------
def build_coupe(objs):
    frames = wheel_frames(objs)
    style = lambda name, r, w: wheels.WheelStyle(rim=0.64, tread="road", spokes=10, spoke_style="mesh", lugs=5,
                                                 segments=64)
    hubs = rebuild_wheels(objs, style)
    body, trim_obj = objs["body"], objs["trim"]
    body_part = kit.Part(body)
    kit.bevel_hard_edges(body_part, 0.022, 3)
    tree = kit.bvh(body_part)
    kit.finish_object(body_part)
    anchors = exhaust_anchors(body)
    trim = kit.Part(trim_obj)
    removed = kit.delete_islands(trim, lambda mats, c, s: near_wheel(frames, c, s))
    # The old exhaust pipes (the new tips sit on the FX anchors instead).
    kit.delete_islands(trim, lambda mats, c, s: mats == {"m_Chrome"} and c.y > 1.75 and abs(abs(c.x) - 0.5) < 0.02)
    # Aero in carbon: wing, end plates and stands, diffuser and fins, front splitter.
    kit.restyle_islands(trim, lambda mats, c, s: mats == {"m_Trim"} and c.y > 1.5 and c.z > 0.4, "m_Carbon")
    kit.restyle_islands(trim, lambda mats, c, s: mats == {"m_Trim"} and c.y > 1.65 and c.z < -0.3, "m_Carbon")
    kit.restyle_islands(trim, lambda mats, c, s: mats == {"m_Trim"} and c.y < -1.7 and c.z < -0.3, "m_Carbon")
    # Seats: stitched upholstery.
    kit.restyle_islands(trim, lambda mats, c, s: mats == {"m_Trim"} and 0.5 < c.y < 0.6 and c.z > -0.1, "m_Seat")
    kit.bevel_hard_edges(trim, 0.012, 2)

    # Domed lenses over the headlamps and fog lamps, LED strip on the wing, a third brake light.
    for sgn in (1, -1):
        lamp(trim, V(0.62 * sgn, -1.80, 0.18), V(0.1 * sgn, -1, 0), 0.088)
        lamp(trim, V(0.78 * sgn, -1.845, -0.20), V(0, -1, 0), 0.036, mat="m_Amber", depth=0.015)
        lamp(trim, V(0.86 * sgn, 1.75, 0.20), V(0, 1, 0), 0.05, mat="m_Tail", depth=0.02)
    kit.box(trim, "m_Tail", (0.9, 0.012, 0.018), Matrix.Translation(V(0, 1.747, 0.668)), bevel=0.004)
    # Exhaust tips on the FX anchors, with a dark surround in the rear valance.
    for a in anchors:
        exhaust_tip(trim, a, 0.048, 0.24)
        kit.box(trim, "m_Rubber", (0.16, 0.03, 0.12), Matrix.Translation(V(a.x, 1.712, a.z)), bevel=0.02, segments=2)
    # Door roundels and a chrome fuel cap.
    for sgn in (1, -1):
        kit.decal(trim, "m_Roundel", tree, V(1.8 * sgn, -0.22, 0.06), V(-sgn, 0, 0), 0.20, rings=4)
    loc, n = kit.hit(tree, V(0.62, 1.05, 1.5), V(0, 0, -1))
    if loc is not None:
        kit.cylinder(trim, "m_Chrome", 0.055, 0.016, kit.frame(loc + n * 0.006, x=n, z=V(0, -1, 0)), segments=20,
                     bevel=0.005)
    # Bonnet roundel, beside the scoop.
    kit.decal(trim, "m_Roundel", tree, V(0.48, -1.22, 1.5), V(0, 0, -1), 0.14, up=V(0, -1, 0), rings=4)
    kit.finish_object(trim)
    return {"wheels": hubs, "removed_static_rim_faces": removed, "exhaust_anchors": [[round(x, 3) for x in a] for a in anchors]}


# --------------------------------------------------------------------------------------------------
# Truck
# --------------------------------------------------------------------------------------------------
def build_truck(objs):
    frames = wheel_frames(objs)
    style = lambda name, r, w: wheels.WheelStyle(rim=0.52, tread="lugs", tread_depth=0.075, blocks=20, spokes=8,
                                                 spoke_style="single", lugs=8, beadlock=True, segments=64)
    hubs = rebuild_wheels(objs, style)
    body, trim_obj = objs["body"], objs["trim"]
    body_part = kit.Part(body)
    kit.bevel_hard_edges(body_part, 0.025, 3)
    tree = kit.bvh(body_part)
    kit.finish_object(body_part)
    anchors = exhaust_anchors(body)
    trim = kit.Part(trim_obj)
    removed = kit.delete_islands(trim, lambda mats, c, s: near_wheel(frames, c, s))
    # Seats: the two cab seats (dark trim) become stitched upholstery.
    kit.restyle_islands(trim, lambda mats, c, s: mats == {"m_Trim"} and 0.5 < c.y < 0.6 and c.z > 0.8, "m_Seat")
    kit.bevel_hard_edges(trim, 0.014, 1)

    # Coil-over shocks, two per wheel, axle to frame, in the accent colour.
    for name, (c, e) in frames.items():
        sgn = 1 if c.x > 0 else -1
        for dy in (-0.16, 0.16):
            base = V(0.74 * sgn, c.y + dy, c.z + 0.05)
            top = V(0.66 * sgn, c.y + dy * 1.3, 0.74)
            axis = top - base
            kit.cylinder(trim, "m_Chrome", 0.016, axis.length, kit.frame((base + top) / 2, x=axis, z=V(0, 0, 1)),
                         segments=10)
            kit.cylinder(trim, "m_Hub", 0.034, axis.length * 0.5, kit.frame(base + axis * 0.68, x=axis, z=V(0, 0, 1)),
                         segments=14, bevel=0.006)
            kit.helix(trim, "m_Spring", base + axis * 0.08, axis, 0.06, 0.011, 7, axis.length * 0.84, sides=8)
            for p in (base, top):
                kit.cylinder(trim, "m_Chrome", 0.028, 0.05, kit.frame(p, x=V(1, 0, 0), z=V(0, 0, 1)), segments=12,
                             bevel=0.006)

    # Lens domes on the headlamps and the roll-bar lamps.
    for sgn in (1, -1):
        lamp(trim, V(0.68 * sgn, -1.685, 1.02), V(0, -1, 0), 0.08)
    for x in (-0.48, -0.16, 0.16, 0.48):
        lamp(trim, V(x, 0.598, 1.80), V(0, -1, 0), 0.048, depth=0.02)
    for sgn in (1, -1):
        lamp(trim, V(0.72 * sgn, 1.68, 0.98), V(0, 1, 0), 0.05, mat="m_Tail", depth=0.015)

    # Twin exhaust on the FX anchors, out through the rear bumper (the flames start just inside the tips).
    for a in anchors:
        exhaust_tip(trim, a, 0.055, 0.36, shield="m_Carbon", protrude=0.09)

    # A spare wheel strapped in the bed.
    start = len(trim.new_faces)
    spare_style = wheels.WheelStyle(rim=0.52, tread="lugs", tread_depth=0.06, blocks=16, spokes=8, spoke_style="single",
                                    lugs=8, beadlock=True, segments=40, disc=False)
    wheels.build_wheel(trim, V(0, 0, 0), 0.40, 0.26, 1.0, spare_style)
    new_verts = {v for f in trim.new_faces[start:] if f.is_valid for v in f.verts}
    # The bed is a solid block up to its rim (z ~1.135): the spare lies on top of it.
    deck, _ = kit.hit(tree, V(0, 1.22, 3.0), V(0, 0, -1))
    deck_z = deck.z if deck is not None else 1.135
    place = Matrix.Translation(V(0, 1.22, deck_z + 0.13 + 0.002)) @ Matrix.Rotation(-math.pi / 2, 4, "Y")
    bmesh.ops.transform(trim.bm, matrix=place, verts=list(new_verts))
    for sgn in (1, -1):
        kit.tube(trim, "m_Rubber", [V(0.30 * sgn, 0.92, deck_z), V(0.30 * sgn, 0.98, deck_z + 0.272),
                                    V(0.30 * sgn, 1.46, deck_z + 0.272), V(0.30 * sgn, 1.52, deck_z)], 0.012, sides=6,
                 bend=0.04)
    # Door roundels and one on the tailgate.
    for sgn in (1, -1):
        kit.decal(trim, "m_Roundel", tree, V(1.8 * sgn, 0.02, 0.86), V(-sgn, 0, 0), 0.19, rings=4)
    kit.decal(trim, "m_Roundel", tree, V(0, 2.5, 0.93), V(0, -1, 0), 0.14, rings=4)
    kit.finish_object(trim)
    return {"wheels": hubs, "removed_static_rim_faces": removed, "exhaust_anchors": [[round(x, 3) for x in a] for a in anchors]}


BUILD = {"kart": build_kart, "moto": build_moto, "coupe": build_coupe, "truck": build_truck}
