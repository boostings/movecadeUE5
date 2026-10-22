# Runs the game windowed with the test hooks, waits for it to quit, and lists the screenshots it took.
#   .\Tools\ue\play.ps1 -Map L_VioletLoop -Shots '6,12' -QuitAfter 20 -Extra '-KartSoloView'
#   .\Tools\ue\play.ps1 -Map L_Bowling -NoAutoDrive -Extra '-ExecCmds="Movecade.AvatarShowcase"'
# Log: Saved/Logs/KartGame.log. Screenshots: Saved/Screenshots/WindowsEditor.
param(
    [string]$Map = 'L_SunsetHarbor',
    [string]$Shots = '6,12',
    [int]$QuitAfter = 20,
    [int]$ResX = 1920,
    [int]$ResY = 1080,
    [string]$Extra = '-KartSoloView -KartSkipIntro',
    [switch]$NoAutoDrive,
    # How long to wait past -QuitAfter before killing the game (loading takes most of it).
    [int]$TimeoutSeconds = 0
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

$before = Get-KartShotNames
$gameArgs = @("`"$KartUProject`"", "/Game/Maps/$Map", '-game', '-windowed', "-ResX=$ResX", "-ResY=$ResY",
    '-nosound', "`"-KartShots=$Shots`"", "-KartQuitAfter=$QuitAfter", '-log=KartGame.log')
if (-not $NoAutoDrive) { $gameArgs += '-KartAutoDrive' }
$gameArgs += Split-KartArgs $Extra
if ($TimeoutSeconds -le 0) { $TimeoutSeconds = $QuitAfter + 120 }
Invoke-KartEditor -Arguments $gameArgs -TimeoutSeconds $TimeoutSeconds

Show-KartLog "$KartLogs\KartGame.log" 'Kart lap|Error:|Fatal|respawn' 12
Get-KartNewShots $before
