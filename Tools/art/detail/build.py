"""Builds the detailed vehicles: Tools/art/.cache/out/vehicles/<v>.glb (decode.mjs) in,
Tools/art/.cache/detail/vehicles/<v>.glb out, plus detail_report.json (triangle counts, wheel hubs, anchors).

    "C:\\Program Files\\Blender Foundation\\Blender 5.0\\blender.exe" --background --factory-startup ^
        --python Tools/art/detail/build.py -- [kart moto coupe truck]

Deterministic: no randomness, so the same inputs give the same GLBs. Then import with
    .\\Tools\\ue\\run.ps1 import_karts.py @{ KART_ART = 'vehicles' }
"""
import json
import math
import os
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import kit  # noqa: E402
import vehicles  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SRC = os.path.join(ROOT, "Tools", "art", ".cache", "out", "vehicles")
DST = os.path.join(ROOT, "Tools", "art", ".cache", "detail", "vehicles")


def load(vid):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(SRC, f"{vid}.glb"))
    objs = list(bpy.context.scene.objects)
    worlds = {o.name: o.matrix_world.copy() for o in objs}
    for o in objs:
        o.parent = None
    for o in objs:
        if o.type == "MESH":
            o.data.transform(worlds[o.name])
            o.matrix_world.identity()
            o.data.name = o.name
        else:
            o.matrix_world = worlds[o.name]
    root = bpy.data.objects.get("root")
    if root is not None:
        bpy.data.objects.remove(root)
    return {o.name: o for o in bpy.context.scene.objects}


def stats(objs):
    return {name: kit.tri_count(o) for name, o in sorted(objs.items()) if o.type == "MESH"}


def export(vid):
    os.makedirs(DST, exist_ok=True)
    path = os.path.join(DST, f"{vid}.glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=False, export_apply=True,
                              export_yup=True, export_texcoords=True, export_normals=True, export_tangents=False,
                              export_materials="EXPORT", export_cameras=False, export_lights=False,
                              export_extras=False, export_animations=False)
    return path


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ids = argv or ["kart", "moto", "coupe", "truck"]
    report_path = os.path.join(os.path.dirname(DST), "detail_report.json")
    report = {}
    if os.path.exists(report_path):
        with open(report_path, encoding="utf-8") as f:
            report = json.load(f)
    for vid in ids:
        objs = load(vid)
        before = stats(objs)
        info = vehicles.BUILD[vid](objs)
        objs = {o.name: o for o in bpy.context.scene.objects}
        for o in objs.values():
            if o.type == "MESH":
                kit.add_uv1(o)  # the untouched nodes (grips, the kart's body) get the detail-map UVs too
        after = stats(objs)
        path = export(vid)
        report[vid] = {"before": before, "after": after, "before_total": sum(before.values()),
                       "after_total": sum(after.values()), **info}
        print(f"DETAIL {vid}: {sum(before.values())} -> {sum(after.values())} tris; {path}")
        for k in sorted(after):
            print(f"DETAIL   {k}: {before.get(k, 0)} -> {after[k]}")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1, sort_keys=True)


main()
