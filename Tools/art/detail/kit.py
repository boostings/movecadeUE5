"""Modelling kit for the vehicle detail pass (Blender 5.0, bpy + bmesh, headless).

Frame: the decoded GLBs imported into Blender, so +X is the vehicle's left, -Y its front, +Z up, metres. The
contact patch is at z = -0.49 on every vehicle. UE's model frame is (x, -y, z) * 100 of this.

Every primitive is written straight into a Part (one bmesh per vehicle node) with a material by name, a UV0
that suits the primitive (lathes and tubes unwrap around and along; boxes box-project in metres) and, at the
end, a UV1 box projection in metres for the tiling detail maps (flake, carbon, brushed metal, cast).
"""
import math

import bmesh
import bpy
from mathutils import Matrix, Vector

TAU = math.tau


def V(x, y, z):
    return Vector((x, y, z))


def ordered(items):
    """De-duplicates BMesh elements in first-seen order. A set orders them by memory address, so a bevel fed
    from one builds its new elements in a different order on every run and the GLB bytes change."""
    return list(dict.fromkeys(items))


# --------------------------------------------------------------------------------------------------
# Materials
# --------------------------------------------------------------------------------------------------
def material(name, colour=(0.5, 0.5, 0.5), metallic=0.0, roughness=0.5, emissive=0.0):
    """A plain Principled material: only its name matters to UE (import_karts.py maps names to instances);
    the colour makes the Blender previews readable."""
    mat = bpy.data.materials.get(name)
    if mat is None:
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf:
            bsdf.inputs["Base Color"].default_value = (*colour, 1.0)
            bsdf.inputs["Metallic"].default_value = metallic
            bsdf.inputs["Roughness"].default_value = roughness
            if emissive > 0:
                bsdf.inputs["Emission Color"].default_value = (*colour, 1.0)
                bsdf.inputs["Emission Strength"].default_value = emissive
    return mat


# Preview colours for the new material names (UE assigns the real looks).
PREVIEW = {
    "m_Tyre": ((0.02, 0.02, 0.022), 0.0, 0.85),
    "m_Rim": ((0.8, 0.8, 0.82), 1.0, 0.15),
    "m_Disc": ((0.55, 0.55, 0.56), 1.0, 0.35),
    "m_Hub": ((0.1, 0.1, 0.11), 0.6, 0.4),
    "m_Chrome": ((0.9, 0.9, 0.92), 1.0, 0.1),
    "m_Carbon": ((0.04, 0.04, 0.045), 0.0, 0.3),
    "m_Brushed": ((0.7, 0.7, 0.72), 1.0, 0.3),
    "m_Rubber": ((0.03, 0.03, 0.03), 0.0, 0.8),
    "m_Seat": ((0.06, 0.06, 0.08), 0.0, 0.6),
    "m_Engine": ((0.12, 0.12, 0.13), 0.8, 0.45),
    "m_Spring": ((0.95, 0.75, 0.05), 0.3, 0.35),
    "m_Chain": ((0.15, 0.15, 0.16), 1.0, 0.4),
    "m_Lamp": ((1.0, 0.9, 0.7), 0.0, 0.2),
    "m_Tail": ((0.8, 0.02, 0.03), 0.0, 0.3),
    "m_Amber": ((1.0, 0.45, 0.02), 0.0, 0.3),
    "m_Glow": ((1.0, 0.5, 0.1), 0.0, 0.3),
    "m_Roundel": ((0.95, 0.95, 0.9), 0.0, 0.3),
    "m_Trim": ((0.03, 0.03, 0.05), 0.0, 0.55),
    "m_Accent": ((0.9, 0.6, 0.1), 0.0, 0.45),
    "m_Glass": ((0.03, 0.04, 0.06), 0.3, 0.1),
}


def mat_named(name):
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    colour, metal, rough = PREVIEW.get(name, ((0.5, 0.5, 0.5), 0.0, 0.5))
    emissive = 3.0 if name in ("m_Lamp", "m_Tail", "m_Amber", "m_Glow") else 0.0
    return material(name, colour, metal, rough, emissive)


# --------------------------------------------------------------------------------------------------
# Parts
# --------------------------------------------------------------------------------------------------
class Part:
    """One vehicle node's mesh being edited: the original geometry (model space) plus new primitives."""

    def __init__(self, obj):
        self.obj = obj
        self.bm = bmesh.new()
        self.bm.from_mesh(obj.data)
        uv = self.bm.loops.layers.uv
        self.uv0 = uv[0] if len(uv) else uv.new("UVMap")
        self.uv1 = uv.get("UV1") or uv.new("UV1")
        self.new_faces = []

    def mat(self, name):
        mats = self.obj.data.materials
        for i, m in enumerate(mats):
            if m and m.name == name:
                return i
        mats.append(mat_named(name))
        return len(mats) - 1

    def faces_of(self, verts):
        return ordered(f for v in verts for f in v.link_faces)

    def finish(self, faces, mat):
        idx = self.mat(mat)
        for f in faces:
            f.material_index = idx
            f.smooth = True
        self.new_faces.extend(faces)
        return faces

    def commit(self):
        self.bm.to_mesh(self.obj.data)
        self.obj.data.update()
        self.bm.free()


def apply_transforms(obj):
    """Bakes the object's world transform into its vertices (UE bakes model space anyway)."""
    m = obj.matrix_world.copy()
    obj.parent = None
    obj.data.transform(m)
    obj.matrix_world = Matrix.Identity(4)


# --------------------------------------------------------------------------------------------------
# UVs
# --------------------------------------------------------------------------------------------------
def box_uv(faces, layer, scale=1.0, centre=Vector((0, 0, 0))):
    """Box projection, metres x scale: each face takes the plane its normal faces most."""
    for f in faces:
        n = f.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        for loop in f.loops:
            p = (loop.vert.co - centre) * scale
            if ax == 0:
                loop[layer].uv = (p.y * (1 if n.x > 0 else -1), p.z)
            elif ax == 1:
                loop[layer].uv = (p.x * (-1 if n.y > 0 else 1), p.z)
            else:
                loop[layer].uv = (p.x, p.y * (1 if n.z > 0 else -1))


def planar_uv(faces, layer, origin, u_axis, v_axis, size):
    """0..1 across a square of side `size` centred on origin (roundels, lenses)."""
    for f in faces:
        for loop in f.loops:
            d = loop.vert.co - origin
            loop[layer].uv = (d.dot(u_axis) / size + 0.5, d.dot(v_axis) / size + 0.5)


# --------------------------------------------------------------------------------------------------
# Primitives
# --------------------------------------------------------------------------------------------------
def frame(origin, x=None, y=None, z=None):
    """A matrix from an origin and up to two axes (the third follows)."""
    if x is not None and z is not None:
        x = x.normalized(); z = (z - x * z.dot(x)).normalized(); y = z.cross(x)
    elif y is not None and z is not None:
        y = y.normalized(); z = (z - y * z.dot(y)).normalized(); x = y.cross(z)
    elif x is not None and y is not None:
        x = x.normalized(); y = (y - x * y.dot(x)).normalized(); z = x.cross(y)
    else:
        x, y, z = V(1, 0, 0), V(0, 1, 0), V(0, 0, 1)
    m = Matrix((x, y, z)).transposed().to_4x4()
    m.translation = origin
    return m


def box(part, mat, size, matrix=Matrix.Identity(4), bevel=0.0, segments=2, uv_scale=1.0):
    """A rounded box: size (x, y, z) centred on the matrix origin, edges bevelled by `bevel` metres."""
    res = bmesh.ops.create_cube(part.bm, size=1.0)
    verts = res["verts"]
    s = Matrix.Diagonal((*size, 1.0))
    bmesh.ops.transform(part.bm, matrix=matrix @ s, verts=verts)
    if bevel > 0:
        edges = ordered(e for v in verts for e in v.link_edges)
        bres = bmesh.ops.bevel(part.bm, geom=verts + edges, offset=bevel, offset_type="OFFSET", segments=segments,
                               profile=0.5, affect="EDGES", clamp_overlap=True)
        verts = ordered(list(verts) + list(bres.get("verts", [])))
        verts = [v for v in verts if v.is_valid]
    faces = part.faces_of(verts)
    if matrix.determinant() < 0:
        bmesh.ops.reverse_faces(part.bm, faces=faces)
    for f in faces:
        f.normal_update()
    box_uv(faces, part.uv0, uv_scale)
    return part.finish(faces, mat)


def lathe(part, mat, profile, segments, matrix=Matrix.Identity(4), phase=0.0, u_repeat=1.0):
    """Revolves profile [(axial x, radius r)] about the local X axis. The visible side is on the left of the
    walk in the (x, r) plane: walking +x faces away from the axis, walking +r faces -x. UV0: u around
    (x u_repeat), v along the profile."""
    bm = part.bm
    rings = []
    for (x, r) in profile:
        ring = []
        for i in range(segments):
            a = phase + TAU * i / segments
            ring.append(bm.verts.new(matrix @ V(x, r * math.cos(a), r * math.sin(a))))
        rings.append(ring)
    faces = []
    lengths = [0.0]
    for k in range(1, len(profile)):
        (x0, r0), (x1, r1) = profile[k - 1], profile[k]
        lengths.append(lengths[-1] + math.hypot(x1 - x0, r1 - r0))
    total = lengths[-1] or 1.0
    for k in range(len(rings) - 1):
        a, b = rings[k], rings[k + 1]
        for i in range(segments):
            j = (i + 1) % segments
            if profile[k][1] < 1e-6 and profile[k + 1][1] < 1e-6:
                continue
            f = bm.faces.new((a[i], a[j], b[j], b[i]))
            for loop, (uu, vv) in zip(f.loops, ((i, k), (i + 1, k), (i + 1, k + 1), (i, k + 1))):
                loop[part.uv0].uv = (uu / segments * u_repeat, lengths[vv] / total)
            faces.append(f)
    # Poles (r = 0) collapse to one vertex.
    poles = [v for k, r in enumerate(rings) if profile[k][1] < 1e-6 for v in r]
    if poles:
        bmesh.ops.remove_doubles(bm, verts=poles, dist=1e-6)
    faces = [f for f in faces if f.is_valid]
    if matrix.determinant() < 0:
        bmesh.ops.reverse_faces(bm, faces=faces)
    return part.finish(faces, mat)


def cylinder(part, mat, radius, length, matrix=Matrix.Identity(4), segments=24, bevel=0.0, axis="X"):
    """A capped cylinder along local X (or Z), centred, with optional rounded cap edges."""
    h = length / 2
    b = min(bevel, radius * 0.45, h * 0.9)
    prof = [(-h, 0.0)]
    if b > 0:
        for t in (0.0, 0.5, 1.0):
            a = t * math.pi / 2
            prof.append((-h + b - b * math.cos(a), radius - b + b * math.sin(a)))
        for t in (1.0, 0.5, 0.0):
            a = t * math.pi / 2
            prof.append((h - b + b * math.cos(a), radius - b + b * math.sin(a)))
    else:
        prof += [(-h, radius), (h, radius)]
    prof.append((h, 0.0))
    if axis == "Z":
        matrix = matrix @ Matrix.Rotation(-math.pi / 2, 4, "Y")
    elif axis == "Y":
        matrix = matrix @ Matrix.Rotation(math.pi / 2, 4, "Z")
    return lathe(part, mat, prof, segments, matrix)


def sphere(part, mat, radius, matrix=Matrix.Identity(4), segments=16, rings=8, squash=1.0):
    prof = []
    for i in range(rings + 1):
        a = math.pi * i / rings
        prof.append((-math.cos(a) * radius * squash, math.sin(a) * radius))
    return lathe(part, mat, prof, segments, matrix)


def torus(part, mat, major, minor, matrix=Matrix.Identity(4), segments=32, sides=8):
    prof = []
    for i in range(sides + 1):
        a = TAU * i / sides
        prof.append((minor * math.sin(a), major + minor * math.cos(a)))
    return lathe(part, mat, prof, segments, matrix)


def _frames(points):
    """Parallel-transport frames along a polyline."""
    tangents = []
    for i in range(len(points)):
        a = points[max(i - 1, 0)]
        b = points[min(i + 1, len(points) - 1)]
        tangents.append((b - a).normalized())
    t0 = tangents[0]
    up = V(0, 0, 1) if abs(t0.z) < 0.9 else V(1, 0, 0)
    n = (up - t0 * up.dot(t0)).normalized()
    normals = [n]
    for i in range(1, len(points)):
        t = tangents[i]
        n = (n - t * n.dot(t)).normalized()
        normals.append(n)
    return tangents, normals


def fillet(points, radius, steps=4):
    """Rounds a polyline's corners (tube bends)."""
    if len(points) < 3 or radius <= 0:
        return points
    out = [points[0]]
    for i in range(1, len(points) - 1):
        p0, p1, p2 = points[i - 1], points[i], points[i + 1]
        d0, d1 = (p0 - p1), (p2 - p1)
        l0, l1 = d0.length, d1.length
        r = min(radius, l0 * 0.45, l1 * 0.45)
        a = p1 + d0.normalized() * r
        b = p1 + d1.normalized() * r
        for s in range(steps + 1):
            t = s / steps
            out.append(a * (1 - t) ** 2 + p1 * 2 * (1 - t) * t + b * t * t)
    out.append(points[-1])
    return out


def tube(part, mat, points, radius, sides=10, caps=True, bend=0.0):
    """A round tube along a polyline (world points), optional rounded bends."""
    pts = fillet([Vector(p) for p in points], bend) if bend > 0 else [Vector(p) for p in points]
    radii = radius if isinstance(radius, (list, tuple)) else [radius] * len(pts)
    if len(radii) != len(pts):
        radii = [radii[0]] * len(pts)
    tangents, normals = _frames(pts)
    bm = part.bm
    rings = []
    for p, t, n, r in zip(pts, tangents, normals, radii):
        b = t.cross(n)
        rings.append([bm.verts.new(p + (n * math.cos(TAU * i / sides) + b * math.sin(TAU * i / sides)) * r)
                      for i in range(sides)])
    faces = []
    length = [0.0]
    for i in range(1, len(pts)):
        length.append(length[-1] + (pts[i] - pts[i - 1]).length)
    for k in range(len(rings) - 1):
        for i in range(sides):
            j = (i + 1) % sides
            f = bm.faces.new((rings[k][i], rings[k][j], rings[k + 1][j], rings[k + 1][i]))
            for loop, (uu, vv) in zip(f.loops, ((i, k), (i + 1, k), (i + 1, k + 1), (i, k + 1))):
                loop[part.uv0].uv = (uu / sides, length[vv])
            faces.append(f)
    if caps:
        faces.append(bm.faces.new(list(reversed(rings[0]))))
        faces.append(bm.faces.new(rings[-1]))
    bmesh.ops.recalc_face_normals(bm, faces=faces)
    return part.finish(faces, mat)


def helix(part, mat, base, axis, coil_radius, wire, turns, length, sides=6, steps_per_turn=14):
    """A coil spring from `base` along `axis` (spring dampers)."""
    axis = axis.normalized()
    side = V(1, 0, 0) if abs(axis.x) < 0.9 else V(0, 1, 0)
    u = (side - axis * side.dot(axis)).normalized()
    w = axis.cross(u)
    n = int(turns * steps_per_turn)
    pts = []
    for i in range(n + 1):
        t = i / n
        a = TAU * turns * t
        pts.append(base + axis * (length * t) + (u * math.cos(a) + w * math.sin(a)) * coil_radius)
    return tube(part, mat, pts, wire, sides=sides, caps=True)


def disc(part, mat, radius, matrix, segments=32, uv_size=None):
    """A flat round face in the local YZ plane, facing +X (decals, lenses); UV0 0..1 across it."""
    bm = part.bm
    centre = bm.verts.new(matrix @ V(0, 0, 0))
    ring = [bm.verts.new(matrix @ V(0, radius * math.cos(TAU * i / segments), radius * math.sin(TAU * i / segments)))
            for i in range(segments)]
    faces = [bm.faces.new((centre, ring[i], ring[(i + 1) % segments])) for i in range(segments)]
    normal = (matrix.to_3x3() @ V(1, 0, 0)).normalized()
    for f in faces:
        f.normal_update()
        if f.normal.dot(normal) < 0:
            f.normal_flip()
    u_axis = (matrix.to_3x3() @ V(0, 1, 0)).normalized()  # seen from +X (Z up), local +Y is screen right
    v_axis = (matrix.to_3x3() @ V(0, 0, 1)).normalized()
    planar_uv(faces, part.uv0, matrix.translation, u_axis, v_axis, uv_size or radius * 2)
    return part.finish(faces, mat)


# --------------------------------------------------------------------------------------------------
# Whole-mesh passes
# --------------------------------------------------------------------------------------------------
def bevel_hard_edges(part, width, segments=2, angle_deg=35.0, materials=None):
    """Rounds the original low-poly geometry: welds the glTF's split vertices, then bevels every manifold
    edge sharper than angle_deg (optionally only between faces of the given materials)."""
    bm = part.bm
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=2e-4)
    limit = math.radians(angle_deg)
    allowed = None
    if materials is not None:
        allowed = {i for i, m in enumerate(part.obj.data.materials) if m and m.name in materials}
    edges = []
    for e in bm.edges:
        if not e.is_manifold or len(e.link_faces) != 2:
            continue
        if allowed is not None and not all(f.material_index in allowed for f in e.link_faces):
            continue
        try:
            if e.calc_face_angle() > limit:
                edges.append(e)
        except ValueError:
            pass
    if edges:
        res = bmesh.ops.bevel(bm, geom=edges, offset=width, offset_type="OFFSET", segments=segments, profile=0.5,
                              affect="EDGES", clamp_overlap=True, harden_normals=False)
        # The bevel's new faces take material 0; give each the material of the original faces it touches.
        pending = set(res.get("faces", []))
        for _ in range(segments * 2 + 4):
            done = set()
            for f in pending:
                votes = {}
                for e in f.edges:
                    for g in e.link_faces:
                        if g is not f and g not in pending:
                            votes[g.material_index] = votes.get(g.material_index, 0) + 1
                if votes:
                    f.material_index = max(sorted(votes), key=votes.get)
                    done.add(f)
            # Corner patches touch only other bevel faces: they inherit on a later pass.
            pending -= done
            if not pending or not done:
                break
    for f in bm.faces:
        f.smooth = True
    return len(edges)


def finish_object(part, sharp_deg=40.0, uv1_scale=1.0, weighted=True):
    """UV1 box projection for everything, smooth shading split by angle, weighted normals."""
    box_uv(part.bm.faces, part.uv1, uv1_scale)
    for f in part.bm.faces:
        f.smooth = True
    part.commit()
    me = part.obj.data
    me.set_sharp_from_angle(angle=math.radians(sharp_deg))
    if weighted:
        mod = part.obj.modifiers.new("Weighted", "WEIGHTED_NORMAL")
        mod.mode = "FACE_AREA"
        mod.keep_sharp = True
        mod.weight = 50


def add_uv1(obj, scale=1.0):
    """Adds the UV1 metre box projection to an untouched mesh, without a bmesh round trip (so its own normals
    and geometry stay exactly as they are: the grips and the kart's textured body)."""
    me = obj.data
    if "UV1" in me.uv_layers:
        return
    layer = me.uv_layers.new(name="UV1", do_init=False)
    for poly in me.polygons:
        n = poly.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        for li in poly.loop_indices:
            p = me.vertices[me.loops[li].vertex_index].co * scale
            if ax == 0:
                layer.data[li].uv = (p.y * (1 if n.x > 0 else -1), p.z)
            elif ax == 1:
                layer.data[li].uv = (p.x * (-1 if n.y > 0 else 1), p.z)
            else:
                layer.data[li].uv = (p.x, p.y * (1 if n.z > 0 else -1))
    me.uv_layers.active_index = 0


def tri_count(obj):
    deps = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(deps)
    me = ev.to_mesh()
    me.calc_loop_triangles()
    n = len(me.loop_triangles)
    ev.to_mesh_clear()
    return n


def bounds(obj):
    pts = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    mn = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    mx = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return mn, mx


def mesh_bounds(obj):
    vs = [obj.matrix_world @ v.co for v in obj.data.vertices]
    mn = Vector((min(p.x for p in vs), min(p.y for p in vs), min(p.z for p in vs)))
    mx = Vector((max(p.x for p in vs), max(p.y for p in vs), max(p.z for p in vs)))
    return mn, mx


def new_object(name, collection=None):
    me = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, me)
    (collection or bpy.context.scene.collection).objects.link(obj)
    return obj


def delete_faces(part, predicate):
    """Removes faces of the original mesh (replaced parts)."""
    doomed = [f for f in part.bm.faces if predicate(f)]
    bmesh.ops.delete(part.bm, geom=doomed, context="FACES")
    return len(doomed)


def island_info(part, group):
    vs = {v for f in group for v in f.verts}
    mn = Vector((min(v.co.x for v in vs), min(v.co.y for v in vs), min(v.co.z for v in vs)))
    mx = Vector((max(v.co.x for v in vs), max(v.co.y for v in vs), max(v.co.z for v in vs)))
    mats = {part.obj.data.materials[f.material_index].name for f in group}
    return mats, (mn + mx) / 2, mx - mn


def delete_islands(part, predicate):
    """Deletes connected pieces of the original mesh for which predicate(materials, centre, size) holds."""
    bmesh.ops.remove_doubles(part.bm, verts=part.bm.verts, dist=1e-4)
    doomed = []
    for g in islands(part.bm):
        mats, c, s = island_info(part, g)
        if predicate(mats, c, s):
            doomed.extend(g)
    if doomed:
        verts = ordered(v for f in doomed for v in f.verts)
        bmesh.ops.delete(part.bm, geom=verts, context="VERTS")
    return len(doomed)


def restyle_islands(part, predicate, mat):
    """Moves pieces for which predicate(materials, centre, size) holds onto material `mat`."""
    bmesh.ops.remove_doubles(part.bm, verts=part.bm.verts, dist=1e-4)
    idx = part.mat(mat)
    n = 0
    for g in islands(part.bm):
        mats, c, s = island_info(part, g)
        if predicate(mats, c, s):
            for f in g:
                f.material_index = idx
            n += 1
    return n


def bvh(part):
    from mathutils.bvhtree import BVHTree
    part.bm.normal_update()
    return BVHTree.FromBMesh(part.bm)


def hit(tree, origin, direction, distance=10.0):
    """First surface along a ray: (location, normal) or (None, None)."""
    loc, normal, _, _ = tree.ray_cast(Vector(origin), Vector(direction).normalized(), distance)
    if loc is None:
        return None, None
    if normal.dot(Vector(direction)) > 0:
        normal = -normal
    return loc, normal


def decal(part, mat, tree, origin, direction, radius, up=Vector((0, 0, 1)), rings=4, segments=24, lift=0.003,
          right=None):
    """A round decal shrink-wrapped onto the surface hit by a ray from `origin` (roundels on curved panels).
    UV0 spans 0..1 across it, upright with respect to `up`."""
    centre, normal = hit(tree, origin, direction)
    if centre is None:
        print(f"DETAIL WARNING decal ray from {tuple(origin)} missed")
        return []
    u = right if right is not None else up.cross(normal)
    if u.length < 1e-4:
        u = Vector((1, 0, 0)).cross(normal)
    u.normalize()
    v = normal.cross(u).normalized()
    bm = part.bm
    grid = []
    for k in range(rings + 1):
        r = radius * k / rings
        ring = []
        for i in range(segments if k else 1):
            a = TAU * i / segments
            p = centre + (u * math.cos(a) + v * math.sin(a)) * r
            loc, n = hit(tree, p + normal * 0.05, -normal, 0.2)
            q = (loc if loc is not None else p) + normal * lift
            vert = bm.verts.new(q)
            ring.append((vert, (math.cos(a) * r / (2 * radius) + 0.5, math.sin(a) * r / (2 * radius) + 0.5)))
        grid.append(ring)
    faces = []
    for k in range(rings):
        inner, outer = grid[k], grid[k + 1]
        for i in range(segments):
            j = (i + 1) % segments
            if k == 0:
                corners = (inner[0], outer[i], outer[j])
            else:
                corners = (inner[i], outer[i], outer[j], inner[j])
            f = bm.faces.new([c[0] for c in corners])
            for loop, c in zip(f.loops, corners):
                loop[part.uv0].uv = c[1]
            f.normal_update()
            if f.normal.dot(normal) < 0:
                f.normal_flip()
            faces.append(f)
    return part.finish(faces, mat)


def islands(bm, faces=None):
    """Connected face groups (by shared vertices)."""
    remaining = ordered(faces if faces is not None else bm.faces)
    faces = set(remaining)
    out = []
    for seed in remaining:
        if seed not in faces:
            continue
        faces.discard(seed)
        group = {seed}
        stack = [seed]
        while stack:
            f = stack.pop()
            for v in f.verts:
                for g in v.link_faces:
                    if g in faces:
                        faces.discard(g)
                        group.add(g)
                        stack.append(g)
        out.append(group)
    return out
