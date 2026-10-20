"""Imports the track exporter's binaries (Tools/track-export/out, from `npm run export`) into Content/Tracks.

Run headless:  .\\Tools\\ue\\run.ps1 import_track_assets.py
Optional env:  KART_IMPORT=violet,harbor,neon,shared (default all), KART_MATERIALS_ONLY=1

- Master materials in /Game/Tracks/Shared/Materials, which ATrackBuilder instances per part:
  M_TrackVertexColor (lit, Color x vertex colour), M_TrackTextured (lit, Map x Color x vertex colour, UV
  Tiling and PanU/PanV), M_TrackUnlitTexture (signs), M_TrackTranslucent and M_TrackAdditive (unlit, Opacity),
  M_TrackSurface / M_TrackSurfaceWall (Map x Color x vertex colour x a world-space CC0 detail set: colour
  divided by the set's mean, world-space normal, roughness; top-down or dominant-axis projection) and
  M_TrackBoostPad (emissive chevrons that scroll forward and pulse).
- The CC0 surface sets (out/shared/surfaces, normal maps already flipped to DirectX) to /Game/Tracks/Shared/Surfaces.
- GLBs through Interchange: <dir>/<file>/StaticMeshes/<node> (+ Materials, Textures from the GLB).
- Canvas PNGs to <dir>/Textures/<name>.
- Violet's 8x8 palette textures sample nearest with no mips, so each surface keeps its flat colour.
- Nanite on the big opaque meshes (the Violet circuit, Harbor's prop batches, Neon City's buildings).
- STALE: assets an older export made that the current one no longer has (the Violet IP cleanup) are deleted.
"""
import os

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
    ("violet", "violet/violet_kerbs.glb", "/Game/Tracks/VioletLoop", False),
    ("shared", "shared/violet_prefabs.glb", "/Game/Tracks/Shared", False),
    ("shared", "shared/movecade_props.glb", "/Game/Tracks/Shared", False),
    ("harbor", "harbor/harbor_ribbons.glb", "/Game/Tracks/SunsetHarbor", False),
    ("harbor", "harbor/harbor_scenery.glb", "/Game/Tracks/SunsetHarbor", True),
    ("harbor", "harbor/harbor_motion.glb", "/Game/Tracks/SunsetHarbor", False),
    ("neon", "neon/neon_ribbons.glb", "/Game/Tracks/NeonCity", False),
    ("neon", "neon/neon_scenery.glb", "/Game/Tracks/NeonCity", True),
]
TEXTURES = [
    ("violet", "violet/textures", "/Game/Tracks/VioletLoop/Textures"),
    ("harbor", "harbor/textures", "/Game/Tracks/SunsetHarbor/Textures"),
    ("neon", "neon/textures", "/Game/Tracks/NeonCity/Textures"),
    ("shared", "shared/textures", "/Game/Tracks/Shared/Textures"),
]
# The Violet IP cleanup (Tools/track-export/src/violetMesh.mjs) no longer exports these: the warp pipes
# (Object_12, material_7) and the brick blocks (Object_47, material_3).
STALE = [
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
            mel.connect_material_property(mul(m, color, opacity, -400, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        else:
            mel.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
            mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
        mel.recompile_material(m)
    make_surface_material("M_TrackSurface", wall=False)
    make_surface_material("M_TrackSurfaceWall", wall=True)
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


def make_surface_material(name, wall):
    """Map(UV0) x Color x vertex colour x CC0 detail (trackSurfaces.ts / worldSurface.ts on the web).

    Detail UVs are world metres / DetailTileM: top-down (XY) on ground, or on the plane of the dominant normal
    axis for walls and props (wall=True). The detail colour is divided by the set's linear mean (DetailMean)
    and blended in by DetailAlbedo, so a palette colour keeps its hue. Normals are world space (no tangents
    needed: the Violet palette UVs are degenerate): the detail normal's XY tilts the vertex normal in the
    projection plane by DetailNormal. Roughness x lerp(1, 2 x rough map, DetailRough).
    """
    m = new_material(name)
    m.set_editor_property("two_sided", True)
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


def make_sky_material():
    """The web's sky dome (render/skyShader.ts): a three-stop gradient (top, horizon, below) plus a sun disc
    and glow, unlit and flagged as sky so the real-time sky light captures it."""
    m = new_material("M_TrackSky")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("is_sky", True)
    # The view ray: CameraVector points from the pixel back to the camera.
    view = mul(m, expr(m, unreal.MaterialExpressionCameraVectorWS, -1800, 0), expr(m, unreal.MaterialExpressionConstant, -1800, 100, r=-1.0), -1600, 0)
    z = mask(m, view, -1400, 0, b=True)
    sat = lambda src, x, y: (lambda e: (mel.connect_material_expressions(src, "", e, ""), e)[1])(expr(m, unreal.MaterialExpressionSaturate, x, y))
    up = expr(m, unreal.MaterialExpressionPower, -1100, -200)
    mel.connect_material_expressions(sat(z, -1250, -200), "", up, "Base")
    mel.connect_material_expressions(sparam(m, "TopCurve", -1250, -100, 0.55), "", up, "Exp")
    top = vparam(m, "SkyTop", -1100, -400, (0.05, 0.16, 0.66, 1))
    horizon = vparam(m, "SkyHorizon", -1100, 0, (0.66, 0.79, 0.91, 1))
    below = vparam(m, "SkyBelow", -1100, 300, (0.4, 0.52, 0.67, 1))
    upper = expr(m, unreal.MaterialExpressionLinearInterpolate, -850, -250)
    mel.connect_material_expressions(horizon, "", upper, "A")
    mel.connect_material_expressions(top, "", upper, "B")
    mel.connect_material_expressions(up, "", upper, "Alpha")
    down = mul(m, op(m, unreal.MaterialExpressionSubtract, expr(m, unreal.MaterialExpressionConstant, -1400, 300, r=0.0), z, -1250, 300),
               expr(m, unreal.MaterialExpressionConstant, -1400, 400, r=4.0), -1100, 400)
    lower = expr(m, unreal.MaterialExpressionLinearInterpolate, -850, 250)
    mel.connect_material_expressions(horizon, "", lower, "A")
    mel.connect_material_expressions(below, "", lower, "B")
    mel.connect_material_expressions(sat(down, -950, 400), "", lower, "Alpha")
    sky = expr(m, unreal.MaterialExpressionLinearInterpolate, -600, 0)
    mel.connect_material_expressions(lower, "", sky, "A")
    mel.connect_material_expressions(upper, "", sky, "B")
    mel.connect_material_expressions(sat(mul(m, z, expr(m, unreal.MaterialExpressionConstant, -900, 150, r=200.0), -750, 100), -650, 150), "", sky, "Alpha")
    # Sun: glow pow(max(dot, 0), GlowPower) x SunGlow, disc where dot > 1 - SunSize.
    d = expr(m, unreal.MaterialExpressionDotProduct, -1400, 700)
    mel.connect_material_expressions(view, "", d, "A")
    mel.connect_material_expressions(vparam(m, "SunDir", -1600, 750, (0.5, 0.5, 0.5, 1)), "", d, "B")
    ds = sat(d, -1250, 700)
    glow = expr(m, unreal.MaterialExpressionPower, -1100, 700)
    mel.connect_material_expressions(ds, "", glow, "Base")
    mel.connect_material_expressions(sparam(m, "GlowPower", -1250, 800, 24.0), "", glow, "Exp")
    disc = sat(mul(m, op(m, unreal.MaterialExpressionSubtract, ds, sparam(m, "SunCos", -1250, 900, 0.9995), -1100, 900),
                   expr(m, unreal.MaterialExpressionConstant, -1250, 1000, r=4000.0), -950, 900), -800, 900)
    sun_amount = op(m, unreal.MaterialExpressionAdd, mul(m, glow, sparam(m, "SunGlow", -1100, 1000, 0.6), -900, 750),
                    mul(m, disc, sparam(m, "SunDisc", -950, 1050, 8.0), -800, 1000), -650, 800)
    sun = mul(m, sun_amount, vparam(m, "SunColor", -800, 1100, (1, 0.95, 0.85, 1)), -500, 800)
    total = op(m, unreal.MaterialExpressionAdd, sky, sun, -350, 300)
    mel.connect_material_property(mul(m, total, sparam(m, "Brightness", -500, 500, 1.0), -200, 300), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)


def make_boost_pad_material():
    """BoostPads.jsx's pad, emissive: the chevron art scrolls forward (PanV) and pulses so it blooms."""
    m = new_material("M_TrackBoostPad")
    tex = texture_sample(m, -500, -200)
    rgb = mask(m, tex, -350, -200, r=True, g=True, b=True)
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


def main():
    groups = set((os.environ.get("KART_IMPORT") or "violet,harbor,neon,shared").split(","))
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
