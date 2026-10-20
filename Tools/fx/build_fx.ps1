# Rebuilds all the race VFX content: textures, materials, then the Niagara systems in a fresh editor session.
#   .\Tools\fx\build_fx.ps1            (from the project root, with no editor open on this project)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
python "$root\Tools\fx\make_fx_textures.py"
& "$root\Tools\ue\run.ps1" make_fx_materials.py
# The Niagara systems must be created at paths that are empty when the editor starts (see make_fx_niagara.py).
Get-ChildItem "$root\Content\FX\Niagara" -Filter *.uasset -ErrorAction SilentlyContinue | Remove-Item
& "$root\Tools\ue\run.ps1" make_fx_niagara.py
