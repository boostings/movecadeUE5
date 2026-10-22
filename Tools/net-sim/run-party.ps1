# Runs the Movecade room server (PartyKit) locally for the UE host. Local only: this never deploys.
#
#   .\Tools\net-sim\run-party.ps1            # clone + npm ci on first use, then serve ws://localhost:1999
#   .\Tools\net-sim\run-party.ps1 -Source Tools\.cache\movecade-v2-pad   # serve a patched scratch clone in place
#
# By default the clone lives in Tools/.cache/movecade (git-ignored); the movecade-v2 checkout itself is never touched.
# -Source pointing anywhere else is taken to be your own scratch clone (for example one with the Docs/Port/server
# patches applied): it is served from where it is, after `npm ci` if it has no node_modules yet.
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
$original = 'C:\Users\Jimmy\Documents\Projects\movecade\movecade-v2'
$sourcePath = (Resolve-Path $Source).Path.TrimEnd('\')
if ($sourcePath -ieq $original.TrimEnd('\')) {
    # The read-only original: work on a local clone of it.
    $clone = Join-Path $root 'Tools\.cache\movecade'
    if (-not (Test-Path $clone)) {
        git clone --local $Source $clone
    }
} else {
    $clone = $sourcePath
}
Write-Host "run-party: serving $clone on port $Port"
# The CLI the clone's lockfile pinned. Never `npx partykit`: if the install had failed, npx would fetch whatever
# version the registry serves and, without a terminal, run it without asking.
$partykit = Join-Path $clone 'node_modules\.bin\partykit.cmd'
if (-not (Test-Path $partykit)) {
    Push-Location $clone
    try {
        # --ignore-scripts: an optional native dependency (cpu-features) fails to build without a C++ toolchain on PATH.
        $env:PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD = '1'
        npm ci --no-audit --no-fund --ignore-scripts
        if ($LASTEXITCODE -ne 0) { throw "npm ci failed (exit $LASTEXITCODE) in $clone" }
    }
    finally {
        Pop-Location
    }
    if (-not (Test-Path $partykit)) { throw "npm ci did not install partykit in $clone (is it in package-lock.json?)" }
}
Push-Location $clone
try {
    # Same as `npm run dev:party` (package.json), plus the UE host's Origin.
    & $partykit dev --config partykit.json --port $Port `
        --var SCORE_SIGNING_SECRET=local-dev-not-a-secret `
        --var "EXTRA_ALLOWED_ORIGINS=$ExtraOrigins"
}
finally {
    Pop-Location
}
