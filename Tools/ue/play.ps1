# Runs the game windowed with the test hooks, waits for it to quit, and lists the screenshots it took.
#   .\Tools\ue\play.ps1 -Map L_VioletLoop -Shots 6,12 -QuitAfter 20 -Extra '-KartSoloView'
param(
    [string]$Map = 'L_SunsetHarbor',
    [string]$Shots = '6,12',
    [int]$QuitAfter = 20,
    [int]$ResX = 1920,
    [int]$ResY = 1080,
    [string]$Extra = '-KartSoloView -KartSkipIntro',
    [switch]$NoAutoDrive
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$shotDir = "$project\Saved\Screenshots\WindowsEditor"
$before = if (Test-Path $shotDir) { @(Get-ChildItem $shotDir -Filter *.png | ForEach-Object Name) } else { @() }

$gameArgs = @("`"$project\MyProject.uproject`"", "/Game/Maps/$Map", '-game', '-windowed', "-ResX=$ResX", "-ResY=$ResY",
    '-nosound', "`"-KartShots=$Shots`"", "-KartQuitAfter=$QuitAfter", '-log=KartGame.log')
if (-not $NoAutoDrive) { $gameArgs += '-KartAutoDrive' }
$gameArgs += $Extra.Split(' ', [StringSplitOptions]::RemoveEmptyEntries)
$proc = Start-Process -FilePath $editor -ArgumentList $gameArgs -PassThru -WorkingDirectory $project
if (-not $proc.WaitForExit(($QuitAfter + 120) * 1000)) { Stop-Process -Id $proc.Id -Force; Write-Output 'timed out' }

Select-String -Path "$project\Saved\Logs\KartGame.log" -Pattern 'Kart lap|Error:|Fatal|respawn' | Select-Object -Last 12 | ForEach-Object { $_.Line }
Get-ChildItem $shotDir -Filter *.png | Where-Object { $before -notcontains $_.Name } | ForEach-Object { $_.FullName }
