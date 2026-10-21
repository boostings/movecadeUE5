"""Builds the track life assets (ATrackLife: lawn grass, crowds, butterflies, the trample field) under
/Game/Tracks/Shared/Life. Every asset is rebuilt from this script, so it is the source of truth.

    .\\Tools\\ue\\run.ps1 make_track_life.py -NullRhi

- MPC_TrackLife: what the world shares with these materials, set by ATrackLife every frame.
    Kart0..Kart7  UE world position (cm) and speed in w (m/s); far away and 0 when the seat is empty
    Wind          (direction x, direction y, strength, unused)
    Field         the trample field's world rectangle (min x, min y, size x, size y, cm)
    Cheer         0..1, the whole crowd cheering (GO, laps, the finish)
- RT_TrackTrampleA/B: the trample field, ping-ponged by ATrackLife. RGBA16F: rg is the push direction times
  its strength (world xy), b how flat the grass is. Each frame the previous field decays into the other
  target (M_TrampleDecay) and every kart stamps a soft disc into it (M_TrampleStamp, additive).
- M_TrackGrass: geometry blades on the runtime-built tufts and flowers. UV0 = (across, height 0..1), UV1.x the
  part (0 blade, 1 petal, 2 flower heart); per-instance custom data 0..2 the instance's world origin, 3..5 the
  petal colour. World position
  offset: a travelling wind gust, plus the trample field bending each blade over along the push and pressing
  it flat; flattened grass reads lighter (a mown trail) until it springs back.
- M_TrackCrowd: spectators. UV1.x the part (0 shirt, 1 skin, 2 trousers, 3 hair, 4 shoes, 5 eyes), UV1.y the arm
  (-1 left, +1 right). Custom data: 0..2 shirt colour, 3 skin tone, 4 hair tone, 5 seed, 6..8 world origin. They turn their heads
  toward the nearest kart, and when one comes close (or on a Cheer) they jump and wave both arms.
- M_TrackButterfly: wings flap about the body axis (UV1.y = side), coloured by custom data 0..2, flap rate in 3.
"""
import os

import unreal

quit_after = os.environ.get("KART_QUIT") == "1"
DIR = "/Game/Tracks/Shared/Life"

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MP = unreal.MaterialProperty
F1, F2, F3, F4 = (unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.CustomMaterialOutputType.CMOT_FLOAT2,
                  unreal.CustomMaterialOutputType.CMOT_FLOAT3, unreal.CustomMaterialOutputType.CMOT_FLOAT4)

KARTS = [f"Kart{i}" for i in range(8)]
FAR = 1.0e7


def log(msg):
    unreal.log(f"make_track_life: {msg}")


# ---------------------------------------------------------------- helpers

def new_material(name):
    """A fresh graph in an existing asset (references stay valid), or a new asset."""
    path = f"{DIR}/{name}"
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = tools.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())
    for flag in ("used_with_instanced_static_meshes",):
        m.set_editor_property(flag, True)
    return m


def expr(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def link(a, a_out, b, b_in):
    if not mel.connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError(f"connect failed {a.get_name()}.{a_out} -> {b.get_name()}.{b_in}")


def to_prop(a, a_out, prop):
    if not mel.connect_material_property(a, a_out, prop):
        raise RuntimeError(f"connect to {prop} failed")


def scalar(m, name, value, x, y):
    return expr(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def vector(m, name, value, x, y):
    return expr(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*value))


def const(m, value, x, y):
    return expr(m, unreal.MaterialExpressionConstant, x, y, r=value)


def mask(m, src, src_out, x, y, channels):
    e = expr(m, unreal.MaterialExpressionComponentMask, x, y, **{c: (c[0] in channels) for c in ("r", "g", "b", "a")})
    link(src, src_out, e, "")
    return e


def custom(m, x, y, code, inputs, out=F1, desc="Custom"):
    node = expr(m, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", out)
    node.set_editor_property("description", desc)
    ins = []
    for name in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        ins.append(ci)
    node.set_editor_property("inputs", ins)
    return node


def wire(m, node, sources):
    """sources: {input name: node or (node, output name)}."""
    for name, src in sources.items():
        if isinstance(src, tuple):
            link(src[0], src[1], node, name)
        else:
            link(src, "", node, name)


def mpc_param(m, mpc, name, x, y):
    return expr(m, unreal.MaterialExpressionCollectionParameter, x, y, collection=mpc, parameter_name=name)


def uv(m, index, x, y):
    return expr(m, unreal.MaterialExpressionTextureCoordinate, x, y, coordinate_index=index)


def custom_data(m, index, x, y, default=0.0):
    return expr(m, unreal.MaterialExpressionPerInstanceCustomData, x, y, data_index=index, const_default_value=default)


def transform_vector(m, src, src_out, source, dest, x, y):
    e = expr(m, unreal.MaterialExpressionTransform, x, y, transform_source_type=source, transform_type=dest)
    link(src, src_out, e, "")
    return e


def instance_origin(m, first, x, y):
    """The instance's world origin, which ATrackLife writes into custom data floats first..first+2: on these
    instanced components neither Object Position nor a local/instance transform yields the instance's own."""
    node = custom(m, x, y, "return float3(X, Y, Z);", ["X", "Y", "Z"], out=F3, desc="InstanceOrigin")
    wire(m, node, {k: custom_data(m, first + i, x - 200, y + i * 70) for i, k in enumerate("XYZ")})
    return node


def finish(m):
    try:
        mel.layout_material_expressions(m)
    except Exception:  # noqa: BLE001 - layout is cosmetic
        pass
    mel.recompile_material(m)
    eal.save_loaded_asset(m, only_if_is_dirty=False)
    log(f"saved {m.get_path_name()}")


# ---------------------------------------------------------------- shared data

def make_collection():
    path = f"{DIR}/MPC_TrackLife"
    mpc = unreal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        "MPC_TrackLife", DIR, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    want_vectors = [(k, (FAR, FAR, -FAR, 0.0)) for k in KARTS] + [("Wind", (0.8, 0.6, 1.0, 0.0)), ("Field", (0.0, 0.0, 100000.0, 100000.0))]
    want_scalars = [("Cheer", 0.0)]
    have = [str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("vector_parameters")]
    if have != [n for n, _ in want_vectors]:
        params = []
        for name, value in want_vectors:
            p = unreal.CollectionVectorParameter()
            p.set_editor_property("parameter_name", name)
            p.set_editor_property("default_value", unreal.LinearColor(*value))
            params.append(p)
        mpc.set_editor_property("vector_parameters", params)
    have = [str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("scalar_parameters")]
    if have != [n for n, _ in want_scalars]:
        params = []
        for name, value in want_scalars:
            p = unreal.CollectionScalarParameter()
            p.set_editor_property("parameter_name", name)
            p.set_editor_property("default_value", value)
            params.append(p)
        mpc.set_editor_property("scalar_parameters", params)
    eal.save_loaded_asset(mpc, only_if_is_dirty=False)
    log("saved MPC_TrackLife")
    return mpc


def make_render_targets():
    targets = []
    for name in ("RT_TrackTrampleA", "RT_TrackTrampleB"):
        path = f"{DIR}/{name}"
        rt = unreal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
            name, DIR, unreal.TextureRenderTarget2D, unreal.TextureRenderTargetFactoryNew())
        rt.set_editor_property("render_target_format", unreal.TextureRenderTargetFormat.RTF_RGBA16F)
        rt.set_editor_property("size_x", 1024)
        rt.set_editor_property("size_y", 1024)
        rt.set_editor_property("clear_color", unreal.LinearColor(0, 0, 0, 0))
        rt.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
        rt.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        eal.save_loaded_asset(rt, only_if_is_dirty=False)
        targets.append(rt)
    log("saved the trample targets")
    return targets


def trample_texture(m, rt, x, y):
    return expr(m, unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name="TrampleMap", texture=rt,
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)


# ---------------------------------------------------------------- trample field

STAMP = r"""
float2 d = (UV - 0.5) * 2.0;
float r = length(d);
float f = saturate(1.0 - r);
f = f * f * (3.0 - 2.0 * f);
float2 radial = r > 1e-3 ? d / r : float2(0.0, 0.0);
// Blades part round the kart and lie down along its travel.
float2 dir = normalize(radial * 0.6 + Travel.xy + float2(1e-5, 0.0));
return float3(dir * f * Push, f * Flat);
"""


def make_trample_materials(rt):
    m = new_material("M_TrampleStamp")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    node = custom(m, -400, 0, STAMP, ["UV", "Push", "Flat", "Travel"], out=F3, desc="TrampleStamp")
    wire(m, node, {"UV": uv(m, 0, -700, 0), "Push": scalar(m, "Push", 0.0, -700, 100), "Flat": scalar(m, "Flat", 0.0, -700, 200),
                   "Travel": (vector(m, "Travel", (0, 0, 0, 0), -700, 300), "RGB")})
    to_prop(node, "", MP.MP_EMISSIVE_COLOR)
    finish(m)

    m = new_material("M_TrampleDecay")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    prev = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -600, 0, parameter_name="Prev", texture=rt,
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    link(uv(m, 0, -800, 0), "", prev, "UVs")
    keep = vector(m, "Keep", (0.9, 0.9, 0.97, 1.0), -600, 200)
    mul = expr(m, unreal.MaterialExpressionMultiply, -300, 0)
    link(prev, "RGB", mul, "A")
    link(keep, "RGB", mul, "B")
    to_prop(mul, "", MP.MP_EMISSIVE_COLOR)
    finish(m)


# ---------------------------------------------------------------- grass

GRASS_WPO = r"""
float3 rel = WP - Root;
float h = saturate(UV0.y);
float len = max(length(rel), 0.1);
float2 wd = normalize(Wind.xy + float2(1e-4, 0.0));
float2 p = Root.xy * 0.01;
// A slow gust wave rolls across the lawn along the wind; each tuft adds its own sway.
float gust = 0.55 + 0.45 * sin(T * 0.85 - dot(p, wd) * 0.11) * sin(T * 0.31 - dot(p, float2(-wd.y, wd.x)) * 0.045 + 1.3);
float sway = 0.35 * sin(T * 2.4 + dot(p, float2(1.7, 2.3)) + Rand * 6.2832);
float2 wind = wd * Wind.z * WindBend * (gust + sway) * h * h;
// The trample field under the tuft.
float2 fuv = (Root.xy - Field.xy) / Field.zw;
float4 tr = Tex.SampleLevel(TexSampler, fuv, 0);
float inside = (fuv.x > 0.0 && fuv.y > 0.0 && fuv.x < 1.0 && fuv.y < 1.0) ? 1.0 : 0.0;
float2 push = tr.rg * inside;
float pl = length(push);
float flat = saturate(tr.b * inside);
float bend = saturate(max(flat, pl * 1.5));
float2 pdir = pl > 1e-3 ? push / pl : wd;
// Bend the blade over along the push (more toward the tip), keeping its length.
float a = bend * 1.45 * h;
float3 bent = float3(rel.xy * cos(a * 0.4) + pdir * len * sin(a), rel.z * cos(a));
float3 o = float3(wind * (1.0 - bend), 0.0) + (bent - rel);
// Far tufts sink into the lawn before the instances cull, so the edge of the grass never pops.
float fade = saturate((FadeEnd - length(Root - Cam)) / max(FadeEnd - FadeStart, 1.0));
return o * fade - rel * (1.0 - fade);
"""

GRASS_COLOR = r"""
float h = saturate(UV0.y);
float3 tip = lerp(TipColor, TipColorAlt, frac(Rand * 7.31));
float3 grass = lerp(RootColor, tip, pow(h, 0.8));
// Flattened grass catches the light: a lighter, yellower mown trail.
float2 fuv = (WP.xy - Field.xy) / Field.zw;
float flat = saturate(Tex.SampleLevel(TexSampler, fuv, 0).b);
grass = lerp(grass, grass * 1.35 + float3(0.05, 0.05, 0.01), flat);
float3 c = grass;
if (Part > 0.5 && Part < 1.5) c = Petal;
if (Part > 1.5) c = Heart;
// FieldDebug paints the field: red where flat, and blue across the field's whole rectangle.
float4 fd = Tex.SampleLevel(TexSampler, fuv, 0);
float inField = (fuv.x > 0.0 && fuv.y > 0.0 && fuv.x < 1.0 && fuv.y < 1.0) ? 1.0 : 0.0;
c = lerp(c, float3(saturate(fd.b) * 4.0, 0.0, inField), FieldDebug);
return c;
"""


def make_grass_material(mpc, rt):
    m = new_material("M_TrackGrass")
    m.set_editor_property("two_sided", True)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    m.set_editor_property("tangent_space_normal", False)
    tex = trample_texture(m, rt, -1600, 600)
    wind = mpc_param(m, mpc, "Wind", -1600, 400)
    field = mpc_param(m, mpc, "Field", -1600, 500)
    rand = expr(m, unreal.MaterialExpressionPerInstanceRandom, -1600, 300)
    time = expr(m, unreal.MaterialExpressionTime, -1600, 200)
    uv0 = uv(m, 0, -1600, 100)
    uv1 = uv(m, 1, -1600, 0)

    wpo = custom(m, -900, 600, GRASS_WPO, ["WP", "Root", "UV0", "Rand", "T", "Wind", "Field", "WindBend", "Tex", "Cam", "FadeStart", "FadeEnd"], out=F3, desc="GrassWPO")
    wire(m, wpo, {
        "WP": expr(m, unreal.MaterialExpressionWorldPosition, -1300, 700),
        "Root": instance_origin(m, 0, -1300, 800),
        "UV0": uv0, "Rand": rand, "T": time, "Wind": wind, "Field": field,
        "WindBend": scalar(m, "WindBend", 9.0, -1300, 900), "Tex": tex,
        "Cam": expr(m, unreal.MaterialExpressionCameraPositionWS, -1300, 1000),
        "FadeStart": scalar(m, "FadeStart", 4500.0, -1300, 1100), "FadeEnd": scalar(m, "FadeEnd", 6500.0, -1300, 1200),
    })
    to_prop(wpo, "", MP.MP_WORLD_POSITION_OFFSET)

    petal = custom(m, -1200, -200, "return float3(R, G, B);", ["R", "G", "B"], out=F3, desc="Petal")
    wire(m, petal, {"R": custom_data(m, 3, -1500, -300, 1.0), "G": custom_data(m, 4, -1500, -230, 1.0), "B": custom_data(m, 5, -1500, -160, 1.0)})
    color = custom(m, -900, 0, GRASS_COLOR, ["UV0", "Part", "Rand", "WP", "Field", "Tex", "RootColor", "TipColor", "TipColorAlt", "Petal", "Heart", "FieldDebug"], out=F3, desc="GrassColor")
    wire(m, color, {
        "UV0": uv0, "Part": mask(m, uv1, "", -1300, 0, "r"), "Rand": rand,
        "WP": expr(m, unreal.MaterialExpressionWorldPosition, -1300, 100), "Field": field, "Tex": tex,
        "RootColor": (vector(m, "RootColor", (0.035, 0.07, 0.02, 1), -1300, 200), "RGB"),
        "TipColor": (vector(m, "TipColor", (0.2, 0.38, 0.07, 1), -1300, 300), "RGB"),
        "TipColorAlt": (vector(m, "TipColorAlt", (0.27, 0.4, 0.06, 1), -1300, 400), "RGB"),
        "Petal": petal,
        "Heart": (vector(m, "HeartColor", (1.0, 0.72, 0.12, 1), -1300, 500), "RGB"),
        "FieldDebug": scalar(m, "FieldDebug", 0.0, -1300, 600),
    })
    to_prop(color, "", MP.MP_BASE_COLOR)
    sss = expr(m, unreal.MaterialExpressionMultiply, -600, 100)
    link(color, "", sss, "A")
    link(vector(m, "SubsurfaceTint", (0.8, 0.95, 0.45, 1), -800, 150), "RGB", sss, "B")
    to_prop(sss, "", MP.MP_SUBSURFACE_COLOR)
    # Soft, cartoon-friendly lighting: the blade normal bent halfway to straight up.
    normal = custom(m, -600, 300, "return normalize(lerp(N, float3(0.0, 0.0, 1.0), Up));", ["N", "Up"], out=F3, desc="SoftNormal")
    wire(m, normal, {"N": expr(m, unreal.MaterialExpressionVertexNormalWS, -800, 300), "Up": scalar(m, "NormalUp", 0.6, -800, 400)})
    to_prop(normal, "", MP.MP_NORMAL)
    ao = custom(m, -600, 450, "return lerp(Root, 1.0, saturate(UV0.y * 1.6));", ["UV0", "Root"], out=F1, desc="BladeAO")
    wire(m, ao, {"UV0": uv0, "Root": scalar(m, "RootOcclusion", 0.35, -800, 500)})
    to_prop(ao, "", MP.MP_AMBIENT_OCCLUSION)
    to_prop(scalar(m, "Roughness", 0.62, -600, 550), "", MP.MP_ROUGHNESS)
    to_prop(const(m, 0.35, -600, 600), "", MP.MP_SPECULAR)
    finish(m)


# ---------------------------------------------------------------- crowd

CROWD_WATCH = r"""
// The nearest kart (vector from this spectator, world cm) and how excited the spectator is.
float4 K[8] = { Kart0, Kart1, Kart2, Kart3, Kart4, Kart5, Kart6, Kart7 };
float best = 1e12;
float3 near = float3(100000.0, 0.0, 0.0);
float e = 0.0;
for (int i = 0; i < 8; i++)
{
    float3 d = K[i].xyz - Pos;
    float dist = length(d);
    if (dist < best) { best = dist; near = d; }
    e = max(e, saturate(1.3 - dist / Radius) * saturate(K[i].w / 16.0));
}
return float4(near, saturate(max(e, Cheer)));
"""

CROWD_POSE = r"""
float ph = Seed * 6.2832;
float3 o = float3(0.0, 0.0, 0.0);
// Idle: a slow weight shift. Excited: hop and bounce.
float hop = E * max(0.0, sin(T * (8.0 + Seed * 3.0) + ph)) * 24.0;
float idle = (1.0 - E) * (0.5 + 0.5 * sin(T * 1.6 + ph)) * 1.5;
float hw = saturate(LP.z / 160.0);
o.z += hop + idle;
o.y += sin(T * 1.1 + ph) * 2.5 * hw * (1.0 - E * 0.5);
// Watch the nearest kart: the head turns, the torso follows a little.
float yaw = clamp(atan2(Look.y, Look.x), -1.35, 1.35);
float headW = saturate((LP.z - 128.0) / 6.0);
float torsoW = saturate((LP.z - 60.0) / 70.0) * 0.3;
float a = yaw * max(headW, torsoW);
float ca = cos(a), sa = sin(a);
float2 r = float2(LP.x * ca - LP.y * sa, LP.x * sa + LP.y * ca);
o.xy += r - LP.xy;
// Arms: hanging, or up and waving when excited.
if (abs(Arm) > 0.5)
{
    float3 sh = float3(0.0, Arm * 21.0, 126.0);
    float3 v = LP - sh;
    float raise = lerp(0.12 + 0.05 * sin(T * 1.3 + ph), 2.75 + 0.3 * sin(T * 11.0 + ph + Arm * 1.3), E);
    float ang = raise * Arm;
    float c = cos(ang), s = sin(ang);
    float2 yz = float2(v.y * c - v.z * s, v.y * s + v.z * c);
    o += float3(v.x, yz) - v;
}
return o;
"""

CROWD_COLOR = r"""
float3 skin = lerp(float3(0.85, 0.55, 0.38), float3(0.2, 0.09, 0.045), Skin);
float3 hair = lerp(float3(0.03, 0.018, 0.01), float3(0.62, 0.36, 0.1), Hair * Hair);
float3 pants = lerp(float3(0.03, 0.045, 0.11), float3(0.16, 0.12, 0.08), frac(Seed * 7.13));
float3 c = Shirt;
if (Part > 0.5) c = skin;
if (Part > 1.5) c = pants;
if (Part > 2.5) c = hair;
if (Part > 3.5) c = float3(0.75, 0.75, 0.78);
if (Part > 4.5) c = float3(0.012, 0.01, 0.012);
return c;
"""


def make_crowd_material(mpc):
    m = new_material("M_TrackCrowd")
    karts = {k: mpc_param(m, mpc, k, -2200, -400 + i * 80) for i, k in enumerate(KARTS)}
    pos = instance_origin(m, 6, -2200, 300)
    watch = custom(m, -1800, 0, CROWD_WATCH, ["Pos", "Radius", "Cheer"] + KARTS, out=F4, desc="CrowdWatch")
    wire(m, watch, dict({"Pos": pos, "Radius": scalar(m, "ReactRadius", 5500.0, -2200, 400), "Cheer": mpc_param(m, mpc, "Cheer", -2200, 500)}, **karts))
    look = transform_vector(m, mask(m, watch, "", -1600, 0, "rgb"), "", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                            unreal.MaterialVectorCoordTransform.TRANSFORM_INSTANCE, -1450, 0)
    uv1 = uv(m, 1, -1600, 200)
    seed = custom_data(m, 5, -1600, 300, 0.5)
    pose = custom(m, -1200, 0, CROWD_POSE, ["LP", "Arm", "Seed", "E", "Look", "T"], out=F3, desc="CrowdPose")
    wire(m, pose, {
        "LP": expr(m, unreal.MaterialExpressionPreSkinnedPosition, -1450, -150),
        "Arm": mask(m, uv1, "", -1450, 200, "g"), "Seed": seed, "E": mask(m, watch, "", -1600, 100, "a"),
        "Look": look, "T": expr(m, unreal.MaterialExpressionTime, -1450, 350),
    })
    to_prop(transform_vector(m, pose, "", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_INSTANCE,
                             unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD, -900, 0), "", MP.MP_WORLD_POSITION_OFFSET)

    shirt = custom(m, -1200, 500, "return float3(R, G, B);", ["R", "G", "B"], out=F3, desc="Shirt")
    wire(m, shirt, {"R": custom_data(m, 0, -1450, 450, 0.8), "G": custom_data(m, 1, -1450, 520, 0.2), "B": custom_data(m, 2, -1450, 590, 0.3)})
    color = custom(m, -900, 400, CROWD_COLOR, ["Part", "Shirt", "Skin", "Hair", "Seed"], out=F3, desc="CrowdColor")
    wire(m, color, {"Part": mask(m, uv1, "", -1200, 400, "r"), "Shirt": shirt, "Skin": custom_data(m, 3, -1200, 650, 0.3),
                    "Hair": custom_data(m, 4, -1200, 720, 0.2), "Seed": seed})
    to_prop(color, "", MP.MP_BASE_COLOR)
    to_prop(scalar(m, "Roughness", 0.7, -600, 600), "", MP.MP_ROUGHNESS)
    finish(m)


# ---------------------------------------------------------------- butterflies

BUTTERFLY_WPO = r"""
if (abs(Side) < 0.5) return float3(0.0, 0.0, 0.0);
float ang = Side * (0.35 + 1.05 * sin(T * Rate + Phase * 6.2832));
float c = cos(ang), s = sin(ang);
float2 yz = float2(LP.y * c - LP.z * s, LP.y * s + LP.z * c);
return float3(LP.x, yz) - LP;
"""

BUTTERFLY_COLOR = r"""
if (abs(Side) < 0.5) return float3(0.02, 0.015, 0.02);
float2 q = UV - float2(0.0, 0.5);
float r = length(q * float2(1.0, 1.4));
float edge = smoothstep(0.62, 0.8, r);
float spot = 1.0 - smoothstep(0.08, 0.12, length(UV - float2(0.62, 0.62)));
float3 c = lerp(Wing, Wing * 0.12, edge);
return lerp(c, float3(1.0, 0.97, 0.9), spot * (1.0 - edge));
"""


def make_butterfly_material():
    m = new_material("M_TrackButterfly")
    m.set_editor_property("two_sided", True)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    uv1 = uv(m, 1, -1200, 200)
    side = mask(m, uv1, "", -1000, 200, "g")
    wpo = custom(m, -700, 300, BUTTERFLY_WPO, ["LP", "Side", "T", "Rate", "Phase"], out=F3, desc="Flap")
    wire(m, wpo, {"LP": expr(m, unreal.MaterialExpressionPreSkinnedPosition, -1000, 300), "Side": side,
                  "T": expr(m, unreal.MaterialExpressionTime, -1000, 400), "Rate": custom_data(m, 3, -1000, 500, 14.0),
                  "Phase": expr(m, unreal.MaterialExpressionPerInstanceRandom, -1000, 600)})
    to_prop(transform_vector(m, wpo, "", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_INSTANCE,
                             unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD, -400, 300), "", MP.MP_WORLD_POSITION_OFFSET)
    wing = custom(m, -1000, -100, "return float3(R, G, B);", ["R", "G", "B"], out=F3, desc="Wing")
    wire(m, wing, {"R": custom_data(m, 0, -1200, -150, 1.0), "G": custom_data(m, 1, -1200, -80, 0.5), "B": custom_data(m, 2, -1200, -10, 0.8)})
    color = custom(m, -700, 0, BUTTERFLY_COLOR, ["UV", "Side", "Wing"], out=F3, desc="WingColor")
    wire(m, color, {"UV": uv(m, 0, -1000, 50), "Side": side, "Wing": wing})
    to_prop(color, "", MP.MP_BASE_COLOR)
    sss = expr(m, unreal.MaterialExpressionMultiply, -400, 100)
    link(color, "", sss, "A")
    link(const(m, 0.8, -600, 150), "", sss, "B")
    to_prop(sss, "", MP.MP_SUBSURFACE_COLOR)
    emissive = expr(m, unreal.MaterialExpressionMultiply, -400, 0)
    link(color, "", emissive, "A")
    link(scalar(m, "Emissive", 0.25, -600, 50), "", emissive, "B")
    to_prop(emissive, "", MP.MP_EMISSIVE_COLOR)
    to_prop(const(m, 0.55, -400, 200), "", MP.MP_ROUGHNESS)
    finish(m)


def run():
    eal.make_directory(DIR)
    mpc = make_collection()
    rt_a, _ = make_render_targets()
    make_trample_materials(rt_a)
    make_grass_material(mpc, rt_a)
    make_crowd_material(mpc)
    make_butterfly_material()


try:
    run()
    log("done")
except Exception as error:  # noqa: BLE001 - report and still quit a headless run
    import traceback
    unreal.log_error(f"make_track_life: failed: {error}\n{traceback.format_exc()}")
finally:
    if quit_after:
        unreal.SystemLibrary.quit_editor()
