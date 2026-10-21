"""Creates (or rebuilds) a level for one of the party games: its venue actor and its game mode.

    .\\Tools\\ue\\run.ps1 make_game_map.py @{ KART_GAME = 'bowling' }

Games (KART_GAME): bowling -> L_Bowling (ABowlingAlley, ABowlingGameMode); darts -> L_Darts (ADartsPub, ADartsGameMode). The venue builds itself in code
(OnConstruction), like ATrackBuilder, so the level only holds the actor and the mode override.
"""
import os
import stat

import unreal

GAMES = {
    # id: (map, venue class, game mode class)
    "bowling": ("L_Bowling", "/Script/MovecadeKart.BowlingAlley", "/Script/MovecadeKart.BowlingGameMode"),
    "darts": ("L_Darts", "/Script/MovecadeKart.DartsPub", "/Script/MovecadeKart.DartsGameMode"),
}
game = os.environ.get("KART_GAME", "bowling")
quit_after = os.environ.get("KART_QUIT") == "1"


def make_map():
    map_name, venue_path, mode_path = GAMES[game]
    map_path = f"/Game/Maps/{map_name}"
    content = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    file = os.path.join(content, "Maps", f"{map_name}.umap")
    if os.path.exists(file):
        os.chmod(file, stat.S_IREAD | stat.S_IWRITE)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    venue_class = unreal.load_class(None, venue_path)
    mode_class = unreal.load_class(None, mode_path)
    if unreal.EditorAssetLibrary.does_asset_exist(map_path):
        if not levels.load_level(map_path):
            raise RuntimeError(f"Could not load {map_path}")
        for actor in actors.get_all_level_actors():
            if actor.get_class() == venue_class:
                actors.destroy_actor(actor)
    elif not levels.new_level(map_path):
        raise RuntimeError(f"Could not create {map_path}")
    venue = actors.spawn_actor_from_class(venue_class, unreal.Vector(0, 0, 0))
    venue.set_actor_label(f"Venue_{game}")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", mode_class)
    levels.save_current_level()
    unreal.log(f"make_game_map: saved {map_path}")


try:
    make_map()
except Exception as error:  # noqa: BLE001 - report and still quit a headless run
    unreal.log_error(f"make_game_map: failed: {error}")
    raise
finally:
    if quit_after:
        unreal.SystemLibrary.quit_editor()
