"""Imports the decoded vehicles, avatar4 drivers and outfits (Tools/art/.cache/out, from Tools/art/decode.mjs)
and builds their materials.

Run headless from the project root, after `node Tools/art/decode.mjs`:
    .\\Tools\\ue\\run.ps1 import_karts.py              (everything)
    .\\Tools\\ue\\run.ps1 import_karts.py @{ KART_ART = 'vehicles' }   (vehicles | drivers | outfits | materials)

Layout:
    /Game/Karts/Materials      M_KartPaint (clear coat), M_KartTyre, M_VehiclePart, default textures
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

with open(os.path.join(ART, "materials.json"), encoding="utf-8") as f:
    MATERIAL_TABLE = json.load(f)


def log(msg):
    unreal.log(f"KartArt: {msg}")


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
        if ASSETS.does_asset_exist(path):
            ASSETS.delete_asset(path)
        self.mat = TOOLS.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
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
        MEL.layout_material_expressions(self.mat)
        MEL.recompile_material(self.mat)
        ASSETS.save_loaded_asset(self.mat)
        return self.mat


SAMPLER_COLOR = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
SAMPLER_LINEAR = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
SAMPLER_MASKS = unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
SAMPLER_NORMAL = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL




def flat_normal():
    return ASSETS.load_asset("/Engine/EngineMaterials/DefaultNormal")


def make_kart_masters():
    white, linear, black = default_tex("T_Default_White"), default_tex("T_Default_WhiteLinear"), default_tex("T_Default_Black")

    # Spec 01 Â§9.2: BaseColor = Tex * PaintColor, Roughness = 0.38 * MR.G, Metallic = 0.12 * MR.B,
    # ClearCoat 1 / 0.07, Emissive = PaintColor * 0.045 * 0.3 (plus the texture's own lamp strips).
    g = Graph(KART_MATS, "M_KartPaint", unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    paint = g.vector("PaintColor", (0.871, 0.084, 0.270))
    base = g.texture("BaseTex", white, SAMPLER_COLOR)
    g.out(g.mul(base, paint, "RGB"), "BaseColor")
    mr = g.texture("MRTex", linear, SAMPLER_MASKS)
    g.out(g.mul(mr, g.scalar("RoughnessScale", 0.38), "G"), "Roughness")
    g.out(g.mul(mr, g.scalar("MetallicScale", 0.12), "B"), "Metallic")
    normal = g.texture("NormalTex", flat_normal(), SAMPLER_NORMAL)
    g.out(normal, "Normal", "RGB")
    glow = g.mul(paint, g.scalar("PaintGlow", 0.0135))
    lamps = g.mul(g.texture("EmissiveTex", black, SAMPLER_COLOR), g.scalar("EmissiveStrength", 4.0), "RGB")
    g.out(g.add(glow, lamps), "EmissiveColor")
    g.out(g.scalar("ClearCoat", 1.0), "ClearCoat")
    g.out(g.scalar("ClearCoatRoughness", 0.07), "ClearCoatRoughness")
    g.save()

    # Tyre (kartMaterials.ts:46-53): #1b1b1d multiplying the tyre texture, roughness 0.9.
    g = Graph(KART_MATS, "M_KartTyre", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    tint = g.vector("Tint", (0.0116, 0.0116, 0.0131))
    g.out(g.mul(g.texture("BaseTex", white, SAMPLER_COLOR), tint, "RGB"), "BaseColor")
    mr = g.texture("MRTex", linear, SAMPLER_MASKS)
    g.out(g.mul(mr, g.scalar("RoughnessScale", 0.9), "G"), "Roughness")
    g.out(g.scalar("Specular", 0.35), "Specular")
    g.out(g.texture("NormalTex", flat_normal(), SAMPLER_NORMAL), "Normal", "RGB")
    g.save()

    # Untextured vehicle trim (moto/coupe/truck m_Accent, m_Chrome, m_Lamp, m_Glass, ...).
    g = Graph(KART_MATS, "M_VehiclePart", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    color = g.vector("Color", (0.5, 0.5, 0.5))
    g.out(color, "BaseColor")
    g.out(g.scalar("Roughness", 0.5), "Roughness")
    g.out(g.scalar("Metallic", 0.0), "Metallic")
    g.out(g.scalar("Specular", 0.5), "Specular")
    g.out(g.mul(color, g.scalar("Emissive", 0.0)), "EmissiveColor")
    g.save()

    # Name plate: unlit, two-sided text and its backing.
    g = Graph(KART_MATS, "M_NamePlateBack", unreal.MaterialShadingModel.MSM_UNLIT)
    g.mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    g.mat.set_editor_property("two_sided", True)
    color = g.vector("Color", (0.02, 0.02, 0.03))
    g.out(color, "EmissiveColor")
    g.out(g.scalar("Opacity", 0.6), "Opacity")
    g.save()


def make_avatar_masters():
    white, linear = default_tex("T_Default_White"), default_tex("T_Default_WhiteLinear")

    def common(g, tint_default, rough, use_vertex_color=False, ao=True):
        tint = g.vector("Tint", tint_default)
        base = g.mul(g.texture("BaseTex", white, SAMPLER_COLOR), tint, "RGB")
        if use_vertex_color:
            vc = g.node(unreal.MaterialExpressionVertexColor)
            base = g.mul(base, vc, "", "")
        g.out(base, "BaseColor")
        g.out(g.scalar("Roughness", rough), "Roughness")
        g.out(g.texture("NormalTex", flat_normal(), SAMPLER_NORMAL), "Normal", "RGB")
        if ao:
            g.out(g.texture("AOTex", linear, SAMPLER_MASKS), "AmbientOcclusion", "R")
        return tint

    # Skin: screen-space subsurface (the engine's default profile) over the albedo detail, vertex-colour
    # warmth on cheeks and nose, a soft sheen.
    g = Graph(AVATAR_MATS, "M_AvatarSkin", unreal.MaterialShadingModel.MSM_SUBSURFACE_PROFILE)
    common(g, (0.63, 0.25, 0.14), 0.5, use_vertex_color=True)
    g.out(g.scalar("Specular", 0.35), "Specular")
    g.save()

    g = Graph(AVATAR_MATS, "M_AvatarHair", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    common(g, (0.04, 0.018, 0.012), 0.42)
    g.out(g.scalar("Specular", 0.3), "Specular")
    g.save()

    # Fabric: the cloth shading model (fuzz sheen at grazing angles) for jersey, pants, shoes and outfits.
    g = Graph(AVATAR_MATS, "M_AvatarCloth", unreal.MaterialShadingModel.MSM_CLOTH)
    tint = common(g, (0.8, 0.8, 0.8), 0.82)
    fuzz = g.mul(g.add(tint, g.scalar("FuzzLift", 0.35)), g.scalar("FuzzScale", 0.55))
    g.out(fuzz, "SubsurfaceColor")
    g.out(g.scalar("Cloth", 0.6), "ClearCoat")
    g.save()

    g = Graph(AVATAR_MATS, "M_AvatarSurface", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    tint = g.vector("Tint", (0.5, 0.5, 0.5))
    g.out(tint, "BaseColor")
    g.out(g.scalar("Roughness", 0.5), "Roughness")
    g.out(g.scalar("Specular", 0.5), "Specular")
    g.out(g.scalar("Metallic", 0.0), "Metallic")
    g.save()


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
def import_vehicles():
    paint = ASSETS.load_asset(f"{KART_MATS}/M_KartPaint")
    tyre = ASSETS.load_asset(f"{KART_MATS}/M_KartTyre")
    part = ASSETS.load_asset(f"{KART_MATS}/M_VehiclePart")
    for vid, vname in VEHICLES.items():
        folder = f"/Game/Karts/{vname}"
        if ASSETS.does_directory_exist(folder):
            ASSETS.delete_directory(folder)
        import_glb(os.path.join(ART, "vehicles", f"{vid}.glb"), folder, skeletal=False)
        fix_textures(folder)
        table = MATERIAL_TABLE[f"vehicles/{vid}.glb"]
        mis = {}
        for mname, entry in table.items():
            tex = {k: find_texture(folder, v) for k, v in entry["tex"].items()}
            if mname == "m_Body":
                mis[mname] = make_mi(f"{folder}/Materials", f"MI_{vname}_Paint", paint,
                                     vectors={"PaintColor": entry["base"][:3]},
                                     textures={"BaseTex": tex.get("BaseColor"), "MRTex": tex.get("MetallicRoughness"),
                                               "NormalTex": tex.get("Normal"), "EmissiveTex": tex.get("Emissive")})
            elif mname == "m_Tire" and tex:
                mis[mname] = make_mi(f"{folder}/Materials", f"MI_{vname}_Tyre", tyre,
                                     textures={"BaseTex": tex.get("BaseColor"), "MRTex": tex.get("MetallicRoughness"),
                                               "NormalTex": tex.get("Normal")})
            else:
                emissive = {"m_Lamp": 3.0, "m_Tail": 0.6}.get(mname, 0.0)
                spec = 0.9 if mname == "m_Glass" else 0.5
                mis[mname] = make_mi(f"{folder}/Materials", f"MI_{vname}_{mname[2:]}", part,
                                     scalars={"Roughness": entry["rough"], "Metallic": entry["metal"],
                                              "Emissive": emissive, "Specular": spec},
                                     vectors={"Color": entry["base"][:3]})
        for mesh in assets_in(folder, unreal.StaticMesh):
            short = mesh.get_name()
            for prefix in ("SM_",):
                if short.startswith(prefix):
                    short = short[len(prefix):]
            renamed = rename(mesh, folder, f"SM_{vname}_{short}")
            assign_slots(renamed, lambda slot: mis.get(slot))
            box = renamed.get_bounding_box()
            log(f"  {renamed.get_name()}: slots {[str(m.get_editor_property('material_slot_name')) for m in renamed.get_editor_property('static_materials')]} "
                f"bounds min {box.min} max {box.max}")
        delete_imported_materials_except(folder, f"{folder}/Materials")
        ASSETS.save_directory(folder)


def delete_imported_materials_except(folder, keep_folder):
    for mat in assets_in(folder, unreal.MaterialInterface):
        path = mat.get_path_name().split(".")[0]
        if not path.startswith(keep_folder):
            ASSETS.delete_asset(path)


# --------------------------------------------------------------------------------------------------
# Drivers and outfits
# --------------------------------------------------------------------------------------------------
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
        "eyeWhite": make_mi(folder, "MI_Avatar_EyeWhite", surface, {"Roughness": 0.18, "Specular": 0.6}, {"Tint": EYE_WHITE}),
        "iris": make_mi(folder, "MI_Avatar_Iris", surface, {"Roughness": 0.08, "Specular": 0.7}, {"Tint": IRIS}),
        "eyeDark": make_mi(folder, "MI_Avatar_EyeDark", surface, {"Roughness": 0.08, "Specular": 0.7}, {"Tint": EYE_DARK}),
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
    if STAGE in ("all", "materials"):
        import_defaults()
        make_kart_masters()
        make_avatar_masters()
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
