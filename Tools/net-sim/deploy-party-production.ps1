# Deploys the Movecade room server to production from the clean checkout at C:\Users\Jimmy\party-deploy
# (outside the repo, as movecade-v2's deploy script requires). Asks for the Cloudflare account ID, the API token
# and the score signing secret here, hidden, so they never pass through chat or a file, and clears them afterwards.
#
# The steps of `npm run deploy:production` (scripts/deploy-party-production.mjs), run here because that script
# spawns `npx` without a shell, which cannot start npx.cmd on Windows ("partykit deploy exited null"):
#   1. the script's own guards: no .env/.env.local above the checkout, the required production vars present
#   2. npx partykit deploy --config partykit.production.json --with-vars --var SCORE_SIGNING_SECRET=...
#      --with-vars REPLACES every variable on the worker with partykit.production.json's (plus --var), so the
#      signing secret, which is not in that file, must be passed too: without it production refuses every room
#      ("SCORE_SIGNING_SECRET is not set"). It must be the same secret as the Go API's.
#   3. readiness on fresh room ids, after the new deployment has replaced the old instances
param([string]$Checkout = 'C:\Users\Jimmy\party-deploy')
$ErrorActionPreference = 'Stop'
Set-Location $Checkout
Write-Host "Deploying $(git log --oneline -1) to production (party.movecade.com)." -ForegroundColor Cyan

# 1. The deploy script's guards, imported from it so they stay exactly its rules.
$guard = @'
import { readFileSync } from 'node:fs';
import { findEnvFilesAbove, missingRequiredVars, PRODUCTION_CONFIG } from './scripts/deploy-party-production.mjs';
const envFiles = findEnvFilesAbove(process.cwd());
if (envFiles.length) { console.error('refused: the PartyKit CLI would bake these into the worker: ' + envFiles.join(', ')); process.exit(1); }
const missing = missingRequiredVars(JSON.parse(readFileSync(PRODUCTION_CONFIG, 'utf8')));
if (missing.length) { console.error('refused: ' + PRODUCTION_CONFIG + ' vars is missing ' + missing.join(', ')); process.exit(1); }
console.log('guards passed');
'@
node --input-type=module -e $guard
if ($LASTEXITCODE -ne 0) { Write-Host 'DEPLOY REFUSED by the guards above.' -ForegroundColor Red; exit 1 }

function Read-Secret([string]$Prompt) {
    $secure = Read-Host $Prompt -AsSecureString
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringAuto($bstr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}

$env:CLOUDFLARE_ACCOUNT_ID = Read-Host 'Cloudflare account ID'
$env:CLOUDFLARE_API_TOKEN = Read-Secret 'Cloudflare API token (hidden)'
$signing = Read-Secret 'SCORE_SIGNING_SECRET, the same as the Go API''s (hidden)'
try {
    if ($signing.Length -lt 24) { Write-Host 'Refused: the signing secret must be at least 24 characters (production rule).' -ForegroundColor Red; exit 1 }
    # 2. The deploy, with the production vars and the signing secret. The CLI only takes vars as arguments, so the
    #    secret is on the npx/node command line for the deploy's few seconds: readable by this Windows account (and
    #    administrators), not by other users. Don't run this with process monitoring or command-line logging on.
    npx partykit deploy --config partykit.production.json --with-vars --var "SCORE_SIGNING_SECRET=$signing"
    if ($LASTEXITCODE -ne 0) { Write-Host "DEPLOY FAILED: partykit deploy exited $LASTEXITCODE." -ForegroundColor Red; exit 1 }
}
finally {
    $signing = $null
    Remove-Item Env:CLOUDFLARE_API_TOKEN, Env:CLOUDFLARE_ACCOUNT_ID -ErrorAction SilentlyContinue
}

# 3. Readiness on rooms that did not exist before this deploy (an old instance can still answer for a few seconds).
Start-Sleep -Seconds 15
$ok = $true
foreach ($i in 1..3) {
    $room = 'r-check' + (Get-Random -Maximum 999999)
    $body = (Invoke-WebRequest -UseBasicParsing -Headers @{ 'cache-control' = 'no-cache' } "https://party.movecade.com/parties/main/$room").Content
    Write-Host "$room -> $body"
    if ($body -notmatch '"ready":true') { $ok = $false }
}
if ($ok) { Write-Host 'DEPLOY OK: production rooms are accepting players.' -ForegroundColor Green }
else { Write-Host 'NOT READY: production rooms are refusing players (reasons above).' -ForegroundColor Red; exit 1 }
