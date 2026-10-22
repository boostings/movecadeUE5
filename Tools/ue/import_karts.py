"""Imports the decoded vehicles, avatar4 drivers and outfits (Tools/art/.cache/out, from Tools/art/decode.mjs)
and builds their materials.

Run headless from the project root, after `node Tools/art/decode.mjs` and the vehicle detail pass
(Tools/art/detail/build.ps1, which writes Tools/art/.cache/detail/vehicles):
    .\\Tools\\ue\\run.ps1 import_karts.py              (everything)
    .\\Tools\\ue\\run.ps1 import_karts.py @{ KART_ART = 'vehicles' }   (vehicles | drivers | outfits | materials)
    .\\Tools\\ue\\run.ps1 import_karts.py @{ KART_ART = 'kartmaterials' }   (vehicle masters + detail maps only)
    .\\Tools\\ue\\run.ps1 import_karts.py @{ KART_ART = 'avatarmaterials' }   (avatar masters + shared eye instances)

Layout:
    /Game/Karts/Materials      M_KartPaint (clear coat, metallic flake), M_KartTyre, M_VehiclePart (detail maps),
                               M_KartCarbon (clear-coated weave), default textures
    /Game/Karts/Materials/Textures   the generated detail maps (Tools/art/detail/textures, make_textures.py)
    /Game/Karts/<Vehicle>      SM_<Vehicle>_<node> static meshes (node-local pivots: wheels spin about their hub)
    /Game/Avatars/Materials    M_AvatarSkin (subsurface profile), M_AvatarHair, M_AvatarCloth (cloth), M_AvatarSurface
    /Game/Avatars/SK_Avatar4_Skeleton   the one 26-joint skeleton every driver and outfit shares
    /Game/Avatars/Drivers/<face>_<body>/SK_Driver_<face>_<body>
    /Game/Avatars/Outfits/<outfit>_<body>/SK_Outfit_<outfit>_<body>
Joint, node and material-slot names are kept from the GLBs; AKartPawn looks slots up by name.
"""
import json
import os

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
ART = os.path.join(PROJECT, "Tools", "art", ".cache", "out")
STAGE = os.environ.get("KART_ART", "all")
ASSETS = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

KART_MATS = "/Game/Karts/Materials"
AVATAR_MATS = "/Game/Avatars/Materials"
SKELETON = "/Game/Avatars/SK_Avatar4_Skeleton"
VEHICLES = {"kart": "Kart", "moto": "Moto", "coupe": "Coupe", "truck": "Truck"}
# The detailed vehicles (Tools/art/detail/build.py) and their generated textures.
DETAIL = os.path.join(PROJECT, "Tools", "art", ".cache", "detail", "vehicles")
DETAIL_TEXTURES = os.path.join(PROJECT, "Tools", "art", "detail", "textures")
DETAIL_TEX = f"{KART_MATS}/Textures"

# The decoded art's material table (Tools/art/decode.mjs). Only the GLB imports need it, so the material-only
# stages also run on a machine without the decoded art.
MATERIAL_TABLE = {}
if os.path.exists(os.path.join(ART, "materials.json")):
    with open(os.path.join(ART, "materials.json"), encoding="utf-8") as f:
        MATERIAL_TABLE = json.load(f)


def log(msg):
    unreal.log(f"KartArt: {msg}")


def make_writable(*folders):
    """Lockable LFS assets check out read-only, and the editor's saves (and deletes) then fail silently."""
    for folder in folders:
        root = os.path.join(unreal.Paths.project_content_dir(), folder[len("/Game/"):])
        for dirpath, _, files in os.walk(root):
            for name in files:
                os.chmod(os.path.join(dirpath, name), 0o666)


def set_if(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as error:  # noqa: BLE001 - optional across engine versions
        log(f"  (no {type(obj).__name__}.{prop}: {error})")
        return False


def enum_value(enum_name, *candidates):
    enum = getattr(unreal, enum_name, None)
    if enum is None:
        return None
    for name in candidates:
        if hasattr(enum, name):
            return getattr(enum, name)
    log(f"  enum {enum_name} has none of {candidates}: {[n for n in dir(enum) if n.isupper()]}")
    return None


# --------------------------------------------------------------------------------------------------
# Import
# --------------------------------------------------------------------------------------------------
def make_pipeline(skeletal, skeleton=None):
    p = unreal.InterchangeGenericAssetsPipeline()
    common = p.get_editor_property("common_meshes_properties")
    # Vehicles come out in model space whatever this says (AKartPawn re-pivots wheels and the rim about
    # their bounds); skinned parts must bake so the importer combines them into one skeletal mesh.
    set_if(common, "bake_meshes", True)
    set_if(common, "keep_sections_separate", True)
    set_if(common, "import_sockets", False)
    set_if(common, "recompute_normals", False)
    set_if(common, "recompute_tangents", True)
    set_if(common, "use_mikk_t_space", True)
    force = enum_value("InterchangeForceMeshType",
                       "IFMT_SKELETAL_MESH" if skeletal else "IFMT_STATIC_MESH")
    if force is not None:
        set_if(common, "force_all_mesh_as_type", force)
    mesh = p.get_editor_property("mesh_pipeline")
    set_if(mesh, "import_static_meshes", not skeletal)
    set_if(mesh, "import_skeletal_meshes", skeletal)
    set_if(mesh, "combine_static_meshes_behavior", unreal.InterchangeCombineStaticMeshesBehavior.DO_NOT_COMBINE)
    set_if(mesh, "create_physics_asset", False)
    set_if(mesh, "import_morph_targets", True)
    set_if(mesh, "build_nanite", False)
    set_if(mesh, "collision", False)
    set_if(mesh, "generate_lightmap_u_vs", False)
    if skeletal:
        set_if(mesh, "combine_skeletal_meshes_behavior", unreal.InterchangeCombineSkeletalMeshesBehavior.BY_SKELETON)
    skel = p.get_editor_property("common_skeletal_meshes_and_animations_properties")
    if skeleton is not None:
        set_if(skel, "skeleton", skeleton)
    anim = p.get_editor_property("animation_pipeline")
    set_if(anim, "import_animations", False)
    return p


def import_glb(path, dest, skeletal, skeleton=None):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", dest)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", make_pipeline(skeletal, skeleton))
    TOOLS.import_asset_tasks([task])
    paths = list(task.get_editor_property("imported_object_paths") or [])
    log(f"imported {os.path.basename(path)} -> {dest}: {len(paths)} objects")
    return paths


def assets_in(folder, cls):
    out = []
    for path in ASSETS.list_assets(folder, recursive=True, include_folder=False):
        asset = ASSETS.load_asset(path.split(".")[0])
        if isinstance(asset, cls):
            out.append(asset)
    return out


def rename(asset, folder, name):
    target = f"{folder}/{name}"
    current = asset.get_path_name().split(".")[0]
    if current == target:
        return asset
    if ASSETS.does_asset_exist(target):
        ASSETS.delete_asset(target)
    ASSETS.rename_asset(current, target)
    return ASSETS.load_asset(target)


def fix_textures(folder):
    """glTF normal maps are OpenGL-style (+Y): flip green for UE. Data maps are linear."""
    for tex in assets_in(folder, unreal.Texture2D):
        name = tex.get_name().lower()
        changed = False
        if "normal" in name or name in ("twill", "nylon") or name.endswith("_normal"):
            log(f"  {tex.get_name()}: flip_green was {tex.get_editor_property('flip_green_channel')}")
            set_if(tex, "compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            set_if(tex, "srgb", False)
            set_if(tex, "flip_green_channel", True)
            changed = True
        elif "metallicroughness" in name or name.endswith("_ao") or "occlusion" in name:
            set_if(tex, "srgb", False)
            set_if(tex, "compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            changed = True
        if changed:
            ASSETS.save_loaded_asset(tex)


def delete_imported_materials(folder):
    for mat in assets_in(folder, unreal.MaterialInterface):
        ASSETS.delete_asset(mat.get_path_name().split(".")[0])


def import_defaults():
    folder = f"{KART_MATS}/Defaults"
    tasks = []
    for name in ("T_Default_White", "T_Default_WhiteLinear", "T_Default_Black"):
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(ART, "defaults", f"{name}.png"))
        task.set_editor_property("destination_path", folder)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        tasks.append(task)
    TOOLS.import_asset_tasks(tasks)
    linear = ASSETS.load_asset(f"{folder}/T_Default_WhiteLinear")
    set_if(linear, "srgb", False)
    set_if(linear, "compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    for name in ("T_Default_White", "T_Default_WhiteLinear", "T_Default_Black"):
        ASSETS.save_asset(f"{folder}/{name}", only_if_is_dirty=False)


def default_tex(name):
    return ASSETS.load_asset(f"{KART_MATS}/Defaults/{name}")


# --------------------------------------------------------------------------------------------------
# Master materials
# --------------------------------------------------------------------------------------------------
class Graph:
    def __init__(self, folder, name, shading):
        path = f"{folder}/{name}"
        self.path = path
        self.replaces = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
        # An existing master is rebuilt beside it and swapped in on save, so its instances keep a parent.
        self.mat = TOOLS.create_asset(f"{name}_Rebuilt" if self.replaces else name, folder, unreal.Material,
                                      unreal.MaterialFactoryNew())
        self.mat.set_editor_property("shading_model", shading)
        self.x = -900
        self.y = -300

    def _place(self, node):
        node.set_editor_property("material_expression_editor_x", self.x)
        node.set_editor_property("material_expression_editor_y", self.y)
        self.y += 180
        return node

    def node(self, cls):
        return self._place(MEL.create_material_expression(self.mat, cls, self.x, self.y))

    def scalar(self, name, value):
        n = self.node(unreal.MaterialExpressionScalarParameter)
        n.set_editor_property("parameter_name", name)
        n.set_editor_property("default_value", value)
        return n

    def vector(self, name, rgb):
        n = self.node(unreal.MaterialExpressionVectorParameter)
        n.set_editor_property("parameter_name", name)
        n.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
        return n

    def texture(self, name, tex, sampler):
        n = self.node(unreal.MaterialExpressionTextureSampleParameter2D)
        n.set_editor_property("parameter_name", name)
        n.set_editor_property("texture", tex)
        n.set_editor_property("sampler_type", sampler)
        return n

    def op(self, cls, a, b, a_out="", b_out=""):
        n = self.node(cls)
        MEL.connect_material_expressions(a, a_out, n, "A")
        MEL.connect_material_expressions(b, b_out, n, "B")
        return n

    def mul(self, a, b, a_out="", b_out=""):
        return self.op(unreal.MaterialExpressionMultiply, a, b, a_out, b_out)

    def add(self, a, b, a_out="", b_out=""):
        return self.op(unreal.MaterialExpressionAdd, a, b, a_out, b_out)

    def link(self, a, a_out, b, pin):
        if not MEL.connect_material_expressions(a, a_out, b, pin):
            log(f"  could not connect {type(a).__name__}.{a_out or '(out)'} -> {type(b).__name__}.{pin or '(in)'} "
                f"on {self.mat.get_name()}")

    def uv(self, index, scale):
        """TexCoord[index] * scale (a parameter node)."""
        tc = self.node(unreal.MaterialExpressionTextureCoordinate)
        tc.set_editor_property("coordinate_index", index)
        return self.mul(tc, scale)

    def texture_at(self, name, tex, sampler, uvs):
        n = self.texture(name, tex, sampler)
        self.link(uvs, "", n, "UVs")
        return n

    def mask(self, node, out, channels):
        m = self.node(unreal.MaterialExpressionComponentMask)
        for k in "rgba":
            m.set_editor_property(k, k in channels)
        self.link(node, out, m, "")
        return m

    def append(self, a, b):
        n = self.node(unreal.MaterialExpressionAppendVector)
        self.link(a, "", n, "A")
        self.link(b, "", n, "B")
        return n

    def lerp(self, a, b, alpha, a_out="", b_out=""):
        n = self.node(unreal.MaterialExpressionLinearInterpolate)
        self.link(a, a_out, n, "A")
        self.link(b, b_out, n, "B")
        self.link(alpha, "", n, "Alpha")
        return n

    def node_dot(self, a, b):
        n = self.node(unreal.MaterialExpressionDotProduct)
        self.link(a, "", n, "A")
        self.link(b, "", n, "B")
        return n

    def custom(self, code, inputs, out=unreal.CustomMaterialOutputType.CMOT_FLOAT3, desc="Custom"):
        """A Custom HLSL node; inputs maps pin names to nodes (or (node, output) pairs)."""
        n = self.node(unreal.MaterialExpressionCustom)
        n.set_editor_property("code", code)
        n.set_editor_property("output_type", out)
        n.set_editor_property("description", desc)
        pins = []
        for name in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", name)
            pins.append(ci)
        n.set_editor_property("inputs", pins)
        for name, src in inputs.items():
            src, src_out = src if isinstance(src, tuple) else (src, "")
            self.link(src, src_out, n, name)
        return n

    def const(self, value):
        n = self.node(unreal.MaterialExpressionConstant)
        n.set_editor_property("r", value)
        return n

    def blend_normals(self, base, base_out, detail, detail_out, strength):
        """normalize(float3(base.xy + detail.xy * strength, base.z)): a detail map over a base normal."""
        xy = self.add(self.mask(base, base_out, "rg"), self.mul(self.mask(detail, detail_out, "rg"), strength))
        n = self.node(unreal.MaterialExpressionNormalize)
        self.link(self.append(xy, self.mask(base, base_out, "b")), "", n, "VectorInput")
        return n

    def clear_coat_bottom_normal(self, node, output=""):
        """The base layer's own normal under the clear coat (used when r.ClearCoatNormal is on)."""
        cc = self.node(unreal.MaterialExpressionClearCoatNormalCustomOutput)
        if not MEL.connect_material_expressions(node, output, cc, ""):
            self.link(node, output, cc, "Input")

    def out(self, node, pin, output=""):
        """Connects to a pin of one MakeMaterialAttributes node (Python's MaterialProperty enum has no
        CustomData0/1, which carry Clear Coat and Cloth; the attribute pins do)."""
        if not hasattr(self, "attrs"):
            self.mat.set_editor_property("use_material_attributes", True)
            self.attrs = MEL.create_material_expression(self.mat, unreal.MaterialExpressionMakeMaterialAttributes, -200, 0)
            MEL.connect_material_property(self.attrs, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
        if not MEL.connect_material_expressions(node, output, self.attrs, pin):
            log(f"  could not connect {pin} on {self.mat.get_name()}")

    def save(self):
        set_usage(self.mat)
        MEL.layout_material_expressions(self.mat)
        MEL.recompile_material(self.mat)
        ASSETS.save_loaded_asset(self.mat)
        if self.replaces:
            self.mat = swap_master(self.path)
        return self.mat


def set_usage(mat):
    """A cooked or -game build only compiles the shader permutations a material is flagged for; drivers
    are skinned with morph targets, so without these flags they fall back to the default material."""
    if mat.get_path_name().startswith(AVATAR_MATS):
        for flag in ("used_with_skeletal_mesh", "used_with_morph_targets"):
            set_if(mat, flag, True)
    if mat.get_path_name().startswith(KART_MATS):
        # The vehicle meshes are Nanite; a cooked build can't add the flag later (fix_material_usage.py).
        set_if(mat, "used_with_nanite", True)


def fix_usage():
    for name in ("M_AvatarSkin", "M_AvatarHair", "M_AvatarCloth", "M_AvatarSurface"):
        mat = ASSETS.load_asset(f"{AVATAR_MATS}/{name}")
        set_usage(mat)
        MEL.recompile_material(mat)
        ASSETS.save_loaded_asset(mat)
        log(f"usage flags on {name}")


def swap_master(path):
    """Moves every instance under /Game/Karts and /Game/Avatars from <path> to <path>_Rebuilt, deletes the
    old master and renames the rebuilt one into its place."""
    rebuilt_path = f"{path}_Rebuilt"
    rebuilt = ASSETS.load_asset(rebuilt_path)
    old_name = path.rsplit("/", 1)[1]
    moved = []
    for root in ("/Game/Karts", "/Game/Avatars"):
        for mi in assets_in(root, unreal.MaterialInstanceConstant):
            parent = mi.get_editor_property("parent")
            if parent is not None and parent.get_name() in (old_name, f"{old_name}_Rebuilt"):
                MEL.set_material_instance_parent(mi, rebuilt)
                ASSETS.save_loaded_asset(mi)
                moved.append(mi.get_path_name().split(".")[0])
    if ASSETS.does_asset_exist(path):
        ASSETS.delete_asset(path)
    ASSETS.rename_asset(rebuilt_path, path)
    mat = ASSETS.load_asset(path)
    ASSETS.save_loaded_asset(mat)
    for mi_path in moved:
        ASSETS.save_asset(mi_path, only_if_is_dirty=False)
    # The rename leaves a redirector at the old name; repoint and remove it.
    redirector = unreal.EditorAssetLibrary.find_asset_data(rebuilt_path)
    if redirector.is_valid():
        TOOLS.fix_up_referencers([ASSETS.load_asset(rebuilt_path)])
        if ASSETS.does_asset_exist(rebuilt_path):
            ASSETS.delete_asset(rebuilt_path)
    log(f"rebuilt {path}: {len(moved)} instances re-parented")
    return mat


def finish_swaps():
    for folder in (KART_MATS, AVATAR_MATS):
        for path in ASSETS.list_assets(folder, recursive=False, include_folder=False):
            path = path.split(".")[0]
            if path.endswith("_Rebuilt"):
                swap_master(path[: -len("_Rebuilt")])


SAMPLER_COLOR = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
SAMPLER_LINEAR = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
SAMPLER_MASKS = unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
SAMPLER_NORMAL = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL




def flat_normal():
    return ASSETS.load_asset("/Engine/EngineMaterials/DefaultNormal")


# Generated detail maps: file stem in Tools/art/detail/textures -> (asset, kind).
DETAIL_TEXTURE_FILES = {
    "flake_n": ("T_Flake_N", "normal"), "carbon_c": ("T_Carbon_C", "color"), "carbon_n": ("T_Carbon_N", "normal"),
    "brushed_n": ("T_Brushed_N", "normal"), "brushed_r": ("T_Brushed_R", "mask"), "cast_n": ("T_Cast_N", "normal"),
    "plastic_n": ("T_Plastic_N", "normal"), "seat_c": ("T_Seat_C", "color"), "seat_n": ("T_Seat_N", "normal"),
    "tyre_n": ("T_Tyre_N", "normal"), "lens_n": ("T_Lens_N", "normal"), "roundel_c": ("T_Roundel_C", "color"),
}


def import_detail_textures():
    tasks = []
    for stem, (name, _) in sorted(DETAIL_TEXTURE_FILES.items()):
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(DETAIL_TEXTURES, f"{stem}.png"))
        task.set_editor_property("destination_path", DETAIL_TEX)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        tasks.append(task)
    TOOLS.import_asset_tasks(tasks)
    for stem, (name, kind) in sorted(DETAIL_TEXTURE_FILES.items()):
        tex = ASSETS.load_asset(f"{DETAIL_TEX}/{name}")
        if kind == "normal":
            # Generated DirectX-style (green down): no flip.
            set_if(tex, "compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            set_if(tex, "srgb", False)
            set_if(tex, "flip_green_channel", False)
        elif kind == "mask":
            set_if(tex, "compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            set_if(tex, "srgb", False)
        ASSETS.save_asset(f"{DETAIL_TEX}/{name}", only_if_is_dirty=False)
    log(f"detail textures: {len(tasks)} in {DETAIL_TEX}")


def detail_tex(name, fallback=None):
    path = f"{DETAIL_TEX}/{name}"
    return ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else fallback


def make_kart_masters():
    white, linear, black = default_tex("T_Default_White"), default_tex("T_Default_WhiteLinear"), default_tex("T_Default_Black")

    # Spec 01 Â§9.2: BaseColor = Tex * PaintColor, Roughness = 0.38 * MR.G, Metallic = 0.12 * MR.B,
    # ClearCoat 1 / 0.07, Emissive = PaintColor * 0.045 * 0.3 (plus the texture's own lamp strips).
    g = Graph(KART_MATS, "M_KartPaint", unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    paint = g.vector("PaintColor", (0.871, 0.084, 0.270))
    base = g.texture("BaseTex", white, SAMPLER_COLOR)
    # Upgrade over the web: the stock kart texture is multi-coloured (orange, yellow, grey), so a straight
    # multiply turns cyan paint lime and pink paint orange. TexDesaturation pulls the texture toward its
    # own luminance (x TexBrightness) first, so the whole body reads as the paint. 0 / 1 = spec 01 §9.2.
    weights = g.node(unreal.MaterialExpressionConstant3Vector)
    weights.set_editor_property("constant", unreal.LinearColor(0.2126, 0.7152, 0.0722, 1.0))
    luma = g.node(unreal.MaterialExpressionDotProduct)
    MEL.connect_material_expressions(base, "RGB", luma, "A")
    MEL.connect_material_expressions(weights, "", luma, "B")
    grey = g.mul(luma, g.scalar("TexBrightness", 1.0))
    mix = g.node(unreal.MaterialExpressionLinearInterpolate)
    MEL.connect_material_expressions(base, "RGB", mix, "A")
    MEL.connect_material_expressions(grey, "", mix, "B")
    MEL.connect_material_expressions(g.scalar("TexDesaturation", 0.0), "", mix, "Alpha")
    g.out(g.mul(mix, paint), "BaseColor")
    mr = g.texture("MRTex", linear, SAMPLER_MASKS)
    # Metallic flake (detail pass): flakes on UV1 (metres, FlakeScale tiles per metre). Each flake's tilt is a
    # per-flake random value: it lifts the base layer's metallic and smooths it, so the paint sparkles under the
    # mirror-smooth coat. With r.ClearCoatNormal on, the flakes also tilt the base layer's normal.
    flake = g.texture_at("FlakeTex", detail_tex("T_Flake_N", flat_normal()), SAMPLER_NORMAL,
                         g.uv(1, g.scalar("FlakeScale", 16.0)))
    tilt = g.mask(flake, "RGB", "rg")
    flake_amount = g.node(unreal.MaterialExpressionSaturate)
    contrast = g.mul(g.node_dot(tilt, tilt), g.scalar("FlakeContrast", 9.0))
    if not MEL.connect_material_expressions(contrast, "", flake_amount, ""):
        g.link(contrast, "", flake_amount, "Input")
    sparkle = g.mul(flake_amount, g.scalar("FlakeStrength", 0.3))
    rough = g.mul(mr, g.scalar("RoughnessScale", 0.38), "G")
    g.out(g.mul(rough, g.add(g.const(1.0), g.mul(sparkle, g.const(-0.4)))), "Roughness")
    g.out(g.add(g.mul(mr, g.scalar("MetallicScale", 0.12), "B"), g.mul(sparkle, g.scalar("FlakeMetallic", 0.2))),
          "Metallic")
    normal = g.texture("NormalTex", flat_normal(), SAMPLER_NORMAL)
    g.out(normal, "Normal", "RGB")
    g.clear_coat_bottom_normal(g.blend_normals(normal, "RGB", flake, "RGB", sparkle))
    glow = g.mul(paint, g.scalar("PaintGlow", 0.0135))
    lamps = g.mul(g.texture("EmissiveTex", black, SAMPLER_COLOR), g.scalar("EmissiveStrength", 4.0), "RGB")
    g.out(g.add(glow, lamps), "EmissiveColor")
    g.out(g.scalar("ClearCoat", 1.0), "ClearCoat")
    g.out(g.scalar("ClearCoatRoughness", 0.07), "ClearCoatRoughness")
    g.save()

    # Tyre (kartMaterials.ts:46-53): #1b1b1d multiplying the tyre texture, roughness 0.9. The detail pass's
    # tyres put the sidewall rings and sipes (T_Tyre_N) on UV0.
    g = Graph(KART_MATS, "M_KartTyre", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    tint = g.vector("Tint", (0.0116, 0.0116, 0.0131))
    g.out(g.mul(g.texture("BaseTex", white, SAMPLER_COLOR), tint, "RGB"), "BaseColor")
    mr = g.texture("MRTex", linear, SAMPLER_MASKS)
    g.out(g.mul(mr, g.scalar("RoughnessScale", 0.9), "G"), "Roughness")
    g.out(g.scalar("Specular", 0.35), "Specular")
    g.out(g.texture("NormalTex", flat_normal(), SAMPLER_NORMAL), "Normal", "RGB")
    g.save()

    # Vehicle parts (m_Accent, m_Chrome, m_Lamp, m_Glass, and the detail pass's rims, discs, seats, lamps...):
    # a colour, optional UV0 maps (decals, lens patterns) and optional tiling detail maps on UV1 (metres):
    # a colour mask (seat stitching), a normal (brushed, cast, quilted) and a roughness mask.
    g = Graph(KART_MATS, "M_VehiclePart", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    color = g.vector("Color", (0.5, 0.5, 0.5))
    base = g.mul(color, g.texture("BaseTex", white, SAMPLER_COLOR), "", "RGB")
    duv = g.uv(1, g.scalar("DetailScale", 4.0))
    detail = g.texture_at("DetailTex", white, SAMPLER_COLOR, duv)
    tinted = g.mul(base, g.lerp(g.const(1.0), detail, g.scalar("DetailTexAmount", 0.0), "", "RGB"))
    g.out(tinted, "BaseColor")
    drough = g.texture_at("DetailRough", linear, SAMPLER_MASKS, duv)
    rough_mix = g.lerp(g.const(1.0), drough, g.scalar("DetailRoughAmount", 0.0), "", "R")
    g.out(g.mul(g.scalar("Roughness", 0.5), rough_mix), "Roughness")
    g.out(g.scalar("Metallic", 0.0), "Metallic")
    g.out(g.scalar("Specular", 0.5), "Specular")
    n0 = g.texture("NormalTex", flat_normal(), SAMPLER_NORMAL)
    n1 = g.texture_at("DetailNormal", flat_normal(), SAMPLER_NORMAL, duv)
    g.out(g.blend_normals(n0, "RGB", n1, "RGB", g.scalar("DetailNormalStrength", 1.0)), "Normal")
    g.out(g.mul(tinted, g.scalar("Emissive", 0.0)), "EmissiveColor")
    g.save()

    # Carbon fibre: the twill weave on UV1 under a glossy clear coat (the weave's relief is on the base layer).
    g = Graph(KART_MATS, "M_KartCarbon", unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    cuv = g.uv(1, g.scalar("CarbonScale", 10.0))
    weave = g.texture_at("CarbonTex", detail_tex("T_Carbon_C", white), SAMPLER_COLOR, cuv)
    g.out(g.mul(weave, g.vector("Tint", (1.0, 1.0, 1.0)), "RGB"), "BaseColor")
    weave_n = g.texture_at("CarbonNormal", detail_tex("T_Carbon_N", flat_normal()), SAMPLER_NORMAL, cuv)
    # Without a second clear-coat normal the weave must not wrinkle the coat: keep it subtle on the top normal.
    flat = g.node(unreal.MaterialExpressionConstant3Vector)
    flat.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
    g.out(g.blend_normals(flat, "", weave_n, "RGB", g.scalar("WeaveRelief", 0.35)), "Normal")
    g.clear_coat_bottom_normal(weave_n, "RGB")
    g.out(g.scalar("Roughness", 0.38), "Roughness")
    g.out(g.scalar("Metallic", 0.0), "Metallic")
    g.out(g.scalar("Specular", 0.5), "Specular")
    g.out(g.scalar("ClearCoat", 1.0), "ClearCoat")
    g.out(g.scalar("ClearCoatRoughness", 0.04), "ClearCoatRoughness")
    g.save()

    # Name plate: unlit, two-sided text and its backing.
    g = Graph(KART_MATS, "M_NamePlateBack", unreal.MaterialShadingModel.MSM_UNLIT)
    g.mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    g.mat.set_editor_property("two_sided", True)
    color = g.vector("Color", (0.02, 0.02, 0.03))
    g.out(color, "EmissiveColor")
    g.out(g.scalar("Opacity", 0.6), "Opacity")
    g.save()


AVATAR_RIM = r"""
float facing = saturate(dot(normalize(N), normalize(V)));
return Tint * pow(1.0 - facing, Power) * Rim;
"""

# Skin: a warm peach-fuzz rim (the light scattering through the skin's edge, pulled toward a rose glow), a
# faint lift so faces never go muddy in shadow, and the player's rim colour for silhouette readability.
SKIN_GLOW = r"""
float facing = saturate(dot(normalize(N), normalize(V)));
float edge = pow(1.0 - facing, Power);
float3 warm = lerp(Tint, float3(1.0, 0.42, 0.32) * dot(Tint, float3(0.4, 0.4, 0.2)) * 2.2, 0.55);
return warm * edge * Rim + Tint * Lift + RimColor * pow(1.0 - facing, 4.0) * RimBoost;
"""

# A stylised hair highlight in two lobes: a tight bright band where the normal sits between straight up and the
# view (it slides over the crown as the head turns, like an anime sheen ring) and a broad tinted one under it,
# plus a rim so dark hair keeps its silhouette and the player's rim colour.
HAIR_SHEEN = r"""
float3 n = normalize(N);
float3 v = normalize(V);
float band = pow(saturate(dot(n, normalize(v + float3(0.0, 0.0, 1.3)))), 36.0);
float broad = pow(saturate(dot(n, normalize(v + float3(0.0, 0.0, 0.55)))), 9.0);
float rim = pow(1.0 - saturate(dot(n, v)), 3.0);
float3 lifted = Tint * 2.4 + 0.07;
return (lifted * band + Tint * broad * 0.45) * Sheen + Tint * rim * Rim + RimColor * rim * RimBoost;
"""

# Fabric: a soft sheen at grazing angles (velvet-like, in the cloth's own colour) and the player's rim colour.
CLOTH_SHEEN = r"""
float facing = saturate(dot(normalize(N), normalize(V)));
float edge = pow(1.0 - facing, 2.5);
return (Tint * 0.6 + 0.04) * edge * Sheen + RimColor * pow(1.0 - facing, 4.0) * RimBoost;
"""

# Eyes: a big catchlight from a light just above the camera and a small second one beside it, so they read alive
# in every shot.
EYE_CATCHLIGHT = r"""
float3 v = normalize(V);
float3 r = reflect(-v, normalize(N));
float3 key = normalize(v + float3(0.0, 0.0, 0.9));
float3 fill = normalize(v + float3(0.35, 0.45, 0.25));
return (pow(saturate(dot(r, key)), 220.0) + 0.55 * pow(saturate(dot(r, fill)), 900.0)) * Catchlight;
"""


def make_avatar_masters():
    white, linear = default_tex("T_Default_White"), default_tex("T_Default_WhiteLinear")

    def common(g, tint_default, rough, use_vertex_color=False, ao=True, detail=1.0, ao_strength=1.0, detail_normal=False):
        tint = g.vector("Tint", tint_default)
        tex = g.texture("BaseTex", white, SAMPLER_COLOR)
        if detail < 1.0:
            # How much of the baked albedo shows under the tint (the atlas carries blush and seams).
            base = g.mul(g.lerp(g.const(1.0), tex, g.scalar("AlbedoDetail", detail), "", "RGB"), tint, "", "RGB")
        else:
            base = g.mul(tex, tint, "RGB")
        if use_vertex_color:
            vc = g.node(unreal.MaterialExpressionVertexColor)
            base = g.mul(base, vc, "", "")
        g.out(base, "BaseColor")
        g.out(g.scalar("Roughness", rough), "Roughness")
        normal = g.texture("NormalTex", flat_normal(), SAMPLER_NORMAL)
        if detail_normal:
            # A tiled weave over the baked normal (off by default; the stock clothes turn it on).
            weave = g.texture_at("DetailNormalTex", flat_normal(), SAMPLER_NORMAL, g.uv(0, g.scalar("DetailScale", 24.0)))
            g.out(g.blend_normals(normal, "RGB", weave, "RGB", g.scalar("DetailNormalStrength", 0.0)), "Normal")
        else:
            g.out(normal, "Normal", "RGB")
        if ao:
            occlusion = g.texture("AOTex", linear, SAMPLER_MASKS)
            if ao_strength < 1.0:
                # The 512 px atlas bakes heavy blobs into eye sockets and nostrils: only a share of it shows.
                g.out(g.lerp(g.const(1.0), occlusion, g.scalar("AOStrength", ao_strength), "", "R"), "AmbientOcclusion")
            else:
                g.out(occlusion, "AmbientOcclusion", "R")
        return tint

    def view(g):
        return g.node(unreal.MaterialExpressionCameraVectorWS), g.node(unreal.MaterialExpressionVertexNormalWS)

    def rim_colour(g):
        return g.vector("RimColor", (0.0, 0.0, 0.0)), g.scalar("RimBoost", 0.0)

    # Skin: screen-space subsurface (a warm profile of its own) over a softened albedo bake, vertex-colour
    # warmth on cheeks and nose, the peach-fuzz rim and the lift.
    g = Graph(AVATAR_MATS, "M_AvatarSkin", unreal.MaterialShadingModel.MSM_SUBSURFACE_PROFILE)
    tint = common(g, (0.63, 0.25, 0.14), 0.55, use_vertex_color=True, detail=0.7, ao_strength=0.35)
    v, n = view(g)
    rim, boost = rim_colour(g)
    g.out(g.custom(SKIN_GLOW, {"N": n, "V": v, "Tint": (tint, "RGB"), "Rim": g.scalar("Rim", 0.12), "Power": g.scalar("RimPower", 3.0),
                               "Lift": g.scalar("Lift", 0.008), "RimColor": (rim, "RGB"), "RimBoost": boost}, desc="SkinGlow"), "EmissiveColor")
    g.out(g.scalar("Specular", 0.36), "Specular")
    profile = skin_profile()
    if profile is not None:
        set_if(g.mat, "subsurface_profile", profile)
    g.save()

    g = Graph(AVATAR_MATS, "M_AvatarHair", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    tint = common(g, (0.04, 0.018, 0.012), 0.4, detail=0.5, ao_strength=0.45)
    v, n = view(g)
    rim, boost = rim_colour(g)
    g.out(g.custom(HAIR_SHEEN, {"N": n, "V": v, "Tint": (tint, "RGB"), "Sheen": g.scalar("Sheen", 0.42), "Rim": g.scalar("Rim", 0.55),
                                "RimColor": (rim, "RGB"), "RimBoost": boost}, desc="HairSheen"), "EmissiveColor")
    g.out(g.scalar("Specular", 0.5), "Specular")
    g.save()

    # Fabric: the cloth shading model (fuzz sheen at grazing angles) for jersey, pants, shoes and outfits, a
    # velvet edge in emissive so the folds read under flat light, and an optional tiled weave.
    g = Graph(AVATAR_MATS, "M_AvatarCloth", unreal.MaterialShadingModel.MSM_CLOTH)
    tint = common(g, (0.8, 0.8, 0.8), 0.82, ao_strength=0.6, detail_normal=True)
    fuzz = g.mul(g.add(tint, g.scalar("FuzzLift", 0.35)), g.scalar("FuzzScale", 0.5))
    g.out(fuzz, "SubsurfaceColor")
    g.out(g.scalar("Cloth", 0.55), "ClearCoat")
    v, n = view(g)
    rim, boost = rim_colour(g)
    g.out(g.custom(CLOTH_SHEEN, {"N": n, "V": v, "Tint": (tint, "RGB"), "Sheen": g.scalar("Sheen", 0.07), "RimColor": (rim, "RGB"),
                                 "RimBoost": boost}, desc="ClothSheen"), "EmissiveColor")
    g.save()

    # Flat surfaces (eyes, mouth, teeth, glasses). Eyes add catchlights and glow faintly in their own colour
    # (Glow), so a shaded iris never goes dead.
    g = Graph(AVATAR_MATS, "M_AvatarSurface", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    tint = g.vector("Tint", (0.5, 0.5, 0.5))
    g.out(tint, "BaseColor")
    g.out(g.scalar("Roughness", 0.5), "Roughness")
    g.out(g.scalar("Specular", 0.5), "Specular")
    g.out(g.scalar("Metallic", 0.0), "Metallic")
    v, n = view(g)
    glint = g.custom(EYE_CATCHLIGHT, {"N": n, "V": v, "Catchlight": g.scalar("Catchlight", 0.0)},
                     out=unreal.CustomMaterialOutputType.CMOT_FLOAT1, desc="Catchlight")
    g.out(g.add(glint, g.mul(tint, g.scalar("Glow", 0.0), "RGB")), "EmissiveColor")
    g.save()


def skin_profile():
    """A warm skin subsurface profile (the engine default scatters grey-pink): red-orange scatter, soft falloff."""
    path = f"{AVATAR_MATS}/SP_AvatarSkin"
    try:
        if ASSETS.does_asset_exist(path):
            profile = ASSETS.load_asset(path)
        else:
            factory = unreal.SubsurfaceProfileFactory() if hasattr(unreal, "SubsurfaceProfileFactory") else None
            profile = TOOLS.create_asset("SP_AvatarSkin", AVATAR_MATS, unreal.SubsurfaceProfile, factory)
        settings = profile.get_editor_property("settings")
        for prop, value in (("surface_albedo", unreal.LinearColor(0.91, 0.34, 0.27, 1.0)),
                            ("mean_free_path_color", unreal.LinearColor(1.0, 0.36, 0.18, 1.0)),
                            ("mean_free_path_distance", 0.7),
                            ("world_unit_scale", 0.12),
                            ("subsurface_color", unreal.LinearColor(0.95, 0.32, 0.22, 1.0)),
                            ("falloff_color", unreal.LinearColor(1.0, 0.37, 0.3, 1.0)),
                            ("boundary_color_bleed", unreal.LinearColor(0.85, 0.75, 0.7, 1.0))):
            set_if(settings, prop, value)
        profile.set_editor_property("settings", settings)
        ASSETS.save_loaded_asset(profile)
        log(f"subsurface profile {path}")
        return profile
    except Exception as error:  # noqa: BLE001 - the engine default profile is a fine fallback
        log(f"  no subsurface profile: {error}")
        return None


# The stock clothes' weave (the outfits' own knit and twill normal maps) and the shared instances' tuning.
STOCK_WEAVE = "/Game/Avatars/Outfits/glow_hoodie_female/glow-hoodie-female/Textures/knit"


def tune_driver_instances():
    """Updates every driver's cloth, skin and hair instance in place (the meshes reference them)."""
    weave = ASSETS.load_asset(STOCK_WEAVE) if ASSETS.does_asset_exist(STOCK_WEAVE) else None
    for face in FACES:
        for body in BODIES:
            mats = f"/Game/Avatars/Drivers/{face}_{body}/Materials"
            cloth = f"{mats}/MI_{face}_{body}_Cloth"
            if ASSETS.does_asset_exist(cloth):
                mi = ASSETS.load_asset(cloth)
                if weave is not None:
                    MEL.set_material_instance_texture_parameter_value(mi, "DetailNormalTex", weave)
                    MEL.set_material_instance_scalar_parameter_value(mi, "DetailNormalStrength", 0.22)
                    MEL.set_material_instance_scalar_parameter_value(mi, "DetailScale", 28.0)
                MEL.set_material_instance_scalar_parameter_value(mi, "Roughness", 0.8)
                ASSETS.save_loaded_asset(mi)
            hair = f"{mats}/MI_{face}_{body}_Hair"
            if ASSETS.does_asset_exist(hair):
                mi = ASSETS.load_asset(hair)
                MEL.set_material_instance_scalar_parameter_value(mi, "Roughness", 0.36)
                ASSETS.save_loaded_asset(mi)
            skin = f"{mats}/MI_{face}_{body}_Skin"
            if ASSETS.does_asset_exist(skin):
                mi = ASSETS.load_asset(skin)
                MEL.set_material_instance_scalar_parameter_value(mi, "Roughness", 0.5)
                ASSETS.save_loaded_asset(mi)
    log("driver instances tuned")


def make_mi(folder, name, parent, scalars=None, vectors=None, textures=None):
    path = f"{folder}/{name}"
    if ASSETS.does_asset_exist(path):
        ASSETS.delete_asset(path)
    mi = TOOLS.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
    for k, v in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1.0))
    for k, v in (textures or {}).items():
        if v is not None:
            MEL.set_material_instance_texture_parameter_value(mi, k, v)
    ASSETS.save_loaded_asset(mi)
    return mi


def find_texture(folder, name):
    if not name:
        return None
    key = name.lower()
    for tex in assets_in(folder, unreal.Texture2D):
        n = tex.get_name().lower()
        if n == key or n == f"t_{key}" or n.endswith(key):
            return tex
    log(f"  texture {name} not found in {folder}")
    return None


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def hex_linear(code):
    code = code.lstrip("#")
    return tuple(srgb_to_linear(int(code[i:i + 2], 16) / 255.0) for i in (0, 2, 4))


def assign_slots(mesh, chooser):
    """Replaces every material slot with chooser(slot_name) (None keeps the slot as is)."""
    if isinstance(mesh, unreal.StaticMesh):
        mats = list(mesh.get_editor_property("static_materials"))
        for i, m in enumerate(mats):
            new = chooser(str(m.get_editor_property("material_slot_name")))
            if new is not None:
                m.set_editor_property("material_interface", new)
                mats[i] = m
        mesh.set_editor_property("static_materials", mats)
    else:
        mats = list(mesh.get_editor_property("materials"))
        for i, m in enumerate(mats):
            new = chooser(str(m.get_editor_property("material_slot_name")))
            if new is not None:
                m.set_editor_property("material_interface", new)
                mats[i] = m
        mesh.set_editor_property("materials", mats)
    ASSETS.save_loaded_asset(mesh)


# --------------------------------------------------------------------------------------------------
# Vehicles
# --------------------------------------------------------------------------------------------------
# Looks for the detail pass's material names (and premium overrides of the stock ones), as
# (master, scalars, vectors, textures). A vector of None keeps the stock colour from materials.json.
# Colours are linear.
PART_LOOKS = {
    "m_Tyre": ("tyre", {"RoughnessScale": 0.86, "Specular": 0.3}, {"Tint": (0.011, 0.011, 0.012)}, {"NormalTex": "T_Tyre_N"}),
    "m_Rim": ("part", {"Roughness": 0.12, "Metallic": 1.0, "Specular": 0.5}, {"Color": (0.86, 0.86, 0.9)}, {}),
    "m_Hub": ("part", {"Roughness": 0.36, "Metallic": 0.85, "DetailScale": 8.0, "DetailNormalStrength": 0.6},
              {"Color": (0.035, 0.036, 0.04)}, {"DetailNormal": "T_Cast_N"}),
    "m_Disc": ("part", {"Roughness": 0.3, "Metallic": 1.0, "DetailScale": 6.0, "DetailRoughAmount": 0.6,
                        "DetailNormalStrength": 0.5}, {"Color": (0.52, 0.52, 0.54)},
               {"DetailNormal": "T_Brushed_N", "DetailRough": "T_Brushed_R"}),
    "m_Chrome": ("part", {"Roughness": 0.06, "Metallic": 1.0, "Specular": 0.5}, {"Color": (0.92, 0.92, 0.95)}, {}),
    "m_Carbon": ("carbon", {"CarbonScale": 10.0, "Roughness": 0.38, "ClearCoatRoughness": 0.03}, {}, {}),
    "m_Rubber": ("part", {"Roughness": 0.72, "Specular": 0.35, "DetailScale": 6.0, "DetailNormalStrength": 0.5},
                 {"Color": (0.012, 0.012, 0.013)}, {"DetailNormal": "T_Plastic_N"}),
    "m_Seat": ("part", {"Roughness": 0.48, "Specular": 0.4, "DetailScale": 5.0, "DetailNormalStrength": 1.0,
                        "DetailTexAmount": 1.0}, {"Color": (0.04, 0.04, 0.05)},
               {"DetailNormal": "T_Seat_N", "DetailTex": "T_Seat_C"}),
    "m_Engine": ("part", {"Roughness": 0.42, "Metallic": 0.9, "DetailScale": 8.0, "DetailNormalStrength": 0.8},
                 {"Color": (0.09, 0.09, 0.1)}, {"DetailNormal": "T_Cast_N"}),
    "m_Spring": ("part", {"Roughness": 0.28, "Metallic": 0.5}, {"Color": (1.0, 0.62, 0.02)}, {}),
    "m_Chain": ("part", {"Roughness": 0.45, "Metallic": 1.0}, {"Color": (0.12, 0.12, 0.13)}, {}),
    "m_Lamp": ("part", {"Roughness": 0.08, "Specular": 0.8, "Emissive": 6.0}, {"Color": (1.0, 0.9, 0.72)},
               {"NormalTex": "T_Lens_N"}),
    "m_Tail": ("part", {"Roughness": 0.1, "Specular": 0.8, "Emissive": 3.0}, {"Color": (0.8, 0.02, 0.025)},
               {"NormalTex": "T_Lens_N"}),
    "m_Amber": ("part", {"Roughness": 0.1, "Specular": 0.8, "Emissive": 2.5}, {"Color": (1.0, 0.38, 0.02)},
                {"NormalTex": "T_Lens_N"}),
    "m_Glow": ("part", {"Roughness": 0.6, "Specular": 0.2, "Emissive": 0.5}, {"Color": (0.75, 0.1, 0.012)}, {}),
    "m_Roundel": ("part", {"Roughness": 0.22, "Specular": 0.6}, {"Color": (1.0, 1.0, 1.0)}, {"BaseTex": "T_Roundel_C"}),
    "m_Glass": ("part", {"Roughness": 0.04, "Metallic": 0.3, "Specular": 0.9}, {"Color": (0.02, 0.03, 0.05)}, {}),
    "m_Trim": ("part", {"Roughness": 0.5, "DetailScale": 6.0, "DetailNormalStrength": 0.35}, None,
               {"DetailNormal": "T_Plastic_N"}),
    "m_Accent": ("part", {"Roughness": 0.4, "DetailScale": 6.0, "DetailNormalStrength": 0.2}, None,
                 {"DetailNormal": "T_Plastic_N"}),
}
# Per vehicle: rim finishes and spring colours that suit each one.
VEHICLE_LOOKS = {
    "kart": {"m_Rim": ({"Roughness": 0.1, "Metallic": 1.0}, {"Color": (0.88, 0.88, 0.92)}),
             "m_Spring": ({}, {"Color": (1.0, 0.62, 0.02)})},
    "moto": {"m_Rim": ({"Roughness": 0.2, "Metallic": 1.0}, {"Color": (0.95, 0.6, 0.16)}),  # gold anodised
             "m_Spring": ({}, {"Color": (0.95, 0.6, 0.16)})},
    "coupe": {"m_Rim": ({"Roughness": 0.16, "Metallic": 1.0}, {"Color": (0.8, 0.8, 0.84)})},
    "truck": {"m_Rim": ({"Roughness": 0.34, "Metallic": 0.6}, {"Color": (0.03, 0.03, 0.035)}),  # satin black
              "m_Spring": ({}, {"Color": (1.0, 0.33, 0.02)})},
}
# Stock paint without a texture gets a metallic base under the coat.
PAINT_METALLIC = 0.28


def mesh_slots(mesh):
    return [str(m.get_editor_property("material_slot_name")) for m in mesh.get_editor_property("static_materials")]


def enable_nanite(mesh):
    settings = mesh.get_editor_property("nanite_settings")
    settings.set_editor_property("enabled", True)
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if hasattr(subsystem, "set_nanite_settings"):
        subsystem.set_nanite_settings(mesh, settings, apply_changes=True)
    else:
        mesh.set_editor_property("nanite_settings", settings)


def import_vehicles():
    paint = ASSETS.load_asset(f"{KART_MATS}/M_KartPaint")
    tyre = ASSETS.load_asset(f"{KART_MATS}/M_KartTyre")
    part = ASSETS.load_asset(f"{KART_MATS}/M_VehiclePart")
    carbon = ASSETS.load_asset(f"{KART_MATS}/M_KartCarbon")
    masters = {"tyre": tyre, "part": part, "carbon": carbon}
    make_writable("/Game/Karts")
    for vid, vname in VEHICLES.items():
        folder = f"/Game/Karts/{vname}"
        source = os.path.join(DETAIL, f"{vid}.glb")
        if not os.path.exists(source):
            raise RuntimeError(f"{source} is missing: run Tools/art/detail/build.ps1 first")
        if ASSETS.does_directory_exist(folder):
            ASSETS.delete_directory(folder)
        import_glb(source, folder, skeletal=False)
        fix_textures(folder)
        meshes = []
        for mesh in assets_in(folder, unreal.StaticMesh):
            short = mesh.get_name()
            if short.startswith("SM_"):
                short = short[len("SM_"):]
            meshes.append(rename(mesh, folder, f"SM_{vname}_{short}"))
        used = {slot for mesh in meshes for slot in mesh_slots(mesh)}
        table = MATERIAL_TABLE[f"vehicles/{vid}.glb"]
        mats = f"{folder}/Materials"
        mis = {}
        for mname in sorted(used):
            entry = table.get(mname)
            if mname == "m_Body":
                tex = {k: find_texture(folder, v) for k, v in entry["tex"].items()}
                textured = "BaseColor" in entry["tex"]
                mis[mname] = make_mi(mats, f"MI_{vname}_Paint", paint,
                                     scalars={"TexDesaturation": PAINT_DESATURATION if textured else 0.0,
                                              "TexBrightness": PAINT_BRIGHTNESS if textured else 1.0,
                                              **({} if textured else {"MetallicScale": PAINT_METALLIC})},
                                     vectors={"PaintColor": entry["base"][:3]},
                                     textures={"BaseTex": tex.get("BaseColor"), "MRTex": tex.get("MetallicRoughness"),
                                               "NormalTex": tex.get("Normal"), "EmissiveTex": tex.get("Emissive")})
            elif mname in PART_LOOKS:
                kind, scalars, vectors, textures = PART_LOOKS[mname]
                scalars = dict(scalars)
                if vectors is None:  # keep the stock colour (and its roughness/metalness unless overridden)
                    vectors = {"Color": entry["base"][:3]} if entry else {}
                    scalars.setdefault("Metallic", entry["metal"] if entry else 0.0)
                vectors = dict(vectors)
                over_s, over_v = VEHICLE_LOOKS.get(vid, {}).get(mname, ({}, {}))
                scalars.update(over_s)
                vectors.update(over_v)
                mis[mname] = make_mi(mats, f"MI_{vname}_{mname[2:]}", masters[kind], scalars=scalars, vectors=vectors,
                                     textures={k: detail_tex(v) for k, v in textures.items()})
            elif entry is not None:
                mis[mname] = make_mi(mats, f"MI_{vname}_{mname[2:]}", part,
                                     scalars={"Roughness": entry["rough"], "Metallic": entry["metal"]},
                                     vectors={"Color": entry["base"][:3]})
            else:
                log(f"  WARNING {vname}: no look for slot {mname}")
        for mesh in meshes:
            assign_slots(mesh, lambda slot: mis.get(slot))
            enable_nanite(mesh)
            ASSETS.save_loaded_asset(mesh)
            box = mesh.get_bounding_box()
            log(f"  {mesh.get_name()}: slots {mesh_slots(mesh)} bounds min {box.min} max {box.max}")
        delete_imported_materials_except(folder, mats)
        ASSETS.save_directory(folder)


def delete_imported_materials_except(folder, keep_folder):
    for mat in assets_in(folder, unreal.MaterialInterface):
        path = mat.get_path_name().split(".")[0]
        if not path.startswith(keep_folder):
            ASSETS.delete_asset(path)


# --------------------------------------------------------------------------------------------------
# Drivers and outfits
# --------------------------------------------------------------------------------------------------
# The stock kart's texture under the paint (see M_KartPaint).
PAINT_DESATURATION = 0.75
PAINT_BRIGHTNESS = 2.0

FACES = ["mira", "kai", "leo", "nia", "omar", "rose"]
BODIES = ["female", "male"]

# appearanceColors.ts fixed surfaces
EYE_WHITE = hex_linear("#fff8f0")
EYE_DARK = hex_linear("#1a1220")
IRIS = hex_linear("#5c3a22")


def shared_avatar_mis():
    surface = ASSETS.load_asset(f"{AVATAR_MATS}/M_AvatarSurface")
    skin = ASSETS.load_asset(f"{AVATAR_MATS}/M_AvatarSkin")
    cloth = ASSETS.load_asset(f"{AVATAR_MATS}/M_AvatarCloth")
    folder = f"{AVATAR_MATS}/Shared"
    return {
        "eyeWhite": make_mi(folder, "MI_Avatar_EyeWhite", surface, {"Roughness": 0.18, "Specular": 0.6, "Glow": 0.06}, {"Tint": EYE_WHITE}),
        "iris": make_mi(folder, "MI_Avatar_Iris", surface, {"Roughness": 0.06, "Specular": 0.7, "Catchlight": 6.0, "Glow": 0.25}, {"Tint": IRIS}),
        "eyeDark": make_mi(folder, "MI_Avatar_EyeDark", surface, {"Roughness": 0.06, "Specular": 0.7, "Catchlight": 6.0}, {"Tint": EYE_DARK}),
        "mouth": make_mi(folder, "MI_Avatar_Mouth", surface, {"Roughness": 0.6}, {"Tint": (0.038, 0.014, 0.014)}),
        "nostril": make_mi(folder, "MI_Avatar_Nostril", surface, {"Roughness": 0.7}, {"Tint": (0.141, 0.051, 0.033)}),
        "teeth": make_mi(folder, "MI_Avatar_Teeth", surface, {"Roughness": 0.35}, {"Tint": (0.799, 0.738, 0.631)}),
        "rubber": make_mi(folder, "MI_Avatar_Rubber", surface, {"Roughness": 0.85, "Specular": 0.3}, {"Tint": (0.558, 0.552, 0.515)}),
        "lace": make_mi(folder, "MI_Avatar_Lace", cloth, {"Roughness": 0.85}, {"Tint": (0.855, 0.823, 0.745)}),
        "acc_glasses": make_mi(folder, "MI_Avatar_Glasses", surface, {"Roughness": 0.25, "Specular": 0.6}, {"Tint": EYE_DARK}),
        "lips": make_mi(folder, "MI_Avatar_Lips", skin, {"Roughness": 0.45}, {"Tint": (0.45, 0.13, 0.12)}),
    }


def import_drivers():
    skin_m = ASSETS.load_asset(f"{AVATAR_MATS}/M_AvatarSkin")
    hair_m = ASSETS.load_asset(f"{AVATAR_MATS}/M_AvatarHair")
    cloth_m = ASSETS.load_asset(f"{AVATAR_MATS}/M_AvatarCloth")
    shared = shared_avatar_mis()
    only = [s for s in os.environ.get("KART_FACES", "").split(",") if s]
    if not only:
        # A full run rebuilds the shared skeleton from the first driver.
        if ASSETS.does_directory_exist("/Game/Avatars/Drivers"):
            ASSETS.delete_directory("/Game/Avatars/Drivers")
        if ASSETS.does_asset_exist(SKELETON):
            ASSETS.delete_asset(SKELETON)
    skeleton = ASSETS.load_asset(SKELETON) if ASSETS.does_asset_exist(SKELETON) else None
    for face in FACES:
        for body in BODIES:
            if only and f"{face}_{body}" not in only:
                continue
            stem = f"{face}-{body}"
            folder = f"/Game/Avatars/Drivers/{face}_{body}"
            if ASSETS.does_directory_exist(folder):
                ASSETS.delete_directory(folder)
            import_glb(os.path.join(ART, "avatar4", f"{stem}.glb"), folder, skeletal=True, skeleton=skeleton)
            fix_textures(folder)
            if skeleton is None:
                skels = assets_in(folder, unreal.Skeleton)
                if not skels:
                    raise RuntimeError(f"{stem} produced no skeleton")
                skeleton = rename(skels[0], "/Game/Avatars", "SK_Avatar4_Skeleton")
            meshes = assets_in(folder, unreal.SkeletalMesh)
            if len(meshes) != 1:
                log(f"  WARNING {stem}: {len(meshes)} skeletal meshes: {[m.get_name() for m in meshes]}")
            mesh = rename(meshes[0], folder, f"SK_Driver_{face}_{body}")
            for extra in assets_in(folder, unreal.PhysicsAsset):
                ASSETS.delete_asset(extra.get_path_name().split(".")[0])
            entry = MATERIAL_TABLE[f"avatar4/{stem}.glb"]["skin"]["tex"]
            albedo, normal, ao = (find_texture(folder, entry.get(k)) for k in ("BaseColor", "Normal", "Occlusion"))
            textures = {"BaseTex": albedo, "NormalTex": normal, "AOTex": ao}
            mats = f"{folder}/Materials"
            mi_skin = make_mi(mats, f"MI_{face}_{body}_Skin", skin_m, {"Roughness": 0.52}, textures=textures)
            mi_hair = make_mi(mats, f"MI_{face}_{body}_Hair", hair_m, {"Roughness": 0.42}, textures=textures)
            mi_cloth = make_mi(mats, f"MI_{face}_{body}_Cloth", cloth_m, {"Roughness": 0.82}, textures=textures)

            def choose(slot):
                if slot in ("skin", "skin_body"):
                    return mi_skin
                if slot == "hair" or slot.startswith("hair_"):
                    return mi_hair
                if slot in ("jersey", "pants", "waist", "shoes", "acc_cap", "acc_headband"):
                    return mi_cloth
                return shared.get(slot)

            assign_slots(mesh, choose)
            slots = [str(m.get_editor_property("material_slot_name")) for m in mesh.get_editor_property("materials")]
            log(f"  {mesh.get_name()}: {len(slots)} slots {slots}")
            log(f"  morphs: {[t.get_name() for t in mesh.get_editor_property('morph_targets')]}")
            delete_imported_materials_except(folder, mats)
            ASSETS.save_directory(folder)
    ASSETS.save_asset(SKELETON, only_if_is_dirty=False)


def import_outfits():
    cloth_m = ASSETS.load_asset(f"{AVATAR_MATS}/M_AvatarCloth")
    skin_m = ASSETS.load_asset(f"{AVATAR_MATS}/M_AvatarSkin")
    skeleton = ASSETS.load_asset(SKELETON)
    shared = shared_avatar_mis()
    for rel, table in MATERIAL_TABLE.items():
        if not rel.startswith("clothing/"):
            continue
        stem = os.path.basename(rel)[:-4]
        outfit, body = stem.rsplit("-", 1)
        safe = outfit.replace("-", "_")
        folder = f"/Game/Avatars/Outfits/{safe}_{body}"
        if ASSETS.does_directory_exist(folder):
            ASSETS.delete_directory(folder)
        import_glb(os.path.join(ART, "clothing", f"{stem}.glb"), folder, skeletal=True, skeleton=skeleton)
        fix_textures(folder)
        meshes = assets_in(folder, unreal.SkeletalMesh)
        mesh = rename(meshes[0], folder, f"SK_Outfit_{safe}_{body}")
        for extra in assets_in(folder, unreal.PhysicsAsset):
            ASSETS.delete_asset(extra.get_path_name().split(".")[0])
        mats = f"{folder}/Materials"
        mis = {}
        for mname, entry in table.items():
            if mname == "skin":
                mis[mname] = make_mi(mats, f"MI_{safe}_{body}_Skin", skin_m, {"Roughness": 0.52})
            elif mname in shared:
                mis[mname] = shared[mname]
            else:
                normal = find_texture(folder, entry["tex"].get("Normal"))
                mis[mname] = make_mi(mats, f"MI_{safe}_{body}_{mname.replace('Clothing_', '')}", cloth_m,
                                     {"Roughness": entry["rough"]}, {"Tint": entry["base"][:3]},
                                     {"NormalTex": normal})
        assign_slots(mesh, lambda slot: mis.get(slot))
        log(f"  {mesh.get_name()}: {[str(m.get_editor_property('material_slot_name')) for m in mesh.get_editor_property('materials')]}")
        delete_imported_materials_except(folder, mats)
        ASSETS.save_directory(folder)


def main():
    if STAGE in ("all", "materials", "vehicles", "kartmaterials"):
        make_writable(KART_MATS, AVATAR_MATS)
    if STAGE == "kartmaterials":
        # Only the vehicle masters and detail maps (leaves the avatar materials alone).
        import_detail_textures()
        make_kart_masters()
    if STAGE in ("all", "materials"):
        import_defaults()
        import_detail_textures()
        make_kart_masters()
        make_avatar_masters()
    if STAGE == "avatarmaterials":
        # Only the avatar masters and the shared eye and mouth instances (drivers keep their own instances).
        make_writable(AVATAR_MATS, "/Game/Avatars/Drivers")
        make_avatar_masters()
        # Update the shared instances in place: the driver meshes reference them, so they must not be recreated.
        for name, scalars in (("EyeWhite", {"Glow": 0.06}), ("Iris", {"Roughness": 0.06, "Catchlight": 6.0, "Glow": 0.25}),
                              ("EyeDark", {"Roughness": 0.06, "Catchlight": 6.0})):
            mi = ASSETS.load_asset(f"{AVATAR_MATS}/Shared/MI_Avatar_{name}")
            for key, value in scalars.items():
                MEL.set_material_instance_scalar_parameter_value(mi, key, value)
            ASSETS.save_loaded_asset(mi)
        tune_driver_instances()
    if STAGE == "swaps":
        finish_swaps()
    if STAGE == "usage":
        fix_usage()
    if STAGE == "paint":
        make_kart_masters()
        mi = ASSETS.load_asset("/Game/Karts/Kart/Materials/MI_Kart_Paint")
        MEL.set_material_instance_scalar_parameter_value(mi, "TexDesaturation", PAINT_DESATURATION)
        MEL.set_material_instance_scalar_parameter_value(mi, "TexBrightness", PAINT_BRIGHTNESS)
        ASSETS.save_loaded_asset(mi)
    if STAGE in ("all", "vehicles"):
        import_vehicles()
    if STAGE in ("all", "drivers"):
        import_drivers()
    if STAGE in ("all", "outfits"):
        import_outfits()
    log("done")


try:
    main()
except Exception as error:  # noqa: BLE001 - report and still quit a headless run
    import traceback
    unreal.log_error(f"KartArt: failed: {error}\n{traceback.format_exc()}")
finally:
    if os.environ.get("KART_QUIT") == "1":
        unreal.SystemLibrary.quit_editor()
