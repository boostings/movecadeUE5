# Runs the game in a phone-shaped window (-MobilePreview) with the test hooks, and lists the screenshots it took.
#   .\Tools\ue\preview.ps1 -Map L_Darts -Orientation portrait -Shots '8' -QuitAfter 12 -Extra '-DartsPlayers=0 -DartsBots=2'
#   .\Tools\ue\preview.ps1 -Map L_Stacker -Orientation landscape -Device pixel8
#   .\Tools\ue\preview.ps1 -Map L_Samurai          # auto: the level's own orientation from the catalog
# The window is the phone's aspect scaled to fit the desktop; the safe area (notch, home indicator) is simulated and
# drawn as a translucent mask (-MobilePreviewNoMask hides it). Not available in Shipping builds.
# Log: Saved/Logs/KartGame.log (LogMovecadeOrientation). Screenshots: Saved/Screenshots/WindowsEditor.
param(
    [string]$Map = 'L_SunsetHarbor',
    [ValidateSet('auto', 'portrait', 'landscape')][string]$Orientation = 'auto',
    [ValidateSet('iphone15pro', 'iphonese', 'pixel8')][string]$Device = 'iphone15pro',
    [string]$Shots = '6',
    [int]$QuitAfter = 12,
    [string]$Extra = '',
    [switch]$AutoDrive,
    [int]$TimeoutSeconds = 0
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

if ($Map -notmatch '^[A-Za-z0-9_]+$') { throw "preview.ps1: -Map must be a level name (got '$Map')" }
$before = Get-KartShotNames
# No -ResX/-ResY: the game sizes its window to the phone before creating it.
$gameArgs = @("`"$KartUProject`"", "/Game/Maps/$Map", '-game', '-windowed', '-nosound', "-MobilePreview=$Orientation,$Device",
    "`"-KartShots=$Shots`"", "-KartQuitAfter=$QuitAfter", '-log=KartGame.log')
if ($AutoDrive) { $gameArgs += '-KartAutoDrive' }
$gameArgs += Split-KartArgs $Extra
if ($TimeoutSeconds -le 0) { $TimeoutSeconds = $QuitAfter + 120 }
Invoke-KartEditor -Arguments $gameArgs -TimeoutSeconds $TimeoutSeconds

Show-KartLog "$KartLogs\KartGame.log" 'LogMovecadeOrientation|Error:|Fatal' 12
Get-KartNewShots $before
