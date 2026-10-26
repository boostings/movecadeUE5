"""Mobile content budget report: every mesh and texture under /Game with what it costs on a phone.

    .\\Tools\\ue\\run.ps1 mobile_content_report.py -NullRhi

Writes Saved/MobileAudit/content/*.csv and logs the totals and the worst offenders:
  - skeletal meshes: LODs, vertices per LOD, morph targets (the mobile renderer applies morph targets on the CPU:
    every active morph on a visible mesh is rebuilt on the render thread each frame);
  - static meshes: triangles at LOD0, LOD count, Nanite and its fallback (phones draw the fallback mesh);
  - textures: size, mips, streaming, LOD group and the estimated resident size on a phone (ASTC 4x4 = 1 byte/texel
    for colour, 8x8 for normals is not used here: the estimate stays conservative at 1 byte/texel).
Read-only: nothing is modified or saved.
"""
import csv
import os

import unreal

quit_after = os.environ.get("KART_QUIT") == "1"
ROOTS = ["/Game/Avatars", "/Game/Karts", "/Game/Tracks", "/Game/FX", "/Game/UI", "/Game/Vehicles"]
OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "MobileAudit", "content")

eal = unreal.EditorAssetLibrary
sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)


def log(msg):
    unreal.log(f"mobile_content_report: {msg}")


def assets_of(class_name):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    found = []
    for root in ROOTS:
        flt = unreal.ARFilter(package_paths=[root], recursive_paths=True,
                              class_paths=[unreal.TopLevelAssetPath("/Script/Engine", class_name)])
        found += [str(a.package_name) + "." + str(a.asset_name) for a in registry.get_assets(flt)]
    return sorted(set(found))


def skeletal_rows():
    rows = []
    for path in assets_of("SkeletalMesh"):
        mesh = unreal.load_asset(path)
        if mesh is None:
            continue
        lods = unreal.EditorSkeletalMeshLibrary.get_lod_count(mesh) if hasattr(unreal, "EditorSkeletalMeshLibrary") else mesh.get_lod_num()
        verts = []
        for lod in range(lods):
            try:
                verts.append(unreal.EditorSkeletalMeshLibrary.get_num_verts(mesh, lod))
            except Exception:  # noqa: BLE001 - older API spellings
                verts.append(-1)
        morphs = mesh.get_editor_property("morph_targets") or []
        rows.append({"asset": path, "lods": lods, "verts_lod0": verts[0] if verts else -1,
                     "verts_by_lod": "/".join(str(v) for v in verts), "morph_targets": len(morphs)})
    return rows


def static_rows():
    rows = []
    for path in assets_of("StaticMesh"):
        mesh = unreal.load_asset(path)
        if mesh is None:
            continue
        lods = sms.get_lod_count(mesh)
        tris = -1
        try:
            tris = mesh.get_num_triangles(0)
        except Exception:  # noqa: BLE001
            pass
        nanite = mesh.get_editor_property("nanite_settings")
        enabled = bool(nanite.get_editor_property("enabled")) if nanite else False
        fallback = ""
        if enabled:
            for prop in ("fallback_percent_triangles", "fallback_relative_error", "fallback_target"):
                try:
                    fallback += f"{prop}={nanite.get_editor_property(prop)} "
                except Exception:  # noqa: BLE001
                    pass
        rows.append({"asset": path, "lods": lods, "tris_lod0": tris, "verts_lod0": sms.get_number_verts(mesh, 0),
                     "nanite": enabled, "nanite_fallback": fallback.strip()})
    return rows


def texture_rows():
    rows = []
    for path in assets_of("Texture2D"):
        tex = unreal.load_asset(path)
        if tex is None:
            continue
        x, y = tex.blueprint_get_size_x(), tex.blueprint_get_size_y()
        mips = tex.get_editor_property("mip_gen_settings")
        never = tex.get_editor_property("never_stream")
        group = tex.get_editor_property("lod_group")
        no_mips = str(mips).endswith("NO_MIPMAPS")
        # Resident estimate on a phone: ASTC 4x4 = 1 byte per texel, plus a third for the mip chain.
        mb = x * y * (1.0 if no_mips else 4.0 / 3.0) / (1024.0 * 1024.0)
        rows.append({"asset": path, "size": f"{x}x{y}", "texels": x * y, "mips": str(mips).split(".")[-1],
                     "never_stream": never, "lod_group": str(group).split(".")[-1], "phone_mb": round(mb, 2)})
    return rows


def write(name, rows):
    if not rows:
        return
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)


def run():
    skel, stat, tex = skeletal_rows(), static_rows(), texture_rows()
    write("skeletal.csv", skel)
    write("static.csv", stat)
    write("textures.csv", tex)
    log(f"{len(skel)} skeletal meshes, {len(stat)} static meshes, {len(tex)} textures -> {OUT}")
    for r in sorted(skel, key=lambda r: -r["verts_lod0"])[:12]:
        log(f"skeletal {r['asset']}: LODs {r['lods']} verts {r['verts_by_lod']} morphs {r['morph_targets']}")
    for r in sorted(stat, key=lambda r: -r["tris_lod0"])[:12]:
        log(f"static {r['asset']}: tris {r['tris_lod0']} LODs {r['lods']} nanite {r['nanite']} {r['nanite_fallback']}")
    total = sum(r["phone_mb"] for r in tex)
    log(f"textures: {total:.0f} MB if all resident at full size (phone estimate)")
    for r in sorted(tex, key=lambda r: -r["phone_mb"])[:15]:
        log(f"texture {r['asset']}: {r['size']} {r['mips']} group {r['lod_group']} never_stream {r['never_stream']} ~{r['phone_mb']} MB")


try:
    run()
except Exception as error:  # noqa: BLE001 - report and still quit a headless run
    unreal.log_error(f"mobile_content_report: failed: {error}")
    raise
finally:
    if quit_after:
        unreal.SystemLibrary.quit_editor()
