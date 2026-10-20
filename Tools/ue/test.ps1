# Builds the editor target (optional) and runs every MovecadeKart automation test headless.
#   .\Tools\ue\test.ps1            # build, then test
#   .\Tools\ue\test.ps1 -NoBuild   # test only
#   .\Tools\ue\test.ps1 -Filter MovecadeKart.Feel
param(
    [switch]$NoBuild,
    [string]$Filter = 'MovecadeKart'
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$engine = 'C:\Program Files\Epic Games\UE_5.8\Engine'

if (-not $NoBuild) {
    # -NoHotReloadFromIDE: build even while another editor (any project) holds Live Coding.
    & "$engine\Build\BatchFiles\Build.bat" MyProjectEditor Win64 Development "-Project=$project\MyProject.uproject" -WaitMutex -NoHotReloadFromIDE |
        Select-String -Pattern 'error|Result:' | ForEach-Object { $_.Line }
    if ($LASTEXITCODE -ne 0) { Write-Output 'Build failed.'; exit 1 }
}

$log = "$project\Saved\Logs\Tests.log"
& "$engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$project\MyProject.uproject" "-ExecCmds=Automation RunTests $Filter; Quit" `
    -unattended -nullrhi -nosplash -nosound "-abslog=$log" | Out-Null

$results = Select-String -Path $log -Pattern 'Test Completed\. Result=\{(\w+)\}.*Path=\{([^}]*)\}' |
    ForEach-Object { [pscustomobject]@{ Result = $_.Matches[0].Groups[1].Value; Test = $_.Matches[0].Groups[2].Value } }
$failed = @($results | Where-Object Result -ne 'Success')
Write-Output ("{0} passed, {1} failed" -f ($results.Count - $failed.Count), $failed.Count)
foreach ($f in $failed) {
    Write-Output "FAIL $($f.Test)"
}
Select-String -Path $log -Pattern 'Error: Expected|Error: .*\.cpp\(' | Select-Object -First 15 | ForEach-Object { '  ' + ($_.Line -replace '.*Error: ', '') }
if ($failed.Count -gt 0 -or $results.Count -eq 0) { exit 1 }
