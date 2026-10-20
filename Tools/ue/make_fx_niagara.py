"""Builds the race VFX Niagara systems under /Game/FX/Niagara.

    .\\Tools\\ue\\run.ps1 make_fx_niagara.py        (after make_fx_materials.py)

Every system is one CPU emitter driven by the same user parameters (the web's ParticleField emit() options,
KartFxLayer.tsx). The C++ side (Source/MovecadeKart/.../FX/KartFxSubsystem) sets them, then either bursts
User.Count particles once or streams User.Rate per second. Two kinds:

- Fields (sprites): Smoke (toon puffs), Fire (additive glow), Spark (velocity-stretched streaks), Ring (ground
  shock ring), Flare (star twinkles) and Confetti (flipping paper in a party palette).
  Per particle at spawn (snapshotted, so a stream's options can change every frame without touching live
  particles): lifetime, size, speed, a direction uniform in the cone of half-angle User.Spread around
  User.Direction, a jittered start, the inherited velocity, start/end colours and the end-size multiplier.
  Per frame the motion is the web's closed form under linear drag d and gravity g:
      p(t) = p0 + v0 (1 - e^-dt) / d + g (t / d - (1 - e^-dt) / d^2)
- SpeedLines: a local-space emitter for the camera: thin streaks in a tube around the view axis rushing past.

The materials (make_fx_materials.py) apply the life fade and the shapes. Authored through the editor's Niagara
toolset (NiagaraToolsets.NiagaraToolset_System via ToolsetRegistry).

Run it on an empty /Game/FX/Niagara (Tools/fx/build_fx.ps1 clears it first). Recreating a system at the path of
one deleted earlier in the same editor session saves graph pins that still point into the old package; the editor
copes, but the game (-game) then crashes loading it (an access violation in UEdGraphNode::Serialize). So the
script refuses to overwrite and asks for a fresh session instead.
"""
import json
import os
import traceback

import unreal

quit_after = os.environ.get("KART_QUIT") == "1"
FOLDER = "/Game/FX/Niagara"
TS = "NiagaraToolsets.NiagaraToolset_System"
TEMPLATE_SYSTEM = "/Niagara/DefaultAssets/Templates/Systems/DirectionalBurst.DirectionalBurst"
TEMPLATE_EMITTER = "/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"
EMITTER = "Field"

FLOAT = "/Script/Niagara.NiagaraFloat"
INT = "/Script/Niagara.NiagaraInt32"
VEC2 = "/Script/CoreUObject.Vector2f"
VEC3 = "/Script/CoreUObject.Vector3f"
POS = "/Script/Niagara.NiagaraPosition"
COLOR = "/Script/CoreUObject.LinearColor"

# Fields: ParticleField props per pool (KartFxLayer.tsx:268-274) in UE units (cm, Z up); Flare, Confetti and the
# speed lines are port additions.
SYSTEMS = {
    "Smoke": dict(kind="field", material="M_FX_Puff", drag=1.6, gravity=(0, 0, 60)),
    "Fire": dict(kind="field", material="M_FX_Soft", drag=3.0, gravity=(0, 0, 150), spin=True),
    "Spark": dict(kind="field", material="M_FX_Spark", drag=1.2, gravity=(0, 0, -900), stretch=1.4),
    "Ring": dict(kind="field", material="M_FX_Ring", drag=0.0, gravity=(0, 0, 0), ring=True),
    "Flare": dict(kind="field", material="M_FX_Flare", drag=1.0, gravity=(0, 0, 40), spin=True),
    "Confetti": dict(kind="field", material="M_FX_Confetti", drag=1.9, gravity=(0, 0, -420), confetti=True),
    "SpeedLines": dict(kind="speedlines", material="M_FX_SpeedLine"),
}

USER = [
    ("Count", INT, {"value": 0}),
    ("Rate", FLOAT, {"value": 0.0}),
    ("Direction", VEC3, {"x": 0.0, "y": 0.0, "z": 1.0}),
    ("Spread", FLOAT, {"value": 0.5}),
    ("SpeedRange", VEC2, {"x": 200.0, "y": 200.0}),
    ("LifeRange", VEC2, {"x": 0.6, "y": 0.6}),
    ("SizeRange", VEC2, {"x": 12.0, "y": 12.0}),
    ("SizeEnd", FLOAT, {"value": 0.2}),
    ("Color", COLOR, {"r": 1.0, "g": 1.0, "b": 1.0, "a": 1.0}),
    ("ColorEnd", COLOR, {"r": 1.0, "g": 1.0, "b": 1.0, "a": 1.0}),
    ("Jitter", FLOAT, {"value": 0.0}),
    ("Inherit", VEC3, {"x": 0.0, "y": 0.0, "z": 0.0}),
]

# Party palette for confetti (sRGB-ish linear values): pink, yellow, cyan, lime, violet, orange.
CONFETTI = ["float3(1.0, 0.18, 0.45)", "float3(1.0, 0.82, 0.08)", "float3(0.1, 0.75, 1.0)",
            "float3(0.35, 0.95, 0.2)", "float3(0.6, 0.25, 1.0)", "float3(1.0, 0.45, 0.08)"]

LOG = []


def log(msg):
    LOG.append(msg)
    unreal.log(f"make_fx_niagara: {msg}")


def tool(name, args, toolset=TS):
    res = unreal.ToolsetRegistry.execute_tool(toolset, name, json.dumps(args))
    for _ in range(500000):
        done = res.is_complete() if callable(res.is_complete) else res.is_complete
        if done:
            break
    err = res.get_editor_property("error")
    if err:
        raise RuntimeError(f"{name}: {err}")
    val = res.get_value_as_json_string()
    try:
        out = json.loads(val)
        # Some results come back JSON-encoded twice.
        return json.loads(out) if isinstance(out, str) and out[:1] in "{[" else out
    except Exception:  # noqa: BLE001
        return val


def ref(system, emitter="", script="", module="", renderer=-1, inputs=None):
    return {"system": {"refPath": system}, "emitterName": emitter, "scriptName": script, "moduleName": module,
            "rendererIndex": renderer, "inputNameStack": inputs or []}


def typ(path):
    return {"classStructOrEnum": {"refPath": path}}


def link_value(name, type_path):
    return {"struct": {"refPath": "/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"},
            "value": {"linkedVariable": {"name": name, "type": typ(type_path)}}}


def hlsl_value(expr):
    return {"struct": {"refPath": "/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression"},
            "value": {"hlslExpression": expr}}


def set_input(system, script, module, input_name, value):
    return tool("SetStackInputData", {"stackInputRef": ref(system, EMITTER, script, module, inputs=[input_name]), "inputData": value})


def set_params(system, script, entries):
    """One SetParameters module; entries = [(attribute, type, hlsl)]. Returns the module name."""
    params = [{"variable": {"name": n, "type": typ(t)}} for n, t, _ in entries]
    topo = tool("AddSetParametersModule", {"moduleLocationRef": ref(system, EMITTER, script), "parameters": params})
    module = topo["returnValue"]["moduleName"]
    for n, _, expr in entries:
        set_input(system, script, module, n, hlsl_value(expr))
    return module


def add_module(system, script, asset):
    topo = tool("AddModule", {"moduleLocationRef": ref(system, EMITTER, script), "moduleAsset": {"refPath": asset}})
    return topo["returnValue"]["moduleName"]


def enum_value(enum_path, display):
    info = tool("UEnum_Info", {"enum": {"refPath": enum_path}}, "NiagaraToolsets.NiagaraToolset_Info")
    text = info.get("returnValue", info) if isinstance(info, dict) else info
    data = json.loads(text) if isinstance(text, str) else text
    short = enum_path.split(".")[-1]
    names = [v["Name"] for v in data["Values"] if v["DisplayName"] == display]
    if not names:
        raise RuntimeError(f"{short} has no '{display}': {[v['DisplayName'] for v in data['Values']]}")
    return f"{short}::{names[0]}"


def set_enum(system, script, module, input_name, enum_path, display, emitter=EMITTER):
    tool("SetStackInputData", {"stackInputRef": ref(system, emitter, script, module, inputs=[input_name]),
                               "inputData": {"struct": {"refPath": "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"},
                                             "value": {"enum": {"refPath": enum_path}, "enumName": enum_value(enum_path, display),
                                                       "displayName": display}}})


def rand(k1, k2):
    return f"frac(sin(Particles.Seed * {k1} + {k2}) * 43758.5453)"


def new_system(name, local_space=False):
    """A system from the template with one empty CPU emitter, looping forever."""
    created = tool("CreateNiagaraSystem", {"assetName": name, "assetPath": FOLDER, "templateSystem": {"refPath": TEMPLATE_SYSTEM}})
    if not isinstance(created, dict):
        raise RuntimeError(f"CreateNiagaraSystem returned {created!r}")
    system = created["returnValue"]["refPath"]
    summary = tool("GetSystemSummary", {"system": {"refPath": system}})["returnValue"]
    for e in summary["emitters"]:
        tool("RemoveEmitter", {"emitterToRemove": ref(system, e["emitterName"])})
    tool("AddEmitter", {"system": {"refPath": system}, "templateEmitter": {"refPath": TEMPLATE_EMITTER}, "emitterName": EMITTER})
    tool("SetEmitterData", {"emitter": ref(system, EMITTER), "emitterData": {"propertyValues": json.dumps({
        "SimTarget": "CPUSim", "bLocalSpace": local_space, "bRequiresPersistentIDs": True, "InterpolatedSpawnMode": "Interpolation"})}})
    tool("AddUserVariables", {"system": {"refPath": system}, "variablesToAdd": [
        {"name": f"User.{n}", "type": typ(t), "defaultValue": {"struct": {"refPath": t}, "value": v}, "description": ""} for n, t, v in USER]})

    # Emitter update: stream at User.Rate, burst User.Count once per activation.
    rate = add_module(system, "EmitterUpdateScript", "/Niagara/Modules/Emitter/SpawnRate.SpawnRate")
    set_input(system, "EmitterUpdateScript", rate, "SpawnRate", link_value("User.Rate", FLOAT))
    burst = add_module(system, "EmitterUpdateScript", "/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous")
    set_input(system, "EmitterUpdateScript", burst, "Spawn Count", link_value("User.Count", INT))
    # The system and the emitter loop forever: a burst fires once per activation, a stream runs while active.
    try:
        set_enum(system, "SystemUpdateScript", "SystemState", "Loop Behavior",
                 "/Niagara/Enums/ENiagara_EmitterStateOptions.ENiagara_EmitterStateOptions", "Infinite", emitter="")
    except Exception as error:  # noqa: BLE001
        log(f"{name}: system loop not changed: {error}")
    set_enum(system, "EmitterUpdateScript", "EmitterState", "Life Cycle Mode",
             "/Niagara/Enums/ENiagaraEmitterLifeCycleMode.ENiagaraEmitterLifeCycleMode", "Self")
    set_enum(system, "EmitterUpdateScript", "EmitterState", "Loop Behavior",
             "/Niagara/Enums/ENiagara_EmitterStateOptions.ENiagara_EmitterStateOptions", "Infinite")
    return system


def common_spawn(system, extra_first=None):
    """Seed, life, size, speed, cone direction, jittered start and colours (shared by the fields)."""
    S = "ParticleSpawnScript"
    set_params(system, S, [("Particles.Seed", FLOAT,
        "frac(sin(float(Particles.UniqueID) * 12.9898 + Engine.Time * 78.233 + Engine.Owner.Position.x * 0.0131 + Engine.Owner.Position.y * 0.0173) * 43758.5453)")])
    set_params(system, S, [
        ("Particles.Lifetime", FLOAT, f"max(0.01, lerp(User.LifeRange.x, User.LifeRange.y, {rand(91.7, 0.31)}))"),
        ("Particles.BaseSize", FLOAT, f"lerp(User.SizeRange.x, User.SizeRange.y, {rand(37.3, 1.7)})"),
        ("Particles.Speed0", FLOAT, f"lerp(User.SpeedRange.x, User.SpeedRange.y, {rand(53.1, 2.9)})"),
        ("Particles.CosT", FLOAT, f"1.0 - {rand(71.9, 4.1)} * (1.0 - cos(User.Spread))"),
        ("Particles.Phi", FLOAT, f"{rand(17.7, 5.3)} * 6.2831853"),
        ("Particles.Start", POS, f"Particles.Position + (float3({rand(11.1, 0.7)}, {rand(23.3, 1.1)}, {rand(29.9, 2.3)}) * 2.0 - 1.0) * User.Jitter"),
        ("Particles.ColorStart", COLOR, "User.Color"),
        ("Particles.ColorEnd", COLOR, "User.ColorEnd"),
        ("Particles.SizeEndMul", FLOAT, "User.SizeEnd"),
        ("Particles.Dir", VEC3, "normalize(User.Direction + float3(0.0, 0.0, 0.00001))"),
    ])
    set_params(system, S, [("Particles.BasisA", VEC3,
        "normalize(cross(abs(Particles.Dir.z) > 0.95 ? float3(1.0, 0.0, 0.0) : float3(0.0, 0.0, 1.0), Particles.Dir))")])
    set_params(system, S, [("Particles.Velocity0", VEC3,
        "(Particles.Dir * Particles.CosT + (Particles.BasisA * cos(Particles.Phi) + cross(Particles.Dir, Particles.BasisA) * sin(Particles.Phi))"
        " * sqrt(max(0.0, 1.0 - Particles.CosT * Particles.CosT))) * Particles.Speed0 + User.Inherit")])


def motion(cfg):
    d = cfg["drag"]
    g = "float3({:.4f}, {:.4f}, {:.4f})".format(*cfg["gravity"])
    if d > 0.0:
        e = f"(1.0 - exp(-{d:.4f} * Particles.Age))"
        pos = f"Particles.Start + Particles.Velocity0 * ({e} / {d:.4f}) + {g} * (Particles.Age / {d:.4f} - {e} / {d * d:.5f})"
        vel = f"Particles.Velocity0 * exp(-{d:.4f} * Particles.Age) + {g} * ({e} / {d:.4f})"
    else:
        pos = f"Particles.Start + Particles.Velocity0 * Particles.Age + 0.5 * {g} * Particles.Age * Particles.Age"
        vel = f"Particles.Velocity0 + {g} * Particles.Age"
    return pos, vel


def build_field(name, cfg):
    system = new_system(name)
    common_spawn(system)
    S, U = "ParticleSpawnScript", "ParticleUpdateScript"
    ring, confetti = cfg.get("ring", False), cfg.get("confetti", False)
    spawn_last = [("Particles.Position", POS, "Particles.Start"), ("Particles.Velocity", VEC3, "Particles.Velocity0"),
                  ("Particles.Color", COLOR, "Particles.ColorStart"),
                  ("Particles.SpriteSize", VEC2, "float2(Particles.BaseSize, Particles.BaseSize)")]
    if ring:
        spawn_last += [("Particles.SpriteFacing", VEC3, "float3(0.0, 0.0, 1.0)"), ("Particles.SpriteAlignment", VEC3, "float3(1.0, 0.0, 0.0)")]
    if confetti:
        # A palette colour per piece, tinted by User.Color (white keeps the palette).
        pick = f"floor({rand(5.3, 8.1)} * {len(CONFETTI)}.0)"
        chain = CONFETTI[-1]
        for i in range(len(CONFETTI) - 2, -1, -1):
            chain = f"({pick} < {i + 1}.0 ? {CONFETTI[i]} : {chain})"
        spawn_last.append(("Particles.Tint", VEC3, chain))
    set_params(system, S, spawn_last)

    pos, vel = motion(cfg)
    set_params(system, U, [("Particles.Position", POS, pos), ("Particles.Velocity", VEC3, vel)])
    t = "sqrt(saturate(Particles.NormalizedAge))" if ring else "saturate(Particles.NormalizedAge)"
    grow = f"lerp(1.0, Particles.SizeEndMul, {t})"
    colour = "lerp(Particles.ColorStart.rgb, Particles.ColorEnd.rgb, saturate(Particles.NormalizedAge))"
    if cfg.get("stretch", 0.0) > 0.0:
        size = f"float2(Particles.BaseSize, Particles.BaseSize * (1.0 + length(Particles.Velocity) * 0.01 * {cfg['stretch'] * 0.06:.4f})) * {grow}"
    elif confetti:
        # A paper rectangle flipping over: its height swings through zero and it darkens as it turns away.
        size = f"float2(Particles.BaseSize, Particles.BaseSize * 0.6 * abs(cos(Particles.Age * (5.0 + Particles.Seed * 9.0) + Particles.Seed * 6.0))) * {grow}"
        colour = (f"Particles.Tint * lerp(Particles.ColorStart.rgb, Particles.ColorEnd.rgb, saturate(Particles.NormalizedAge))"
                  f" * (0.55 + 0.45 * abs(cos(Particles.Age * (5.0 + Particles.Seed * 9.0) + Particles.Seed * 6.0)))")
    else:
        size = f"float2(Particles.BaseSize, Particles.BaseSize) * {grow}"
    update = [("Particles.Color", COLOR, f"float4({colour}, 1.0)"), ("Particles.SpriteSize", VEC2, size)]
    if cfg.get("spin"):
        update.append(("Particles.SpriteRotation", FLOAT, "(Particles.Seed * 6.2831 + Particles.Age * (Particles.Seed - 0.5) * 2.0) * 57.29578"))
    if confetti:
        update.append(("Particles.SpriteRotation", FLOAT, "(Particles.Seed * 6.2831 + Particles.Age * (Particles.Seed - 0.5) * 9.0) * 57.29578"))
    set_params(system, U, update)

    renderer = {"material": {"refPath": f"/Game/FX/Materials/{cfg['material']}.{cfg['material']}"}, "sortMode": "None"}
    if cfg.get("stretch", 0.0) > 0.0:
        renderer["alignment"] = "VelocityAligned"
    if ring:
        renderer["alignment"] = "CustomAlignment"
        renderer["facingMode"] = "CustomFacingVector"
    if cfg["material"] == "M_FX_Puff":
        # Toon puffs are alpha blended: draw back to front so overlapping puffs layer properly.
        renderer["sortMode"] = "ViewDistance"
    tool("SetRendererData", {"renderer": ref(system, EMITTER, renderer=0), "rendererData": {"propertyValues": json.dumps(renderer)}})
    return system


def build_speedlines(name, cfg):
    """Local space, attached to a camera (+X forward): streaks in a tube around the view axis rush back past it."""
    system = new_system(name, local_space=True)
    S, U = "ParticleSpawnScript", "ParticleUpdateScript"
    set_params(system, S, [("Particles.Seed", FLOAT,
        "frac(sin(float(Particles.UniqueID) * 12.9898 + Engine.Time * 78.233) * 43758.5453)")])
    # User.SizeRange: tube radius (cm); User.SpeedRange: rush speed (cm/s); User.LifeRange: life (s);
    # User.Jitter: streak width (cm); Color.a: opacity.
    set_params(system, S, [
        ("Particles.Lifetime", FLOAT, f"max(0.01, lerp(User.LifeRange.x, User.LifeRange.y, {rand(91.7, 0.31)}))"),
        ("Particles.Phi", FLOAT, f"{rand(17.7, 5.3)} * 6.2831853"),
        ("Particles.Radius", FLOAT, f"lerp(User.SizeRange.x, User.SizeRange.y, sqrt({rand(37.3, 1.7)}))"),
        ("Particles.Speed0", FLOAT, f"lerp(User.SpeedRange.x, User.SpeedRange.y, {rand(53.1, 2.9)})"),
    ])
    set_params(system, S, [
        ("Particles.Position", POS, f"float3(lerp(500.0, 1600.0, {rand(11.1, 0.7)}), cos(Particles.Phi) * Particles.Radius, sin(Particles.Phi) * Particles.Radius * 0.62)"),
        ("Particles.Velocity", VEC3, "float3(-Particles.Speed0, 0.0, 0.0)"),
        ("Particles.Color", COLOR, "User.Color"),
        ("Particles.SpriteSize", VEC2, "float2(User.Jitter, Particles.Speed0 * 0.05)"),
    ])
    set_params(system, U, [
        ("Particles.Position", POS, "Particles.Position + Particles.Velocity * Engine.DeltaTime"),
        ("Particles.Color", COLOR, "float4(User.Color.rgb, User.Color.a * sin(saturate(Particles.NormalizedAge) * 3.14159))"),
    ])
    renderer = {"material": {"refPath": f"/Game/FX/Materials/{cfg['material']}.{cfg['material']}"}, "sortMode": "None",
                "alignment": "VelocityAligned", "bCastShadows": False}
    tool("SetRendererData", {"renderer": ref(system, EMITTER, renderer=0), "rendererData": {"propertyValues": json.dumps(renderer)}})
    return system


BUILDERS = {"field": build_field, "speedlines": build_speedlines}
BUILT = []


def query(tool_name, system):
    """Starts an async toolset query (it completes over editor ticks)."""
    return unreal.ToolsetRegistry.execute_tool(TS, tool_name, json.dumps({"system": {"refPath": system}}))


def complete(res):
    return res.is_complete() if callable(res.is_complete) else res.is_complete


def result_text(res):
    err = res.get_editor_property("error")
    return None if err else res.get_value_as_json_string(), str(err) if err else ""


def compiled(res):
    """True once a finished GetSystemCompileState says nothing is compiling or stale."""
    text, _ = result_text(res)
    text = (text or "").replace("\\", "")
    return '"bIsCompiling":false' in text and '"bIsStale":false' in text


def run():
    eal = unreal.EditorAssetLibrary
    only = os.environ.get("FX_ONLY")
    wanted = [f for f in SYSTEMS if not only or f in only.split(",")]
    stale = [f"NS_FX_{f}" for f in wanted if eal.does_asset_exist(f"{FOLDER}/NS_FX_{f}")]
    if stale:
        raise RuntimeError(f"{', '.join(stale)} already exist: delete them (Content/FX/Niagara) with the editor closed and run "
                           "again in a fresh session, or use Tools/fx/build_fx.ps1")
    eal.make_directory(FOLDER)
    for field in wanted:
        cfg = SYSTEMS[field]
        name = f"NS_FX_{field}"
        try:
            system = BUILDERS[cfg["kind"]](name, cfg)
            BUILT.append((name, f"{FOLDER}/{name}.{name}", system))
        except Exception as error:  # noqa: BLE001
            log(f"{name} FAILED: {error} {traceback.format_exc()}")


def write_log():
    out = os.environ.get("FX_LOG")
    if out:
        with open(out, "w", encoding="utf-8") as f:
            f.write(chr(10).join(LOG))


# Per built system: the in-flight compile-state query, then (once compiled) its stack-issues query.
JOBS = {}
TICKS = {"n": 0, "handle": None, "phase": "compile"}
POLL_TICKS = 20
MAX_TICKS = 6000


def report_issues(name, res):
    text, err = result_text(res)
    if err:
        log(f"{name}: issues error {err}")
    elif '"severity":"Error"' in (text or "").replace("\\", ""):
        log(f"{name}: STACK ERRORS {text[:4000]}")


def on_tick(_delta):
    """Polls every system's compile state until all are compiled (saving mid-compile stores a stale system),
    then collects the stack issues, saves and quits."""
    TICKS["n"] += 1
    timed_out = TICKS["n"] > MAX_TICKS
    if TICKS["phase"] == "compile":
        if TICKS["n"] % POLL_TICKS:
            return
        for name, job in JOBS.items():
            if job.get("done"):
                continue
            res = job.get("state")
            if res is not None and complete(res):
                if compiled(res):
                    job["done"] = True
                    text, _ = result_text(res)
                    if '"bHasErrors":true' in (text or "").replace("\\", ""):
                        log(f"{name}: COMPILE ERRORS {text[:4000]}")
                    continue
                res = None
            if res is None:
                job["state"] = query("GetSystemCompileState", job["system"])
        if all(j.get("done") for j in JOBS.values()) or timed_out:
            if timed_out:
                log("compile wait timed out: " + ", ".join(n for n, j in JOBS.items() if not j.get("done")))
            for job in JOBS.values():
                job["issues"] = query("GetStackIssues", job["system"])
            TICKS["phase"] = "issues"
        return
    if not all(complete(j["issues"]) for j in JOBS.values()) and not timed_out:
        return
    unreal.unregister_slate_post_tick_callback(TICKS["handle"])
    try:
        for name, job in JOBS.items():
            report_issues(name, job["issues"])
        for name, path, _ in BUILT:
            unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
            log(f"saved {path} after {TICKS['n']} ticks")
        log("done")
    finally:
        write_log()
        if quit_after:
            unreal.SystemLibrary.quit_editor()


try:
    run()
    JOBS.update({name: {"system": system} for name, _, system in BUILT})
    TICKS["handle"] = unreal.register_slate_post_tick_callback(on_tick)
except Exception as error:  # noqa: BLE001 - report and still quit a headless run
    unreal.log_error(f"make_fx_niagara: failed: {error} {traceback.format_exc()}")
    write_log()
    if quit_after:
        unreal.SystemLibrary.quit_editor()
