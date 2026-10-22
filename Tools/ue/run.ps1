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

$scriptPath = (Join-Path $PSScriptRoot $Script) -replace '\\', '/'
$logName = [IO.Path]::GetFileNameWithoutExtension($Script) + '.log'
foreach ($key in $Env.Keys) { Set-Item "env:$key" $Env[$key] }
$env:KART_QUIT = '1'
# exec() avoids the py command's relative path lookup; the command contains no ',' or ';' (ExecCmds separators).
$exec = "py exec(open(r'$scriptPath').read())"
$editorArgs = @("`"$KartUProject`"", '-unattended', '-nosplash', '-nosound', "-log=$logName", "-ExecCmds=`"$exec`"")
if ($NullRhi) { $editorArgs += '-nullrhi' }
Invoke-KartEditor -Arguments $editorArgs -TimeoutSeconds $TimeoutSeconds

Show-KartLog "$KartLogs\$logName" 'LogPython|Traceback' 25
