$ErrorActionPreference = 'Stop'
$buildOutput = Join-Path $PSScriptRoot 'test\build'
New-Item -ItemType Directory -Path $buildOutput -Force | Out-Null
$resourceObject = Join-Path $buildOutput 'TrayGroups.resources.o'
Push-Location $PSScriptRoot
try {
    & 'C:\Environment\msys64\mingw64\bin\windres.exe' -i TrayGroups.rc -o $resourceObject
    if ($LASTEXITCODE -ne 0) { throw 'Icon resource build failed.' }
} finally { Pop-Location }
& 'C:\Environment\msys64\mingw64\bin\g++.exe' -std=c++17 -Os -s -static -fno-exceptions -fno-rtti -municode -mwindows '-Wl,--nxcompat,--dynamicbase' -o "$buildOutput\TrayGroups.exe" "$PSScriptRoot\TrayGroups.cpp" $resourceObject -lcomctl32 -lshell32 -luser32 -lgdi32 -lpsapi -luxtheme
if ($LASTEXITCODE -ne 0) { throw 'Production build failed.' }
Write-Output "Production executable: $buildOutput\TrayGroups.exe"
& "$PSScriptRoot\ScreenCaptureTimer\build.ps1"
if ($LASTEXITCODE -ne 0) { throw 'ScreenCaptureTimer build failed.' }
