# Runs an editor Python script headless (full editor, no user input); the script quits the editor when KART_QUIT=1.
#   .\Tools\ue\run.ps1 make_track_map.py @{ KART_TRACK = 'sunset-harbor'; KART_MAP = 'L_SunsetHarbor' }
#   .\Tools\ue\run.ps1 import_ui_assets.py -NullRhi
# Log: Saved/Logs/<script>.log.
param(
    [Parameter(Mandatory = $true)][string]$Script,
    [hashtable]$Env = @{},
    [int]$TimeoutSeconds = 600,
    # No GPU: for asset-only scripts (imports), so a busy GPU shared with other editors can't drop the device.
    [switch]$NullRhi
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

# The script name goes into a Python string inside -ExecCmds: only a plain file name of a script in this folder,
# so a quote or a path can't turn it into other Python or another file.
if ($Script -notmatch '^[A-Za-z0-9_\-]+\.py$') { throw "run.ps1: -Script must be a script name in Tools\ue (got '$Script')" }
$scriptPath = (Join-Path $PSScriptRoot $Script) -replace '\\', '/'
if (-not (Test-Path $scriptPath -PathType Leaf)) { throw "run.ps1: no $Script in Tools\ue" }
if ($scriptPath.Contains("'")) { throw "run.ps1: the checkout path can't contain a quote ($scriptPath)" }
$logName = [IO.Path]::GetFileNameWithoutExtension($Script) + '.log'
foreach ($key in $Env.Keys) {
    if ($key -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') { throw "run.ps1: bad environment variable name '$key'" }
    Set-Item "env:$key" $Env[$key]
}
$env:KART_QUIT = '1'
# exec() avoids the py command's relative path lookup; the command contains no ',' or ';' (ExecCmds separators).
$exec = "py exec(open(r'$scriptPath').read())"
$editorArgs = @("`"$KartUProject`"", '-unattended', '-nosplash', '-nosound', "-log=$logName", "-ExecCmds=`"$exec`"")
if ($NullRhi) { $editorArgs += '-nullrhi' }
Invoke-KartEditor -Arguments $editorArgs -TimeoutSeconds $TimeoutSeconds

Show-KartLog "$KartLogs\$logName" 'LogPython|Traceback' 25
