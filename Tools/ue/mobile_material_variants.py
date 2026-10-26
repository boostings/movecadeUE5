"""Mobile-only material variants: re-routes chosen material inputs through a Feature Level Switch, so phones (ES3.1,
the mobile renderer) get a value tuned for their lighting while every desktop feature level keeps the exact graph it
had. The PC look cannot change: the switch's Default pin carries the original expression, and SM5/SM6 use Default.

    .\\Tools\\ue\\run.ps1 mobile_material_variants.py -NullRhi

Why each variant exists (the mobile renderer has no Lumen reflections and no depth-based water absorption):
  M_TrackWater  The single layer water falls back to a translucent pass on mobile (MOBILE_SINGLELAYERWATER_FALLBACK):
                no scattering through depth, and its 0.04-roughness specular reflects only the sky light's bright
                horizon, so the sea reads as flat white. Phones get the sea colour the PC water shows at mid depth
                as base colour, a broader highlight and less specular, and fog per pixel (see FLAGS).
Idempotent: a property already routed through a switch made by this script only gets its mobile value updated.
Run it again after any script that rebuilds these materials (import_track_assets.py rebuilds M_TrackWater).
"""
import os
import stat

import unreal

quit_after = os.environ.get("KART_QUIT") == "1"
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
TAG = "KartMobileVariant"

# material -> { property: mobile value (float or RGB tuple) }
VARIANTS = {
    "/Game/Tracks/Shared/Materials/M_TrackWater": {
        MP.MP_BASE_COLOR: (0.02, 0.09, 0.12),
        MP.MP_ROUGHNESS: 0.45,
        MP.MP_SPECULAR: 0.2,
    },
}


# material -> { material property flag: value }, for flags that only the mobile path reads.
#   compute_fog_per_pixel: translucent materials fog per vertex by default; the water's quads are hundreds of metres
#   wide, so on phones the far vertices' fog smeared white over the whole sea. Desktop draws single layer water in
#   its own pass with per-pixel fog, so the flag has no desktop effect.
FLAGS = {
    "/Game/Tracks/Shared/Materials/M_TrackWater": {"compute_fog_per_pixel": True},
}


def log(msg):
    unreal.log(f"mobile_material_variants: {msg}")


def writable(material):
    package = material.get_path_name().split(".")[0]
    content = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    file = os.path.join(content, package[len("/Game/"):] + ".uasset")
    if os.path.exists(file):
        os.chmod(file, stat.S_IREAD | stat.S_IWRITE)


def constant(material, value, x, y):
    if isinstance(value, tuple):
        node = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, x, y)
        node.set_editor_property("constant", unreal.LinearColor(value[0], value[1], value[2], 1.0))
    else:
        node = mel.create_material_expression(material, unreal.MaterialExpressionConstant, x, y)
        node.set_editor_property("r", float(value))
    node.set_editor_property("desc", TAG)
    return node


def is_ours(node):
    return isinstance(node, unreal.MaterialExpressionFeatureLevelSwitch) and node.get_editor_property("desc") == TAG


def route(material, prop, value, index):
    current = mel.get_material_property_input_node(material, prop)
    y = 1600 + index * 200
    if current is not None and is_ours(current):
        # Already switched: replace the mobile value only (the old mobile constant is deleted, the desktop input kept).
        for node in mel.get_inputs_for_material_expression(material, current):
            if node is not None and not is_ours(node) and node.get_editor_property("desc") == TAG:
                mel.delete_material_expression(material, node)
        mobile = constant(material, value, -600, y)
        if not mel.connect_material_expressions(mobile, "", current, "ES3_1"):
            raise RuntimeError(f"{material.get_name()}: reconnecting the ES3_1 pin failed for {prop}")
        return "updated"
    if current is None:
        raise RuntimeError(f"{material.get_name()}: {prop} has no input to keep for the desktop")
    out_name = mel.get_material_property_input_node_output_name(material, prop)
    switch = mel.create_material_expression(material, unreal.MaterialExpressionFeatureLevelSwitch, -300, y)
    switch.set_editor_property("desc", TAG)
    if not mel.connect_material_expressions(current, out_name, switch, "Default"):
        raise RuntimeError(f"{material.get_name()}: connecting the desktop input failed for {prop}")
    mobile = constant(material, value, -600, y)
    if not mel.connect_material_expressions(mobile, "", switch, "ES3_1"):
        raise RuntimeError(f"{material.get_name()}: connecting the ES3_1 pin failed for {prop}")
    if not mel.connect_material_property(switch, "", prop):
        raise RuntimeError(f"{material.get_name()}: routing {prop} through the switch failed")
    return "switched"


def run():
    for path in sorted(set(VARIANTS) | set(FLAGS)):
        material = unreal.load_asset(path)
        if material is None:
            raise RuntimeError(f"missing {path}")
        props = VARIANTS.get(path, {})
        results = [f"{prop.name} {route(material, prop, value, i)}" for i, (prop, value) in enumerate(props.items())]
        for flag, value in FLAGS.get(path, {}).items():
            material.set_editor_property(flag, value)
            results.append(f"{flag}={value}")
        mel.recompile_material(material)
        writable(material)
        if not eal.save_loaded_asset(material, only_if_is_dirty=False):
            raise RuntimeError(f"{path}: save failed")
        log(f"{path}: {', '.join(results)}")


try:
    run()
except Exception as error:  # noqa: BLE001 - report and still quit a headless run
    unreal.log_error(f"mobile_material_variants: failed: {error}")
    raise
finally:
    if quit_after:
        unreal.SystemLibrary.quit_editor()
