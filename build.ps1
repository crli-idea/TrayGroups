$ErrorActionPreference = 'Stop'
$mingwBin = 'C:\Environment\msys64\mingw64\bin'
if (-not (Test-Path (Join-Path $mingwBin 'g++.exe'))) {
    $mingwBin = Split-Path -Parent (Get-Command g++.exe -ErrorAction Stop).Source
}
$buildOutput = Join-Path $PSScriptRoot 'test\build'
New-Item -ItemType Directory -Path $buildOutput -Force | Out-Null
$resourceObject = Join-Path $buildOutput 'TrayGroups.resources.o'
Push-Location (Join-Path $PSScriptRoot 'resources')
try {
    & (Join-Path $mingwBin 'windres.exe') -i TrayGroups.rc -o $resourceObject
    if ($LASTEXITCODE -ne 0) { throw 'Icon resource build failed.' }
} finally { Pop-Location }
$sources = @('app\TrayGroups.cpp','config\TrayConfig.cpp','tray\TrayIdentity.cpp','tray\TrayGuid.cpp','capture\ScreenCaptureTimer.cpp') | ForEach-Object { Join-Path $PSScriptRoot "src\$_" }
& (Join-Path $mingwBin 'g++.exe') -std=c++17 -Os -s -static -fno-exceptions -fno-rtti -DSCREEN_CAPTURE_EMBEDDED -municode -mwindows '-Wl,--nxcompat,--dynamicbase' -o "$buildOutput\TrayGroups.exe" @sources $resourceObject -lcomctl32 -lshell32 -luser32 -lgdi32 -lpsapi -luxtheme -lz -lwtsapi32 -ladvapi32 -lole32 -luuid
if ($LASTEXITCODE -ne 0) { throw 'Production build failed.' }
Write-Output "Production executable: $buildOutput\TrayGroups.exe"
