# Builds the trailer: captures five gameplay clips with capture.ps1, then cuts them together with the title and
# track-select screens, crossfades, and the menu music.
#   .\Tools\video\trailer.ps1            # capture and assemble (about 15-25 minutes)
#   .\Tools\video\trailer.ps1 -NoCapture # re-assemble from the clips already in Saved\Video
# Needs the UI stills from: .\Tools\ui\shots.ps1 -Steps title,tracks
param([switch]$NoCapture)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$video = "$project\Saved\Video"
$shots = "$project\Saved\Screenshots\WindowsEditor"
$python = "$project\Tools\.cache\venv\Scripts\python.exe"

# Name, map, from (s), length (s), extra flags. Cinematic shots cut every 4.5 s of race time.
$clips = @(
    @('t_violet_cine', 'L_VioletLoop', 7, 9, '-KartSoloView -KartSkipIntro -KartCinematic -KartCaptureNoUI'),
    @('t_harbor_cine', 'L_SunsetHarbor', 16, 9, '-KartSoloView -KartSkipIntro -KartCinematic -KartCaptureNoUI'),
    @('t_violet_hud', 'L_VioletLoop', 20, 7, '-KartSoloView -KartSkipIntro'),
    @('t_harbor_split', 'L_SunsetHarbor', 9, 6, '-KartSkipIntro'),
    @('t_harbor_hud', 'L_SunsetHarbor', 30, 7, '-KartSoloView -KartSkipIntro')
)
if (-not $NoCapture) {
    foreach ($c in $clips) {
        & "$PSScriptRoot\capture.ps1" -Name $c[0] -Map $c[1] -From $c[2] -Seconds $c[3] -Extra $c[4]
    }
}

$ffmpeg = & $python -c "import imageio_ffmpeg; print(imageio_ffmpeg.get_ffmpeg_exe())"
$fade = 0.5
$inputs = @('-loop', '1', '-t', '3', '-framerate', '60', '-i', "$shots\KartUi_title_1920x1080.png")
$lengths = @(3.0)
foreach ($c in $clips) {
    $inputs += @('-i', "$video\$($c[0]).mp4")
    $lengths += [double]$c[3]
}
$inputs += @('-loop', '1', '-t', '3', '-framerate', '60', '-i', "$shots\KartUi_tracks_1920x1080.png")
$lengths += 3.0

# Normalise every input, then chain crossfades: each offset is the running length minus the fades so far.
$filter = ''
for ($i = 0; $i -lt $lengths.Count; $i++) {
    $filter += "[$i`:v]setpts=PTS-STARTPTS,scale=1920:1080,fps=60,format=yuv420p,settb=AVTB[v$i];"
}
$prev = 'v0'
$offset = 0.0
for ($i = 1; $i -lt $lengths.Count; $i++) {
    $offset += $lengths[$i - 1] - $fade
    $out = if ($i -eq $lengths.Count - 1) { 'vout' } else { "x$i" }
    $filter += "[$prev][v$i]xfade=transition=fade:duration=$fade`:offset=$([Math]::Round($offset, 3))[$out];"
    $prev = $out
}
$total = $offset + $lengths[-1]
$music = "$project\Tools\ui\sounds\music_menu_loop.wav"
$audioIndex = $lengths.Count
$filter += "[$audioIndex`:a]atrim=0:$total,afade=t=in:d=0.5,afade=t=out:st=$($total - 2):d=2[aout]"
$inputs += @('-stream_loop', '-1', '-i', $music)

$mp4 = "$video\trailer.mp4"
& $ffmpeg -y -loglevel error @inputs -filter_complex $filter -map '[vout]' -map '[aout]' -t $total `
    -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -c:a aac -b:a 192k -movflags +faststart $mp4
Write-Output ("trailer: {0:N1} s -> {1}" -f $total, $mp4)
