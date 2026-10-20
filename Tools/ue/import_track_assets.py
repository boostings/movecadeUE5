"""Imports the track exporter's binaries (Tools/track-export/out, from `npm run export`) into Content/Tracks.

Run headless:  .\\Tools\\ue\\run.ps1 import_track_assets.py
Optional env:  KART_IMPORT=violet,harbor,neon,shared (default all), KART_MATERIALS_ONLY=1

- Master materials in /Game/Tracks/Shared/Materials, which ATrackBuilder instances per part:
  M_TrackVertexColor (lit, Color x vertex colour), M_TrackTextured (lit, Map x Color x vertex colour, UV
  Tiling and PanU/PanV), M_TrackUnlitTexture (signs), M_TrackTranslucent and M_TrackAdditive (unlit, Opacity),
  M_TrackSurface / M_TrackSurfaceWall (Map x Color x vertex colour x a world-space CC0 detail set: colour
  divided by the set's mean, world-space normal, roughness; top-down or dominant-axis projection) and
  M_TrackBoostPad (emissive chevrons that scroll forward and pulse), M_TrackFoliage (M_TrackSurfaceWall with
  two-sided foliage shading: hedges, crowns, lavender), M_TrackCrystal (glossy facets with an inner glow),
  M_TrackGlass (lit translucent glazing), M_TrackWater (single layer water: depth-tinted, two scrolling
  normal maps, real reflections) and M_TrackSky (gradient dome, sun, horizon glow and a scrolling cloud layer).
- The CC0 surface sets (out/shared/surfaces, normal maps already flipped to DirectX) to /Game/Tracks/Shared/Surfaces.
- GLBs through Interchange: <dir>/<file>/StaticMeshes/<node> (+ Materials, Textures from the GLB).
- Canvas PNGs to <dir>/Textures/<name>.
- Violet's 8x8 palette textures sample nearest with no mips, so each surface keeps its flat colour.
- Nanite on the big opaque meshes (the Violet circuit, Harbor's prop batches, Neon City's buildings).
- STALE: assets an older export made that the current one no longer has (the Violet IP cleanup) are deleted.
"""
import os
import stat

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT, "Tools/track-export/out")
MAT_DIR = "/Game/Tracks/Shared/Materials"
SURFACE_DIR = "/Game/Tracks/Shared/Surfaces"
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary

GLBS = [
    # (group, file under out/, destination, nanite)
    ("violet", "violet/violet_loop.glb", "/Game/Tracks/VioletLoop", True),
    ("violet", "violet/violet_garden.glb", "/Game/Tracks/VioletLoop", True),
    ("violet", "violet/violet_garden_props.glb", "/Game/Tracks/VioletLoop", True),
    ("shared", "shared/violet_prefabs.glb", "/Game/Tracks/Shared", False),
    ("shared", "shared/movecade_props.glb", "/Game/Tracks/Shared", False),
    ("harbor", "harbor/harbor_ribbons.glb", "/Game/Tracks/SunsetHarbor", False),
    ("harbor", "harbor/harbor_scenery.glb", "/Game/Tracks/SunsetHarbor", True),
    ("harbor", "harbor/harbor_motion.glb", "/Game/Tracks/SunsetHarbor", False),
    ("harbor", "harbor/harbor_coast.glb", "/Game/Tracks/SunsetHarbor", True),
    ("neon", "neon/neon_ribbons.glb", "/Game/Tracks/NeonCity", False),
    ("neon", "neon/neon_scenery.glb", "/Game/Tracks/NeonCity", True),
]
TEXTURES = [
    ("violet", "violet/textures", "/Game/Tracks/VioletLoop/Textures"),
    ("harbor", "harbor/textures", "/Game/Tracks/SunsetHarbor/Textures"),
    ("neon", "neon/textures", "/Game/Tracks/NeonCity/Textures"),
    ("shared", "shared/textures", "/Game/Tracks/Shared/Textures"),
]
# The Violet rebuild (Tools/track-export/src/violetMesh.mjs, violetGarden.ts) no longer exports these: the
# warp pipes (Object_12, material_7), the brick blocks (Object_47, material_3), the barrier blocks
# (Object_13, material_8), the start structure (Object_10, material_5), the hills (Object_18, material_12),
# the speed quad (Object_27, material_21) and the old kerbs.
STALE = [
    "/Game/Tracks/VioletLoop/violet_loop/StaticMeshes/Object_10",
    "/Game/Tracks/VioletLoop/violet_loop/StaticMeshes/Object_13",
    "/Game/Tracks/VioletLoop/violet_loop/StaticMeshes/Object_18",
    "/Game/Tracks/VioletLoop/violet_loop/StaticMeshes/Object_27",
    "/Game/Tracks/VioletLoop/violet_loop/Materials/material_5",
    "/Game/Tracks/VioletLoop/violet_loop/Materials/material_8",
    "/Game/Tracks/VioletLoop/violet_loop/Materials/material_12",
    "/Game/Tracks/VioletLoop/violet_loop/Materials/material_21",
    "/Game/Tracks/VioletLoop/violet_loop/Textures/material_5_basecolor",
    "/Game/Tracks/VioletLoop/violet_loop/Textures/material_8_recoloured",
    "/Game/Tracks/VioletLoop/violet_loop/Textures/material_12_basecolor",
    "/Game/Tracks/VioletLoop/violet_loop/Textures/material_21_basecolor",
    "/Game/Tracks/VioletLoop/violet_loop/Textures/material_21_emissive",
    "/Game/Tracks/VioletLoop/violet_kerbs/StaticMeshes/violet_kerbs",
    "/Game/Tracks/VioletLoop/violet_kerbs/Materials/violet_kerbs",
    "/Game/Tracks/VioletLoop/violet_loop/StaticMeshes/Object_12",
    "/Game/Tracks/VioletLoop/violet_loop/StaticMeshes/Object_47",
    "/Game/Tracks/VioletLoop/violet_loop/Materials/material_7",
    "/Game/Tracks/VioletLoop/violet_loop/Materials/material_3",
    "/Game/Tracks/VioletLoop/violet_loop/Textures/material_7_basecolor",
    "/Game/Tracks/VioletLoop/violet_loop/Textures/material_3_basecolor",
    # The barrier palette is recoloured and exported as material_8_recoloured (a reimport keeps old pixels).
    "/Game/Tracks/VioletLoop/violet_loop/Textures/material_8_basecolor",
]


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
            # Light shafts: fade toward the silhouette (where the surface turns edge-on), so no hard outline.
            fres = expr(m, unreal.MaterialExpressionFresnel, -900, 300, exponent=1.5, base_reflect_fraction=0.0)
            facing = op(m, unreal.MaterialExpressionSubtract, expr(m, unreal.MaterialExpressionConstant, -900, 400, r=1.0), fres, -700, 300)
            soft = mul(m, facing, facing, -550, 300)
            mel.connect_material_property(mul(m, mul(m, color, opacity, -400, 0), soft, -250, 100), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        else:
            mel.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
            mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
        mel.recompile_material(m)
    make_surface_material("M_TrackSurface", wall=False)
    make_surface_material("M_TrackSurfaceWall", wall=True)
    make_surface_material("M_TrackFoliage", wall=True, foliage=True)
    make_crystal_material()
    make_glass_material()
    make_water_material()
    make_boost_pad_material()
    make_sky_material()
    for path in eal.list_assets(MAT_DIR):
        eal.save_asset(path, only_if_is_dirty=False)
    log("materials built")


def mask(m, src, x, y, r=False, g=False, b=False, a=False):
    e = expr(m, unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=a)
    mel.connect_material_expressions(src, "", e, "")
    return e


def op(m, cls, a, b, x, y, a_out="", b_out=""):
    e = expr(m, cls, x, y)
    mel.connect_material_expressions(a, a_out, e, "A")
    mel.connect_material_expressions(b, b_out, e, "B")
    return e


def const3(m, v, x, y):
    return expr(m, unreal.MaterialExpressionConstant3Vector, x, y, constant=unreal.LinearColor(v[0], v[1], v[2], 1))


def if_greater(m, a, b, greater, less, x, y):
    """A > B ? greater : less (equal goes to greater)."""
    e = expr(m, unreal.MaterialExpressionIf, x, y)
    mel.connect_material_expressions(a, "", e, "A")
    mel.connect_material_expressions(b, "", e, "B")
    # The If node's pins go by their display names.
    mel.connect_material_expressions(greater, "", e, "A > B")
    mel.connect_material_expressions(greater, "", e, "A == B")
    mel.connect_material_expressions(less, "", e, "A < B")
    return e


def surface_texture(m, name, set_id, kind, uv, x, y):
    tex = unreal.load_asset(f"{SURFACE_DIR}/T_{set_id}_{kind}")
    sampler = {"color": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
               "normal": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
               "rough": unreal.MaterialSamplerType.SAMPLERTYPE_MASKS}[kind]
    e = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, texture=tex, sampler_type=sampler)
    mel.connect_material_expressions(uv, "", e, "UVs")
    return e


def make_surface_material(name, wall, foliage=False):
    """Map(UV0) x Color x vertex colour x CC0 detail (trackSurfaces.ts / worldSurface.ts on the web).

    Detail UVs are world metres / DetailTileM: top-down (XY) on ground, or on the plane of the dominant normal
    axis for walls and props (wall=True). The detail colour is divided by the set's linear mean (DetailMean)
    and blended in by DetailAlbedo, so a palette colour keeps its hue. Normals are world space (no tangents
    needed: the Violet palette UVs are degenerate): the detail normal's XY tilts the vertex normal in the
    projection plane by DetailNormal. Roughness x lerp(1, 2 x rough map, DetailRough).

    foliage=True: two-sided foliage shading, light through the leaves tinted by SubsurfaceTint.
    """
    m = new_material(name)
    m.set_editor_property("two_sided", True)
    if foliage:
        m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    m.set_editor_property("tangent_space_normal", False)
    X = -2600
    base_map = texture_sample(m, -1700, -900)
    tint = mul(m, vparam(m, "Color", -1900, -500), expr(m, unreal.MaterialExpressionVertexColor, -1900, -380), -1650, -500)
    base_raw = mul(m, base_map, tint, -1400, -700, a_out="RGB")
    # BaseDesaturate tames a loud palette colour (Violet's sand) without touching its detail.
    base = expr(m, unreal.MaterialExpressionDesaturation, -1250, -700)
    mel.connect_material_expressions(base_raw, "", base, "")
    mel.connect_material_expressions(sparam(m, "BaseDesaturate", -1400, -600, 0.0), "", base, "Fraction")

    wp = expr(m, unreal.MaterialExpressionWorldPosition, X, 0)
    tile = mul(m, sparam(m, "DetailTileM", X, 120, 4.0), expr(m, unreal.MaterialExpressionConstant, X, 220, r=100.0), X + 200, 120)
    pos = op(m, unreal.MaterialExpressionDivide, wp, tile, X + 400, 0)
    if wall:
        n = expr(m, unreal.MaterialExpressionAbs, X + 400, 400)
        mel.connect_material_expressions(expr(m, unreal.MaterialExpressionVertexNormalWS, X + 200, 400), "", n, "")
        nx, ny, nz = (mask(m, n, X + 600, 360 + i * 80, **{c: True}) for i, c in enumerate("rgb"))
        uv_x, uv_y, uv_z = mask(m, pos, X + 600, 0, g=True, b=True), mask(m, pos, X + 600, 80, r=True, b=True), mask(m, pos, X + 600, 160, r=True, g=True)
        uv = if_greater(m, nx, op(m, unreal.MaterialExpressionMax, ny, nz, X + 800, 440),
                        uv_x, if_greater(m, ny, nz, uv_y, uv_z, X + 800, 200), X + 1000, 100)
    else:
        uv = mask(m, pos, X + 600, 0, r=True, g=True)

    color = surface_texture(m, "DetailColor", "asphalt_track", "color", uv, -1700, 0)
    normal = surface_texture(m, "DetailNormal", "asphalt_track", "normal", uv, -1700, 300)
    rough = surface_texture(m, "DetailRough", "asphalt_track", "rough", uv, -1700, 600)

    # Albedo: lerp(1, desaturated detail / mean, DetailAlbedo).
    lum = expr(m, unreal.MaterialExpressionDesaturation, -1450, 0)
    mel.connect_material_expressions(color, "RGB", lum, "")
    mel.connect_material_expressions(sparam(m, "Desaturate", -1650, 120, 0.0), "", lum, "Fraction")
    ratio = op(m, unreal.MaterialExpressionDivide, lum, vparam(m, "DetailMean", -1450, 120, (0.2, 0.2, 0.2, 1)), -1250, 0)
    clamp = expr(m, unreal.MaterialExpressionClamp, -1100, 0, min_default=0.0, max_default=3.0)
    mel.connect_material_expressions(ratio, "", clamp, "")
    detail = expr(m, unreal.MaterialExpressionLinearInterpolate, -950, 0)
    mel.connect_material_expressions(expr(m, unreal.MaterialExpressionConstant, -1100, 120, r=1.0), "", detail, "A")
    mel.connect_material_expressions(clamp, "", detail, "B")
    mel.connect_material_expressions(sparam(m, "DetailAlbedo", -1100, 200, 0.5), "", detail, "Alpha")
    final = mul(m, base, detail, -700, -300)
    mel.connect_material_property(final, "", unreal.MaterialProperty.MP_BASE_COLOR)
    if foliage:
        sss = mul(m, final, vparam(m, "SubsurfaceTint", -900, -420, (0.75, 0.9, 0.45, 1)), -500, -420)
        mel.connect_material_property(sss, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    mel.connect_material_property(mul(m, final, sparam(m, "Emissive", -900, -150, 0.0), -500, -150), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(sparam(m, "Metallic", -500, 900, 0.0), "", unreal.MaterialProperty.MP_METALLIC)

    # World-space normal: the detail XY in the projection plane, added to the vertex normal.
    nxy = mul(m, mask(m, normal, -1450, 300, r=True, g=True), sparam(m, "DetailNormal", -1450, 400, 0.5), -1250, 300)
    nxx, nyy = mask(m, nxy, -1100, 300, r=True), mask(m, nxy, -1100, 380, g=True)
    zero = expr(m, unreal.MaterialExpressionConstant, -1100, 460, r=0.0)

    def vec3(a, b, c, x, y):
        ab = expr(m, unreal.MaterialExpressionAppendVector, x, y)
        mel.connect_material_expressions(a, "", ab, "A")
        mel.connect_material_expressions(b, "", ab, "B")
        abc = expr(m, unreal.MaterialExpressionAppendVector, x + 150, y)
        mel.connect_material_expressions(ab, "", abc, "A")
        mel.connect_material_expressions(c, "", abc, "B")
        return abc

    if wall:
        offset = if_greater(m, nx, op(m, unreal.MaterialExpressionMax, ny, nz, -1100, 700),
                            vec3(zero, nxx, nyy, -950, 300),
                            if_greater(m, ny, nz, vec3(nxx, zero, nyy, -950, 400), vec3(nxx, nyy, zero, -950, 500), -700, 450), -550, 350)
    else:
        offset = vec3(nxx, nyy, zero, -950, 300)
    # The engine normalises a world-space Normal input itself.
    bent = op(m, unreal.MaterialExpressionAdd, expr(m, unreal.MaterialExpressionVertexNormalWS, -700, 600), offset, -400, 350)
    mel.connect_material_property(bent, "", unreal.MaterialProperty.MP_NORMAL)

    # Roughness x lerp(1, 2 x map, DetailRough).
    r2 = mul(m, mask(m, rough, -1450, 600, r=True), expr(m, unreal.MaterialExpressionConstant, -1450, 700, r=2.0), -1250, 600)
    rl = expr(m, unreal.MaterialExpressionLinearInterpolate, -1100, 600)
    mel.connect_material_expressions(expr(m, unreal.MaterialExpressionConstant, -1250, 700, r=1.0), "", rl, "A")
    mel.connect_material_expressions(r2, "", rl, "B")
    mel.connect_material_expressions(sparam(m, "DetailRough", -1250, 800, 0.5), "", rl, "Alpha")
    rr = mul(m, rl, sparam(m, "Roughness", -1100, 760, 0.8), -950, 650)
    rc = expr(m, unreal.MaterialExpressionClamp, -800, 650, min_default=0.04, max_default=1.0)
    mel.connect_material_expressions(rr, "", rc, "")
    mel.connect_material_property(rc, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)


def make_crystal_material():
    """Crystals: glossy faceted base plus an inner glow that is strongest where a facet faces the viewer
    (1 - Fresnel) and toward the tips (world height above the instance pivot, per metre of GlowRise)."""
    m = new_material("M_TrackCrystal")
    color = mul(m, vparam(m, "Color", -1100, 0), expr(m, unreal.MaterialExpressionVertexColor, -1100, 150), -900, 0)
    mel.connect_material_property(mul(m, color, expr(m, unreal.MaterialExpressionConstant, -900, 120, r=0.32), -600, 0), "", unreal.MaterialProperty.MP_BASE_COLOR)
    fres = expr(m, unreal.MaterialExpressionFresnel, -1100, 300, exponent=2.5, base_reflect_fraction=0.0)
    facing = op(m, unreal.MaterialExpressionSubtract, expr(m, unreal.MaterialExpressionConstant, -1100, 400, r=1.0), fres, -900, 300)
    height = op(m, unreal.MaterialExpressionSubtract, mask(m, expr(m, unreal.MaterialExpressionWorldPosition, -1300, 550), -1150, 550, b=True),
                mask(m, expr(m, unreal.MaterialExpressionObjectPositionWS, -1300, 650), -1150, 650, b=True), -1000, 550)
    tips = expr(m, unreal.MaterialExpressionSaturate, -700, 550)
    mel.connect_material_expressions(op(m, unreal.MaterialExpressionDivide, height, mul(m, sparam(m, "GlowRise", -1000, 700, 3.0), expr(m, unreal.MaterialExpressionConstant, -1000, 780, r=100.0), -850, 700), -850, 550), "", tips, "")
    glow = op(m, unreal.MaterialExpressionAdd, mul(m, facing, facing, -700, 300), mul(m, tips, expr(m, unreal.MaterialExpressionConstant, -700, 650, r=0.6), -550, 600), -500, 400)
    glow = op(m, unreal.MaterialExpressionAdd, glow, expr(m, unreal.MaterialExpressionConstant, -500, 500, r=0.12), -350, 450)
    mel.connect_material_property(mul(m, mul(m, color, glow, -250, 300), sparam(m, "Emissive", -400, 620, 1.5), -100, 350), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(sparam(m, "Roughness", -300, 800, 0.12), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(expr(m, unreal.MaterialExpressionConstant, -300, 900, r=0.9), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(m)


def make_glass_material():
    """Glazing: lit translucency (forward shaded per pixel, so the sun and sky reflect), tinted, see-through."""
    m = new_material("M_TrackGlass")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    for mode in ("TLM_SURFACE_PER_PIXEL_LIGHTING", "TLM_SURFACE"):
        if hasattr(unreal.TranslucencyLightingMode, mode):
            m.set_editor_property("translucency_lighting_mode", getattr(unreal.TranslucencyLightingMode, mode))
            break
    color = mul(m, vparam(m, "Color", -700, 0), expr(m, unreal.MaterialExpressionVertexColor, -700, 120), -450, 0)
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(sparam(m, "Roughness", -450, 200, 0.05), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(sparam(m, "Metallic", -450, 300, 0.0), "", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(expr(m, unreal.MaterialExpressionConstant, -450, 400, r=1.0), "", unreal.MaterialProperty.MP_SPECULAR)
    fres = expr(m, unreal.MaterialExpressionFresnel, -700, 500, exponent=4.0, base_reflect_fraction=0.04)
    opacity = op(m, unreal.MaterialExpressionAdd, sparam(m, "Opacity", -700, 650, 0.2), mul(m, fres, expr(m, unreal.MaterialExpressionConstant, -700, 750, r=0.6), -500, 600), -300, 550)
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    mel.connect_material_property(mul(m, color, sparam(m, "Emissive", -700, 900, 0.0), -300, 850), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)


def panning_normal(m, tex, tile_m, speed, x, y):
    """A world-space (top-down) normal map sample: UV = world XY / (TileM x 100) + Time x speed."""
    wp = mask(m, expr(m, unreal.MaterialExpressionWorldPosition, x - 900, y), x - 750, y, r=True, g=True)
    uv = op(m, unreal.MaterialExpressionDivide, wp, mul(m, tile_m, expr(m, unreal.MaterialExpressionConstant, x - 900, y + 100, r=100.0), x - 750, y + 100), x - 600, y)
    pan = mul(m, expr(m, unreal.MaterialExpressionTime, x - 750, y + 200), expr(m, unreal.MaterialExpressionConstant2Vector, x - 750, y + 280, r=speed[0], g=speed[1]), x - 600, y + 200)
    uvp = op(m, unreal.MaterialExpressionAdd, uv, pan, x - 450, y)
    e = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, x - 300, y, parameter_name=tex, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
             texture=unreal.load_asset("/Engine/EngineMaterials/DefaultNormal"))
    mel.connect_material_expressions(uvp, "", e, "UVs")
    return e


def make_water_material():
    """Single layer water: the engine absorbs and scatters light through the depth behind the surface (so a
    sloped seabed reads turquoise in the shallows and deep blue further out), Lumen reflects the sky and
    the scenery. Two normal maps (WaveNormal, a generated wave texture) scroll across each other in world
    space; a third, coarser sample breaks up the tiling."""
    # Rebuilt from scratch: delete_all_material_expressions leaves the water output node behind.
    if eal.does_asset_exist(f"{MAT_DIR}/M_TrackWater"):
        eal.delete_asset(f"{MAT_DIR}/M_TrackWater")
    m = new_material("M_TrackWater")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    m.set_editor_property("two_sided", False)
    a = panning_normal(m, "WaveNormal", sparam(m, "WaveTileA", -1600, -300, 9.0), (0.006, 0.0035), -400, -300)
    b = panning_normal(m, "WaveNormal", sparam(m, "WaveTileB", -1600, 200, 23.0), (-0.0025, 0.004), -400, 200)
    c = panning_normal(m, "WaveNormal", sparam(m, "WaveTileC", -1600, 700, 71.0), (0.0012, -0.001), -400, 700)
    ab = op(m, unreal.MaterialExpressionAdd, op(m, unreal.MaterialExpressionAdd, a, b, -150, 0), c, -50, 200)
    flat = mul(m, ab, expr(m, unreal.MaterialExpressionConstant3Vector, -150, 350, constant=unreal.LinearColor(1, 1, 0, 1)), 50, 200)
    strength = mul(m, flat, sparam(m, "WaveStrength", -150, 450, 0.35), 150, 250)
    normal = op(m, unreal.MaterialExpressionAdd, strength, expr(m, unreal.MaterialExpressionConstant3Vector, 150, 400, constant=unreal.LinearColor(0, 0, 1, 1)), 300, 250)
    mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
    # Base colour is the surface film only (scattering and absorption give the water its colour); foam on the
    # shore is a separate translucent ribbon, since scene depth is not readable from a single layer water pass.
    mel.connect_material_property(vparam(m, "Color", -300, 1100, (0.02, 0.03, 0.03, 1)), "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(sparam(m, "Roughness", 0, 600, 0.04), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(sparam(m, "Metallic", 0, 700, 0.0), "", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(expr(m, unreal.MaterialExpressionConstant, 0, 800, r=1.0), "", unreal.MaterialProperty.MP_SPECULAR)
    out = mel.create_material_expression(m, unreal.MaterialExpressionSingleLayerWaterMaterialOutput, 400, 900)
    scatter = mul(m, vparam(m, "Scattering", 0, 900, (0.02, 0.12, 0.11, 1)), expr(m, unreal.MaterialExpressionConstant, 0, 1000, r=0.01), 200, 900)
    absorb = mul(m, vparam(m, "Absorption", 0, 1050, (0.45, 0.09, 0.06, 1)), expr(m, unreal.MaterialExpressionConstant, 0, 1150, r=0.01), 200, 1050)
    for name, src in (("ScatteringCoefficients", scatter), ("AbsorptionCoefficients", absorb)):
        if not mel.connect_material_expressions(src, "", out, name):
            mel.connect_material_expressions(src, "", out, name.replace("Coefficients", " Coefficients"))
    mel.connect_material_expressions(sparam(m, "PhaseG", 200, 1200, 0.2), "", out, "PhaseG")
    mel.recompile_material(m)


def make_sky_material():
    """The sky dome: a four-stop gradient (below, horizon, mid, top), a horizon glow toward the sun, the sun
    disc, and a cloud layer: CloudMap (a tileable density texture) projected onto a flat sky plane
    (view.xy / (view.z + 0.12), so clouds crowd toward the horizon), lit on the sun side (CloudLit) and
    shaded away from it (CloudShade). Unlit, flagged as sky so the real-time sky light captures it."""
    m = new_material("M_TrackSky")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("is_sky", True)
    view = mul(m, expr(m, unreal.MaterialExpressionCameraVectorWS, -2200, 0), expr(m, unreal.MaterialExpressionConstant, -2200, 100, r=-1.0), -2000, 0)
    z = mask(m, view, -1800, 0, b=True)
    sat = lambda src, x, y: (lambda e: (mel.connect_material_expressions(src, "", e, ""), e)[1])(expr(m, unreal.MaterialExpressionSaturate, x, y))

    def lerp3(a, b, alpha, x, y):
        e = expr(m, unreal.MaterialExpressionLinearInterpolate, x, y)
        mel.connect_material_expressions(a, "", e, "A")
        mel.connect_material_expressions(b, "", e, "B")
        mel.connect_material_expressions(alpha, "", e, "Alpha")
        return e

    def smooth(src, lo, hi, x, y):
        e = expr(m, unreal.MaterialExpressionSmoothStep, x, y)
        mel.connect_material_expressions(src, "", e, "Value")
        mel.connect_material_expressions(expr(m, unreal.MaterialExpressionConstant, x - 150, y + 60, r=lo), "", e, "Min")
        mel.connect_material_expressions(expr(m, unreal.MaterialExpressionConstant, x - 150, y + 120, r=hi), "", e, "Max")
        return e

    up = expr(m, unreal.MaterialExpressionPower, -1500, -200)
    mel.connect_material_expressions(sat(z, -1650, -200), "", up, "Base")
    mel.connect_material_expressions(sparam(m, "TopCurve", -1650, -100, 0.55), "", up, "Exp")
    top = vparam(m, "SkyTop", -1300, -500, (0.05, 0.16, 0.66, 1))
    mid = vparam(m, "SkyMid", -1300, -350, (0.3, 0.45, 0.8, 1))
    horizon = vparam(m, "SkyHorizon", -1300, 0, (0.66, 0.79, 0.91, 1))
    below = vparam(m, "SkyBelow", -1300, 300, (0.4, 0.52, 0.67, 1))
    upper = lerp3(lerp3(horizon, mid, smooth(up, 0.0, 0.45, -1100, -150), -900, -300), top, smooth(up, 0.35, 1.0, -1100, -50), -700, -300)
    down = mul(m, op(m, unreal.MaterialExpressionSubtract, expr(m, unreal.MaterialExpressionConstant, -1650, 300, r=0.0), z, -1500, 300),
               expr(m, unreal.MaterialExpressionConstant, -1650, 400, r=4.0), -1350, 400)
    lower = lerp3(horizon, below, sat(down, -1150, 400), -900, 250)
    sky = lerp3(lower, upper, sat(mul(m, z, expr(m, unreal.MaterialExpressionConstant, -1100, 150, r=200.0), -950, 100), -850, 150), -600, 0)

    # Sun: glow pow(max(dot, 0), GlowPower) x SunGlow, a wide horizon glow, and the disc.
    sun_dir = vparam(m, "SunDir", -2000, 750, (0.5, 0.5, 0.5, 1))
    d = expr(m, unreal.MaterialExpressionDotProduct, -1800, 700)
    mel.connect_material_expressions(view, "", d, "A")
    mel.connect_material_expressions(sun_dir, "", d, "B")
    ds = sat(d, -1650, 700)
    glow = expr(m, unreal.MaterialExpressionPower, -1500, 700)
    mel.connect_material_expressions(ds, "", glow, "Base")
    mel.connect_material_expressions(sparam(m, "GlowPower", -1650, 800, 24.0), "", glow, "Exp")
    wide = expr(m, unreal.MaterialExpressionPower, -1500, 900)
    mel.connect_material_expressions(ds, "", wide, "Base")
    mel.connect_material_expressions(expr(m, unreal.MaterialExpressionConstant, -1650, 950, r=3.0), "", wide, "Exp")
    abs_z = expr(m, unreal.MaterialExpressionAbs, -1800, 1100)
    mel.connect_material_expressions(z, "", abs_z, "")
    near_horizon = op(m, unreal.MaterialExpressionSubtract, expr(m, unreal.MaterialExpressionConstant, -1650, 1050, r=1.0), sat(mul(m, abs_z, expr(m, unreal.MaterialExpressionConstant, -1800, 1180, r=3.0), -1650, 1120), -1500, 1120), -1350, 1050)
    horizon_glow = mul(m, mul(m, wide, near_horizon, -1200, 950), sparam(m, "HorizonGlow", -1350, 1150, 0.5), -1050, 950)
    disc = sat(mul(m, op(m, unreal.MaterialExpressionSubtract, ds, sparam(m, "SunCos", -1650, 1250, 0.9995), -1500, 1250),
                   expr(m, unreal.MaterialExpressionConstant, -1650, 1350, r=4000.0), -1350, 1250), -1200, 1250)
    sun_color = vparam(m, "SunColor", -1200, 1400, (1, 0.95, 0.85, 1))
    sun_amount = op(m, unreal.MaterialExpressionAdd, op(m, unreal.MaterialExpressionAdd, mul(m, glow, sparam(m, "SunGlow", -1500, 1450, 0.6), -1300, 750), horizon_glow, -1100, 800),
                    mul(m, disc, sparam(m, "SunDisc", -1350, 1500, 8.0), -1100, 1300), -950, 900)
    with_sun = op(m, unreal.MaterialExpressionAdd, sky, mul(m, sun_amount, sun_color, -800, 900), -450, 300)

    # Clouds on a sky plane, above the horizon only.
    zc = op(m, unreal.MaterialExpressionAdd, z, expr(m, unreal.MaterialExpressionConstant, -1800, 1650, r=0.12), -1650, 1600)
    plane = op(m, unreal.MaterialExpressionDivide, mask(m, view, -1800, 1550, r=True, g=True), zc, -1500, 1550)
    pan = mul(m, expr(m, unreal.MaterialExpressionTime, -1500, 1700), expr(m, unreal.MaterialExpressionConstant2Vector, -1500, 1780, r=0.004, g=0.0015), -1350, 1700)
    uv = op(m, unreal.MaterialExpressionAdd, mul(m, plane, sparam(m, "CloudScale", -1500, 1850, 0.35), -1350, 1550), pan, -1200, 1600)
    cloud_tex = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -1050, 1600, parameter_name="CloudMap", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                     texture=unreal.load_asset("/Engine/EngineResources/Black"))
    mel.connect_material_expressions(uv, "", cloud_tex, "UVs")
    density = mask(m, cloud_tex, -900, 1600, r=True)
    cover = op(m, unreal.MaterialExpressionSubtract, density, op(m, unreal.MaterialExpressionSubtract, expr(m, unreal.MaterialExpressionConstant, -1050, 1750, r=1.0), sparam(m, "Clouds", -1200, 1800, 0.5), -900, 1750), -750, 1650)
    alpha = sat(mul(m, cover, expr(m, unreal.MaterialExpressionConstant, -750, 1800, r=3.0), -600, 1700), -450, 1700)
    fade = sat(mul(m, z, expr(m, unreal.MaterialExpressionConstant, -750, 1950, r=7.0), -600, 1900), -450, 1900)
    cloud_alpha = mul(m, alpha, fade, -300, 1750)
    lit = lerp3(vparam(m, "CloudShade", -750, 2050, (0.5, 0.5, 0.6, 1)), vparam(m, "CloudLit", -750, 2150, (1, 0.95, 0.9, 1)),
                sat(op(m, unreal.MaterialExpressionAdd, mul(m, d, expr(m, unreal.MaterialExpressionConstant, -900, 2250, r=0.5), -750, 2250), expr(m, unreal.MaterialExpressionConstant, -900, 2320, r=0.5), -600, 2250), -450, 2250), -300, 2100)
    lit = op(m, unreal.MaterialExpressionAdd, lit, mul(m, glow, mul(m, sun_color, expr(m, unreal.MaterialExpressionConstant, -450, 2400, r=0.6), -300, 2400), -150, 2350), -50, 2150)
    final = lerp3(with_sun, lit, cloud_alpha, -100, 600)
    mel.connect_material_property(mul(m, final, sparam(m, "Brightness", -300, 800, 1.0), 100, 600), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)


def make_boost_pad_material():
    """BoostPads.jsx's pad, emissive: the chevron art scrolls forward (PanV) and pulses so it blooms."""
    m = new_material("M_TrackBoostPad")
    tex = texture_sample(m, -500, -200)
    rgb = expr(m, unreal.MaterialExpressionComponentMask, -350, -200, r=True, g=True, b=True)
    mel.connect_material_expressions(tex, "RGB", rgb, "")
    mel.connect_material_property(mul(m, rgb, expr(m, unreal.MaterialExpressionConstant, -500, 0, r=0.45), -250, -100), "", unreal.MaterialProperty.MP_BASE_COLOR)
    sine = expr(m, unreal.MaterialExpressionSine, -500, 300, period=6.283185)
    mel.connect_material_expressions(mul(m, expr(m, unreal.MaterialExpressionTime, -800, 300), sparam(m, "PulseRate", -800, 400, 6.0), -650, 300), "", sine, "")
    pulse = op(m, unreal.MaterialExpressionAdd, expr(m, unreal.MaterialExpressionConstant, -500, 200, r=1.0), mul(m, sine, sparam(m, "Pulse", -500, 400, 0.5), -350, 300), -250, 250)
    glow = mul(m, mul(m, rgb, sparam(m, "Emissive", -350, 100, 2.0), -200, 100), pulse, -100, 150)
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(sparam(m, "Roughness", -200, 400, 0.35), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)


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


def import_surfaces():
    """The CC0 sets: colour sRGB, normal as a (DirectX, already flipped) normal map, roughness linear."""
    folder = os.path.join(OUT, "shared/surfaces")
    if not os.path.isdir(folder):
        unreal.log_error("import_track_assets: missing out/shared/surfaces; run npm run export in Tools/track-export")
        return
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".png"):
            continue
        for path in import_file(os.path.join(folder, name), SURFACE_DIR):
            tex = unreal.load_asset(path)
            if name.endswith("_normal.png"):
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
                tex.set_editor_property("srgb", False)
            elif name.endswith("_rough.png"):
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
                tex.set_editor_property("srgb", False)
            eal.save_asset(path.split(".")[0], only_if_is_dirty=False)
    log(f"surfaces -> {SURFACE_DIR}")


def remove_stale():
    for path in STALE:
        if eal.does_asset_exist(path):
            eal.delete_asset(path)
            log(f"deleted stale {path}")


def make_writable():
    """Git LFS checks lockable .uasset files out read-only, and a save to one fails without an error."""
    root = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()), "Tracks")
    for folder, _, files in os.walk(root):
        for name in files:
            os.chmod(os.path.join(folder, name), stat.S_IREAD | stat.S_IWRITE)


def main():
    groups = set((os.environ.get("KART_IMPORT") or "violet,harbor,neon,shared").split(","))
    make_writable()
    import_surfaces()
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
    if "violet" in groups:
        remove_stale()  # after the import, which repoints the GLB materials away from them
    for group, folder, dest in TEXTURES:
        if group not in groups or not os.path.isdir(os.path.join(OUT, folder)):
            continue
        for name in sorted(os.listdir(os.path.join(OUT, folder))):
            if name.endswith(".png"):
                for path in import_file(os.path.join(OUT, folder, name), dest):
                    tex = unreal.load_asset(path)
                    if name.endswith("_normal.png"):
                        # Generated normal maps (env_waves_normal) are already DirectX.
                        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
                        tex.set_editor_property("srgb", False)
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
