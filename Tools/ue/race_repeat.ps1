# Races the same seeded, self-driving grid twice and checks the two runs step for step: the -KartStepTrace
# fingerprints (every kart's sim state and inputs once a second of race time) and the lap times must be identical.
#   .\Tools\ue\race_repeat.ps1                      # Sunset Harbor, 3 bots, 45 s
#   .\Tools\ue\race_repeat.ps1 -Map L_VioletLoop -Seconds 70
param(
    [string]$Map = 'L_SunsetHarbor',
    [int]$Seconds = 45,
    [string]$Extra = '-KartSoloView -KartSkipIntro -KartBots=3'
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$log = "$project\Saved\Logs\KartGame.log"
$runs = @()
foreach ($run in 1..2) {
    & "$PSScriptRoot\play.ps1" -Map $Map -Shots '' -QuitAfter $Seconds -Extra "$Extra -KartStepTrace" | Out-Null
    $lines = Select-String -Path $log -Pattern 'KartStepTrace step|Kart lap' | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
    $runs += , @($lines)
    Write-Output ("run {0}: {1} trace lines" -f $run, $lines.Count)
}
$a = $runs[0]; $b = $runs[1]
$n = [Math]::Min($a.Count, $b.Count)
for ($i = 0; $i -lt $n; ++$i) {
    if ($a[$i] -ne $b[$i]) {
        Write-Output "DIVERGED at line $i"
        Write-Output "  run 1: $($a[$i])"
        Write-Output "  run 2: $($b[$i])"
        exit 1
    }
}
if ($n -lt 10) { Write-Output "Too few trace lines ($n) to compare."; exit 1 }
Write-Output "IDENTICAL over $n lines"
