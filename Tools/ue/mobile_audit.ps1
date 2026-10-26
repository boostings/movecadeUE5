# Plays every level on the desktop renderer and on the mobile renderer (ES3.1 preview, emulating a phone device
# profile) and collects a screenshot pair plus the perf summary of each run into Saved/MobileAudit/<Label>/.
#   .\Tools\ue\mobile_audit.ps1 -Label before
#   .\Tools\ue\mobile_audit.ps1 -Label after -Maps L_Sumo,L_Darts -Modes mobile
#   .\Tools\ue\mobile_audit.ps1 -Label warm -Modes mobile -NoShots   # compile the ES3.1 shaders first
#   .\Tools\ue\mobile_audit.ps1 -Label shots -FixedStep               # the same game moment in both modes
#   .\Tools\ue\mobile_audit.ps1 -Label prof -Maps L_Samurai -Modes mobile -ProfileAt 15
#
# Mobile mode runs -featureleveles31 -DP=<Profile> (default IOS_High, the iPhone 13 bucket in
# Config/DefaultDeviceProfiles.ini) at the phone's render resolution. The numbers are a PC GPU's: use them to compare
# levels and before/after, not as phone frame times (the Mac session measures those on device).
# Output: <map>_<mode>.png per run, <map>_<mode>.log (the LogKartRender lines), and summary.md / summary.csv.
param(
    [string]$Label = 'audit',
    [string[]]$Maps = @('L_SunsetHarbor', 'L_VioletLoop', 'L_NeonCity', 'L_Bowling', 'L_Darts', 'L_Samurai', 'L_Stacker', 'L_Sumo', 'L_Derby', 'L_Fishing'),
    [string[]]$Modes = @('pc', 'mobile'),
    [string]$Profile = 'IOS_High',
    # When the screenshot is taken and when the run quits (seconds after the level starts).
    [int]$ShotAt = 18,
    [int]$QuitAfter = 24,
    [switch]$NoShots,
    # Fixed 60 Hz game step (-benchmark): both modes reach the same game moment at ShotAt, for side-by-side shots.
    [switch]$FixedStep,
    # Log a 120-frame stat tree average (stat dumpave) this many seconds in (0 = off); lands in <map>_<mode>.log.
    [int]$ProfileAt = 0,
    # Extra cvars for every run (they outrank the device profile), e.g. 'kart.MobileLighting=0,kart.FX.SpawnScale=1'.
    [string]$DpCVars = ''
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

# Per level: the extra arguments that put the scene in play with bots (kart tracks drive themselves).
$MapArgs = @{
    'L_SunsetHarbor' = '-KartSoloView -KartSkipIntro -KartBots=7'
    'L_VioletLoop'   = '-KartSoloView -KartSkipIntro -KartBots=7'
    'L_NeonCity'     = '-KartSoloView -KartSkipIntro -KartBots=7'
    'L_Bowling'      = '-BowlPlayers=0 -BowlBots=4'
    'L_Darts'        = '-DartsPlayers=0 -DartsBots=4'
    'L_Samurai'      = '-SamuraiPlayers=0 -SamuraiBots=4'
    'L_Stacker'      = '-StackerPlayers=0 -StackerBots=4'
    'L_Sumo'         = '-SumoPlayers=0 -SumoBots=4'
    'L_Derby'        = '-DerbyPlayers=0 -DerbyBots=4'
    'L_Fishing'      = '-FishingPlayers=0 -FishingBots=4'
}

$out = "$KartProject\Saved\MobileAudit\$Label"
New-Item -ItemType Directory -Force $out | Out-Null
$rows = @()

function Get-LastMatch([string]$Log, [string]$Pattern) {
    $m = Select-String -Path $Log -Pattern $Pattern | Select-Object -Last 1
    if ($m) { return $m.Matches[0] }
    return $null
}

foreach ($map in $Maps) {
    foreach ($mode in $Modes) {
        # Engine.ini [ConsoleVariables] overrides, not -ExecCmds or -dpcvars: they reach the game module's cvars too
        # (registered after startup, so the other two drop them), and they outrank the device profile.
        $cvars = @('kart.Render.LogInterval=5')
        if ($ProfileAt -gt 0) { $cvars += "kart.Render.ProfileAfter=$ProfileAt" }
        if ($DpCVars) { $cvars += $DpCVars.Split(',') }
        $extra = "$($MapArgs[$map]) -ini:Engine:" + (($cvars | ForEach-Object { "[ConsoleVariables]:$_" }) -join ',')
        if ($FixedStep) { $extra += ' -benchmark -fps=60 -deterministic' }
        $resX = 1920; $resY = 1080
        if ($mode -eq 'mobile') {
            # iPhone 13: 2532x1170 panel, rendered at r.MobileContentScaleFactor 2 = 1688x780 (IOS_High).
            $extra += " -featureleveles31 -DP=$Profile"
            $resX = 1688; $resY = 780
        }
        $kart = $map -in @('L_SunsetHarbor', 'L_VioletLoop', 'L_NeonCity')
        $shots = if ($NoShots) { '' } else { "$ShotAt" }
        Write-Output "== $map ($mode)"
        $before = Get-KartShotNames
        $playArgs = @{ Map = $map; Shots = $shots; QuitAfter = $QuitAfter; ResX = $resX; ResY = $resY; Extra = $extra; TimeoutSeconds = 1500 }
        if (-not $kart) { $playArgs.NoAutoDrive = $true }
        & "$PSScriptRoot\play.ps1" @playArgs | Out-Null

        $log = "$KartLogs\KartGame.log"
        Select-String -Path $log -Pattern 'LogKartRender|LogKartMobileLighting|LogStats|Failed to compile|LogShaderCompilers: Warning|\[SM\]' |
            ForEach-Object Line | Set-Content -Encoding utf8 "$out\${map}_$mode.log"
        $shot = Get-KartNewShots $before | Select-Object -Last 1
        if ($shot) { Move-Item -Force $shot "$out\${map}_$mode.png" }

        $perf = Get-LastMatch $log 'fps \| frame avg ([\d.]+) p95 ([\d.]+) ms \| GPU avg ([\d.]+) p95 ([\d.]+) ms \| game ([\d.]+) \| render ([\d.]+)'
        $content = Get-LastMatch $log 'draws (\d+) \| prims (\d+)k \| process (\d+) MB, peak (\d+) MB \| textures (\d+) MB'
        $load = Get-LastMatch $log "Map \S*$map loaded in ([\d.]+) s"
        $failed = @(Select-String -Path $log -Pattern 'Failed to compile Material|Material .* failed to compile').Count
        $g = { param($m, $i) if ($m) { $m.Groups[$i].Value } else { '' } }
        $rows += [pscustomobject]@{
            Map = $map; Mode = $mode
            Draws = & $g $content 1; PrimsK = & $g $content 2
            GpuMs = & $g $perf 3; GpuP95 = & $g $perf 4; GameMs = & $g $perf 5; RenderMs = & $g $perf 6; FrameMs = & $g $perf 1
            ProcessMB = & $g $content 3; PeakMB = & $g $content 4; TexturesMB = & $g $content 5
            LoadS = & $g $load 1; ShaderErrors = $failed
        }
    }
}

$rows | Export-Csv -NoTypeInformation -Encoding utf8 "$out\summary.csv"
$md = @('| Map | Mode | Draws | Tris (k) | GPU ms (p95) | Game ms | Render ms | Frame ms | Textures MB | Process MB (peak) | Load s | Shader errors |',
        '|---|---|---|---|---|---|---|---|---|---|---|---|')
foreach ($r in $rows) {
    $md += "| $($r.Map) | $($r.Mode) | $($r.Draws) | $($r.PrimsK) | $($r.GpuMs) ($($r.GpuP95)) | $($r.GameMs) | $($r.RenderMs) | $($r.FrameMs) | $($r.TexturesMB) | $($r.ProcessMB) ($($r.PeakMB)) | $($r.LoadS) | $($r.ShaderErrors) |"
}
$md | Set-Content -Encoding utf8 "$out\summary.md"
$md
