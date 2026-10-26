"""Adds phone LODs to the driver avatars (SK_Driver_*), keeping the PC look identical.

    .\\Tools\\ue\\run.ps1 make_avatar_lods.py -NullRhi -TimeoutSeconds 3600

The drivers are ~275k vertices with 13 face morph targets and a single LOD. On the mobile renderer morph targets are
applied on the CPU (GPU morphs need SM5), so every visible driver costs ~5 ms of render thread on a desktop CPU and
far more on a phone, and four to eight of them are on screen in most games. This script generates:
  LOD1: 12 % of the triangles, morph targets remapped (what iPhone 13 class phones draw: r.SkeletalMeshLODBias=1),
  LOD2:  5 % of the triangles, morph targets remapped (Low/Mid phones, r.SkeletalMeshLODBias=2).
Both LODs get a screen size of 0, so the desktop renderer, which runs with r.SkeletalMeshLODBias=0 at every preset
the game offers, never selects them: PC keeps drawing LOD0 exactly as before. Phones reach them only through the
device profiles' LOD bias (Config/DefaultDeviceProfiles.ini). Idempotent: meshes that already have the LODs with
these settings are left alone. KART_AVATAR_FILTER=<substring> limits the run to matching assets.
"""
import os
import stat

import unreal

quit_after = os.environ.get("KART_QUIT") == "1"
only = os.environ.get("KART_AVATAR_FILTER", "")
ROOT = "/Game/Avatars/Drivers"
# (LOD index, share of LOD0's triangles)
LODS = ((1, 0.12), (2, 0.05))

eal = unreal.EditorAssetLibrary
SK = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)


def log(msg):
    unreal.log(f"make_avatar_lods: {msg}")


def writable(asset_path):
    package = asset_path.split(".")[0]
    content = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    file = os.path.join(content, package[len("/Game/"):] + ".uasset")
    if os.path.exists(file):
        os.chmod(file, stat.S_IREAD | stat.S_IWRITE)


LOD_SETTINGS = "/Game/Avatars/SkeletalLODSettings_DriverPhone"


def lod_settings():
    """The shared LOD settings asset (USkeletalMeshLODSettings, a data asset): LOD0 as imported, then the phone LODs."""
    asset = unreal.load_asset(LOD_SETTINGS)
    if asset is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.SkeletalMeshLODSettings)
        name = LOD_SETTINGS.split("/")[-1]
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, "/Game/Avatars", unreal.SkeletalMeshLODSettings, factory)
    groups = []
    for index, share in ((0, 1.0),) + LODS:
        group = unreal.SkeletalMeshLODGroupSettings()
        rs = group.get_editor_property("reduction_settings")
        rs.set_editor_property("num_of_triangles_percentage", share)
        rs.set_editor_property("num_of_vert_percentage", share)
        rs.set_editor_property("remap_morph_targets", True)
        rs.set_editor_property("base_lod", 0)
        group.set_editor_property("reduction_settings", rs)
        # LOD0 keeps the engine default; the phone LODs never switch in by distance (see the module comment).
        group.set_editor_property("screen_size", unreal.PerPlatformFloat(1.0 if index == 0 else 0.0))
        groups.append(group)
    asset.set_editor_property("lod_groups", groups)
    writable(LOD_SETTINGS + "." + LOD_SETTINGS.split("/")[-1])
    eal.save_loaded_asset(asset, only_if_is_dirty=False)
    return asset


def process(path, settings):
    mesh = unreal.load_asset(path)
    if mesh is None:
        return False
    if mesh.get_editor_property("lod_settings") == settings and SK.get_lod_count(mesh) == len(LODS) + 1:
        log(f"{path}: already has the phone LODs")
        return False
    mesh.set_editor_property("lod_settings", settings)
    if not SK.regenerate_lod(mesh, len(LODS) + 1, False, False):
        raise RuntimeError(f"{path}: mesh reduction unavailable")
    verts = [SK.get_num_verts(mesh, i) for i in range(SK.get_lod_count(mesh))]
    morphs = len(mesh.get_editor_property("morph_targets") or [])
    writable(path)
    if not eal.save_loaded_asset(mesh, only_if_is_dirty=False):
        raise RuntimeError(f"{path}: save failed")
    log(f"{path}: LOD verts {verts}, {morphs} morph targets")
    return True


def run():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    flt = unreal.ARFilter(package_paths=[ROOT], recursive_paths=True,
                          class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "SkeletalMesh")])
    paths = sorted(str(a.package_name) + "." + str(a.asset_name) for a in registry.get_assets(flt))
    paths = [p for p in paths if "SK_Driver_" in p and only in p]
    settings = lod_settings()
    changed = sum(1 for p in paths if process(p, settings))
    log(f"done: {changed} of {len(paths)} drivers changed")


try:
    run()
except Exception as error:  # noqa: BLE001 - report and still quit a headless run
    unreal.log_error(f"make_avatar_lods: failed: {error}")
    raise
finally:
    if quit_after:
        unreal.SystemLibrary.quit_editor()
