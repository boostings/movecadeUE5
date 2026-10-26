"""Gives every Nanite mesh a fallback phones can draw faithfully (they have no Nanite and draw the fallback).

    .\\Tools\\ue\\run.ps1 mobile_nanite_fallback.py -NullRhi

Phones have no Nanite: they draw each mesh's fallback. The imports left the fallback at the engine's automatic
target (relative error 1.0), which on the tracks' merged-by-colour scenery meshes collapses small, thin parts: the
road's centre dashes become tan diamonds. A full-mesh fallback fixes that but triples the tracks' triangles on phones
(Sunset Harbor: 0.4M -> 2.5M per frame with shadows), so every mesh gets a reduced fallback at RELATIVE_ERROR (a
quarter of the automatic error): thin parts survive, dense flat areas still simplify. FULL_LIMIT > 0 would give
meshes up to that many triangles their full geometry instead.
Nanite itself, which PC draws, is untouched; the PC ray-tracing scene (Lumen hardware RT also uses the fallback) gets
more exact geometry. Idempotent; KART_NANITE_ROOTS=/Game/Tracks,/Game/Karts (default) picks the folders.
"""
import os
import stat

import unreal

quit_after = os.environ.get("KART_QUIT") == "1"
ROOTS = os.environ.get("KART_NANITE_ROOTS", "/Game/Tracks,/Game/Karts").split(",")
eal = unreal.EditorAssetLibrary
FULL_LIMIT = 0
RELATIVE_ERROR = 0.25


def log(msg):
    unreal.log(f"mobile_nanite_fallback: {msg}")


def writable(path):
    package = path.split(".")[0]
    content = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    file = os.path.join(content, package[len("/Game/"):] + ".uasset")
    if os.path.exists(file):
        os.chmod(file, stat.S_IREAD | stat.S_IWRITE)


def run():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    changed, total = 0, 0
    for root in ROOTS:
        flt = unreal.ARFilter(package_paths=[root], recursive_paths=True,
                              class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "StaticMesh")])
        for data in registry.get_assets(flt):
            path = str(data.package_name) + "." + str(data.asset_name)
            mesh = unreal.load_asset(path)
            settings = mesh.get_editor_property("nanite_settings") if mesh else None
            if settings is None or not settings.get_editor_property("enabled"):
                continue
            total += 1
            tris = mesh.get_num_nanite_triangles()
            full = tris <= FULL_LIMIT
            target = unreal.NaniteFallbackTarget.PERCENT_TRIANGLES if full else unreal.NaniteFallbackTarget.RELATIVE_ERROR
            if settings.get_editor_property("fallback_target") == target and (
                    settings.get_editor_property("fallback_percent_triangles") >= 1.0 if full
                    else abs(settings.get_editor_property("fallback_relative_error") - RELATIVE_ERROR) < 1e-4):
                continue
            settings.set_editor_property("fallback_target", target)
            settings.set_editor_property("fallback_percent_triangles", 1.0)
            settings.set_editor_property("fallback_relative_error", 1.0 if full else RELATIVE_ERROR)
            mesh.set_editor_property("nanite_settings", settings)
            writable(path)
            if not eal.save_loaded_asset(mesh, only_if_is_dirty=False):
                raise RuntimeError(f"{path}: save failed")
            log(f"{path}: {tris} Nanite triangles -> fallback {mesh.get_num_triangles(0)}")
            changed += 1
    log(f"done: {changed} of {total} Nanite meshes changed")


try:
    run()
except Exception as error:  # noqa: BLE001 - report and still quit a headless run
    unreal.log_error(f"mobile_nanite_fallback: failed: {error}")
    raise
finally:
    if quit_after:
        unreal.SystemLibrary.quit_editor()
