# Runs an editor Python script headless (full editor, no user input), then the script quits the editor.
#   .\Tools\ue\run.ps1 make_track_map.py @{ KART_TRACK = 'sunset-harbor'; KART_MAP = 'L_SunsetHarbor' }
param(
    [Parameter(Mandatory = $true)][string]$Script,
    [hashtable]$Env = @{},
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$scriptPath = (Join-Path $PSScriptRoot $Script) -replace '\\', '/'
$logName = [IO.Path]::GetFileNameWithoutExtension($Script) + '.log'

foreach ($key in $Env.Keys) { Set-Item "env:$key" $Env[$key] }
$env:KART_QUIT = '1'
# exec() avoids the py command's relative path lookup; the command contains no ',' or ';' (ExecCmds separators).
$exec = "py exec(open(r'$scriptPath').read())"
$proc = Start-Process -FilePath $editor -PassThru -WorkingDirectory $project -ArgumentList @(
    "`"$project\MyProject.uproject`"", '-unattended', '-nosplash', '-nosound', "-log=$logName", "-ExecCmds=`"$exec`"")
if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) { Stop-Process -Id $proc.Id -Force; Write-Output 'timed out' }
Select-String -Path "$project\Saved\Logs\$logName" -Pattern 'LogPython|Traceback' | Select-Object -Last 25 | ForEach-Object { $_.Line }
