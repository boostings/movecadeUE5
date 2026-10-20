# Runs the -KartUiShots tour: the game opens the front end, steps through each screen, captures it to
# Saved/Screenshots/WindowsEditor/KartUi_<step>_<W>x<H>.png, and quits.
#   .\Tools\ui\shots.ps1                                        # every menu screen at 1920x1080
#   .\Tools\ui\shots.ps1 -Steps title,menu -ResX 2560 -ResY 1440
#   .\Tools\ui\shots.ps1 -Steps race,pause,results -Extra '-KartLaps=1 -KartAutoDrive -KartBots=3'
param(
    [string]$Steps = 'title,menu,racers,rules,tracks,settings,audio,controls,credits,quit',
    [string]$Map = 'L_SunsetHarbor',
    [int]$ResX = 1920,
    [int]$ResY = 1080,
    [string]$Extra = '',
    [int]$TimeoutSeconds = 400
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$gameArgs = @("`"$project\MyProject.uproject`"", "/Game/Maps/$Map", '-game', '-windowed', "-ResX=$ResX", "-ResY=$ResY", '-ForceRes',
    "`"-KartUiShots=$Steps`"", '-log=KartUiShots.log')
$gameArgs += $Extra.Split(' ', [StringSplitOptions]::RemoveEmptyEntries)
$proc = Start-Process -FilePath $editor -ArgumentList $gameArgs -PassThru -WorkingDirectory $project
if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) { Stop-Process -Id $proc.Id -Force; Write-Output 'timed out' }
Select-String -Path "$project\Saved\Logs\KartUiShots.log" -Pattern 'KartUiShots|Error:|Fatal|Ensure' | Select-Object -Last 30 | ForEach-Object { $_.Line }
