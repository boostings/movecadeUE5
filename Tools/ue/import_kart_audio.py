"""Imports the kart audio set (Tools/audio/kart/*.wav, copied from movecade-v2 app/public/audio) as SoundWaves.

Run headless from the project root:
    .\\Tools\\ue\\run.ps1 import_kart_audio.py

Assets land in /Game/Audio/Kart, named after the file with '-' -> '_' (kart-engine-on-1000.wav ->
kart_engine_on_1000). Every cue the web build loops (kartCues.ts `loop: true`) is flagged looping and
set to play when silent, so the engine layers keep their phase while their crossfade gain is 0, exactly
like the always-running Web Audio sources. UKartAudioComponent's cue table refers to these names.
"""
import glob
import os

import unreal

DEST = "/Game/Audio/Kart"
# kartCues.ts: the cues with `loop: true`.
LOOPING = {
    "kart_engine_on_1000", "kart_engine_on_2500", "kart_engine_on_4000", "kart_engine_on_5500",
    "kart_engine_on_7000", "kart_engine_off_2500", "kart_engine_off_6500",
    "kart_tire_skid", "kart_offroad", "kart_wind", "kart_boost_loop", "kart_item_roulette",
    "kart_invincible_loop", "kart_rocket_loop",
}


def main():
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    source_dir = os.path.join(project, "Tools", "audio", "kart")
    files = sorted(glob.glob(os.path.join(source_dir, "*.wav")))
    if not files:
        raise RuntimeError(f"No wavs in {source_dir}")

    tasks = []
    for path in files:
        name = os.path.splitext(os.path.basename(path))[0].replace("-", "_")
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", path)
        task.set_editor_property("destination_path", DEST)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    imported = 0
    for path in files:
        name = os.path.splitext(os.path.basename(path))[0].replace("-", "_")
        asset_path = f"{DEST}/{name}"
        wave = unreal.EditorAssetLibrary.load_asset(asset_path)
        if not isinstance(wave, unreal.SoundWave):
            unreal.log_error(f"KartAudioImport: {asset_path} did not import")
            continue
        loop = name in LOOPING
        wave.set_editor_property("looping", loop)
        wave.set_editor_property(
            "virtualization_mode",
            unreal.VirtualizationMode.PLAY_WHEN_SILENT if loop else unreal.VirtualizationMode.RESTART)
        # The web plays the 48 kHz files as they are; keep the platform from resampling them to 32 kHz.
        wave.set_editor_property("sample_rate_quality", unreal.SoundwaveSampleRateSettings.MAX)
        unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
        imported += 1
        unreal.log(f"KartAudioImport: {name} {wave.get_editor_property('duration'):.3f}s "
                   f"ch={wave.get_editor_property('num_channels')} loop={loop}")
    unreal.log(f"KartAudioImport: {imported}/{len(files)} sound waves in {DEST}")


try:
    main()
finally:
    if os.environ.get("KART_QUIT") == "1":
        unreal.SystemLibrary.quit_editor()
