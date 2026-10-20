# Rebuilds the detailed vehicles end to end (from the project root, with no editor open on this project):
#   .\Tools\art\detail\build.ps1               textures, Blender detail pass, UE import, Nanite usage flags
#   .\Tools\art\detail\build.ps1 -SkipUnreal   only the textures and the GLBs (Tools/art/.cache/detail)
# Needs Tools/art/.cache/out from `node Tools/art/decode.mjs` and Blender 5.0.
param(
    [switch]$SkipUnreal,
    [string]$Blender = 'C:\Program Files\Blender Foundation\Blender 5.0\blender.exe'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..\..\..").Path
if (-not (Test-Path "$root\Tools\art\.cache\out\vehicles\kart.glb")) {
    throw 'Tools/art/.cache/out is missing: run `npm ci` and `node decode.mjs` in Tools/art first'
}
python "$root\Tools\art\detail\make_textures.py"
& $Blender --background --factory-startup --python "$root\Tools\art\detail\build.py" 2>&1 |
    Select-String 'DETAIL|Error|Traceback' | ForEach-Object { $_.Line }
if ($SkipUnreal) { return }
& "$root\Tools\ue\run.ps1" import_karts.py @{ KART_ART = 'kartmaterials' } -NullRhi
& "$root\Tools\ue\run.ps1" import_karts.py @{ KART_ART = 'vehicles' } -NullRhi
& "$root\Tools\ue\run.ps1" fix_material_usage.py -NullRhi
