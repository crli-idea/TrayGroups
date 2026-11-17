param([string]$Version = 'main')
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^(main|v\d+\.\d+\.\d+)$') { throw 'Use main or a vMAJOR.MINOR.PATCH version.' }
$binary = Join-Path $PSScriptRoot 'test\build\TrayGroups.exe'
$ini = Join-Path $PSScriptRoot 'defaults\groups.ini'
$readme = Join-Path $PSScriptRoot 'README.md'
if ($Version -ne 'main' -and ('v' + (Get-Item -LiteralPath $binary).VersionInfo.FileVersion) -ne $Version) {
    throw 'Release tag and executable version do not match.'
}
$archive = Join-Path $PSScriptRoot "test\build\TrayGroups-$Version-windows-x64.zip"
# Explicit files prevent local settings, screenshots or stale staging files leaking.
Compress-Archive -LiteralPath $binary,$ini,$readme -DestinationPath $archive -Force
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
try {
    $names = @($zip.Entries | ForEach-Object { $_.FullName })
    $expected = @('TrayGroups.exe', 'groups.ini', 'README.md')
    if (@(Compare-Object $expected $names).Count) { throw 'Portable package contents are incorrect.' }
    if (@($zip.Entries | Where-Object { $_.Length -eq 0 }).Count) { throw 'Portable package contains an empty file.' }
} finally { $zip.Dispose() }
Write-Output "Verified portable package: $archive (EXE, default INI, README)"
