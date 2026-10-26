"""Mobile-only material variants: re-routes chosen material inputs through a Feature Level Switch, so phones (ES3.1,
the mobile renderer) get a value tuned for their lighting while every desktop feature level keeps the exact graph it
had. The PC look cannot change: the switch's Default pin carries the original expression, and SM5/SM6 use Default.

    .\\Tools\\ue\\run.ps1 mobile_material_variants.py -NullRhi

Why each entry exists (the mobile renderer has no Lumen reflections and no depth-based water absorption):
  M_TrackWater  The single layer water falls back to a translucent pass on mobile (MOBILE_SINGLELAYERWATER_FALLBACK),
                fogged per vertex across quads hundreds of metres wide and without depth scattering: the sea washed
                out to white. Per-pixel fog (FLAGS), a surface colour taken from each venue's own Scattering
                parameter, a damped grazing reflection (specular 0.2) and a softer one (roughness 0.25) bring it
                back. Tried and rejected: a constant teal (Sumo's night sea lit bright blue under the moon) and
                roughness 0.45 (Fishing's low sun spread into a glare column over the lake).
A value of None removes this script's mobile constant from that property (ES3.1 then reads the Default pin).
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

# material -> { property: mobile value (float, RGB tuple, or None = the desktop input) }
VARIANTS = {
    "/Game/Tracks/Shared/Materials/M_TrackWater": {
        # Half the venue's scattering colour as the surface colour: Harbor's turquoise, Sumo's near-black night sea,
        # the lake's green (the same parameter the venues already set; the default matches the master's).
        MP.MP_BASE_COLOR: ("param", "Scattering", (0.02, 0.12, 0.11), 0.5),
        # Blurs the sky light's bright horizon in the reflection; above ~0.3 the low sun spreads into a glare column.
        MP.MP_ROUGHNESS: 0.25,
        # F0 under 0.02 lets the mobile env BRDF damp the grazing sky reflection that whitened the sea.
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
    """The mobile value's expression: a constant, an RGB constant, or ("param", name, default RGB, scale): the material's
    own vector parameter (a same-named node shares the instances' value) times a scale."""
    if isinstance(value, tuple) and value and value[0] == "param":
        _, name, default, scale = value
        param = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, x - 300, y)
        param.set_editor_property("parameter_name", name)
        param.set_editor_property("default_value", unreal.LinearColor(default[0], default[1], default[2], 1.0))
        param.set_editor_property("desc", TAG)
        factor = constant(material, float(scale), x - 300, y + 120)
        node = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, x, y)
        mel.connect_material_expressions(param, "", node, "A")
        mel.connect_material_expressions(factor, "", node, "B")
        node.set_editor_property("desc", TAG)
        return node
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


def delete_mobile_value(material, node):
    """Deletes a mobile value expression made by constant() and its tagged inputs, never the desktop graph."""
    if node is None or is_ours(node) or node.get_editor_property("desc") != TAG:
        return
    for child in mel.get_inputs_for_material_expression(material, node):
        delete_mobile_value(material, child)
    mel.delete_material_expression(material, node)


def route(material, prop, value, index):
    current = mel.get_material_property_input_node(material, prop)
    y = 1600 + index * 200
    if current is not None and is_ours(current):
        # Already switched: replace the mobile value only (the old mobile constant is deleted, the desktop input kept).
        for node in mel.get_inputs_for_material_expression(material, current):
            delete_mobile_value(material, node)
        if value is None:
            return "desktop input"
        mobile = constant(material, value, -600, y)
        if not mel.connect_material_expressions(mobile, "", current, "ES3_1"):
            raise RuntimeError(f"{material.get_name()}: reconnecting the ES3_1 pin failed for {prop}")
        return "updated"
    if value is None:
        return "untouched"
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
