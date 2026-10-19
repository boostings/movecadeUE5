# Runs the Movecade room server (PartyKit) locally for the UE host. Local only: this never deploys.
#
#   .\Tools\net-sim\run-party.ps1            # clone + npm ci on first use, then serve ws://localhost:1999
#
# The clone lives in Tools/.cache/movecade (git-ignored); the movecade-v2 checkout itself is never touched.
# EXTRA_ALLOWED_ORIGINS admits the Origin that UE's libwebsockets sends for ws://localhost:1999
# ("http://localhost", or "http://127.0.0.1" with -RoomServer=127.0.0.1:1999). Without it the server
# refuses the UE host with UNAUTHORIZED_ORIGIN / close 4003 (spec 04 §11 F1).
param(
    [string]$Source = 'C:\Users\Jimmy\Documents\Projects\movecade\movecade-v2',
    [string]$ExtraOrigins = 'http://localhost,http://127.0.0.1',
    [int]$Port = 1999
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$clone = Join-Path $root 'Tools\.cache\movecade'
if (-not (Test-Path $clone)) {
    git clone --local $Source $clone
}
if (-not (Test-Path (Join-Path $clone 'node_modules\.bin\partykit.cmd'))) {
    Push-Location $clone
    # --ignore-scripts: an optional native dependency (cpu-features) fails to build without a C++ toolchain on PATH.
    $env:PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD = '1'
    npm ci --no-audit --no-fund --ignore-scripts
    Pop-Location
}
Push-Location $clone
try {
    # Same as `npm run dev:party` (package.json), plus the UE host's Origin.
    npx partykit dev --config partykit.json --port $Port `
        --var SCORE_SIGNING_SECRET=local-dev-not-a-secret `
        --var "EXTRA_ALLOWED_ORIGINS=$ExtraOrigins"
}
finally {
    Pop-Location
}
