"""Bakes a lawn's top surface into a height grid for ATrackLife, which roots its grass tufts on it.

    .\\Tools\\ue\\run.ps1 bake_lawn_heights.py -NullRhi

Violet's verge (Object_11, the sim's dirt dressed as lawn) is not flat: it crowns 0..27 cm above the road, so
grass placed at road height would be buried. The sim never needs this height (its dirt grid is 2D), so it lives
beside the track file: Data/Tracks/<id>.lawn.json, staged with the track data.

  { "cellM": 0.5, "minX": .., "minZ": .., "cols": .., "rows": .., "heightsCm": "<base64 int16 LE, row-major>" }

x and z are three.js metres (the track JSON's frame), heights are cm in UE space (the rebased road is at 0).
Cells no upward-facing lawn triangle covers hold -32768. Every upward triangle (normal z > 0.5) is rasterised
at cell centres and the highest surface wins.
"""
import base64
import json
import math
import os
import struct

import unreal

LAWNS = [
    # (track id, static mesh)
    ("violet-loop", "/Game/Tracks/VioletLoop/violet_loop/StaticMeshes/Object_11"),
]
CELL = 0.5
NODATA = -32768
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def log(msg):
    unreal.log(f"bake_lawn_heights: {msg}")


def triangles(mesh):
    """Mesh-space triangles (cm); imported prefab axes are (three x, three z, three y) x 100."""
    desc = mesh.get_static_mesh_description(0)
    for t in range(desc.get_triangle_count()):
        corners = desc.get_triangle_vertex_instances(unreal.TriangleID(t))
        yield [desc.get_vertex_position(desc.get_vertex_instance_vertex(vi)) for vi in list(corners)[:3]]


def bake(track_id, asset):
    mesh = unreal.load_asset(asset)
    if mesh is None:
        unreal.log_error(f"bake_lawn_heights: {asset} not found")
        return
    tris = []
    for a, b, c in triangles(mesh):
        ux, uy, uz = b.x - a.x, b.y - a.y, b.z - a.z
        vx, vy, vz = c.x - a.x, c.y - a.y, c.z - a.z
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        length = math.sqrt(nx * nx + ny * ny + nz * nz)
        if length < 1e-6 or abs(nz) / length < 0.5:
            continue  # walls, skirts and slivers
        tris.append(((a.x / 100, a.y / 100, a.z), (b.x / 100, b.y / 100, b.z), (c.x / 100, c.y / 100, c.z)))
    min_x = math.floor(min(min(p[0] for p in t) for t in tris))
    min_z = math.floor(min(min(p[1] for p in t) for t in tris))
    max_x = math.ceil(max(max(p[0] for p in t) for t in tris))
    max_z = math.ceil(max(max(p[1] for p in t) for t in tris))
    cols, rows = int((max_x - min_x) / CELL) + 1, int((max_z - min_z) / CELL) + 1
    grid = [NODATA] * (cols * rows)
    for (ax, az, ay), (bx, bz, by), (cx, cz, cy) in tris:
        det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
        if abs(det) < 1e-9:
            continue
        i0, i1 = int((min(ax, bx, cx) - min_x) / CELL), int((max(ax, bx, cx) - min_x) / CELL) + 1
        j0, j1 = int((min(az, bz, cz) - min_z) / CELL), int((max(az, bz, cz) - min_z) / CELL) + 1
        for j in range(max(j0, 0), min(j1 + 1, rows)):
            pz = min_z + (j + 0.5) * CELL
            for i in range(max(i0, 0), min(i1 + 1, cols)):
                px = min_x + (i + 0.5) * CELL
                w0 = ((bz - cz) * (px - cx) + (cx - bx) * (pz - cz)) / det
                w1 = ((cz - az) * (px - cx) + (ax - cx) * (pz - cz)) / det
                w2 = 1.0 - w0 - w1
                if w0 < -1e-4 or w1 < -1e-4 or w2 < -1e-4:
                    continue
                h = int(round(w0 * ay + w1 * by + w2 * cy))
                k = j * cols + i
                if h > grid[k]:
                    grid[k] = h
    covered = sum(1 for h in grid if h != NODATA)
    out = {
        "cellM": CELL, "minX": min_x, "minZ": min_z, "cols": cols, "rows": rows,
        "heightsCm": base64.b64encode(struct.pack(f"<{len(grid)}h", *grid)).decode("ascii"),
    }
    path = os.path.join(PROJECT, "Data", "Tracks", f"{track_id}.lawn.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(out, f, separators=(",", ":"))
    log(f"{track_id}: {len(tris)} lawn triangles, {cols}x{rows} cells, {covered} covered -> {path}")


try:
    for track, asset in LAWNS:
        bake(track, asset)
except Exception as error:  # noqa: BLE001 - report and still quit a headless run
    import traceback
    unreal.log_error(f"bake_lawn_heights: failed: {error}\n{traceback.format_exc()}")
finally:
    if os.environ.get("KART_QUIT") == "1":
        unreal.SystemLibrary.quit_editor()
