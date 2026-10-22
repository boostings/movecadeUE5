# Builds the editor target (optional) and runs every MovecadeKart automation test headless.
#   .\Tools\ue\test.ps1                            # build, then test
#   .\Tools\ue\test.ps1 -NoBuild                   # test only
#   .\Tools\ue\test.ps1 -Filter MovecadeKart.Feel
# Log: Saved/Logs/Tests.log.
param(
    [switch]$NoBuild,
    [string]$Filter = 'MovecadeKart',
    [int]$TimeoutSeconds = 1800
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

if (-not $NoBuild -and -not (Invoke-KartBuild)) {
    Write-Output 'Build failed.'
    exit 1
}

$log = "$KartLogs\Tests.log"
$testArgs = @("`"$KartUProject`"", "`"-ExecCmds=Automation RunTests $Filter; Quit`"", '-unattended', '-nullrhi', '-nosplash', '-nosound', "`"-abslog=$log`"")
Invoke-KartEditor -Exe $KartEditorCmd -Arguments $testArgs -TimeoutSeconds $TimeoutSeconds -Hidden

$results = Select-String -Path $log -Pattern 'Test Completed\. Result=\{(\w+)\}.*Path=\{([^}]*)\}' |
    ForEach-Object { [pscustomobject]@{ Result = $_.Matches[0].Groups[1].Value; Test = $_.Matches[0].Groups[2].Value } }
$failed = @($results | Where-Object Result -ne 'Success')
Write-Output ("{0} passed, {1} failed" -f ($results.Count - $failed.Count), $failed.Count)
foreach ($f in $failed) {
    Write-Output "FAIL $($f.Test)"
}
Select-String -Path $log -Pattern 'Error: Expected|Error: .*\.cpp\(' | Select-Object -First 15 | ForEach-Object { '  ' + ($_.Line -replace '.*Error: ', '') }
if ($failed.Count -gt 0 -or $results.Count -eq 0) { exit 1 }
