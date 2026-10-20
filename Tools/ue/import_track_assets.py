"""Imports the track exporter's binaries (Tools/track-export/out, from `npm run export`) into Content/Tracks.

Run headless:  .\\Tools\\ue\\run.ps1 import_track_assets.py
Optional env:  KART_IMPORT=violet,harbor,shared (default all), KART_MATERIALS_ONLY=1

- Master materials in /Game/Tracks/Shared/Materials, which ATrackBuilder instances per part:
  M_TrackVertexColor (lit, Color x vertex colour), M_TrackTextured (lit, Map x Color x vertex colour, UV
  Tiling and PanU/PanV), M_TrackUnlitTexture (signs), M_TrackTranslucent and M_TrackAdditive (unlit, Opacity).
- GLBs through Interchange: <dir>/<file>/StaticMeshes/<node> (+ Materials, Textures from the GLB).
- Canvas PNGs to <dir>/Textures/<name>.
- Violet's 8x8 palette textures sample nearest with no mips, so each surface keeps its flat colour.
- Nanite on the big opaque meshes (the Violet circuit, Harbor's prop batches).
"""
import os

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT, "Tools/track-export/out")
MAT_DIR = "/Game/Tracks/Shared/Materials"
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary

GLBS = [
    # (group, file under out/, destination, nanite)
    ("violet", "violet/violet_loop.glb", "/Game/Tracks/VioletLoop", True),
    ("violet", "violet/violet_kerbs.glb", "/Game/Tracks/VioletLoop", False),
    ("shared", "shared/violet_prefabs.glb", "/Game/Tracks/Shared", False),
    ("harbor", "harbor/harbor_ribbons.glb", "/Game/Tracks/SunsetHarbor", False),
    ("harbor", "harbor/harbor_scenery.glb", "/Game/Tracks/SunsetHarbor", True),
    ("harbor", "harbor/harbor_motion.glb", "/Game/Tracks/SunsetHarbor", False),
]
TEXTURES = [("violet", "violet/textures", "/Game/Tracks/VioletLoop/Textures"), ("harbor", "harbor/textures", "/Game/Tracks/SunsetHarbor/Textures")]


def log(msg):
    unreal.log(f"import_track_assets: {msg}")


# ---- Materials ---------------------------------------------------------------------------------

def new_material(name):
    """A fresh graph in an existing asset (references stay valid), or a new asset."""
    path = f"{MAT_DIR}/{name}"
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = tools.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    # Usage flags must be saved: an uncooked -game run will not add them and falls back to the default material.
    for flag in ("used_with_instanced_static_meshes", "used_with_nanite", "used_with_static_lighting"):
        try:
            m.set_editor_property(flag, True)
        except Exception:  # noqa: BLE001 - a flag this engine version lacks
            pass
    return m


def expr(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def vparam(m, name, x, y, value=(1, 1, 1, 1)):
    return expr(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*value))


def sparam(m, name, x, y, value):
    return expr(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def mul(m, a, b, x, y, a_out="", b_out=""):
    e = expr(m, unreal.MaterialExpressionMultiply, x, y)
    mel.connect_material_expressions(a, a_out, e, "A")
    mel.connect_material_expressions(b, b_out, e, "B")
    return e


def lit_outputs(m, base, x=-200):
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(sparam(m, "Roughness", x, 200, 0.8), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(sparam(m, "Metallic", x, 300, 0.0), "", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(mul(m, base, sparam(m, "Emissive", x - 200, 400, 0.0), x, 400), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def texture_sample(m, x, y):
    """Map(UV * Tiling + Time * (PanU, PanV))."""
    uv = mul(m, expr(m, unreal.MaterialExpressionTextureCoordinate, x - 800, y), sparam(m, "Tiling", x - 800, y + 100, 1.0), x - 600, y)
    speed = expr(m, unreal.MaterialExpressionAppendVector, x - 800, y + 300)
    mel.connect_material_expressions(sparam(m, "PanU", x - 1000, y + 300, 0.0), "", speed, "A")
    mel.connect_material_expressions(sparam(m, "PanV", x - 1000, y + 400, 0.0), "", speed, "B")
    pan = mul(m, expr(m, unreal.MaterialExpressionTime, x - 800, y + 200), speed, x - 600, y + 200)
    add = expr(m, unreal.MaterialExpressionAdd, x - 300, y)
    mel.connect_material_expressions(uv, "", add, "A")
    mel.connect_material_expressions(pan, "", add, "B")
    tex = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, x - 150, y, parameter_name="Map",
               texture=unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
    mel.connect_material_expressions(add, "", tex, "UVs")
    return tex


def make_materials():
    m = new_material("M_TrackVertexColor")
    m.set_editor_property("two_sided", True)
    lit_outputs(m, mul(m, vparam(m, "Color", -700, 0), expr(m, unreal.MaterialExpressionVertexColor, -700, 120), -450, 0))
    mel.recompile_material(m)

    m = new_material("M_TrackTextured")
    m.set_editor_property("two_sided", True)
    tex = texture_sample(m, -500, -200)
    tint = mul(m, vparam(m, "Color", -700, 100), expr(m, unreal.MaterialExpressionVertexColor, -700, 220), -500, 100)
    lit_outputs(m, mul(m, tex, tint, -300, 0, a_out="RGB"))
    mel.recompile_material(m)

    m = new_material("M_TrackUnlitTexture")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    tex = texture_sample(m, -500, -200)
    mel.connect_material_property(mul(m, tex, vparam(m, "Color", -700, 100), -300, 0, a_out="RGB"), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)

    for name, blend in (("M_TrackTranslucent", unreal.BlendMode.BLEND_TRANSLUCENT), ("M_TrackAdditive", unreal.BlendMode.BLEND_ADDITIVE)):
        m = new_material(name)
        m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        m.set_editor_property("blend_mode", blend)
        m.set_editor_property("two_sided", True)
        color = vparam(m, "Color", -700, 0)
        opacity = sparam(m, "Opacity", -700, 200, 0.5)
        if blend == unreal.BlendMode.BLEND_ADDITIVE:
            mel.connect_material_property(mul(m, color, opacity, -400, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        else:
            mel.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
            mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
        mel.recompile_material(m)
    for path in eal.list_assets(MAT_DIR):
        eal.save_asset(path, only_if_is_dirty=False)
    log("materials built")


# ---- Imports ------------------------------------------------------------------------------------

def import_file(src, dest):
    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = dest
    task.automated = True
    task.save = False
    task.replace_existing = True
    tools.import_asset_tasks([task])
    return list(task.imported_object_paths or [])


def set_nanite(mesh, enabled):
    settings = mesh.get_editor_property("nanite_settings")
    if settings.get_editor_property("enabled") == enabled:
        return
    settings.set_editor_property("enabled", enabled)
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if hasattr(subsystem, "set_nanite_settings"):
        subsystem.set_nanite_settings(mesh, settings, apply_changes=True)
    else:
        mesh.set_editor_property("nanite_settings", settings)


def tidy_glb(dest, file, nanite):
    folder = f"{dest}/{os.path.splitext(os.path.basename(file))[0]}"
    count = 0
    for path in eal.list_assets(folder, recursive=True):
        asset = unreal.load_asset(path)
        if isinstance(asset, unreal.StaticMesh):
            count += 1
            if nanite:
                set_nanite(asset, True)
        elif isinstance(asset, unreal.Texture2D):
            size = asset.blueprint_get_size_x() if hasattr(asset, "blueprint_get_size_x") else 0
            if size and size <= 64:
                # A palette: one flat colour per 8x8 cell, so no filtering or mips may blend cells.
                asset.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
                asset.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        eal.save_asset(path, only_if_is_dirty=False)
    log(f"{file}: {count} static meshes in {folder}{' (Nanite)' if nanite else ''}")


def main():
    groups = set((os.environ.get("KART_IMPORT") or "violet,harbor,shared").split(","))
    make_materials()
    if os.environ.get("KART_MATERIALS_ONLY") == "1":
        return
    for group, file, dest, nanite in GLBS:
        if group not in groups:
            continue
        src = os.path.join(OUT, file)
        if not os.path.exists(src):
            unreal.log_error(f"import_track_assets: missing {src}; run npm run export in Tools/track-export")
            continue
        import_file(src, dest)
        tidy_glb(dest, file, nanite)
    for group, folder, dest in TEXTURES:
        if group not in groups:
            continue
        for name in sorted(os.listdir(os.path.join(OUT, folder))):
            if name.endswith(".png"):
                for path in import_file(os.path.join(OUT, folder, name), dest):
                    eal.save_asset(path.split(".")[0], only_if_is_dirty=False)
        log(f"textures -> {dest}")
    for path in eal.list_assets(MAT_DIR):
        eal.save_asset(path, only_if_is_dirty=False)


try:
    main()
except Exception as error:  # noqa: BLE001 - report and still quit a headless run
    unreal.log_error(f"import_track_assets: failed: {error}")
    raise
finally:
    if os.environ.get("KART_QUIT") == "1":
        unreal.SystemLibrary.quit_editor()
