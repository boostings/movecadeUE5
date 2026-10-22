# End-to-end check of the UE room host with no phone: runs the game (-game, windowed) as the host with
# the lobby (-KartRoom), has Tools/net-sim/phone-sim.mjs join as a phone and race it against bots
# through the real race layer, then prints the evidence.
#
#   .\Tools\net-sim\run-party.ps1          # in another terminal first (local PartyKit on :1999)
#   .\Tools\net-sim\e2e.ps1
#
# Evidence: Saved/Logs/NetE2E.log (LogRoomHost and kart lap lines), Saved/Logs/phone-sim.log,
# Saved/Screenshots/WindowsEditor/RoomHost_N.png (UI screenshots at -RoomShots seconds).
param(
    [string]$Map = '/Game/Maps/L_SunsetHarbor',
    [int]$QuitAfter = 110,
    [string]$Shots = '10,30,50,100',
    [int]$AutoStartDelay = 12,
    [int]$SimSeconds = 105,
    [int]$Bots = 2,
    [int]$Laps = 1,
    # kart, bowling, darts, samurai or stacker: the room plays bowling on L_Bowling and the simulated phone bowls its turns.
    [string]$RoomGame = 'kart'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$log = Join-Path $root 'Saved\Logs\NetE2E.log'
$simLog = Join-Path $root 'Saved\Logs\phone-sim.log'
Remove-Item $log, $simLog -ErrorAction SilentlyContinue
Remove-Item (Join-Path $root 'Saved\Screenshots\WindowsEditor\RoomHost_*.png') -ErrorAction SilentlyContinue

$game = Start-Process -PassThru -FilePath $editor -ArgumentList @(
    "`"$root\MyProject.uproject`"", $Map, '-game', '-windowed', '-ResX=1280', '-ResY=720', '-nosound',
    '-KartRoom', '-RoomLocal', "-KartBots=$Bots", "-KartLaps=$Laps", '-KartSkipIntro', '-RoomNew', '-RoomMap=sunset-harbor', "-RoomGame=$RoomGame", "-RoomAutoStart=$AutoStartDelay", "-RoomShots=$Shots",
    "-KartQuitAfter=$QuitAfter", '-ExecCmds=DisableAllScreenMessages', '-log=NetE2E.log')
$sim = Start-Process -PassThru -NoNewWindow -FilePath 'node' -ArgumentList @(
    "`"$root\Tools\net-sim\phone-sim.mjs`"", '--from-log', "`"$log`"", '--name', 'SimPhone', '--paint', 'cyan',
    '--seconds', "$SimSeconds", '--game', $RoomGame) -RedirectStandardOutput $simLog -RedirectStandardError "$simLog.err"
$null = $game.WaitForExit(($QuitAfter + 120) * 1000)
$null = $sim.WaitForExit(30000)
if (-not $game.HasExited) { Stop-Process -Id $game.Id -Force }
if (-not $sim.HasExited) { Stop-Process -Id $sim.Id -Force }

Write-Output '--- host (LogRoomHost / laps) ---'
Select-String -Path $log -Pattern 'LogRoomHost|Kart lap|LogKartGameMode|LogBowling|Bowling:|LogDarts|Darts:|LogSamurai|Samurai:|LogStacker|Stacker:|LogDerby|Derby:' | ForEach-Object { $_.Line }
Write-Output '--- phone simulator ---'
Get-Content $simLog
