param([string]$Version = 'main')
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^(main|v\d+\.\d+\.\d+)$') { throw 'Use main or a vMAJOR.MINOR.PATCH version.' }
$binary = Join-Path $PSScriptRoot 'test\build\TrayGroups.exe'
$ini = Join-Path $PSScriptRoot 'defaults\groups.ini'
$readme = Join-Path $PSScriptRoot 'README.md'
$wallpaperIni = Join-Path $PSScriptRoot 'defaults\wallpapers.ini'
if ($Version -ne 'main' -and ('v' + (Get-Item -LiteralPath $binary).VersionInfo.FileVersion) -ne $Version) {
    throw 'Release tag and executable version do not match.'
}
$archiveName = "TrayGroups-$Version-windows-x64.zip"
$archive = Join-Path $PSScriptRoot $archiveName
# Explicit entries prevent personal config, screenshots and build artifacts leaking.
$entries = [ordered]@{
    'TrayGroups.exe'=$binary
    'groups.ini'=$ini
    'README.md'=$readme
    'wallpapers.ini'=$wallpaperIni
    'resources/wallpaper_4717x2984.jpg'=(Join-Path $PSScriptRoot 'resources\wallpaper_4717x2984.jpg')
    'resources/wallpaper_2832x4256.jpg'=(Join-Path $PSScriptRoot 'resources\wallpaper_2832x4256.jpg')
}
Add-Type -AssemblyName System.Drawing
foreach($name in @('wallpaper_4717x2984.jpg','wallpaper_2832x4256.jpg')) {
    $image=[Drawing.Image]::FromFile((Join-Path $PSScriptRoot "resources\$name"))
    try {
        if($name -ne "wallpaper_$($image.Width)x$($image.Height).jpg") {throw 'Wallpaper filename does not match its original image dimensions.'}
    } finally {$image.Dispose()}
}
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$temporary=Join-Path $PSScriptRoot 'test\build\portable-package.zip.tmp'
if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary}
$zip=[System.IO.Compression.ZipFile]::Open($temporary,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach($entry in $entries.GetEnumerator()) {
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$entry.Value,$entry.Key,[IO.Compression.CompressionLevel]::Optimal)
    }
} finally {$zip.Dispose()}
$zip = [System.IO.Compression.ZipFile]::OpenRead($temporary)
try {
    $names = @($zip.Entries | ForEach-Object { $_.FullName })
    $expected = @($entries.Keys)
    if (@(Compare-Object $expected $names).Count) { throw 'Portable package contents are incorrect.' }
    if (@($zip.Entries | Where-Object { $_.Length -eq 0 }).Count) { throw 'Portable package contains an empty file.' }
} finally { $zip.Dispose() }
Move-Item -LiteralPath $temporary -Destination $archive -Force
Write-Output "Verified portable package: .\$archiveName (EXE, default INIs, README, two wallpaper images)"
