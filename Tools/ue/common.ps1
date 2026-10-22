# Shared helpers for the Tools/ue and Tools/ui scripts: where the engine and project are, the editor build, and
# launching the editor. Dot-source it at the top of a script:
#   . "$PSScriptRoot\..\ue\common.ps1"
#
# Every build and launch runs with this checkout's own TMP/TEMP (Saved/Temp) and the build passes -NoMutex: several
# worktrees build and run side by side on one PC, and a shared temp folder or the global UBT mutex would make them
# trip over each other.

$KartEngine = 'C:\Program Files\Epic Games\UE_5.8\Engine'
$KartEditor = "$KartEngine\Binaries\Win64\UnrealEditor.exe"
$KartEditorCmd = "$KartEngine\Binaries\Win64\UnrealEditor-Cmd.exe"
$KartProject = (Resolve-Path "$PSScriptRoot\..\..").Path
$KartUProject = "$KartProject\MyProject.uproject"
$KartLogs = "$KartProject\Saved\Logs"
$KartShotDir = "$KartProject\Saved\Screenshots\WindowsEditor"

# Runs Body with TMP/TEMP pointed at this checkout's Saved/Temp, then restores them (child processes inherit them).
function Invoke-KartPrivateTemp([scriptblock]$Body) {
    $private = "$KartProject\Saved\Temp"
    New-Item -ItemType Directory -Force $private | Out-Null
    $oldTmp = $env:TMP
    $oldTemp = $env:TEMP
    try {
        $env:TMP = $private
        $env:TEMP = $private
        & $Body
    }
    finally {
        $env:TMP = $oldTmp
        $env:TEMP = $oldTemp
    }
}

# Builds the editor target. Prints the error and result lines; returns $true on success.
# -NoHotReloadFromIDE builds even while another editor (any project) holds Live Coding.
function Invoke-KartBuild {
    $log = "$KartLogs\UBT-MyProjectEditor.log"
    $code = Invoke-KartPrivateTemp {
        & "$KartEngine\Build\BatchFiles\Build.bat" MyProjectEditor Win64 Development "-Project=$KartUProject" -NoMutex -NoHotReloadFromIDE "-Log=$log" |
            Select-String -Pattern 'error|Result:' | ForEach-Object { Write-Host $_.Line }
        $LASTEXITCODE
    }
    return $code -eq 0
}

# Starts Exe with Arguments from the project folder, waits up to TimeoutSeconds, and kills it on timeout.
# -Hidden for the console editor (tests), which would otherwise open a console window.
function Invoke-KartEditor([string]$Exe = $KartEditor, [string[]]$Arguments, [int]$TimeoutSeconds, [switch]$Hidden) {
    $style = if ($Hidden) { 'Hidden' } else { 'Normal' }
    $proc = Invoke-KartPrivateTemp { Start-Process -FilePath $Exe -ArgumentList $Arguments -PassThru -WorkingDirectory $KartProject -WindowStyle $style }
    if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $proc.Id -Force
        Write-Output 'timed out'
    }
}

# Splits an -Extra string ('-Foo -Bar=1') into arguments.
function Split-KartArgs([string]$Extra) {
    return @($Extra.Split(' ', [StringSplitOptions]::RemoveEmptyEntries))
}

# The last Count lines of a log that match Pattern.
function Show-KartLog([string]$Log, [string]$Pattern, [int]$Count = 25) {
    if (Test-Path $Log) {
        Select-String -Path $Log -Pattern $Pattern | Select-Object -Last $Count | ForEach-Object { $_.Line }
    }
    else {
        Write-Output "no log at $Log"
    }
}

# The screenshot file names present now, to tell the new ones apart after a run (Get-KartNewShots).
function Get-KartShotNames {
    if (Test-Path $KartShotDir) { return @(Get-ChildItem $KartShotDir -Filter *.png | ForEach-Object Name) }
    return @()
}

function Get-KartNewShots([string[]]$Before) {
    if (-not (Test-Path $KartShotDir)) { return }
    Get-ChildItem $KartShotDir -Filter *.png | Where-Object { $Before -notcontains $_.Name } | ForEach-Object { $_.FullName }
}
