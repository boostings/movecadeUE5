# Downloads the UI fonts (SIL Open Font License 1.1) from Google Fonts into Tools/ui/fonts, with their licences.
# Google Fonts serves static TrueType instances of each weight to old user agents; the licence texts come from
# the google/fonts repository. Import into /Game/UI/Fonts afterwards with:
#   .\Tools\ue\run.ps1 import_ui_assets.py -NullRhi
$ErrorActionPreference = 'Stop'
$out = Join-Path $PSScriptRoot 'fonts'
New-Item -ItemType Directory -Force $out | Out-Null

# Android 2.x gets format('truetype') URLs instead of woff/woff2.
$ua = 'Mozilla/5.0 (Linux; U; Android 2.2; en-us; Nexus One Build/FRF91) AppleWebKit/533.1 (KHTML, like Gecko) Version/4.0 Mobile Safari/533.1'
$families = @(
    @{ Css = 'Fredoka:wght@500;600;700'; Name = 'Fredoka'; Licence = 'ofl/fredoka/OFL.txt' },
    @{ Css = 'Nunito:wght@700;800;900'; Name = 'Nunito'; Licence = 'ofl/nunito/OFL.txt' },
    @{ Css = 'Lilita+One'; Name = 'LilitaOne'; Licence = 'ofl/lilitaone/OFL.txt' }
)
$weightNames = @{ '400' = 'Regular'; '500' = 'Medium'; '600' = 'SemiBold'; '700' = 'Bold'; '800' = 'ExtraBold'; '900' = 'Black' }

foreach ($family in $families) {
    $css = (Invoke-WebRequest -UseBasicParsing -UserAgent $ua "https://fonts.googleapis.com/css2?family=$($family.Css)").Content
    $faces = [regex]::Matches($css, "font-weight:\s*(\d+);[\s\S]*?src:\s*url\(([^)]+\.ttf)\)")
    foreach ($face in $faces) {
        $file = Join-Path $out ("{0}-{1}.ttf" -f $family.Name, $weightNames[$face.Groups[1].Value])
        Invoke-WebRequest -UseBasicParsing $face.Groups[2].Value -OutFile $file
        Write-Output ("{0}  {1} bytes" -f $file, (Get-Item $file).Length)
    }
    Invoke-WebRequest -UseBasicParsing "https://raw.githubusercontent.com/google/fonts/main/$($family.Licence)" -OutFile (Join-Path $out "$($family.Name)-OFL.txt")
}
