"""Imports the front-end/HUD assets the UI code loads by path (Source/MovecadeKart/.../UI).

    python Tools/ui/make_ui_sounds.py         # (re)generate the sounds first
    .\\Tools\\ui\\fetch_fonts.ps1               # (re)download the fonts first
    .\\Tools\\ue\\run.ps1 import_ui_assets.py -NullRhi

- /Game/UI/Fonts/<Family>-<Weight>   FontFace assets (inline data, so cooking needs nothing else) from
  Tools/ui/fonts/*.ttf (SIL OFL 1.1, see Docs/Port/ASSETS.md). UKartUIStyle builds its composite fonts from them.
- /Game/UI/Audio/SC_Music, SC_UI      sound classes for the music and menu sounds; the race keeps the
  default sound class, which the SFX volume drives.
- /Game/UI/Audio/SM_KartVolumes       the sound mix the volume settings override at runtime.
- /Game/UI/Sounds/<name>              SoundWaves from Tools/ui/sounds/*.wav (music_* loops on SC_Music).
"""
import glob
import os

import unreal

quit_after = os.environ.get("KART_QUIT") == "1"
project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def import_files(files, dest):
    tasks = []
    for path in files:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", path)
        task.set_editor_property("destination_path", dest)
        task.set_editor_property("destination_name", os.path.splitext(os.path.basename(path))[0])
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    tools.import_asset_tasks(tasks)


def make_asset(path, cls, factory):
    if assets.does_asset_exist(path):
        return assets.load_asset(path)
    folder, name = path.rsplit("/", 1)
    return tools.create_asset(name, folder, cls, factory)


def fonts():
    files = sorted(glob.glob(os.path.join(project, "Tools", "ui", "fonts", "*.ttf")))
    import_files(files, "/Game/UI/Fonts")
    for path in files:
        name = os.path.splitext(os.path.basename(path))[0]
        face = assets.load_asset(f"/Game/UI/Fonts/{name}")
        if not isinstance(face, unreal.FontFace):
            unreal.log_error(f"import_ui_assets: {name} did not import as a FontFace ({face})")
            continue
        face.set_editor_property("loading_policy", unreal.FontLoadingPolicy.INLINE)
        face.set_editor_property("hinting", unreal.FontHinting.DEFAULT)
        assets.save_loaded_asset(face, only_if_is_dirty=False)
        unreal.log(f"import_ui_assets: font {name}")


def audio():
    music = make_asset("/Game/UI/Audio/SC_Music", unreal.SoundClass, unreal.SoundClassFactory())
    ui = make_asset("/Game/UI/Audio/SC_UI", unreal.SoundClass, unreal.SoundClassFactory())
    mix = make_asset("/Game/UI/Audio/SM_KartVolumes", unreal.SoundMix, unreal.SoundMixFactory())
    for obj in (music, ui, mix):
        assets.save_loaded_asset(obj, only_if_is_dirty=False)

    files = sorted(glob.glob(os.path.join(project, "Tools", "ui", "sounds", "*.wav")))
    import_files(files, "/Game/UI/Sounds")
    for path in files:
        name = os.path.splitext(os.path.basename(path))[0]
        wave = assets.load_asset(f"/Game/UI/Sounds/{name}")
        if not isinstance(wave, unreal.SoundWave):
            unreal.log_error(f"import_ui_assets: {name} did not import")
            continue
        is_music = name.startswith("music_")
        wave.set_editor_property("sound_class_object", music if is_music else ui)
        wave.set_editor_property("looping", is_music)
        wave.set_editor_property("sample_rate_quality", unreal.SoundwaveSampleRateSettings.MAX)
        if is_music:
            wave.set_editor_property("loading_behavior", unreal.SoundWaveLoadingBehavior.RETAIN_ON_LOAD)
        assets.save_loaded_asset(wave, only_if_is_dirty=False)
        unreal.log(f"import_ui_assets: sound {name}")


try:
    fonts()
    audio()
except Exception as error:  # noqa: BLE001 - report and still quit a headless run
    unreal.log_error(f"import_ui_assets: failed: {error}")
    raise
finally:
    if quit_after:
        unreal.SystemLibrary.quit_editor()
