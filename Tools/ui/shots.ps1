# Runs the -KartUiShots tour: the game opens the front end, steps through each screen, captures it to
# Saved/Screenshots/WindowsEditor/KartUi_<step>_<W>x<H>.png, and quits.
#   .\Tools\ui\shots.ps1                                        # every menu screen at 1920x1080
#   .\Tools\ui\shots.ps1 -Steps 'title,menu' -ResX 2560 -ResY 1440
#   .\Tools\ui\shots.ps1 -Steps 'race,pause,results' -Extra '-KartLaps=1 -KartAutoDrive -KartBots=3'
#   .\Tools\ui\shots.ps1 -Pad                                   # gamepad glyphs (-KartUiPad)
#   .\Tools\ui\shots.ps1 -Steps 'menu,race' -StatSlate          # with "stat slate" on screen (batch and layer counts)
#   .\Tools\ui\shots.ps1 -Steps menu -StatSlate -Exec 'Kart.UI.Batching 0'   # console commands at start
#   .\Tools\ui\shots.ps1 -Tag before                            # files become KartUi_<step>_<W>x<H>_before.png
# Log: Saved/Logs/KartUiShots.log.
param(
    [string]$Steps = 'title,welcome,menu,racers,rules,tracks,settings,audio,controls,accessibility,credits,quit,arcade',
    [string]$Map = 'L_SunsetHarbor',
    [int]$ResX = 1920,
    [int]$ResY = 1080,
    [string]$Extra = '',
    [switch]$Pad,
    [switch]$StatSlate,
    [string]$Tag = '',
    [string]$Exec = '',
    [int]$TimeoutSeconds = 400
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\ue\common.ps1"

$gameArgs = @("`"$KartUProject`"", "/Game/Maps/$Map", '-game', '-windowed', "-ResX=$ResX", "-ResY=$ResY", '-ForceRes',
    "`"-KartUiShots=$Steps`"", '-log=KartUiShots.log')
if ($Pad) { $gameArgs += '-KartUiPad' }
$cmds = @(); if ($StatSlate) { $cmds += 'stat slate' }; if ($Exec) { $cmds += $Exec }
if ($cmds.Count -gt 0) { $gameArgs += ('"-ExecCmds=' + ($cmds -join ',') + '"') }
$gameArgs += Split-KartArgs $Extra
$started = Get-Date
Invoke-KartEditor -Arguments $gameArgs -TimeoutSeconds $TimeoutSeconds

Show-KartLog "$KartLogs\KartUiShots.log" 'KartUiShots|Error:|Fatal|Ensure' 30
if ($Tag) {
    Get-ChildItem "$KartShotDir\KartUi_*.png" | Where-Object { $_.LastWriteTime -ge $started -and $_.BaseName -notmatch '_[a-z]+$' } |
        ForEach-Object { Move-Item -Force $_.FullName (Join-Path $_.DirectoryName ($_.BaseName + "_$Tag.png")) }
}
