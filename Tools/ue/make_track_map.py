"""Creates (or rebuilds) a playable level for one exported track.

Run inside the editor:
    py "<project>/Tools/ue/make_track_map.py" <track-id> <MapName> [--quit]
or headless from a shell, with the arguments in KART_TRACK / KART_MAP / KART_QUIT=1 (see Tools/ue/run.ps1).

The level holds one ATrackBuilder (which builds the graybox and lighting from Data/Tracks/<id>.track.json)
and uses AKartGameMode, so Play drops a kart on the grid.
"""
import os
import sys

import unreal

args = [a for a in sys.argv[1:] if not a.startswith("--")]
track_id = args[0] if args else os.environ.get("KART_TRACK", "sunset-harbor")
map_name = args[1] if len(args) > 1 else os.environ.get("KART_MAP", "L_SunsetHarbor")
quit_after = "--quit" in sys.argv or os.environ.get("KART_QUIT") == "1"
map_path = f"/Game/Maps/{map_name}"


def make_map():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    builder_class = unreal.load_class(None, "/Script/MovecadeKart.TrackBuilder")
    mode_class = unreal.load_class(None, "/Script/MovecadeKart.KartGameMode")

    # Rebuild in place: deleting a map and creating it again at the same path fails while it is loaded.
    if unreal.EditorAssetLibrary.does_asset_exist(map_path):
        if not levels.load_level(map_path):
            raise RuntimeError(f"Could not load {map_path}")
        for actor in actors.get_all_level_actors():
            if actor.get_class() == builder_class:
                actors.destroy_actor(actor)
    elif not levels.new_level(map_path):
        raise RuntimeError(f"Could not create {map_path}")
    builder = actors.spawn_actor_from_class(builder_class, unreal.Vector(0, 0, 0))
    # Setting the property in the editor re-runs the actor's construction, which builds the track.
    builder.set_editor_property("track_id", track_id)
    builder.set_actor_label(f"Track_{track_id}")

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", mode_class)

    levels.save_current_level()
    unreal.log(f"make_track_map: saved {map_path} for {track_id}")


try:
    make_map()
except Exception as error:  # noqa: BLE001 - report and still quit a headless run
    unreal.log_error(f"make_track_map: failed: {error}")
    raise
finally:
    if quit_after:
        unreal.SystemLibrary.quit_editor()
