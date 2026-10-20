# Records gameplay to an MP4: the game runs with a fixed 60 fps time step, writes every frame, then ffmpeg
# encodes them. Rendering may run slower than real time; the video plays at full speed.
#   .\Tools\video\capture.ps1 -Name harbor_cinematic -Map L_SunsetHarbor -From 6 -Seconds 20 -Extra '-KartCinematic'
# Needs the tools venv once:  py -3.13 -m venv Tools\.cache\venv; Tools\.cache\venv\Scripts\pip install imageio-ffmpeg
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$Map = 'L_SunsetHarbor',
    [double]$From = 6,
    [double]$Seconds = 15,
    [int]$ResX = 1920,
    [int]$ResY = 1080,
    [string]$Extra = '-KartSoloView -KartSkipIntro',
    [switch]$KeepFrames
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$python = "$project\Tools\.cache\venv\Scripts\python.exe"
$frames = "$project\Saved\Capture\$Name"
$outDir = "$project\Saved\Video"
if (Test-Path $frames) { Remove-Item -Recurse -Force $frames }
New-Item -ItemType Directory -Force $outDir | Out-Null

$quit = [Math]::Ceiling($From + $Seconds + 1)
$gameArgs = @("`"$project\MyProject.uproject`"", "/Game/Maps/$Map", '-game', '-windowed', "-ResX=$ResX", "-ResY=$ResY",
    '-nosound', '-benchmark', '-fps=60', '-KartAutoDrive', "`"-KartCapture=$frames`"", "-KartCaptureFrom=$From",
    "-KartCaptureFor=$Seconds", "-KartQuitAfter=$quit", '-log=KartCapture.log')
$gameArgs += $Extra.Split(' ', [StringSplitOptions]::RemoveEmptyEntries)
$proc = Start-Process -FilePath $editor -ArgumentList $gameArgs -PassThru -WorkingDirectory $project
# Fixed time step: game time runs as fast as frames render and save, so allow generously.
if (-not $proc.WaitForExit(1800 * 1000)) { Stop-Process -Id $proc.Id -Force; Write-Output 'timed out' }

$count = @(Get-ChildItem $frames -Filter 'frame_*.png' -ErrorAction SilentlyContinue).Count
if ($count -eq 0) { Write-Output "No frames captured (see Saved\Logs\KartCapture.log)"; exit 1 }
$ffmpeg = & $python -c "import imageio_ffmpeg; print(imageio_ffmpeg.get_ffmpeg_exe())"
$mp4 = "$outDir\$Name.mp4"
& $ffmpeg -y -loglevel error -framerate 60 -i "$frames\frame_%05d.png" -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -movflags +faststart $mp4
if (-not $KeepFrames) { Remove-Item -Recurse -Force $frames }
Write-Output "$count frames -> $mp4"
