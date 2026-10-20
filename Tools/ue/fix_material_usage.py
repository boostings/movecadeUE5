# Marks every base material used under /Game/Karts as "used with Nanite" and saves it.
# The kart meshes are Nanite; the editor sets the usage flag on the fly, but a cooked build can't, so without
# the saved flag the packaged game draws the default material instead.
#   .\Tools\ue\run.ps1 fix_material_usage.py
import os
import unreal

ROOTS = ['/Game/Karts']
bases = {}
for root in ROOTS:
    for asset_path in unreal.EditorAssetLibrary.list_assets(root, recursive=True, include_folder=False):
        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        material = None
        if isinstance(asset, unreal.MaterialInstance):
            material = asset.get_base_material()
        elif isinstance(asset, unreal.Material):
            material = asset
        if material:
            bases[material.get_path_name()] = material

changed = []
for path, material in sorted(bases.items()):
    unreal.MaterialEditingLibrary.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE)
    unreal.MaterialEditingLibrary.recompile_material(material)
    # Lockable LFS files check out read-only; the editor's save then fails silently.
    package = material.get_path_name().split('.')[0]
    file = os.path.join(unreal.Paths.project_content_dir(), package[len('/Game/'):] + '.uasset')
    if os.path.exists(file):
        os.chmod(file, 0o666)
    if unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        changed.append(path)
unreal.log('fix_material_usage: Nanite usage saved on {} of {}: {}'.format(len(changed), len(bases), ', '.join(changed)))

if os.environ.get('KART_QUIT') == '1':
    unreal.SystemLibrary.quit_editor()
